#include <QTimer>
#include <QShowEvent>
#include "UI/MainWindow.h"

#include <QShortcut>
#include "UI/GraphEditor.h"
#include "UI/SymbolPanel.h"
#include "UI/Scene3DActions.h"
#include "UI/Scene3DPanel.h"
#include "UI/Viewport3D.h"
#include <QTabWidget>
#include <QLabel>
#include <QToolButton>

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

#include <QCheckBox>
#include <QComboBox>
#include <QProgressDialog>
#include <QSpinBox>
#include "Core/Exporter.h"
#include "Core/Settings.h"
#include "Graphics/Compositor.h"
#include "UI/ExposureSheet.h"
#include "UI/Icons.h"
#include "UI/OnionSkinPanel.h"
#include "UI/PropertiesPanel.h"
#include "UI/TimelinePanel.h"

namespace mynd {

namespace {
const char* kFilter = "MyndCraft Project (*.studio)";
}

MainWindow::MainWindow() {
    setObjectName("MainWindow");
    setMinimumSize(1000, 650);   // below this the docked panels cannot fit and start overlapping the toolbars
    setAcceptDrops(true);
    resize(1500, 900);
    m_session = new EditorSession(this);
    m_canvas = new CanvasView(m_session, this);
    {   // The canvas is a dock panel (movable, floatable like the others) inside a small inner window that fills the middle. Sizing is trivial
        // there (one panel, nothing else), and the outer panel layout never depends on the canvas.
        auto* host = new QMainWindow(this);
        host->setObjectName("canvasHost");
        host->setWindowFlags(Qt::Widget);
        auto* ph = new QWidget(host);
        ph->setMaximumSize(0, 0);
        host->setCentralWidget(ph);
        m_canvasDock = new QDockWidget(tr("Canvas"), host);
        m_canvasDock->setObjectName("dock.canvas");
        m_canvasDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);   // never closable
        m_canvasDock->setWidget(m_canvas);
        host->addDockWidget(Qt::TopDockWidgetArea, m_canvasDock);
        setCentralWidget(host);
    }
    m_autosave = new Autosaver(this);
    m_autosave->setEnabled(settings::autosaveEnabled());
    m_autosave->setInterval(settings::autosaveIntervalSeconds());

    buildActions();
    buildToolbars();
    buildDocks();
    buildMenus();

    m_zoomLabel = new QLabel; m_posLabel = new QLabel; m_layerLabel = new QLabel;
    for (QLabel* l : {m_layerLabel, m_posLabel, m_zoomLabel}) { l->setMinimumWidth(110); statusBar()->addPermanentWidget(l); }
    connect(m_canvas, &CanvasView::viewChanged, this, [this] {
        m_zoomLabel->setText(tr("%1%  ∠%2°").arg(qRound(m_canvas->zoom() * 100)).arg(qRound(m_canvas->rotation())));
    });
    connect(m_canvas, &CanvasView::cursorMoved, this, [this](const QPointF& p) {
        m_posLabel->setText(QString("%1, %2").arg(qRound(p.x())).arg(qRound(p.y())));
    });
    connect(m_session, &EditorSession::message, this, [this](const QString& m) { statusBar()->showMessage(m, 5000); });
    connect(m_session, &EditorSession::activeLayerChanged, this, [this] {
        Layer* l = m_session->activeLayer();
        m_layerLabel->setText(l ? l->props.name : QString());
        updateActionStates();
    });
    connect(m_session, &EditorSession::selectionChanged, this, [this] { updateActionStates(); });
    connect(m_session, &EditorSession::toolChanged, this, [this](ToolId t) {
        if (auto* a = m_toolActions.value(t)) a->setChecked(true);
        statusBar()->showMessage(toolName(t), 2000);
    });

    const QByteArray geo = settings::windowGeometry();
    if (!geo.isEmpty()) restoreGeometry(geo);
    const QString ws = settings::lastWorkspace();
    if (!m_workspaces->apply(ws)) m_workspaces->apply("Drawing");

    CompositionSettings cs;
    cs.gridSize = settings::defaultGridSize();
    newDocument(cs);
    updateTitle();
}

MainWindow::~MainWindow() {
    m_autosave->detach(false);
}

// ------------------------------------------------------------------ documents
void MainWindow::adoptDocument(std::unique_ptr<Document> d) {
    m_autosave->detach(true);
    m_session->setDocument(nullptr);
    m_canvas->setDocument(nullptr);
    for (auto& c : m_extraCanvases) if (c) c->setDocument(nullptr);
    m_doc = std::move(d);
    Document* doc = m_doc.get();
    m_session->setDocument(doc);
    m_canvas->setDocument(doc);
    for (auto& c : m_extraCanvases) if (c) c->setDocument(doc);
    connect(doc, &Document::modifiedChanged, this, [this](bool) { updateTitle(); });
    m_autosave->attach(doc);
    bindUndo();
    updateTitle();
    updateActionStates();
    emit m_canvas->viewChanged();
}

void MainWindow::newDocument(const CompositionSettings& s, bool withLayer) {
    auto d = std::make_unique<Document>(s);
    if (withLayer) {
        auto l = std::make_shared<Layer>(LayerType::Raster, tr("Layer 1"));
        l->raster = d->newRasterImage();
        d->rootPtr()->children.push_back(l);
        Document::fixParents(d->root());
    }
    adoptDocument(std::move(d));
    m_doc->undoStack()->clear();
    m_doc->markSaved();
}

bool MainWindow::openFile(const QString& path, QString* error) {
    LoadResult r = ProjectIO::load(path);
    if (!r.doc) { if (error) *error = r.error; return false; }
    if (!r.warnings.isEmpty()) {
        statusBar()->showMessage(tr("Opened with %1 warning(s): %2").arg(r.warnings.size()).arg(r.warnings.first()), 10000);
        if (error) *error = r.warnings.join("\n");
    }
    adoptDocument(std::move(r.doc));
    settings::addRecentFile(path);
    updateRecentMenu();
    return true;
}

bool MainWindow::importImage(const QString& path, QString* error) {
    if (!m_doc) { if (error) *error = tr("No document is open."); return false; }
    QImageReader::setAllocationLimit(2048);   // MB; Qt's default (256) rejects large but valid images
    QImageReader rd(path);
    rd.setAutoTransform(true);
    if (rd.size().isValid() && (rd.size().width() > 16384 || rd.size().height() > 16384)) {
        if (error) *error = tr("Image is larger than 16384 px.");
        return false;
    }
    const QImage img = rd.read();
    if (img.isNull()) { if (error) *error = rd.errorString(); return false; }
    m_session->addImageLayerAboveActive(img, QFileInfo(path).completeBaseName());
    statusBar()->showMessage(tr("Imported %1").arg(QFileInfo(path).fileName()), 5000);
    return true;
}

void MainWindow::actImport() {
    QStringList exts;
    for (const QByteArray& f : QImageReader::supportedImageFormats()) exts << "*." + QString::fromLatin1(f).toLower();
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Image"), QString(),
                                                      tr("Images (%1);;All files (*)").arg(exts.join(' ')));
    if (path.isEmpty()) return;
    QString err;
    if (!importImage(path, &err)) QMessageBox::warning(this, tr("Cannot import image"), err);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        const QString p = u.toLocalFile();
        QString err;
        if (p.endsWith(".studio", Qt::CaseInsensitive)) {
            if (!maybeSave()) return;
            if (!openFile(p, &err) && !m_doc) QMessageBox::warning(this, tr("Cannot open project"), err);
        } else if (!importImage(p, &err)) {
            QMessageBox::warning(this, tr("Cannot import image"), err);
        }
    }
    e->acceptProposedAction();
}

bool MainWindow::saveFile(const QString& path, QString* error) {
    if (!m_doc) return false;
    QString err;
    if (!ProjectIO::save(*m_doc, path, &err)) { if (error) *error = err; return false; }
    settings::addRecentFile(path);
    updateRecentMenu();
    // Saving makes the autosave redundant.
    m_autosave->detach(true);
    m_autosave->attach(m_doc.get());
    updateTitle();
    return true;
}

bool MainWindow::exportImage(const QString& path, QString* error) {
    if (!m_doc) return false;
    QImage img = Compositor::render(*m_doc);
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg" || ext == "bmp") {
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat); p.drawImage(0, 0, img); p.end();
        img = flat;
    }
    if (!img.save(path)) { if (error) *error = tr("Could not write %1 (unsupported format or path).").arg(path); return false; }
    return true;
}

bool MainWindow::maybeSave() {
    if (!m_doc || !m_doc->isModified()) return true;
    const auto r = QMessageBox::warning(this, tr("Unsaved changes"),
                                        tr("Save changes to \"%1\" before closing?").arg(m_doc->filePath().isEmpty() ? tr("Untitled") : QFileInfo(m_doc->filePath()).fileName()),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return actSave();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) { e->ignore(); return; }
    settings::setWindowGeometry(saveGeometry());
    m_workspaces->saveCurrent();
    m_autosave->detach(true);  // clean exit: nothing to recover
    e->accept();
}

void MainWindow::offerRecovery() {
    const auto entries = Autosaver::findRecoverable();
    for (const RecoveryEntry& e : entries) {
        const auto r = QMessageBox::question(this, tr("Recover unsaved work"),
            tr("MyndCraft found an autosaved project from %1%2.\nRecover it?")
                .arg(e.when.toString(Qt::TextDate), e.originalPath.isEmpty() ? QString() : tr(" (originally %1)").arg(e.originalPath)));
        if (r == QMessageBox::Yes) {
            LoadResult lr = ProjectIO::load(e.autosavePath);
            if (lr.doc) {
                lr.doc->setFilePath(e.originalPath);
                adoptDocument(std::move(lr.doc));
                m_doc->markDirty();
                Autosaver::discard(e);
                return;
            }
            QMessageBox::warning(this, tr("Recovery failed"), lr.error);
        }
        Autosaver::discard(e);
    }
}

// ------------------------------------------------------------------ actions
void MainWindow::buildActions() {
    auto mk = [this](const QString& text, const QKeySequence& ks = {}) {
        auto* a = new QAction(text, this);
        if (!ks.isEmpty()) a->setShortcut(ks);
        return a;
    };
    aNew = mk(tr("&New…"), QKeySequence::New);       connect(aNew, &QAction::triggered, this, &MainWindow::actNew);
    aOpen = mk(tr("&Open…"), QKeySequence::Open);    connect(aOpen, &QAction::triggered, this, &MainWindow::actOpen);
    aSave = mk(tr("&Save"), QKeySequence::Save);     connect(aSave, &QAction::triggered, this, [this] { actSave(); });
    aSaveAs = mk(tr("Save &As…"), QKeySequence::SaveAs); connect(aSaveAs, &QAction::triggered, this, [this] { actSaveAs(); });
    aImport = mk(tr("&Import Image…"), QKeySequence("Ctrl+Shift+I")); connect(aImport, &QAction::triggered, this, &MainWindow::actImport);
    aExport = mk(tr("&Export Image…"), QKeySequence("Ctrl+E")); connect(aExport, &QAction::triggered, this, &MainWindow::actExport);
    aExportAnim = mk(tr("Export &Animation…"), QKeySequence("Ctrl+Shift+E")); connect(aExportAnim, &QAction::triggered, this, &MainWindow::actExportAnimation);
    aPrefs = mk(tr("&Preferences…"), QKeySequence::Preferences); connect(aPrefs, &QAction::triggered, this, &MainWindow::actPreferences);
    aQuit = mk(tr("&Quit"), QKeySequence::Quit); connect(aQuit, &QAction::triggered, this, &QWidget::close);

    aUndo = mk(tr("&Undo"), QKeySequence::Undo);
    aRedo = mk(tr("&Redo"), QKeySequence::Redo);
    aUndo->setEnabled(false); aRedo->setEnabled(false);
    connect(aUndo, &QAction::triggered, this, [this] { if (m_doc) m_doc->undoStack()->undo(); });
    connect(aRedo, &QAction::triggered, this, [this] { if (m_doc) m_doc->undoStack()->redo(); });

    aSelAll = mk(tr("Select &All Shapes"), QKeySequence::SelectAll);
    connect(aSelAll, &QAction::triggered, this, [this] {
        Layer* l = m_session->activeLayer();
        if (!l || l->type != LayerType::Vector) return;
        QSet<QUuid> s; for (const auto& sh : l->shapes) s.insert(sh.id);
        m_session->setSelectedShapes(s, false);
    });
    aDeselect = mk(tr("&Deselect"), QKeySequence("Ctrl+D"));
    connect(aDeselect, &QAction::triggered, m_session, &EditorSession::clearSelection);
    aDeleteSel = mk(tr("Delete Selected Shapes"));
    connect(aDeleteSel, &QAction::triggered, this, [this] {
        mutateShapes(tr("Delete Shapes"), [](ShapeList& s, const QSet<QUuid>& sel) {
            s.erase(std::remove_if(s.begin(), s.end(), [&](const VectorShape& x) { return sel.contains(x.id); }), s.end());
        });
        m_session->clearSelection();
    });

    aZoomIn = mk(tr("Zoom &In"), QKeySequence("Ctrl+="));   connect(aZoomIn, &QAction::triggered, this, [this] { m_canvas->setZoom(m_canvas->zoom() * 1.25); });
    aZoomOut = mk(tr("Zoom &Out"), QKeySequence("Ctrl+-")); connect(aZoomOut, &QAction::triggered, this, [this] { m_canvas->setZoom(m_canvas->zoom() / 1.25); });
    aFit = mk(tr("&Fit to Screen"), QKeySequence("Ctrl+0")); connect(aFit, &QAction::triggered, m_canvas, &CanvasView::fitToView);
    aActual = mk(tr("&Actual Pixels (100%)"), QKeySequence("Ctrl+1")); connect(aActual, &QAction::triggered, m_canvas, &CanvasView::actualPixels);
    aRotL = mk(tr("Rotate Canvas &Left 15°"), QKeySequence("Ctrl+Alt+Left")); connect(aRotL, &QAction::triggered, this, [this] { m_canvas->rotateBy(-15); });
    aRotR = mk(tr("Rotate Canvas &Right 15°"), QKeySequence("Ctrl+Alt+Right")); connect(aRotR, &QAction::triggered, this, [this] { m_canvas->rotateBy(15); });
    aRotReset = mk(tr("Reset Canvas Rotation"), QKeySequence("Ctrl+Alt+Up")); connect(aRotReset, &QAction::triggered, m_canvas, &CanvasView::resetRotation);
    auto toggle = [&](const QString& text, const QKeySequence& ks, bool* flag) {
        auto* a = mk(text, ks); a->setCheckable(true); a->setChecked(*flag);
        connect(a, &QAction::toggled, this, [this, flag](bool on) { *flag = on; emit m_session->viewTogglesChanged(); });
        return a;
    };
    aGrid = toggle(tr("Show &Grid"), QKeySequence("Ctrl+'"), &m_session->gridVisible);
    aRulers = toggle(tr("Show &Rulers"), QKeySequence("Ctrl+R"), &m_session->rulersVisible);
    aGuides = toggle(tr("Show G&uides"), QKeySequence("Ctrl+Shift+;"), &m_session->guidesVisible);
    aSnap = toggle(tr("&Snap to Grid / Guides"), QKeySequence("Ctrl+;"), &m_session->snapEnabled);
    aDetachCanvas = mk(tr("New Canvas Window"));
    aDetachCanvas->setToolTip(tr("Opens another view of this document in its own window that can be dragged anywhere (or onto another screen). The main canvas stays where it is."));
    connect(aDetachCanvas, &QAction::triggered, this, [this] { if (m_doc) openCanvasWindow(); });
    aClearGuides = mk(tr("Clear Guides"));
    connect(aClearGuides, &QAction::triggered, this, [this] {
        if (m_doc && !m_doc->comp().guides.isEmpty()) { m_doc->compMutable().guides.clear(); m_doc->markDirty(); m_canvas->update(); }
    });

    aGroupShapes = mk(tr("&Group"), QKeySequence("Ctrl+G"));
    connect(aGroupShapes, &QAction::triggered, this, [this] {
        mutateShapes(tr("Group Shapes"), [](ShapeList& s, const QSet<QUuid>& sel) {
            if (sel.size() < 2) return;
            const QUuid g = QUuid::createUuid();
            for (auto& x : s) if (sel.contains(x.id)) x.groupId = g;
        });
    });
    aUngroupShapes = mk(tr("&Ungroup"), QKeySequence("Ctrl+Shift+G"));
    connect(aUngroupShapes, &QAction::triggered, this, [this] {
        mutateShapes(tr("Ungroup Shapes"), [](ShapeList& s, const QSet<QUuid>& sel) {
            for (auto& x : s) if (sel.contains(x.id)) x.groupId = QUuid();
        });
    });
    auto arrange = [this](const QString& text, int mode) {  // 0 front, 1 forward, 2 backward, 3 back
        mutateShapes(text, [mode](ShapeList& s, const QSet<QUuid>& sel) {
            ShapeList out;
            if (mode == 0 || mode == 3) {
                ShapeList a, b;
                for (const auto& x : s) (sel.contains(x.id) ? a : b).append(x);
                out = mode == 0 ? b + a : a + b;
            } else {
                out = s;
                if (mode == 1) { for (int i = out.size() - 2; i >= 0; --i) if (sel.contains(out[i].id) && !sel.contains(out[i + 1].id)) std::swap(out[i], out[i + 1]); }
                else { for (int i = 1; i < out.size(); ++i) if (sel.contains(out[i].id) && !sel.contains(out[i - 1].id)) std::swap(out[i], out[i - 1]); }
            }
            s = out;
        });
    };
    aFront = mk(tr("Bring to &Front"), QKeySequence("Ctrl+Shift+]")); connect(aFront, &QAction::triggered, this, [=] { arrange(tr("Bring to Front"), 0); });
    aForward = mk(tr("Bring &Forward"), QKeySequence("Ctrl+]")); connect(aForward, &QAction::triggered, this, [=] { arrange(tr("Bring Forward"), 1); });
    aBackward = mk(tr("Send &Backward"), QKeySequence("Ctrl+[")); connect(aBackward, &QAction::triggered, this, [=] { arrange(tr("Send Backward"), 2); });
    aBack = mk(tr("Send to Bac&k"), QKeySequence("Ctrl+Shift+[")); connect(aBack, &QAction::triggered, this, [=] { arrange(tr("Send to Back"), 3); });

    aNewRaster = mk(tr("New &Raster Layer"), QKeySequence("Ctrl+Shift+N"));
    connect(aNewRaster, &QAction::triggered, this, [this] { m_session->addLayerAboveActive(LayerType::Raster, tr("Raster Layer")); });
    aNewVector = mk(tr("New &Vector Layer"), QKeySequence("Ctrl+Alt+N"));
    connect(aNewVector, &QAction::triggered, this, [this] { m_session->addLayerAboveActive(LayerType::Vector, tr("Vector Layer")); });
    aNewGroup = mk(tr("New &Group"));
    connect(aNewGroup, &QAction::triggered, this, [this] { m_session->addLayerAboveActive(LayerType::Group, tr("Group")); });
    aDup = mk(tr("&Duplicate Layer"), QKeySequence("Ctrl+J"));
    aDelLayer = mk(tr("D&elete Layer"));
    aGroupLayer = mk(tr("Group Layer"), QKeySequence("Ctrl+Alt+G"));
    aUngroupLayer = mk(tr("Ungroup Layer Group"));
    aUp = mk(tr("Move Layer &Up"), QKeySequence("Ctrl+Alt+]"));
    aDown = mk(tr("Move Layer &Down"), QKeySequence("Ctrl+Alt+["));
    aToggleVis = mk(tr("Toggle &Visibility"));
    aToggleLock = mk(tr("Toggle &Lock"));
    aCopyLayer = mk(tr("&Copy Layer")); aCutLayer = mk(tr("Cu&t Layer")); aPasteLayer = mk(tr("&Paste Layer"));
    aRenameLayer = mk(tr("&Rename Layer…"), QKeySequence(Qt::Key_F2));
    aTopLayer = mk(tr("Bring Layer to &Front")); aBottomLayer = mk(tr("Send Layer to &Back"));
    // (LayerPanel exists after buildDocks; connect later in buildMenus)

    // Tools
    m_toolGroup = new QActionGroup(this);
    struct T { ToolId id; const char* key; };
    const T tools[] = {{ToolId::Select, "V"}, {ToolId::Brush, "B"}, {ToolId::Pencil, "N"},
                       {ToolId::Eraser, "E"}, {ToolId::Pen, "P"}, {ToolId::Line, "L"}, {ToolId::Rectangle, "M"},
                       {ToolId::Ellipse, "O"}, {ToolId::Polygon, "Y"}, {ToolId::Bucket, "K"}, {ToolId::Gradient, "G"},
                       {ToolId::Eyedropper, "I"}, {ToolId::Hand, "H"}, {ToolId::Zoom, "Z"},
                       {ToolId::Smudge, "S"}, {ToolId::FreeTransform, "Q"}, {ToolId::Text, "T"}};
    for (const T& t : tools) {
        auto* a = new QAction(icons::tool(t.id), toolName(t.id), this);
        a->setCheckable(true);
        a->setShortcut(QKeySequence(t.key));
        a->setToolTip(QString("%1 (%2)").arg(toolName(t.id), t.key));
        a->setActionGroup(m_toolGroup);
        connect(a, &QAction::triggered, this, [this, id = t.id] { m_session->setTool(id); });
        m_toolActions[t.id] = a;
    }
    m_toolActions[m_session->tool()]->setChecked(true);

    auto* sw = new QAction(this); sw->setShortcut(QKeySequence("X"));
    connect(sw, &QAction::triggered, m_session, &EditorSession::swapColors);
    auto* bs1 = new QAction(this); bs1->setShortcut(QKeySequence("["));
    connect(bs1, &QAction::triggered, this, [this] { adjustBrushSize(0.85); });
    auto* bs2 = new QAction(this); bs2->setShortcut(QKeySequence("]"));
    connect(bs2, &QAction::triggered, this, [this] { adjustBrushSize(1.0 / 0.85); });
    addActions({sw, bs1, bs2});
}

void MainWindow::adjustBrushSize(double f) {
    const ToolId t = m_session->tool();
    if (t != ToolId::Brush && t != ToolId::Pencil && t != ToolId::Eraser && t != ToolId::Smudge) return;
    BrushSettings& b = m_session->brushFor(t);
    const double ns = std::clamp(b.size * f + (f > 1 ? 1 : -1), 1.0, 500.0);
    b.size = ns;
    m_session->notifyBrushChanged();
    emit m_session->toolChanged(t);  // refresh the options panel
}

void MainWindow::mutateShapes(const QString& text, const std::function<void(ShapeList&, const QSet<QUuid>&)>& fn) {
    Layer* l = m_session->activeLayer();
    if (!m_doc || !l || l->type != LayerType::Vector || l->props.locked) return;
    const QSet<QUuid> sel = m_session->selectedShapes();
    if (sel.isEmpty()) return;
    ShapeList after = l->shapes;
    fn(after, sel);
    m_doc->setShapes(l->id, after, text);
}

void MainWindow::buildToolbars() {
    auto* tb = addToolBar(tr("Tools"));
    tb->setObjectName("tools");
    tb->setOrientation(Qt::Vertical);
    tb->setIconSize({24, 24});
    tb->setMovable(true);
    removeToolBar(tb);
    addToolBar(Qt::LeftToolBarArea, tb);
    tb->show();
    int i = 0;
    for (auto it = m_toolActions.begin(); it != m_toolActions.end(); ++it, ++i) {}
    const ToolId order[] = {ToolId::Select, ToolId::Hand, ToolId::Zoom, ToolId::Brush, ToolId::Pencil,
                            ToolId::Eraser, ToolId::Smudge, ToolId::Bucket, ToolId::Gradient, ToolId::Eyedropper, ToolId::Pen, ToolId::Line,
                            ToolId::Rectangle, ToolId::Ellipse, ToolId::Polygon, ToolId::Text, ToolId::FreeTransform};
    for (ToolId id : order) {
        tb->addAction(m_toolActions[id]);
        if (id == ToolId::Zoom || id == ToolId::Eyedropper) tb->addSeparator();
    }

    auto* top = addToolBar(tr("Workspace"));
    top->setObjectName("workspace-bar");
    top->setMovable(false);
    top->addWidget(new QLabel(tr(" Workspace: ")));
    m_wsCombo = new QComboBox;
    m_wsCombo->setMinimumWidth(160);
    top->addWidget(m_wsCombo);
    top->addSeparator();
    aDetachCanvas->setText(tr("New Canvas Window"));
    top->addAction(aDetachCanvas);                      // one click: another, movable view of the canvas
    m_crumb = new QLabel;
    m_crumb->setObjectName("breadcrumb");
    m_crumb->setContentsMargins(8, 0, 8, 0);
    m_crumbAction = top->addWidget(m_crumb);
    m_crumbExit = new QToolButton;
    m_crumbExit->setText(tr("◂ Exit Symbol"));
    m_crumbExit->setToolTip(tr("Leave the symbol and go back one level (Ctrl+Shift+Up)"));
    connect(m_crumbExit, &QToolButton::clicked, this, [this] { if (m_symbolActions) m_symbolActions->leave(); });
    m_crumbExitAction = top->addWidget(m_crumbExit);      // a toolbar hides widgets through their action, not the widget
    connect(m_session, &EditorSession::documentChanged, this, [this] {
        if (Document* d = m_session->doc()) connect(d, &Document::contextChanged, this, &MainWindow::updateBreadcrumb);
        updateBreadcrumb();
    });
    updateBreadcrumb();
}

CanvasView* MainWindow::openCanvasWindow() {
    // An extra view of the same document in its own movable window (own zoom and pan). The main canvas and the workspaces are untouched.
    auto* cv = new CanvasView(m_session);
    cv->setDocument(m_doc.get());
    static int counter = 1;
    auto* d = new QDockWidget(tr("Canvas %1").arg(++counter), this);
    d->setObjectName(QString("dock.canvasWindow.%1").arg(counter));
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    d->setWidget(cv);
    addDockWidget(Qt::TopDockWidgetArea, d);
    d->setFloating(true);
    d->resize(qMax(700, width() / 2), qMax(500, height() / 2));
    d->show();
    cv->fitToView();
    m_extraCanvases.append(cv);
    connect(cv, &QObject::destroyed, this, [this] { m_extraCanvases.removeIf([](const QPointer<CanvasView>& p) { return p.isNull(); }); });
    return cv;
}

// Each workspace remembers whether the canvas panel was floating (and where).
void MainWindow::rememberCanvasPlacement() {
    if (!m_workspaces || m_workspaces->current().isEmpty() || m_applyingCanvas) return;
    settings::setCanvasFloatRect(m_workspaces->current(), m_canvasDock->isFloating() ? m_canvasDock->geometry() : QRect());
}

void MainWindow::applyCanvasPlacement() {
    if (!m_workspaces) return;
    const QRect r = settings::canvasFloatRect(m_workspaces->current());
    m_applyingCanvas = true;
    if (r.isEmpty()) {
        if (m_canvasDock->isFloating()) m_canvasDock->setFloating(false);
    } else {
        if (!m_canvasDock->isFloating()) m_canvasDock->setFloating(true);
        m_canvasDock->setGeometry(r);
        m_canvasDock->show();
    }
    m_applyingCanvas = false;
}

// Brings the 3D panels up (placing them if the current workspace never had them).
void MainWindow::show3DPanels() {
    QDockWidget* sc = m_docks.value("scene3d");
    QDockWidget* vp = m_docks.value("viewport3d");
    if (!sc || !vp) return;
    if (!vp->isVisible() && dockWidgetArea(vp) == Qt::NoDockWidgetArea) addDockWidget(Qt::RightDockWidgetArea, vp);
    if (!sc->isVisible() && dockWidgetArea(sc) == Qt::NoDockWidgetArea) addDockWidget(Qt::LeftDockWidgetArea, sc);
    sc->show(); vp->show();
    sc->raise(); vp->raise();
}

void MainWindow::updateBreadcrumb() {
    const Document* d = m_session ? m_session->doc() : nullptr;
    const bool inside = d && d->editingSymbol();
    if (m_crumbExitAction) m_crumbExitAction->setVisible(inside);
    if (m_crumbAction) m_crumbAction->setVisible(inside);
    if (m_crumb) {
        m_crumb->setText(inside ? tr("Editing symbol:  %1").arg(d->breadcrumb().join("  ›  ")) : QString());
        m_crumb->setStyleSheet("color:#ffc83a; font-weight:bold;");
    }
}

void MainWindow::buildDocks() {
    auto mkDock = [this](const QString& key, const QString& title, QWidget* w) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName("dock." + key);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        m_docks[key] = d;
        return d;
    };
    m_layers = new LayerPanel(m_session);
    mkDock("options", tr("Tool Options"), new ToolOptionsPanel(m_session));
    mkDock("style", tr("Color & Style"), new StylePanel(m_session));
    mkDock("layers", tr("Layers"), m_layers);
    mkDock("history", tr("History"), new HistoryPanel(m_session));
    m_anim = new AnimationActions(m_session, this);
    m_symbolActions = new SymbolActions(m_session, this);
    m_scene3dActions = new Scene3DActions(m_session, this);
    connect(m_scene3dActions, &Scene3DActions::sceneLayerActivated, this, &MainWindow::show3DPanels);
    m_layers->setScene3DActions(m_scene3dActions);
    {
        // The graph editor lives in a tab next to the timeline (same dock: saved layouts stay valid).
        auto* tabs = new QTabWidget;
        tabs->setTabPosition(QTabWidget::South);
        tabs->setObjectName("timelineTabs");
        tabs->addTab(new TimelinePanel(m_session, m_anim), tr("Timeline"));
        tabs->addTab(new GraphEditorPanel(m_session), tr("Graph Editor"));
        tabs->addTab(new SymbolPanel(m_session, m_symbolActions), tr("Library"));
        mkDock("timeline", tr("Timeline"), tabs);
    }
    mkDock("exposure", tr("Exposure Sheet"), new ExposureSheetPanel(m_session, m_anim));
    mkDock("onion", tr("Onion Skin"), new OnionSkinPanel(m_session));
    mkDock("properties", tr("Properties"), new PropertiesPanel(m_session, m_anim));
    m_scene3dPanel = new Scene3DPanel(m_session, m_scene3dActions);
    m_viewport3d = new Viewport3D(m_session, m_scene3dActions);
    mkDock("scene3d", tr("3D Scene"), m_scene3dPanel)->hide();
    mkDock("viewport3d", tr("3D View"), m_viewport3d)->hide();
    m_workspaces = new WorkspaceManager(this, m_docks, this);
    connect(m_workspaces, &WorkspaceManager::aboutToChange, this, [this] { rememberCanvasPlacement(); });
    connect(m_workspaces, &WorkspaceManager::changed, this, [this] { applyCanvasPlacement(); });
    connect(m_canvasDock, &QDockWidget::topLevelChanged, this, [this] { if (!m_applyingCanvas) rememberCanvasPlacement(); });
    connect(m_workspaces, &WorkspaceManager::changed, this, &MainWindow::rebuildWorkspaceMenu);
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addActions({aNew, aOpen});
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    file->addSeparator();
    file->addActions({aSave, aSaveAs});
    file->addSeparator();
    file->addActions({aImport, aExport, aExportAnim});
    file->addSeparator();
    file->addActions({aPrefs, aQuit});
    updateRecentMenu();

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({aUndo, aRedo});
    edit->addSeparator();
    edit->addActions({aSelAll, aDeselect, aDeleteSel});

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addActions({aZoomIn, aZoomOut, aFit, aActual});
    view->addSeparator();
    view->addActions({aRotL, aRotR, aRotReset});
    view->addSeparator();
    view->addActions({aGrid, aRulers, aGuides, aSnap, aClearGuides});
    view->addSeparator();
    view->addAction(aDetachCanvas);

    QMenu* obj = menuBar()->addMenu(tr("&Object"));
    obj->addActions({aGroupShapes, aUngroupShapes});
    obj->addSeparator();
    obj->addActions({aFront, aForward, aBackward, aBack});

    connect(aDup, &QAction::triggered, m_layers, &LayerPanel::duplicateActive);
    connect(aDelLayer, &QAction::triggered, m_layers, &LayerPanel::deleteActive);
    connect(aGroupLayer, &QAction::triggered, m_layers, &LayerPanel::groupActive);
    connect(aUngroupLayer, &QAction::triggered, m_layers, &LayerPanel::ungroupActive);
    connect(aUp, &QAction::triggered, this, [this] { m_layers->moveActive(+1); });
    connect(aDown, &QAction::triggered, this, [this] { m_layers->moveActive(-1); });
    connect(aToggleVis, &QAction::triggered, this, [this] {
        if (Layer* l = m_session->activeLayer()) { LayerProps p = l->props; p.visible = !p.visible; m_doc->setLayerProps(l->id, p); }
    });
    connect(aToggleLock, &QAction::triggered, this, [this] {
        if (Layer* l = m_session->activeLayer()) { LayerProps p = l->props; p.locked = !p.locked; m_doc->setLayerProps(l->id, p); }
    });
    connect(aCopyLayer, &QAction::triggered, this, [this] { m_session->copyLayer(m_session->activeLayerId()); updateActionStates(); });
    connect(aCutLayer, &QAction::triggered, this, [this] { m_session->cutLayer(m_session->activeLayerId()); updateActionStates(); });
    connect(aPasteLayer, &QAction::triggered, this, [this] { m_session->pasteLayer(); updateActionStates(); });
    connect(aRenameLayer, &QAction::triggered, m_layers, &LayerPanel::renameActive);
    connect(aTopLayer, &QAction::triggered, this, [this] { if (Layer* l = m_session->activeLayer()) if (l->parent) m_layers->moveActive(int(l->parent->children.size())); });
    connect(aBottomLayer, &QAction::triggered, this, [this] { if (Layer* l = m_session->activeLayer()) if (l->parent) m_layers->moveActive(-int(l->parent->children.size())); });
    // Layer shortcuts that only apply while the layer list has focus (Ctrl+C/V elsewhere keep copying pixels, shapes, ...)
    for (auto [key, act] : {std::pair{QKeySequence::Copy, aCopyLayer}, std::pair{QKeySequence::Cut, aCutLayer}, std::pair{QKeySequence::Paste, aPasteLayer},
                            std::pair{QKeySequence::Delete, aDelLayer}}) {
        auto* sc = new QShortcut(QKeySequence(key), m_layers->tree());
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, act, &QAction::trigger);
    }
    connect(m_layers, &LayerPanel::contextMenuRequested, this, &MainWindow::showLayerContextMenu);
    QMenu* layer = menuBar()->addMenu(tr("&Layer"));
    layer->addActions({aNewRaster, aNewVector, aNewGroup});
    layer->addSeparator();
    layer->addActions({aCopyLayer, aCutLayer, aPasteLayer, aDup, aDelLayer, aRenameLayer});
    layer->addSeparator();
    layer->addActions({aGroupLayer, aUngroupLayer});
    layer->addSeparator();
    layer->addActions({aTopLayer, aUp, aDown, aBottomLayer, aToggleVis, aToggleLock});
    layer->addSeparator();
    layer->addAction(m_scene3dActions->make3D);

    QMenu* anim = menuBar()->addMenu(tr("&Animation"));
    m_anim->fillMenu(anim);
    m_symbolActions->fillMenu(menuBar()->addMenu(tr("&Symbol")));
    m_scene3dActions->fillMenu(menuBar()->addMenu(tr("&3D")));

    QMenu* win = menuBar()->addMenu(tr("&Window"));
    m_workspaceMenu = win->addMenu(tr("&Workspace"));
    auto* saveWs = win->addAction(tr("Save Current Layout as Workspace…"));
    connect(saveWs, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Save Workspace"), tr("Workspace name:"), QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !n.isEmpty() && !WorkspaceManager::presetNames().contains(n)) m_workspaces->saveCurrentAs(n);
    });
    auto* delWs = win->addAction(tr("Delete Custom Workspace…"));
    connect(delWs, &QAction::triggered, this, [this] {
        const QStringList c = m_workspaces->customNames();
        if (c.isEmpty()) { statusBar()->showMessage(tr("No custom workspaces saved."), 3000); return; }
        bool ok = false;
        const QString n = QInputDialog::getItem(this, tr("Delete Workspace"), tr("Workspace:"), c, 0, false, &ok);
        if (ok) m_workspaces->deleteCustom(n);
    });
    auto* resetWs = win->addAction(tr("Reset Current Workspace"));
    connect(resetWs, &QAction::triggered, this, [this] {
        m_workspaces->resetCurrent();
    });
    win->addSeparator();
    for (auto* d : m_docks) win->addAction(d->toggleViewAction());
    win->addAction(findChild<QToolBar*>("tools")->toggleViewAction());
    rebuildWorkspaceMenu();

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    auto* about = help->addAction(tr("&About MyndCraft"));
    connect(about, &QAction::triggered, this, &MainWindow::actAbout);
}

void MainWindow::rebuildWorkspaceMenu() {
    m_workspaceMenu->clear();
    m_wsCombo->blockSignals(true);
    m_wsCombo->clear();
    auto* grp = new QActionGroup(m_workspaceMenu);
    for (const QString& n : m_workspaces->allNames()) {
        auto* a = m_workspaceMenu->addAction(n);
        a->setCheckable(true);
        a->setChecked(n == m_workspaces->current());
        a->setActionGroup(grp);
        connect(a, &QAction::triggered, this, [this, n] { m_workspaces->apply(n); });
        m_wsCombo->addItem(n);
        if (n == m_workspaces->current()) m_wsCombo->setCurrentIndex(m_wsCombo->count() - 1);
    }
    m_wsCombo->blockSignals(false);
    disconnect(m_wsCombo, nullptr, this, nullptr);
    connect(m_wsCombo, &QComboBox::activated, this, [this](int i) { m_workspaces->apply(m_wsCombo->itemText(i)); });
}

void MainWindow::bindUndo() {
    for (auto& c : m_undoConns) disconnect(c);
    m_undoConns.clear();
    QUndoStack* st = m_doc->undoStack();
    auto refresh = [this, st] {
        aUndo->setEnabled(st->canUndo());
        aRedo->setEnabled(st->canRedo());
        aUndo->setText(st->canUndo() ? tr("&Undo %1").arg(st->undoText()) : tr("&Undo"));
        aRedo->setText(st->canRedo() ? tr("&Redo %1").arg(st->redoText()) : tr("&Redo"));
    };
    m_undoConns << connect(st, &QUndoStack::indexChanged, this, refresh)
                << connect(st, &QUndoStack::canUndoChanged, this, refresh)
                << connect(st, &QUndoStack::canRedoChanged, this, refresh);
    refresh();
}

void MainWindow::updateActionStates() {
    Layer* l = m_session->activeLayer();
    const bool vec = l && l->type == LayerType::Vector;
    const bool sel = vec && !m_session->selectedShapes().isEmpty();
    for (QAction* a : {aGroupShapes, aUngroupShapes, aFront, aForward, aBackward, aBack, aDeleteSel}) a->setEnabled(sel);
    aSelAll->setEnabled(vec);
    const bool has = l != nullptr;
    for (QAction* a : {aDup, aDelLayer, aGroupLayer, aUp, aDown, aToggleVis, aToggleLock}) a->setEnabled(has);
    aUngroupLayer->setEnabled(l && l->isGroup());
    for (QAction* a : {aCopyLayer, aCutLayer, aRenameLayer, aTopLayer, aBottomLayer}) a->setEnabled(has);
    aPasteLayer->setEnabled(m_session->canPasteLayer());
}

void MainWindow::showLayerContextMenu(const QPoint& pos) {
    updateActionStates();
    m_scene3dActions->refresh();
    Layer* l = m_session->activeLayer();
    QMenu m(this);
    if (QMenu* nw = m.addMenu(tr("New Layer"))) nw->addActions({aNewRaster, aNewVector, aNewGroup});
    m.addSeparator();
    m.addActions({aCopyLayer, aCutLayer, aPasteLayer, aDup, aDelLayer, aRenameLayer});
    m.addSeparator();
    m.addActions({aGroupLayer, aUngroupLayer});
    m.addSeparator();
    if (QMenu* ord = m.addMenu(tr("Arrange"))) ord->addActions({aTopLayer, aUp, aDown, aBottomLayer});
    m.addActions({aToggleVis, aToggleLock});
    if (l && (l->type != LayerType::Scene3D || m_scene3dActions->make3D->isChecked())) { m.addSeparator(); m.addAction(m_scene3dActions->make3D); }
    if (l && l->type == LayerType::Scene3D) { m.addSeparator(); m.addAction(tr("Show 3D Editor"), this, [this] { show3DPanels(); }); }
    m.exec(pos);
}

void MainWindow::updateTitle() {
    const QString name = m_doc && !m_doc->filePath().isEmpty() ? QFileInfo(m_doc->filePath()).fileName() : tr("Untitled");
    setWindowTitle(QString("%1[*] — MyndCraft").arg(name));
    setWindowModified(m_doc && m_doc->isModified());
}

void MainWindow::updateRecentMenu() {
    m_recentMenu->clear();
    const QStringList r = settings::recentFiles();
    for (const QString& p : r) {
        auto* a = m_recentMenu->addAction(QFileInfo(p).fileName());
        a->setToolTip(p);
        connect(a, &QAction::triggered, this, [this, p] {
            if (!maybeSave()) return;
            QString err;
            if (!openFile(p, &err)) QMessageBox::warning(this, tr("Cannot open project"), err);
        });
    }
    m_recentMenu->setEnabled(!r.isEmpty());
}

// ------------------------------------------------------------------ dialogs
void MainWindow::actNew() {
    if (!maybeSave()) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("New Project"));
    auto* form = new QFormLayout(&dlg);
    auto* w = new QSpinBox; w->setRange(1, kMaxCanvasDim); w->setValue(1920);
    auto* h = new QSpinBox; h->setRange(1, kMaxCanvasDim); h->setValue(1080);
    auto* fps = new QComboBox;
    struct F { const char* n; int num, den; };
    for (F f : {F{"12", 12, 1}, F{"15", 15, 1}, F{"23.976", 24000, 1001}, F{"24", 24, 1}, F{"25", 25, 1}, F{"29.97", 30000, 1001}, F{"30", 30, 1}, F{"50", 50, 1}, F{"60", 60, 1}})
        fps->addItem(QString("%1 fps").arg(f.n), QVariant::fromValue(QPoint(f.num, f.den)));
    fps->setCurrentIndex(3);
    auto* bg = new QComboBox; bg->addItems({tr("White"), tr("Black"), tr("Transparent")});
    form->addRow(tr("Width (px)"), w);
    form->addRow(tr("Height (px)"), h);
    form->addRow(tr("Frame rate"), fps);
    form->addRow(tr("Background"), bg);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    if (qint64(w->value()) * h->value() > 100'000'000) {
        QMessageBox::warning(this, tr("Canvas too large"), tr("Canvases above 100 megapixels are not supported yet (memory safeguard)."));
        return;
    }
    CompositionSettings cs;
    cs.width = w->value(); cs.height = h->value();
    const QPoint f = fps->currentData().toPoint();
    cs.fps = {f.x(), f.y()};
    cs.background = bg->currentIndex() == 0 ? QColor(Qt::white) : bg->currentIndex() == 1 ? QColor(Qt::black) : QColor(0, 0, 0, 0);
    cs.gridSize = settings::defaultGridSize();
    newDocument(cs);
}

void MainWindow::actOpen() {
    if (!maybeSave()) return;
    const QString p = QFileDialog::getOpenFileName(this, tr("Open Project"), {}, kFilter);
    if (p.isEmpty()) return;
    QString err;
    if (!openFile(p, &err)) QMessageBox::warning(this, tr("Cannot open project"), err);
    else if (!err.isEmpty()) QMessageBox::information(this, tr("Opened with warnings"), err);
}

bool MainWindow::actSave() {
    if (!m_doc) return false;
    if (m_doc->filePath().isEmpty()) return actSaveAs();
    QString err;
    if (!saveFile(m_doc->filePath(), &err)) { QMessageBox::critical(this, tr("Save failed"), err); return false; }
    statusBar()->showMessage(tr("Saved %1").arg(m_doc->filePath()), 3000);
    return true;
}

bool MainWindow::actSaveAs() {
    QString p = QFileDialog::getSaveFileName(this, tr("Save Project As"), m_doc ? m_doc->filePath() : QString(), kFilter);
    if (p.isEmpty()) return false;
    if (!p.endsWith(".studio", Qt::CaseInsensitive)) p += ".studio";
    QString err;
    if (!saveFile(p, &err)) { QMessageBox::critical(this, tr("Save failed"), err); return false; }
    statusBar()->showMessage(tr("Saved %1").arg(p), 3000);
    return true;
}

void MainWindow::actExport() {
    const QString p = QFileDialog::getSaveFileName(this, tr("Export Image"), {}, tr("PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp)"));
    if (p.isEmpty()) return;
    QString err;
    if (!exportImage(p, &err)) QMessageBox::critical(this, tr("Export failed"), err);
    else statusBar()->showMessage(tr("Exported %1").arg(p), 3000);
}

void MainWindow::actExportAnimation() {
    if (!m_doc) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Export Animation"));
    auto* form = new QFormLayout(&dlg);
    auto* fmt = new QComboBox; fmt->addItems({tr("Animated GIF"), tr("PNG sequence")});
    auto* first = new QSpinBox; auto* last = new QSpinBox;
    const int maxF = qMax(0, m_doc->durationFrames() - 1);
    for (auto* sp : {first, last}) sp->setRange(0, maxF);
    first->setValue(m_doc->rangeStart()); last->setValue(m_doc->rangeEnd());
    first->setPrefix(tr("frame ")); last->setPrefix(tr("frame "));
    auto* transparent = new QCheckBox(tr("Transparent background"));
    auto* loops = new QSpinBox; loops->setRange(0, 65535); loops->setSpecialValueText(tr("forever"));
    form->addRow(tr("Format:"), fmt);
    form->addRow(tr("First frame:"), first);
    form->addRow(tr("Last frame:"), last);
    form->addRow(QString(), transparent);
    form->addRow(tr("GIF loops:"), loops);
    auto* note = new QLabel(tr("Frames are numbered from the first one exported. Onion skin is never included. "
                               "No video encoder is built in; use the PNG sequence with ffmpeg for video."));
    note->setWordWrap(true); note->setMinimumWidth(360);
    form->addRow(note);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    connect(fmt, &QComboBox::currentIndexChanged, &dlg, [loops](int i) { loops->setEnabled(i == 0); });
    if (dlg.exec() != QDialog::Accepted) return;
    if (last->value() < first->value()) { QMessageBox::warning(this, tr("Export Animation"), tr("The last frame comes before the first.")); return; }

    const bool gif = fmt->currentIndex() == 0;
    const QString p = gif ? QFileDialog::getSaveFileName(this, tr("Export GIF"), {}, tr("GIF (*.gif)"))
                          : QFileDialog::getSaveFileName(this, tr("Export PNG sequence (base name)"), {}, tr("PNG (*.png)"));
    if (p.isEmpty()) return;
    exporter::ExportOptions o;
    o.firstFrame = first->value(); o.lastFrame = last->value();
    o.transparentBackground = transparent->isChecked(); o.loopCount = loops->value();
    QProgressDialog prog(tr("Exporting frames…"), tr("Cancel"), 0, o.lastFrame - o.firstFrame + 1, this);
    prog.setWindowModality(Qt::WindowModal); prog.setMinimumDuration(400);
    o.progress = [&prog](int done, int) { prog.setValue(done); QApplication::processEvents(); return !prog.wasCanceled(); };
    QString err; bool ok;
    if (gif) {
        QString path = p; if (QFileInfo(path).suffix().isEmpty()) path += ".gif";
        ok = exporter::writeGif(*m_doc, path, o, &err);
    } else {
        const QFileInfo fi(p);
        ok = exporter::writePngSequence(*m_doc, fi.absolutePath(), fi.completeBaseName(), o, nullptr, &err);
    }
    prog.close();
    if (!ok) { if (!err.contains("cancel", Qt::CaseInsensitive)) QMessageBox::critical(this, tr("Export failed"), err); else statusBar()->showMessage(tr("Export cancelled"), 3000); }
    else statusBar()->showMessage(tr("Exported %1 frames").arg(o.lastFrame - o.firstFrame + 1), 4000);
}

void MainWindow::actPreferences() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Preferences"));
    auto* form = new QFormLayout(&dlg);
    auto* on = new QCheckBox(tr("Enable autosave")); on->setChecked(settings::autosaveEnabled());
    auto* iv = new QSpinBox; iv->setRange(10, 3600); iv->setSuffix(" s"); iv->setValue(settings::autosaveIntervalSeconds());
    auto* gs = new QDoubleSpinBox; gs->setRange(2, 1000); gs->setValue(settings::defaultGridSize()); gs->setSuffix(" px");
    form->addRow(on);
    form->addRow(tr("Autosave interval"), iv);
    form->addRow(tr("Grid size (new projects)"), gs);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    settings::setAutosaveEnabled(on->isChecked());
    settings::setAutosaveIntervalSeconds(iv->value());
    settings::setDefaultGridSize(gs->value());
    m_autosave->setEnabled(on->isChecked());
    m_autosave->setInterval(iv->value());
    if (on->isChecked() && m_doc) m_autosave->attach(m_doc.get());
}

void MainWindow::actAbout() {
    QMessageBox::about(this, tr("About MyndCraft"),
        tr("<b>MyndCraft %1</b><br>Phase 1: drawing, vector, layers, project files.<br>"
           "Built with Qt %2.").arg(QApplication::applicationVersion(), qVersion()));
}

}  // namespace mynd
