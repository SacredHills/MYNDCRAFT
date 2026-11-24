#include "UI/Panels.h"

#include <QColorDialog>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>

#include "UI/Icons.h"

namespace mynd {

static const int kIdRole = Qt::UserRole + 1;

// =============================================================== Layer panel
void LayerTree::dropEvent(QDropEvent* e) {
    QTreeWidgetItem* dragged = currentItem();
    QTreeWidgetItem* target = itemAt(e->position().toPoint());
    const int ind = int(dropIndicatorPosition());
    if (dragged) {
        emit dropRequested(QUuid(dragged->data(0, kIdRole).toString()),
                           target ? QUuid(target->data(0, kIdRole).toString()) : QUuid(), ind);
    }
    e->setDropAction(Qt::IgnoreAction);  // the model change rebuilds the tree; never let Qt move items itself
    e->accept();
    stopAutoScroll();
    setState(NoState);
    viewport()->update();
}

LayerPanel::LayerPanel(EditorSession* s, QWidget* parent) : QWidget(parent), m_s(s) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    m_tree = new LayerTree(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Layer"), tr("Lock")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->setColumnWidth(1, 44);
    m_tree->setRootIsDecorated(true);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    lay->addWidget(m_tree, 1);

    auto* row = new QHBoxLayout;
    m_blend = new QComboBox;
    for (BlendMode m : allBlendModes()) m_blend->addItem(blendModeName(m), int(m));
    row->addWidget(m_blend, 1);
    m_opacityLabel = new QLabel("100%");
    m_opacityLabel->setMinimumWidth(36);
    m_opacity = new QSlider(Qt::Horizontal);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(100);
    row->addWidget(new QLabel(tr("Opacity")));
    row->addWidget(m_opacity, 2);
    row->addWidget(m_opacityLabel);
    lay->addLayout(row);

    auto* btns = new QHBoxLayout;
    auto addBtn = [&](const QString& text, const QString& tip, std::function<void()> fn) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [fn] { fn(); });
        btns->addWidget(b);
        return b;
    };
    auto* bR = addBtn("", tr("New raster layer"), [this] { m_s->addLayerAboveActive(LayerType::Raster, tr("Raster Layer")); });
    bR->setIcon(icons::layerType(LayerType::Raster));
    auto* bV = addBtn("", tr("New vector layer"), [this] { m_s->addLayerAboveActive(LayerType::Vector, tr("Vector Layer")); });
    bV->setIcon(icons::layerType(LayerType::Vector));
    auto* bA = addBtn("", tr("New animation layer (frame-by-frame)"), [this] { m_s->addLayerAboveActive(LayerType::Animation, tr("Animation Layer")); });
    bA->setIcon(icons::layerType(LayerType::Animation));
    auto* bG = addBtn("", tr("New group"), [this] { m_s->addLayerAboveActive(LayerType::Group, tr("Group")); });
    bG->setIcon(icons::layerType(LayerType::Group));
    addBtn("⧉", tr("Duplicate layer"), [this] { duplicateActive(); });
    addBtn("▲", tr("Move layer up"), [this] { moveActive(+1); });
    addBtn("▼", tr("Move layer down"), [this] { moveActive(-1); });
    addBtn("🗑", tr("Delete layer"), [this] { deleteActive(); });
    btns->addStretch();
    lay->addLayout(btns);

    connect(m_tree, &QTreeWidget::itemChanged, this, &LayerPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        if (m_busy || !it) return;
        m_s->setActiveLayer(QUuid(it->data(0, kIdRole).toString()));
        refreshControls();
    });
    connect(m_tree, &LayerTree::dropRequested, this, &LayerPanel::onDrop);
    connect(m_s, &EditorSession::documentChanged, this, [this] {
        for (auto& c : m_docConns) disconnect(c);
        m_docConns.clear();
        if (m_s->doc()) {
            m_docConns << connect(m_s->doc(), &Document::layerTreeChanged, this, &LayerPanel::rebuild)
                       << connect(m_s->doc(), &Document::layerPropsChanged, this, [this] { rebuild(); });
        }
        rebuild();
    });
    connect(m_s, &EditorSession::activeLayerChanged, this, [this] { syncSelection(); refreshControls(); });

    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        m_opacityLabel->setText(QString::number(v) + "%");
        if (m_busy || !m_s->doc()) return;
        Layer* l = m_s->activeLayer();
        if (!l) return;
        LayerProps p = l->props;
        p.opacity = v / 100.0;
        m_s->doc()->setLayerProps(l->id, p, "opacity");
    });
    connect(m_blend, &QComboBox::activated, this, [this](int i) {
        if (m_busy || !m_s->doc()) return;
        Layer* l = m_s->activeLayer();
        if (!l) return;
        LayerProps p = l->props;
        p.blend = BlendMode(m_blend->itemData(i).toInt());
        m_s->doc()->setLayerProps(l->id, p);
    });
}

QTreeWidgetItem* LayerPanel::makeItem(const Layer& l) {
    auto* it = new QTreeWidgetItem;
    it->setText(0, l.props.name);
    it->setIcon(0, icons::layerType(l.type));
    it->setData(0, kIdRole, l.id.toString());
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEditable |
                 Qt::ItemIsDragEnabled | (l.isGroup() ? Qt::ItemIsDropEnabled : Qt::NoItemFlags));
    it->setCheckState(0, l.props.visible ? Qt::Checked : Qt::Unchecked);
    it->setCheckState(1, l.props.locked ? Qt::Checked : Qt::Unchecked);
    it->setToolTip(0, tr("Checkbox: visibility. Double-click to rename. Drag to reorder."));
    it->setToolTip(1, tr("Checked = locked"));
    if (!l.props.visible) it->setForeground(0, QColor(130, 130, 130));
    // Top-most layer first.
    for (auto itc = l.children.rbegin(); itc != l.children.rend(); ++itc) it->addChild(makeItem(**itc));
    return it;
}

void LayerPanel::rebuild() {
    if (m_busy) return;
    if (m_inItemEdit) {  // never delete the item whose change notification we are inside of
        if (!m_rebuildQueued) { m_rebuildQueued = true; QMetaObject::invokeMethod(this, [this] { m_rebuildQueued = false; rebuild(); }, Qt::QueuedConnection); }
        return;
    }
    m_busy = true;
    QSet<QString> expanded;
    std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* it) {
        if (it->isExpanded()) expanded.insert(it->data(0, kIdRole).toString());
        for (int i = 0; i < it->childCount(); ++i) collect(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) collect(m_tree->topLevelItem(i));
    m_tree->clear();
    if (Document* d = m_s->doc()) {
        for (auto it = d->root()->children.rbegin(); it != d->root()->children.rend(); ++it)
            m_tree->addTopLevelItem(makeItem(**it));
        std::function<void(QTreeWidgetItem*)> restore = [&](QTreeWidgetItem* it) {
            const QString id = it->data(0, kIdRole).toString();
            it->setExpanded(expanded.isEmpty() || expanded.contains(id));
            for (int i = 0; i < it->childCount(); ++i) restore(it->child(i));
        };
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) restore(m_tree->topLevelItem(i));
    }
    m_busy = false;
    syncSelection();
    refreshControls();
}

void LayerPanel::syncSelection() {
    const QString id = m_s->activeLayerId().toString();
    const bool was = m_busy;
    m_busy = true;
    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find = [&](QTreeWidgetItem* it) -> QTreeWidgetItem* {
        if (it->data(0, kIdRole).toString() == id) return it;
        for (int i = 0; i < it->childCount(); ++i) if (auto* r = find(it->child(i))) return r;
        return nullptr;
    };
    QTreeWidgetItem* found = nullptr;
    for (int i = 0; i < m_tree->topLevelItemCount() && !found; ++i) found = find(m_tree->topLevelItem(i));
    m_tree->setCurrentItem(found);
    m_busy = was;
}

void LayerPanel::refreshControls() {
    const bool was = m_busy;
    m_busy = true;
    Layer* l = m_s->activeLayer();
    m_opacity->setEnabled(l);
    m_blend->setEnabled(l);
    if (l) {
        m_opacity->setValue(qRound(l->props.opacity * 100));
        m_opacityLabel->setText(QString::number(qRound(l->props.opacity * 100)) + "%");
        m_blend->setCurrentIndex(m_blend->findData(int(l->props.blend)));
    }
    m_busy = was;
}

void LayerPanel::onItemChanged(QTreeWidgetItem* it, int col) {
    if (m_busy || !m_s->doc()) return;
    Layer* l = m_s->doc()->findLayer(QUuid(it->data(0, kIdRole).toString()));
    if (!l) return;
    LayerProps p = l->props;
    if (col == 0) {
        p.visible = it->checkState(0) == Qt::Checked;
        const QString name = it->text(0).trimmed();
        p.name = name.isEmpty() ? l->props.name : name;
    } else if (col == 1) {
        p.locked = it->checkState(1) == Qt::Checked;
    }
    m_inItemEdit = true;
    m_s->doc()->setLayerProps(l->id, p);
    m_inItemEdit = false;
}

void LayerPanel::onDrop(const QUuid& dragged, const QUuid& target, int indicator) {
    Document* d = m_s->doc();
    if (!d) return;
    Layer* dl = d->findLayer(dragged);
    if (!dl) return;
    QUuid parent; int index = -1;
    Layer* tl = d->findLayer(target);
    // QAbstractItemView::DropIndicatorPosition: OnItem=0, AboveItem=1, BelowItem=2, OnViewport=3
    if (!tl || indicator == 3) { parent = {}; index = int(d->root()->children.size()); }
    else if (indicator == 0 && tl->isGroup()) { parent = tl->id; index = int(tl->children.size()); }
    else {
        parent = tl->parent && tl->parent != d->root() ? tl->parent->id : QUuid();
        const int ti = d->indexInParent(tl);
        // Tree shows top-most first, so "above" in the tree means a higher index in the model.
        index = (indicator == 1) ? ti + 1 : ti;
    }
    // The move command removes the layer first; adjust for same-list shifts.
    Layer* destParent = parent.isNull() ? d->root() : d->findLayer(parent);
    if (destParent && dl->parent == destParent && d->indexInParent(dl) < index) --index;
    d->moveLayer(dragged, parent, index);
    m_s->setActiveLayer(dragged);
}

void LayerPanel::moveActive(int delta) {
    Document* d = m_s->doc();
    Layer* l = m_s->activeLayer();
    if (!d || !l || !l->parent) return;
    const int idx = d->indexInParent(l);
    const int n = int(l->parent->children.size());
    const int ni = idx + delta;
    if (ni < 0 || ni >= n) return;
    const QUuid parent = l->parent == d->root() ? QUuid() : l->parent->id;
    d->moveLayer(l->id, parent, ni);
}
void LayerPanel::deleteActive() { if (m_s->doc() && m_s->activeLayer()) m_s->doc()->removeLayer(m_s->activeLayerId()); }
void LayerPanel::duplicateActive() {
    if (!m_s->doc() || !m_s->activeLayer()) return;
    const QUuid id = m_s->doc()->duplicateLayer(m_s->activeLayerId());
    if (!id.isNull()) m_s->setActiveLayer(id);
}
void LayerPanel::groupActive() {
    if (!m_s->doc() || !m_s->activeLayer()) return;
    const QUuid g = m_s->doc()->groupLayer(m_s->activeLayerId());
    if (!g.isNull()) m_s->setActiveLayer(g);
}
void LayerPanel::ungroupActive() {
    Layer* l = m_s->activeLayer();
    if (m_s->doc() && l && l->isGroup()) m_s->doc()->ungroupLayer(l->id);
}

// =============================================================== Style panel
static void setSwatch(QToolButton* b, const QColor& c) { b->setIcon(icons::colorSwatch(c, 24)); b->setIconSize({24, 24}); }

StylePanel::StylePanel(EditorSession* s, QWidget* parent) : QWidget(parent), m_s(s) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    auto* top = new QHBoxLayout;
    m_fg = new QToolButton; m_fg->setToolTip(tr("Foreground color (brush, bucket, gradient start)"));
    m_bg = new QToolButton; m_bg->setToolTip(tr("Background color (gradient end)"));
    auto* swap = new QToolButton; swap->setText("⇄"); swap->setToolTip(tr("Swap colors (X)"));
    top->addWidget(new QLabel(tr("Paint")));
    top->addWidget(m_fg); top->addWidget(m_bg); top->addWidget(swap); top->addStretch();
    lay->addLayout(top);

    auto* form = new QFormLayout;
    m_fillOn = new QCheckBox(tr("Fill"));
    m_fill = new QToolButton;
    auto* fr = new QHBoxLayout; fr->addWidget(m_fillOn); fr->addWidget(m_fill); fr->addStretch();
    form->addRow(tr("Vector"), fr);
    m_strokeOn = new QCheckBox(tr("Stroke"));
    m_stroke = new QToolButton;
    m_width = new QDoubleSpinBox; m_width->setRange(0.1, 500); m_width->setDecimals(1); m_width->setSuffix(" px");
    auto* sr = new QHBoxLayout; sr->addWidget(m_strokeOn); sr->addWidget(m_stroke); sr->addWidget(m_width); sr->addStretch();
    form->addRow("", sr);
    lay->addLayout(form);
    lay->addStretch();

    auto pick = [this](QToolButton* b, QColor cur, std::function<void(QColor)> apply) {
        connect(b, &QToolButton::clicked, this, [this, cur, apply, b] {
            Q_UNUSED(cur);
            QColor c = QColorDialog::getColor(Qt::white, this, tr("Choose color"), QColorDialog::ShowAlphaChannel);
            Q_UNUSED(b);
            if (c.isValid()) apply(c);
        });
    };
    // The lambdas read the live value so the dialog opens at the current color.
    connect(m_fg, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_s->foreground(), this, tr("Foreground color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) m_s->setForeground(c);
    });
    connect(m_bg, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_s->background(), this, tr("Background color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) m_s->setBackground(c);
    });
    connect(m_fill, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_s->style().fill, this, tr("Fill color"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        VectorStyle st = m_s->style(); st.fill = c; st.fillEnabled = true; m_s->setStyle(st);
    });
    connect(m_stroke, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_s->style().stroke, this, tr("Stroke color"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        VectorStyle st = m_s->style(); st.stroke = c; st.strokeEnabled = true; m_s->setStyle(st);
    });
    Q_UNUSED(pick);
    connect(swap, &QToolButton::clicked, m_s, &EditorSession::swapColors);
    connect(m_fillOn, &QCheckBox::toggled, this, [this] { pushStyle(); });
    connect(m_strokeOn, &QCheckBox::toggled, this, [this] { pushStyle(); });
    connect(m_width, &QDoubleSpinBox::valueChanged, this, [this] { pushStyle(); });
    connect(m_s, &EditorSession::colorsChanged, this, &StylePanel::refresh);
    connect(m_s, &EditorSession::styleChanged, this, &StylePanel::refresh);
    refresh();
}

void StylePanel::pushStyle() {
    if (m_busy) return;
    VectorStyle st = m_s->style();
    st.fillEnabled = m_fillOn->isChecked();
    st.strokeEnabled = m_strokeOn->isChecked();
    st.strokeWidth = m_width->value();
    m_s->setStyle(st);
}

void StylePanel::refresh() {
    m_busy = true;
    setSwatch(m_fg, m_s->foreground());
    setSwatch(m_bg, m_s->background());
    const VectorStyle& st = m_s->style();
    setSwatch(m_fill, st.fill);
    setSwatch(m_stroke, st.stroke);
    m_fillOn->setChecked(st.fillEnabled);
    m_strokeOn->setChecked(st.strokeEnabled);
    m_width->setValue(st.strokeWidth);
    m_busy = false;
}

// =============================================================== Tool options
ToolOptionsPanel::ToolOptionsPanel(EditorSession* s, QWidget* parent) : QWidget(parent), m_s(s) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    m_title = new QLabel;
    QFont f = m_title->font(); f.setBold(true); m_title->setFont(f);
    outer->addWidget(m_title);
    m_form = new QFormLayout;
    outer->addLayout(m_form);
    outer->addStretch();

    auto add = [&](const QString& label, double mn, double mx, int dec, const QString& suf, double scale, auto get, auto set, bool hardness = false) {
        QWidget* w = sliderRow(label, mn, mx, dec, suf, get, set, scale);
        (hardness ? m_hardnessRows : m_brushRows).append(w);
    };
    add(tr("Size"), 1, 500, 0, " px", 1.0, [](const BrushSettings& b) { return b.size; }, [](BrushSettings& b, double v) { b.size = v; });
    add(tr("Opacity"), 0, 100, 0, " %", 100.0, [](const BrushSettings& b) { return b.opacity; }, [](BrushSettings& b, double v) { b.opacity = v; });
    add(tr("Flow"), 1, 100, 0, " %", 100.0, [](const BrushSettings& b) { return b.flow; }, [](BrushSettings& b, double v) { b.flow = v; });
    add(tr("Hardness"), 0, 100, 0, " %", 100.0, [](const BrushSettings& b) { return b.hardness; }, [](BrushSettings& b, double v) { b.hardness = v; }, true);
    add(tr("Spacing"), 1, 200, 0, " %", 100.0, [](const BrushSettings& b) { return b.spacing; }, [](BrushSettings& b, double v) { b.spacing = v; });
    add(tr("Stabilizer"), 0, 100, 0, " %", 100.0, [](const BrushSettings& b) { return b.stabilization; }, [](BrushSettings& b, double v) { b.stabilization = v; });

    m_pressureRow = new QWidget;
    auto* pl = new QVBoxLayout(m_pressureRow); pl->setContentsMargins(0, 0, 0, 0);
    m_pSize = new QCheckBox(tr("Pressure controls size"));
    m_pOpacity = new QCheckBox(tr("Pressure controls opacity"));
    pl->addWidget(m_pSize); pl->addWidget(m_pOpacity);
    m_form->addRow(m_pressureRow);
    connect(m_pSize, &QCheckBox::toggled, this, [this](bool on) { if (!m_busy) { m_s->brushFor(m_s->tool()).pressureSize = on; m_s->notifyBrushChanged(); } });
    connect(m_pOpacity, &QCheckBox::toggled, this, [this](bool on) { if (!m_busy) { m_s->brushFor(m_s->tool()).pressureOpacity = on; m_s->notifyBrushChanged(); } });

    m_sides = new QSpinBox; m_sides->setRange(3, 64); m_sides->setValue(m_s->polygonSides);
    m_form->addRow(tr("Polygon sides"), m_sides);
    m_sidesRow = m_form->itemAt(m_form->rowCount() - 1, QFormLayout::FieldRole)->widget();
    connect(m_sides, &QSpinBox::valueChanged, this, [this](int v) { m_s->polygonSides = v; });
    m_tol = new QSpinBox; m_tol->setRange(0, 255); m_tol->setValue(m_s->bucketTolerance);
    m_form->addRow(tr("Fill tolerance"), m_tol);
    m_tolRow = m_form->itemAt(m_form->rowCount() - 1, QFormLayout::FieldRole)->widget();
    connect(m_tol, &QSpinBox::valueChanged, this, [this](int v) { m_s->bucketTolerance = v; });

    if (m_brushRows.size() > 2) m_flowLabel = qobject_cast<QLabel*>(m_form->labelForField(m_brushRows[2]));
    buildTextRows();

    connect(m_s, &EditorSession::toolChanged, this, &ToolOptionsPanel::refresh);
    connect(m_s, &EditorSession::textStyleChanged, this, &ToolOptionsPanel::refreshText);
    connect(m_s, &EditorSession::activeLayerChanged, this, &ToolOptionsPanel::refresh);
    refresh();
}

void ToolOptionsPanel::buildTextRows() {
    auto addRow = [&](const QString& label, QWidget* w) {
        m_form->addRow(label, w);
        m_textRows.append(w);
    };
    m_font = new QFontComboBox;
    addRow(tr("Font"), m_font);
    m_tSize = new QDoubleSpinBox; m_tSize->setRange(1, 2000); m_tSize->setDecimals(0); m_tSize->setSuffix(" px");
    addRow(tr("Text size"), m_tSize);
    auto* styleRow = new QWidget; auto* sl = new QHBoxLayout(styleRow); sl->setContentsMargins(0, 0, 0, 0);
    m_tBold = new QCheckBox(tr("Bold")); m_tItalic = new QCheckBox(tr("Italic"));
    sl->addWidget(m_tBold); sl->addWidget(m_tItalic); sl->addStretch();
    addRow(tr("Style"), styleRow);
    m_tAlign = new QComboBox; m_tAlign->addItems({tr("Left"), tr("Center"), tr("Right")});
    addRow(tr("Align"), m_tAlign);
    m_tTrack = new QDoubleSpinBox; m_tTrack->setRange(-50, 200); m_tTrack->setDecimals(1); m_tTrack->setSuffix(" px");
    addRow(tr("Tracking"), m_tTrack);
    m_tBox = new QDoubleSpinBox; m_tBox->setRange(0, 20000); m_tBox->setDecimals(0); m_tBox->setSuffix(" px");
    m_tBox->setSpecialValueText(tr("Auto (point text)"));
    addRow(tr("Wrap width"), m_tBox);
    m_tColor = new QPushButton;
    addRow(tr("Color"), m_tColor);

    auto edit = [this](const std::function<void(TextContent&)>& fn) {
        if (m_busy) return;
        TextContent s = m_s->textStyle();
        fn(s);
        m_s->applyTextStyle(s);
    };
    connect(m_font, &QFontComboBox::currentFontChanged, this, [=](const QFont& f) { edit([&](TextContent& s) { s.family = f.family(); }); });
    connect(m_tSize, &QDoubleSpinBox::valueChanged, this, [=](double v) { edit([&](TextContent& s) { s.sizePx = v; }); });
    connect(m_tBold, &QCheckBox::toggled, this, [=](bool v) { edit([&](TextContent& s) { s.bold = v; }); });
    connect(m_tItalic, &QCheckBox::toggled, this, [=](bool v) { edit([&](TextContent& s) { s.italic = v; }); });
    connect(m_tAlign, &QComboBox::currentIndexChanged, this, [=](int i) { edit([&](TextContent& s) { s.align = TextAlign(i); }); });
    connect(m_tTrack, &QDoubleSpinBox::valueChanged, this, [=](double v) { edit([&](TextContent& s) { s.tracking = v; }); });
    connect(m_tBox, &QDoubleSpinBox::valueChanged, this, [=](double v) { edit([&](TextContent& s) { s.boxWidth = v; }); });
    connect(m_tColor, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_s->textStyle().color, this, tr("Text Color"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        TextContent s = m_s->textStyle(); s.color = c; m_s->applyTextStyle(s);
    });
}

void ToolOptionsPanel::refreshText() {
    const bool was = m_busy;
    m_busy = true;
    const TextContent& s = m_s->textStyle();
    m_font->setCurrentFont(QFont(s.family));
    m_tSize->setValue(s.sizePx);
    m_tBold->setChecked(s.bold); m_tItalic->setChecked(s.italic);
    m_tAlign->setCurrentIndex(int(s.align));
    m_tTrack->setValue(s.tracking);
    m_tBox->setValue(s.boxWidth);
    m_tColor->setText(s.color.name(QColor::HexArgb));
    m_tColor->setStyleSheet(QString("QPushButton { background:%1; color:%2; }").arg(s.color.name(), s.color.lightness() > 128 ? "#000" : "#fff"));
    m_busy = was;
}

QWidget* ToolOptionsPanel::sliderRow(const QString& label, double mn, double mx, int dec, const QString& suffix,
                                     const std::function<double(const BrushSettings&)>& get,
                                     const std::function<void(BrushSettings&, double)>& set, double scale) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal);
    const int mult = dec > 0 ? 10 : 1;
    slider->setRange(int(mn * mult), int(mx * mult));
    auto* spin = new QDoubleSpinBox;
    spin->setRange(mn, mx); spin->setDecimals(dec); spin->setSuffix(suffix); spin->setMinimumWidth(78);
    h->addWidget(slider, 1); h->addWidget(spin);
    m_form->addRow(label, w);
    connect(slider, &QSlider::valueChanged, this, [=, this](int v) {
        if (m_busy) return;
        spin->setValue(double(v) / mult);
    });
    connect(spin, &QDoubleSpinBox::valueChanged, this, [=, this](double v) {
        slider->blockSignals(true); slider->setValue(int(v * mult)); slider->blockSignals(false);
        if (m_busy) return;
        set(m_s->brushFor(m_s->tool()), v / scale);
        m_s->notifyBrushChanged();
    });
    m_refreshers.append({w, [=, this] {
        const double v = get(m_s->brush(m_s->tool())) * scale;
        spin->setValue(v);
        slider->blockSignals(true); slider->setValue(int(v * mult)); slider->blockSignals(false);
    }});
    return w;
}

void ToolOptionsPanel::refresh() {
    m_busy = true;
    const ToolId t = m_s->tool();
    m_title->setText(toolName(t));
    const bool brushy = t == ToolId::Brush || t == ToolId::Pencil || t == ToolId::Eraser || t == ToolId::Smudge;
    const Layer* al = m_s->activeLayer();
    const bool textUi = t == ToolId::Text || (al && al->type == LayerType::Text && t == ToolId::Select);
    for (auto& r : m_refreshers) r.second();
    if (m_flowLabel) m_flowLabel->setText(t == ToolId::Smudge ? tr("Strength") : tr("Flow"));
    for (QWidget* w : m_textRows) m_form->setRowVisible(w, textUi);
    refreshText();
    auto vis = [&](QWidget* w, bool v) { m_form->setRowVisible(w, v); };
    for (QWidget* w : m_brushRows) vis(w, brushy);
    for (QWidget* w : m_hardnessRows) vis(w, brushy && t != ToolId::Pencil);
    vis(m_pressureRow, brushy);
    vis(m_sidesRow, t == ToolId::Polygon);
    vis(m_tolRow, t == ToolId::Bucket);
    if (textUi && t != ToolId::Text) m_title->setText(tr("Text Layer"));
    if (brushy) {
        const BrushSettings b = m_s->brush(t);
        m_pSize->setChecked(b.pressureSize);
        m_pOpacity->setChecked(b.pressureOpacity);
    }
    m_busy = false;
}

// =============================================================== History
HistoryPanel::HistoryPanel(EditorSession* s, QWidget* parent) : QUndoView(parent) {
    setEmptyLabel(tr("<original>"));
    auto bind = [this, s] { setStack(s->doc() ? s->doc()->undoStack() : nullptr); };
    connect(s, &EditorSession::documentChanged, this, bind);
    bind();
}

}  // namespace mynd
