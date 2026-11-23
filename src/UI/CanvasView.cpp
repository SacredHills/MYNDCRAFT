#include "UI/CanvasView.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QTabletEvent>
#include <QWheelEvent>
#include <cmath>

#include "Graphics/Compositor.h"

namespace mynd {

CanvasView::CanvasView(EditorSession* session, QWidget* parent) : QWidget(parent), m_session(session) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(200, 150);
    m_tool = createTool(session->tool(), session, this);
    connect(session, &EditorSession::toolChanged, this, [this](ToolId id) { switchTool(id); });
    connect(session, &EditorSession::selectionChanged, this, [this] { update(); });
    connect(session, &EditorSession::viewTogglesChanged, this, [this] { update(); });
    connect(session, &EditorSession::brushChanged, this, [this] { update(); });
    updateCursor();
}
CanvasView::~CanvasView() = default;

void CanvasView::switchTool(ToolId id) {
    if (m_tool) m_tool->cancel();
    m_tool = createTool(id, m_session, this);
    updateCursor();
    update();
}

void CanvasView::updateCursor() {
    if (m_panning || m_spaceDown) setCursor(m_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    else if (m_tool) setCursor(m_tool->cursor());
}

void CanvasView::setDocument(Document* d) {
    endTextEdit(false);
    if (m_conn) disconnect(m_conn);
    if (m_tool) m_tool->cancel();
    m_doc = d;
    m_tool = createTool(m_session->tool(), m_session, this);
    if (d) {
        m_conn = connect(d, &Document::contentChanged, this, &CanvasView::onContentChanged);
        connect(d, &Document::layerTreeChanged, this, [this] { update(); });
        m_center = QPointF(d->comp().width / 2.0, d->comp().height / 2.0);
        m_rotation = 0;
        rebuildCache();
        fitToView();
    } else {
        m_cache = QImage();
        update();
    }
}

void CanvasView::rebuildCache() {
    if (!m_doc) return;
    m_cache = Compositor::render(*m_doc);
    update();
}

void CanvasView::onContentChanged(const QRect& r) {
    if (!m_doc) return;
    if (m_cache.size() != m_doc->canvasSize() || r.isEmpty()) { rebuildCache(); return; }
    const QRect rr = r.adjusted(-2, -2, 2, 2).intersected(m_doc->canvasRect());
    if (rr.isEmpty()) return;
    const QImage part = Compositor::render(*m_doc, rr, {});
    QPainter p(&m_cache);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(rr.topLeft(), part);
    p.end();
    update(docToView().mapRect(QRectF(rr)).toAlignedRect().adjusted(-3, -3, 3, 3));
}

// ------------------------------------------------------------------ view transform
QTransform CanvasView::docToView() const {
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.rotate(m_rotation);
    t.scale(m_zoom, m_zoom);
    t.translate(-m_center.x(), -m_center.y());
    return t;
}
QPointF CanvasView::viewToDoc(const QPointF& v) const { return docToView().inverted().map(v); }

void CanvasView::zoomAt(double factor, const QPointF& anchor) {
    if (m_textEdit) endTextEdit(true);
    m_autoFit = false;
    const QPointF before = viewToDoc(anchor);
    m_zoom = std::clamp(m_zoom * factor, 0.02, 64.0);
    const QPointF after = viewToDoc(anchor);
    m_center += before - after;
    update();
    emit viewChanged();
}
void CanvasView::setZoom(double z) { zoomAt(z / m_zoom, QPointF(width() / 2.0, height() / 2.0)); }

void CanvasView::rotateBy(double deg) {
    if (m_textEdit) endTextEdit(true);
    m_autoFit = false;
    m_rotation = std::fmod(m_rotation + deg, 360.0);
    if (m_rotation < 0) m_rotation += 360.0;
    update();
    emit viewChanged();
}
void CanvasView::resetRotation() { m_rotation = 0; update(); emit viewChanged(); }

void CanvasView::panBy(const QPointF& d) {
    if (m_textEdit) endTextEdit(true);
    m_autoFit = false;
    const QPointF c(width() / 2.0, height() / 2.0);
    m_center += viewToDoc(c) - viewToDoc(c + d);
    update();
    emit viewChanged();
}

void CanvasView::fitToView() {
    if (!m_doc) return;
    const double a = qDegreesToRadians(m_rotation);
    const double w = m_doc->comp().width, h = m_doc->comp().height;
    const double bw = std::abs(w * std::cos(a)) + std::abs(h * std::sin(a));
    const double bh = std::abs(w * std::sin(a)) + std::abs(h * std::cos(a));
    const double avW = std::max(50, width() - 2 * kRuler - 40), avH = std::max(50, height() - 2 * kRuler - 40);
    m_zoom = std::clamp(std::min(avW / bw, avH / bh), 0.02, 64.0);
    m_center = QPointF(w / 2.0, h / 2.0);
    m_autoFit = true;
    update();
    emit viewChanged();
}
void CanvasView::actualPixels() {
    if (!m_doc) return;
    m_zoom = 1.0;
    m_autoFit = false;
    m_center = QPointF(m_doc->comp().width / 2.0, m_doc->comp().height / 2.0);
    update();
    emit viewChanged();
}

void CanvasView::resizeEvent(QResizeEvent*) {
    if (m_textEdit) endTextEdit(true);
    if (m_autoFit) fitToView();  // keep the whole canvas visible until the user zooms/pans
    emit viewChanged();
}

// ------------------------------------------------------------------ snapping / sampling
QPointF CanvasView::snap(const QPointF& p) const {
    if (!m_session->snapEnabled || !m_doc) return p;
    const double tol = 8.0 / m_zoom;
    QPointF r = p;
    double bestX = tol, bestY = tol;
    auto tryX = [&](double x) { if (std::abs(p.x() - x) < bestX) { bestX = std::abs(p.x() - x); r.setX(x); } };
    auto tryY = [&](double y) { if (std::abs(p.y() - y) < bestY) { bestY = std::abs(p.y() - y); r.setY(y); } };
    const double g = m_doc->comp().gridSize;
    if (m_session->gridVisible && g > 0) { tryX(std::round(p.x() / g) * g); tryY(std::round(p.y() / g) * g); }
    if (m_session->guidesVisible)
        for (const auto& gd : m_doc->comp().guides) { if (gd.orientation == Qt::Vertical) tryX(gd.pos); else tryY(gd.pos); }
    tryX(0); tryX(m_doc->comp().width); tryY(0); tryY(m_doc->comp().height);
    return r;
}

QColor CanvasView::sampleColor(const QPoint& p) const {
    if (!m_doc) return {};
    const QImage px = Compositor::render(*m_doc, QRect(p, QSize(1, 1)), {true});
    QColor c = px.pixelColor(0, 0);
    c.setAlpha(255);
    return c;
}

// ------------------------------------------------------------------ painting
static QString fmtNum(double v) { return QString::number(qRound(v)); }

QRectF CanvasView::rulerRectTop() const { return QRectF(kRuler, 0, width() - kRuler, kRuler); }
QRectF CanvasView::rulerRectLeft() const { return QRectF(0, kRuler, kRuler, height() - kRuler); }

void CanvasView::paintEvent(QPaintEvent* ev) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2b, 0x2b, 0x2e));
    if (!m_doc) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("No document. File > New (Ctrl+N) or File > Open (Ctrl+O)."));
        return;
    }
    const QTransform t = docToView();
    const QRectF canvas(0, 0, m_doc->comp().width, m_doc->comp().height);
    const QPolygonF poly = t.map(QPolygonF(canvas));

    // Drop shadow + transparency checkerboard (visible where the composite is transparent).
    p.setRenderHint(QPainter::Antialiasing, false);
    QPainterPath clip; clip.addPolygon(poly);
    p.fillPath(clip.translated(3, 3), QColor(0, 0, 0, 90));
    p.save();
    p.setClipPath(clip);
    static const QPixmap checker = [] {
        QPixmap pm(16, 16); pm.fill(QColor(0xcc, 0xcc, 0xcc));
        QPainter q(&pm); q.fillRect(0, 0, 8, 8, QColor(0x99, 0x99, 0x99)); q.fillRect(8, 8, 8, 8, QColor(0x99, 0x99, 0x99));
        return pm;
    }();
    p.drawTiledPixmap(rect(), checker);
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 2.0);
    p.setTransform(t);
    p.drawImage(QPointF(0, 0), m_cache);
    p.restore();

    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 160), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(poly);

    if (m_session->gridVisible) drawGrid(p, t);
    if (m_session->guidesVisible) drawGuides(p, t);
    if (m_tool) { p.save(); m_tool->paintOverlay(p, t); p.restore(); }

    if (m_session->rulersVisible && std::abs(m_rotation) < 0.01) drawRulers(p);
    Q_UNUSED(ev);
}

void CanvasView::drawGrid(QPainter& p, const QTransform& t) {
    const double g = m_doc->comp().gridSize;
    if (g <= 0 || g * m_zoom < 6) return;
    const int w = m_doc->comp().width, h = m_doc->comp().height;
    if ((w / g + h / g) > 2000) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(120, 160, 255, 70), 1));
    for (double x = 0; x <= w; x += g) p.drawLine(t.map(QPointF(x, 0)), t.map(QPointF(x, h)));
    for (double y = 0; y <= h; y += g) p.drawLine(t.map(QPointF(0, y)), t.map(QPointF(w, y)));
    p.restore();
}

void CanvasView::drawGuides(QPainter& p, const QTransform& t) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(0, 200, 220), 1));
    const double w = m_doc->comp().width, h = m_doc->comp().height;
    const double ext = std::max(w, h) * 4;
    for (const auto& g : m_doc->comp().guides) {
        if (g.orientation == Qt::Horizontal) p.drawLine(t.map(QPointF(-ext, g.pos)), t.map(QPointF(ext, g.pos)));
        else p.drawLine(t.map(QPointF(g.pos, -ext)), t.map(QPointF(g.pos, ext)));
    }
    if (m_creatingGuide && m_hasHover) {
        const QPointF d = m_hover;
        if (m_newGuideOrient == Qt::Horizontal) p.drawLine(t.map(QPointF(-ext, d.y())), t.map(QPointF(ext, d.y())));
        else p.drawLine(t.map(QPointF(d.x(), -ext)), t.map(QPointF(d.x(), ext)));
    }
    p.restore();
}

void CanvasView::drawRulers(QPainter& p) {
    const QTransform t = docToView();
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rulerRectTop(), QColor(0x3a, 0x3a, 0x3e));
    p.fillRect(rulerRectLeft(), QColor(0x3a, 0x3a, 0x3e));
    p.fillRect(QRectF(0, 0, kRuler, kRuler), QColor(0x32, 0x32, 0x36));
    static const double steps[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
    double step = 10000;
    for (double s : steps) if (s * m_zoom >= 60) { step = s; break; }
    const double minor = step / 5;
    QFont f = font(); f.setPixelSize(9); p.setFont(f);
    p.setPen(QColor(190, 190, 190));
    const QTransform inv = t.inverted();
    const double x0 = inv.map(QPointF(kRuler, 0)).x(), x1 = inv.map(QPointF(width(), 0)).x();
    for (double x = std::floor(x0 / minor) * minor; x <= x1; x += minor) {
        const double vx = t.map(QPointF(x, 0)).x();
        if (vx < kRuler) continue;
        const bool major = std::fmod(std::abs(x), step) < 1e-6 || std::abs(std::fmod(std::abs(x), step) - step) < 1e-6;
        p.drawLine(QPointF(vx, major ? 4 : 14), QPointF(vx, kRuler));
        if (major) p.drawText(QPointF(vx + 2, 10), fmtNum(x));
    }
    const double y0 = inv.map(QPointF(0, kRuler)).y(), y1 = inv.map(QPointF(0, height())).y();
    for (double y = std::floor(y0 / minor) * minor; y <= y1; y += minor) {
        const double vy = t.map(QPointF(0, y)).y();
        if (vy < kRuler) continue;
        const bool major = std::fmod(std::abs(y), step) < 1e-6 || std::abs(std::fmod(std::abs(y), step) - step) < 1e-6;
        p.drawLine(QPointF(major ? 4 : 14, vy), QPointF(kRuler, vy));
        if (major) { p.save(); p.translate(10, vy - 2); p.rotate(-90); p.drawText(QPointF(0, 0), fmtNum(y)); p.restore(); }
    }
    if (m_hasHover) {
        p.setPen(QColor(255, 200, 0));
        const QPointF v = t.map(m_hover);
        if (v.x() >= kRuler) p.drawLine(QPointF(v.x(), 0), QPointF(v.x(), kRuler));
        if (v.y() >= kRuler) p.drawLine(QPointF(0, v.y()), QPointF(kRuler, v.y()));
    }
    p.restore();
}

// ------------------------------------------------------------------ input
void CanvasView::pointer(QEvent::Type type, const QPointF& vp, Qt::MouseButton button,
                         Qt::KeyboardModifiers mods, double pressure, double xt, double yt, bool eraserEnd) {
    if (!m_doc) return;
    PointerEvent e;
    e.view = vp;
    e.doc = viewToDoc(vp);
    e.pressure = pressure;
    e.xTilt = xt; e.yTilt = yt;
    e.mods = mods;
    e.button = button;
    e.eraserEnd = eraserEnd;
    m_hover = e.doc;
    m_hasHover = true;
    emit cursorMoved(e.doc);

    const bool panGesture = m_panning || (type == QEvent::MouseButtonPress && (button == Qt::MiddleButton || m_spaceDown));

    if (type == QEvent::MouseButtonPress) {
        setFocus();
        if (panGesture) { m_panning = true; m_lastPanPos = vp; updateCursor(); return; }
        // Guides: drag out of a ruler to create one; Alt+click an existing guide removes it.
        if (m_session->rulersVisible && std::abs(m_rotation) < 0.01 && button == Qt::LeftButton) {
            if (vp.y() < kRuler && vp.x() >= kRuler) { m_creatingGuide = true; m_newGuideOrient = Qt::Horizontal; return; }
            if (vp.x() < kRuler && vp.y() >= kRuler) { m_creatingGuide = true; m_newGuideOrient = Qt::Vertical; return; }
        }
        if (mods & Qt::AltModifier && button == Qt::LeftButton && m_session->guidesVisible &&
            std::abs(m_rotation) < 0.01 && m_session->tool() != ToolId::Eyedropper &&
            m_session->tool() != ToolId::Zoom) {
            auto& gs = m_doc->compMutable().guides;
            for (int i = 0; i < gs.size(); ++i) {
                const double d = gs[i].orientation == Qt::Horizontal ? std::abs(e.doc.y() - gs[i].pos) : std::abs(e.doc.x() - gs[i].pos);
                if (d * m_zoom < 4) { gs.removeAt(i); m_doc->markDirty(); update(); return; }
            }
        }
        if (m_tool) m_tool->press(e);
    } else if (type == QEvent::MouseMove) {
        if (m_panning) { panBy(vp - m_lastPanPos); m_lastPanPos = vp; return; }
        if (m_creatingGuide) { update(); return; }
        if (m_tool) m_tool->move(e);
        if (!(m_tool && m_tool->busy())) update();  // hover overlays
    } else if (type == QEvent::MouseButtonRelease) {
        if (m_panning) { m_panning = false; updateCursor(); return; }
        if (m_creatingGuide) {
            m_creatingGuide = false;
            const bool inside = m_newGuideOrient == Qt::Horizontal ? vp.y() > kRuler : vp.x() > kRuler;
            if (inside) {
                m_doc->compMutable().guides.append({m_newGuideOrient, m_newGuideOrient == Qt::Horizontal ? std::round(e.doc.y()) : std::round(e.doc.x())});
                m_doc->markDirty();
            }
            update();
            return;
        }
        if (m_tool) m_tool->release(e);
        updateCursor();
    }
}

void CanvasView::mousePressEvent(QMouseEvent* e) {
    if (m_penDown) return;
    pointer(QEvent::MouseButtonPress, e->position(), e->button(), e->modifiers(), 1.0, 0, 0, false);
}
void CanvasView::mouseMoveEvent(QMouseEvent* e) {
    if (m_penDown) return;
    pointer(QEvent::MouseMove, e->position(), e->button(), e->modifiers(), 1.0, 0, 0, false);
}
void CanvasView::mouseReleaseEvent(QMouseEvent* e) {
    if (m_penDown) return;
    pointer(QEvent::MouseButtonRelease, e->position(), e->button(), e->modifiers(), 1.0, 0, 0, false);
}
void CanvasView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!m_doc || !m_tool) return;
    PointerEvent pe;
    pe.view = e->position(); pe.doc = viewToDoc(e->position()); pe.mods = e->modifiers(); pe.button = e->button();
    m_tool->doubleClick(pe);
}

void CanvasView::tabletEvent(QTabletEvent* e) {
    const bool eraser = e->pointerType() == QPointingDevice::PointerType::Eraser;
    const double pr = std::clamp(e->pressure(), 0.0, 1.0);
    switch (e->type()) {
    case QEvent::TabletPress:
        m_penDown = true;
        pointer(QEvent::MouseButtonPress, e->position(), Qt::LeftButton, e->modifiers(), std::max(0.01, pr), e->xTilt(), e->yTilt(), eraser);
        break;
    case QEvent::TabletMove:
        pointer(QEvent::MouseMove, e->position(), Qt::LeftButton, e->modifiers(), m_penDown ? std::max(0.01, pr) : 1.0, e->xTilt(), e->yTilt(), eraser);
        break;
    case QEvent::TabletRelease:
        pointer(QEvent::MouseButtonRelease, e->position(), Qt::LeftButton, e->modifiers(), pr, e->xTilt(), e->yTilt(), eraser);
        m_penDown = false;
        break;
    default: break;
    }
    e->accept();
}

void CanvasView::wheelEvent(QWheelEvent* e) {
    if (!m_doc) return;
    const double dy = e->angleDelta().y();
    if (dy == 0) return;
    zoomAt(std::pow(1.0015, dy), e->position());
    e->accept();
}

void CanvasView::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { m_spaceDown = true; updateCursor(); e->accept(); return; }
    if (m_tool && m_tool->keyPress(e)) { e->accept(); return; }
    QWidget::keyPressEvent(e);
}
void CanvasView::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { m_spaceDown = false; updateCursor(); e->accept(); return; }
    QWidget::keyReleaseEvent(e);
}
void CanvasView::leaveEvent(QEvent*) { m_hasHover = false; update(); }

// ------------------------------------------------------------------ inline text editing
void CanvasView::beginTextEdit(const QUuid& layerId, const QPointF& docPos) {
    if (!m_doc) return;
    endTextEdit(true);
    Layer* existing = layerId.isNull() ? nullptr : m_doc->findLayer(layerId);
    if (existing && existing->type != LayerType::Text) existing = nullptr;
    if (existing) { m_editBase = existing->textContent; m_editLayer = existing->id; }
    else {
        m_editBase = m_session->textStyle();
        m_editBase.color = m_session->foreground();
        m_editBase.pos = docPos;
        m_editBase.text.clear();
        m_editLayer = {};
    }
    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->document()->setDocumentMargin(0);
    m_textEdit->setPlainText(m_editBase.text);
    QFont f = m_editBase.font();
    f.setPixelSize(std::max(6, int(std::lround(m_editBase.sizePx * m_zoom))));
    if (m_editBase.tracking != 0) f.setLetterSpacing(QFont::AbsoluteSpacing, m_editBase.tracking * m_zoom);
    m_textEdit->setFont(f);
    // Opaque backdrop so the layer's own rendering does not show through while editing.
    const bool lightText = m_editBase.color.lightness() > 140;
    m_textEdit->setStyleSheet(QString("QPlainTextEdit { color: %1; background: %2; selection-background-color: #2a82da; }")
                                  .arg(m_editBase.color.name(), lightText ? "#303030" : "#f4f4f4"));
    auto fit = [this] {
        const QSizeF s = m_textEdit->document()->size();
        m_textEdit->resize(int(std::max(60.0, s.width() + 24)), int(std::max<double>(m_editBase.sizePx * m_zoom * 1.3, s.height() + 4)));
    };
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, fit);
    fit();
    m_textEdit->move(docToView().map(m_editBase.pos).toPoint());
    m_textEdit->installEventFilter(this);
    m_textEdit->show();
    m_textEdit->setFocus();
    m_textEdit->selectAll();
}

void CanvasView::endTextEdit(bool accept) {
    if (!m_textEdit || m_endingEdit) return;
    m_endingEdit = true;
    QPlainTextEdit* ed = m_textEdit;
    const QString text = ed->toPlainText();
    m_textEdit = nullptr;
    ed->removeEventFilter(this);
    ed->hide();
    ed->deleteLater();
    if (accept && m_doc) {
        TextContent c = m_editBase;
        c.text = text;
        if (!m_editLayer.isNull()) {
            if (c.isEmpty()) m_doc->removeLayer(m_editLayer);          // emptied text layer disappears
            else m_doc->setTextContent(m_editLayer, c, tr("Edit Text"));
        } else if (!c.isEmpty()) {
            QString name = text.trimmed().section('\n', 0, 0);
            if (name.size() > 24) name = name.left(24) + "…";
            m_session->addTextLayerAboveActive(c, name);
        }
    }
    m_editLayer = {};
    m_endingEdit = false;
    setFocus();
    update();
}

bool CanvasView::eventFilter(QObject* o, QEvent* e) {
    if (o == m_textEdit) {
        if (e->type() == QEvent::KeyPress) {
            auto* k = static_cast<QKeyEvent*>(e);
            if (k->key() == Qt::Key_Escape) { endTextEdit(false); return true; }
            if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && (k->modifiers() & Qt::ControlModifier)) { endTextEdit(true); return true; }
        } else if (e->type() == QEvent::FocusOut) {
            QMetaObject::invokeMethod(this, [this] { endTextEdit(true); }, Qt::QueuedConnection);
        }
    }
    return QWidget::eventFilter(o, e);
}

}  // namespace mynd
