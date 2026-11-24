#include "UI/Scene3DPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include "UI/ScrubSpinBox.h"
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QListWidget>
#include <QSpinBox>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QToolButton>

#include "Scene3D/Anim3D.h"
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace mynd {

namespace {

class NodeTree : public QTreeWidget {
public:
    std::function<void(const QUuid&, const QUuid&)> onDrop;
protected:
    void dropEvent(QDropEvent* e) override {
        QTreeWidgetItem* src = currentItem();
        QTreeWidgetItem* dst = itemAt(e->position().toPoint());
        e->ignore();                                     // the tree is rebuilt from the scene; Qt must not move items itself
        if (!src) return;
        onDrop(QUuid(src->data(0, Qt::UserRole).toString()), dst ? QUuid(dst->data(0, Qt::UserRole).toString()) : QUuid());
    }
};

QString glyph(NodeKind k, LightType lt) {
    switch (k) {
    case NodeKind::Mesh: return QString::fromUtf8("▣ ");
    case NodeKind::Camera: return QString::fromUtf8("◉ ");
    case NodeKind::Light: return lt == LightType::Directional ? QString::fromUtf8("☀ ") : QString::fromUtf8("✦ ");
    case NodeKind::Group: break;
    }
    return QString::fromUtf8("▫ ");
}

QDoubleSpinBox* spin(double lo, double hi, double step, int dec, QWidget* parent) {
    auto* s = new ScrubSpinBox(parent);
    s->setRange(lo, hi); s->setSingleStep(step); s->setDecimals(dec);
    s->setKeyboardTracking(false);
    s->setMinimumWidth(48);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return s;
}

}  // namespace

void Scene3DPanel::paintColor(QPushButton* b, const QColor& c) {
    b->setStyleSheet(QString("background:%1; border:1px solid #888; min-height:18px;").arg(c.name(QColor::HexRgb)));
    b->setToolTip(c.alpha() < 255 ? c.name(QColor::HexArgb) : c.name());
}

Scene3DPanel::Scene3DPanel(EditorSession* s, Scene3DActions* a, QWidget* parent) : QWidget(parent), m_s(s), m_a(a) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* bar = new QHBoxLayout;
    auto* imp = new QPushButton(tr("Import Model…"));
    connect(imp, &QPushButton::clicked, a->importModel, &QAction::trigger);
    bar->addWidget(imp);
    auto* add = new QToolButton; add->setText(tr("Add ▾")); add->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(add);
    menu->addActions({a->addCube, a->addSphere, a->addPlane, a->addCylinder});
    menu->addSeparator();
    menu->addActions({a->addPoint, a->addDirectional, a->addSpot, a->addCamera, a->addGroup});
    add->setMenu(menu);
    bar->addWidget(add);
    auto* del = new QToolButton; del->setText(tr("Delete"));
    connect(del, &QToolButton::clicked, a->deleteNode, &QAction::trigger);
    bar->addWidget(del);
    bar->addStretch(1);
    lay->addLayout(bar);

    auto* split = new QSplitter(Qt::Vertical);
    auto* nt = new NodeTree;
    nt->onDrop = [this](const QUuid& n, const QUuid& p) { dropOn(n, p); };
    m_tree = nt;
    m_tree->setHeaderHidden(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    split->addWidget(m_tree);

    auto* inner = new QWidget;
    auto* il = new QVBoxLayout(inner);
    il->setContentsMargins(0, 0, 0, 0);
    auto row3 = [&](QFormLayout* f, const QString& label, QDoubleSpinBox** arr, double lo, double hi, double step, int dec, const QString& key) {
        auto* h = new QHBoxLayout;
        for (int i = 0; i < 3; ++i) {
            arr[i] = spin(lo, hi, step, dec, this);
            arr[i]->setToolTip(QString("%1 %2").arg(label).arg("XYZ"[i]));
            h->addWidget(arr[i]);
            connect(arr[i], &QDoubleSpinBox::valueChanged, this, [this, arr, key, label](double) {
                if (m_updating) return;
                const QVector3D v(float(arr[0]->value()), float(arr[1]->value()), float(arr[2]->value()));
                edit(tr("Change %1").arg(label), "n3d:" + key, [=](Scene3D&, Node3D& n) {
                    QVector3D& t = key == "pos" ? n.position : key == "rot" ? n.rotation : n.scale;
                    if (t == v) return false;
                    t = v; return true;
                });
            });
        }
        f->addRow(labelWithKeys(label, key, false), h);
    };
    m_gXform = new QGroupBox(tr("Transform"));
    auto* fx = new QFormLayout(m_gXform);
    row3(fx, tr("Position"), m_pos, -1e6, 1e6, 0.1, 3, "pos");
    row3(fx, tr("Rotation"), m_rot, -36000, 36000, 1, 2, "rot");
    row3(fx, tr("Scale"), m_scl, -1e5, 1e5, 0.1, 3, "scl");
    il->addWidget(m_gXform);

    m_gCard = new QGroupBox(tr("3D Layer Card"));
    auto* fcd = new QFormLayout(m_gCard);
    m_cardSource = new QLabel; m_cardSource->setWordWrap(true); fcd->addRow(tr("Layer"), m_cardSource);
    m_cardLit = new QCheckBox(tr("Lit by the scene's lights")); m_cardLit->setToolTip(tr("Off: the card looks exactly like the 2D layer. On: lights shade it."));
    fcd->addRow(QString(), m_cardLit);
    auto* cardBack = new QPushButton(tr("Make Layer 2D Again"));
    fcd->addRow(QString(), cardBack);
    il->addWidget(m_gCard);
    connect(m_cardLit, &QCheckBox::toggled, this, [this](bool on) { if (!m_updating) edit(tr("Card Lighting"), {}, [on](Scene3D&, Node3D& n) { if (n.cardLayer.isNull() || n.cardLit == on) return false; n.cardLit = on; return true; }); });
    connect(cardBack, &QPushButton::clicked, this, [this] {
        const Layer* l = m_s->active3DLayer();
        const Node3D* n = l ? l->scene3d->node(m_s->selectedNode()) : nullptr;
        if (n && !n->cardLayer.isNull()) { const QUuid src = n->cardLayer; m_a->removeLayer3D(src); }      // by value: the node is deleted
    });

    m_gMat = new QGroupBox(tr("Material"));
    auto* fm = new QFormLayout(m_gMat);
    m_matCombo = new QComboBox; fm->addRow(tr("Slot"), m_matCombo);
    m_baseColor = new QPushButton; fm->addRow(labelWithKeys(tr("Base colour"), "base", true), m_baseColor);
    m_emissive = new QPushButton; fm->addRow(labelWithKeys(tr("Emissive"), "emis", true), m_emissive);
    m_rough = spin(0, 1, 0.05, 2, this); fm->addRow(labelWithKeys(tr("Roughness"), "rough", true), m_rough);
    m_metal = spin(0, 1, 0.05, 2, this); fm->addRow(labelWithKeys(tr("Metallic"), "metal", true), m_metal);
    m_double = new QCheckBox(tr("Double sided")); fm->addRow(QString(), m_double);
    auto* th = new QHBoxLayout;
    m_texture = new QPushButton(tr("Texture…")); m_clearTexture = new QPushButton(tr("Remove"));
    th->addWidget(m_texture); th->addWidget(m_clearTexture);
    fm->addRow(tr("Base texture"), th);
    il->addWidget(m_gMat);

    m_gLight = new QGroupBox(tr("Light"));
    auto* fl = new QFormLayout(m_gLight);
    m_lightType = new QComboBox; m_lightType->addItems({tr("Directional"), tr("Point"), tr("Spot")}); fl->addRow(tr("Type"), m_lightType);
    m_lightColor = new QPushButton; fl->addRow(labelWithKeys(tr("Colour"), "lcol", false), m_lightColor);
    m_intensity = spin(0, 100, 0.1, 2, this); fl->addRow(labelWithKeys(tr("Intensity"), "lint", false), m_intensity);
    m_range = spin(0, 1e6, 1, 1, this); m_range->setSpecialValueText(tr("Unlimited")); fl->addRow(labelWithKeys(tr("Range"), "lrange", false), m_range);
    m_spot = spin(1, 179, 1, 0, this); fl->addRow(labelWithKeys(tr("Spot angle"), "lspot", false), m_spot);
    il->addWidget(m_gLight);

    m_gCam = new QGroupBox(tr("Camera"));
    auto* fc = new QFormLayout(m_gCam);
    m_fov = spin(1, 170, 1, 1, this); fc->addRow(labelWithKeys(tr("Field of view"), "fov", false), m_fov);
    m_near = spin(0.0001, 1e5, 0.01, 4, this); fc->addRow(tr("Near"), m_near);
    m_far = spin(0.001, 1e7, 10, 1, this); fc->addRow(tr("Far"), m_far);
    m_isSceneCam = new QCheckBox(tr("Scene camera (used for the composition)")); fc->addRow(QString(), m_isSceneCam);
    il->addWidget(m_gCam);

    m_gScene = new QGroupBox(tr("Scene"));
    auto* fs = new QFormLayout(m_gScene);
    m_cameraCombo = new QComboBox; fs->addRow(tr("Scene camera"), m_cameraCombo);
    m_ambientColor = new QPushButton; fs->addRow(tr("Ambient colour"), m_ambientColor);
    m_ambientInt = spin(0, 10, 0.05, 2, this); fs->addRow(tr("Ambient level"), m_ambientInt);
    m_bgColor = new QPushButton; fs->addRow(tr("Background"), m_bgColor);
    il->addWidget(m_gScene);

    m_gClips = new QGroupBox(tr("Animation Clips"));
    auto* fcl = new QVBoxLayout(m_gClips);
    auto* clipHint = new QLabel(tr("Clips come from imported models. Add one to the timeline to play it; keyframes you set on the same property override it."));
    clipHint->setWordWrap(true);
    fcl->addWidget(clipHint);
    m_clipList = new QListWidget; m_clipList->setMaximumHeight(90);
    fcl->addWidget(m_clipList);
    auto* cb = new QHBoxLayout;
    m_clipAdd = new QPushButton(tr("Add to Timeline")); m_clipRename = new QPushButton(tr("Rename…")); m_clipDelete = new QPushButton(tr("Delete"));
    cb->addWidget(m_clipAdd); cb->addWidget(m_clipRename); cb->addWidget(m_clipDelete);
    fcl->addLayout(cb);
    fcl->addWidget(new QLabel(tr("On the timeline:")));
    m_placeList = new QListWidget; m_placeList->setMaximumHeight(90);
    fcl->addWidget(m_placeList);
    auto* pf = new QFormLayout;
    m_pStart = new QSpinBox; m_pStart->setRange(-100000, 100000); m_pStart->setToolTip(tr("Composition frame where the clip starts"));
    m_pSpeed = spin(0.01, 100, 0.1, 2, this); m_pSpeed->setToolTip(tr("Playback speed: 2 plays twice as fast"));
    m_pOffset = new QSpinBox; m_pOffset->setRange(-100000, 100000); m_pOffset->setToolTip(tr("Frames into the clip at the start frame (time offset)"));
    m_pLoop = new QCheckBox(tr("Loop (otherwise holds the last pose)")); m_pEnabled = new QCheckBox(tr("Enabled"));
    pf->addRow(tr("Start frame"), m_pStart); pf->addRow(tr("Speed"), m_pSpeed); pf->addRow(tr("Offset (frames)"), m_pOffset);
    pf->addRow(QString(), m_pLoop); pf->addRow(QString(), m_pEnabled);
    m_placeRemove = new QPushButton(tr("Remove from Timeline"));
    pf->addRow(QString(), m_placeRemove);
    fcl->addLayout(pf);
    il->addWidget(m_gClips);
    il->addStretch(1);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(inner); scroll->setFrameShape(QFrame::NoFrame);
    split->addWidget(scroll);
    split->setStretchFactor(0, 1); split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);

    // ---- wiring ----
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it, QTreeWidgetItem*) {
        if (m_updating) return;
        m_s->setSelectedNode(it ? QUuid(it->data(0, Qt::UserRole).toString()) : QUuid());
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int) {      // visibility checkbox
        if (m_updating) return;
        const QUuid id(it->data(0, Qt::UserRole).toString());
        const bool vis = it->checkState(0) == Qt::Checked;
        Layer* l = m_s->active3DLayer();
        if (l && !l->props.locked) m_s->doc()->editScene3D(l->id, [&](Scene3D& sc) { Node3D* n = sc.node(id); if (!n || n->visible == vis) return false; n->visible = vis; return true; }, vis ? tr("Show Node") : tr("Hide Node"));
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        QMenu m;
        if (m_tree->itemAt(p)) { m.addAction(m_a->duplicateNode); m.addAction(m_a->deleteNode); m.addAction(m_a->useAsSceneCamera); m.addSeparator(); m.addAction(m_a->frameSelection); }
        else m.addAction(m_a->addGroup);
        m.exec(m_tree->viewport()->mapToGlobal(p));
    });
    connect(m_s, &EditorSession::activeLayerChanged, this, [this] { rebuildTree(); refreshInspector(); });
    connect(m_s, &EditorSession::documentChanged, this, [this] { rebuildTree(); refreshInspector(); });
    connect(m_s, &EditorSession::nodeSelectionChanged, this, [this] { rebuildTree(); refreshInspector(); });

    // material
    connect(m_matCombo, &QComboBox::currentIndexChanged, this, [this] { if (!m_updating) refreshInspector(); });
    connect(m_baseColor, &QPushButton::clicked, this, [this] { pickColor(m_baseColor, tr("Base colour"), true, [this](const QColor& c) { editMaterial(tr("Change Colour"), "m3d:base", [c](Material& m) { if (m.baseColor == c) return false; m.baseColor = c; return true; }); }); });
    connect(m_emissive, &QPushButton::clicked, this, [this] { pickColor(m_emissive, tr("Emissive colour"), false, [this](const QColor& c) { editMaterial(tr("Change Emissive"), "m3d:em", [c](Material& m) { if (m.emissive == c) return false; m.emissive = c; return true; }); }); });
    connect(m_rough, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) editMaterial(tr("Change Roughness"), "m3d:rough", [v](Material& m) { if (m.roughness == v) return false; m.roughness = v; return true; }); });
    connect(m_metal, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) editMaterial(tr("Change Metallic"), "m3d:metal", [v](Material& m) { if (m.metallic == v) return false; m.metallic = v; return true; }); });
    connect(m_double, &QCheckBox::toggled, this, [this](bool v) { if (!m_updating) editMaterial(tr("Double Sided"), {}, [v](Material& m) { if (m.doubleSided == v) return false; m.doubleSided = v; return true; }); });
    connect(m_texture, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Base colour texture"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.tga *.webp)"));
        if (path.isEmpty()) return;
        QImage img(path);
        if (img.isNull()) { QMessageBox::warning(this, tr("Texture"), tr("That image could not be read.")); return; }
        setBaseTexture(img);
    });
    connect(m_clearTexture, &QPushButton::clicked, this, [this] { editMaterial(tr("Remove Texture"), {}, [](Material& m) { if (m.baseTexture.isNull()) return false; m.baseTexture = QUuid(); return true; }); });

    // light
    connect(m_lightType, &QComboBox::currentIndexChanged, this, [this](int i) { if (!m_updating) edit(tr("Light Type"), {}, [i](Scene3D&, Node3D& n) { const LightType t = i == 0 ? LightType::Directional : i == 1 ? LightType::Point : LightType::Spot; if (n.kind != NodeKind::Light || n.lightType == t) return false; n.lightType = t; return true; }); });
    connect(m_lightColor, &QPushButton::clicked, this, [this] { pickColor(m_lightColor, tr("Light colour"), false, [this](const QColor& c) { edit(tr("Light Colour"), "l3d:col", [c](Scene3D&, Node3D& n) { if (n.lightColor == c) return false; n.lightColor = c; return true; }); }); });
    connect(m_intensity, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) edit(tr("Light Intensity"), "l3d:int", [v](Scene3D&, Node3D& n) { if (n.intensity == v) return false; n.intensity = v; return true; }); });
    connect(m_range, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) edit(tr("Light Range"), "l3d:range", [v](Scene3D&, Node3D& n) { if (n.range == v) return false; n.range = v; return true; }); });
    connect(m_spot, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) edit(tr("Spot Angle"), "l3d:spot", [v](Scene3D&, Node3D& n) { if (n.spotAngle == v) return false; n.spotAngle = v; return true; }); });
    // camera
    connect(m_fov, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) edit(tr("Field of View"), "c3d:fov", [v](Scene3D&, Node3D& n) { if (n.fov == v) return false; n.fov = v; return true; }); });
    connect(m_near, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) edit(tr("Near Plane"), "c3d:near", [v](Scene3D&, Node3D& n) { if (n.nearPlane == v) return false; n.nearPlane = v; n.farPlane = std::max(n.farPlane, v * 1.01); return true; }); });
    connect(m_far, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) edit(tr("Far Plane"), "c3d:far", [v](Scene3D&, Node3D& n) { const double f = std::max(v, n.nearPlane * 1.01); if (n.farPlane == f) return false; n.farPlane = f; return true; }); });
    connect(m_isSceneCam, &QCheckBox::clicked, this, [this](bool on) { if (on) m_a->setSceneCamera(m_s->selectedNode()); else refreshInspector(); });
    // clips
    connect(m_clipAdd, &QPushButton::clicked, this, [this] { addSelectedClipToTimeline(); });
    connect(m_clipRename, &QPushButton::clicked, this, [this] {
        const Layer* l = m_s->active3DLayer();
        QListWidgetItem* it = m_clipList->currentItem();
        if (!l || !it || l->props.locked) return;
        const QUuid cid(it->data(Qt::UserRole).toString());
        const AnimClip* c = l->scene3d->clip(cid);
        if (!c) return;
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename Clip"), tr("Name"), QLineEdit::Normal, c->name, &ok).trimmed();
        if (ok && !n.isEmpty()) m_s->doc()->editScene3D(l->id, [&](Scene3D& sc) { for (AnimClip& x : sc.clips) if (x.id == cid && x.name != n) { x.name = n; return true; } return false; }, tr("Rename Clip"), {}, false);
    });
    connect(m_clipDelete, &QPushButton::clicked, this, [this] {
        const Layer* l = m_s->active3DLayer();
        QListWidgetItem* it = m_clipList->currentItem();
        if (!l || !it || l->props.locked) return;
        const QUuid cid(it->data(Qt::UserRole).toString());
        m_s->doc()->editScene3D(l->id, [&](Scene3D& sc) {
            const size_t n = sc.clips.size();
            sc.clips.erase(std::remove_if(sc.clips.begin(), sc.clips.end(), [&](const AnimClip& c) { return c.id == cid; }), sc.clips.end());
            sc.placements.erase(std::remove_if(sc.placements.begin(), sc.placements.end(), [&](const ClipPlacement& p) { return p.clip == cid; }), sc.placements.end());
            return sc.clips.size() != n;
        }, tr("Delete Clip"), {}, false);
    });
    connect(m_clipList, &QListWidget::currentRowChanged, this, [this](int) { m_clipAdd->setEnabled(m_clipList->currentItem() != nullptr); });
    connect(m_placeList, &QListWidget::currentRowChanged, this, [this](int) { if (!m_updating) refreshClips(); });
    connect(m_pStart, &QSpinBox::valueChanged, this, [this](int v) { if (!m_updating) editPlacement(tr("Move Clip"), "pl:start", [v](ClipPlacement& p) { p.start = v; }); });
    connect(m_pOffset, &QSpinBox::valueChanged, this, [this](int v) { if (!m_updating) editPlacement(tr("Clip Offset"), "pl:off", [v](ClipPlacement& p) { p.offset = v; }); });
    connect(m_pSpeed, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) editPlacement(tr("Clip Speed"), "pl:speed", [v](ClipPlacement& p) { p.speed = v; }); });
    connect(m_pLoop, &QCheckBox::toggled, this, [this](bool v) { if (!m_updating) editPlacement(tr("Clip Loop"), {}, [v](ClipPlacement& p) { p.loop = v; }); });
    connect(m_pEnabled, &QCheckBox::toggled, this, [this](bool v) { if (!m_updating) editPlacement(tr("Enable Clip"), {}, [v](ClipPlacement& p) { p.enabled = v; }); });
    connect(m_placeRemove, &QPushButton::clicked, this, [this] {
        const Layer* l = m_s->active3DLayer();
        const QUuid pid = selectedPlacement();
        if (!l || pid.isNull() || l->props.locked) return;
        m_s->doc()->editScene3D(l->id, [&](Scene3D& sc) {
            const size_t n = sc.placements.size();
            sc.placements.erase(std::remove_if(sc.placements.begin(), sc.placements.end(), [&](const ClipPlacement& p) { return p.id == pid; }), sc.placements.end());
            return sc.placements.size() != n;
        }, tr("Remove Clip from Timeline"), {}, false);
    });
    // scene
    connect(m_cameraCombo, &QComboBox::activated, this, [this](int i) { m_a->setSceneCamera(QUuid(m_cameraCombo->itemData(i).toString())); });
    connect(m_ambientColor, &QPushButton::clicked, this, [this] { pickColor(m_ambientColor, tr("Ambient colour"), false, [this](const QColor& c) { editScene(tr("Ambient Colour"), "s3d:amb", [c](Scene3D& sc) { if (sc.ambient == c) return false; sc.ambient = c; return true; }); }); });
    connect(m_ambientInt, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_updating) editScene(tr("Ambient Level"), "s3d:ambint", [v](Scene3D& sc) { if (sc.ambientIntensity == v) return false; sc.ambientIntensity = v; return true; }); });
    connect(m_bgColor, &QPushButton::clicked, this, [this] { pickColor(m_bgColor, tr("Background (transparent keeps the 2D layers below visible)"), true, [this](const QColor& c) { editScene(tr("Scene Background"), "s3d:bg", [c](Scene3D& sc) { if (sc.background == c) return false; sc.background = c; return true; }); }); });

    auto hookDoc = [this] {
        if (!m_s->doc()) return;
        connect(m_s->doc(), &Document::scene3DChanged, this, [this] { rebuildTree(); refreshInspector(); });
        connect(m_s->doc(), &Document::frameChanged, this, [this] { refreshInspector(); });
        connect(m_s->doc(), &Document::tracksChanged, this, [this] { refreshInspector(); });
    };
    hookDoc();
    connect(m_s, &EditorSession::documentChanged, this, hookDoc);
    rebuildTree();
    refreshInspector();
}

void Scene3DPanel::pickColor(QPushButton* b, const QString& title, bool alpha, const std::function<void(const QColor&)>& apply) {
    QColor start = b == m_baseColor ? m_baseC : b == m_emissive ? m_emisC : b == m_lightColor ? m_lightC : b == m_ambientColor ? m_ambC : m_bgC;
    const QColor c = QColorDialog::getColor(start, this, title, alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
    if (c.isValid()) apply(c);
}

bool Scene3DPanel::setBaseTexture(const QImage& image) {
    if (image.isNull()) return false;
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    const QUuid mid = currentMaterialId();
    Layer* l = m_s->active3DLayer();
    if (!l || l->props.locked) return false;
    return m_s->doc()->editScene3D(l->id, [=](Scene3D& sc) {
        Material* m = sc.material(mid);
        if (!m) return false;
        const QUuid tid = QUuid::createUuid();
        sc.textures[tid] = img;
        m->baseTexture = tid;
        if (m->baseColor.red() < 255 || m->baseColor.green() < 255 || m->baseColor.blue() < 255) m->baseColor = QColor(255, 255, 255, m->baseColor.alpha());
        sc.pruneUnused();
        return true;
    }, tr("Set Texture"));
}

void Scene3DPanel::dropOn(const QUuid& node, const QUuid& newParent) {
    m_a->reparentNode(node, newParent);
}

void Scene3DPanel::editScene(const QString& text, const QString& key, const std::function<bool(Scene3D&)>& fn) {
    Layer* l = m_s->active3DLayer();
    if (!l || l->props.locked) return;
    m_s->doc()->editScene3D(l->id, fn, text, key, m_s->autoKey());
}

void Scene3DPanel::edit(const QString& text, const QString& key, const std::function<bool(Scene3D&, Node3D&)>& fn) {
    const QUuid id = m_s->selectedNode();
    if (id.isNull()) return;
    editScene(text, key.isEmpty() ? key : key + ":" + id.toString(QUuid::WithoutBraces), [&](Scene3D& s) { Node3D* n = s.node(id); return n && fn(s, *n); });
}

QWidget* Scene3DPanel::labelWithKeys(const QString& text, const QString& what, bool material) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);
    KeyRow r;
    r.what = what; r.material = material;
    r.watch = new QToolButton; r.watch->setText(QString::fromUtf8("⏱")); r.watch->setCheckable(true); r.watch->setAutoRaise(true);
    r.watch->setToolTip(tr("Animate this property (stopwatch): adds a keyframe at the current frame. Click again to stop animating."));
    r.key = new QToolButton; r.key->setText(QString::fromUtf8("◆")); r.key->setAutoRaise(true);
    r.key->setToolTip(tr("Add a keyframe at the current frame, or remove the one that is there"));
    h->addWidget(new QLabel(text));
    h->addStretch(1);
    h->addWidget(r.watch);
    h->addWidget(r.key);
    const int idx = m_keyRows.size();
    m_keyRows.push_back(r);
    connect(r.watch, &QToolButton::clicked, this, [this, idx] {
        const Layer* l = m_s->active3DLayer();
        const QString id = keyPropId(m_keyRows[idx]);
        if (l && !id.isEmpty() && !l->props.locked) m_s->doc()->toggleScene3DAnimation(l->id, id);
        refreshInspector();
    });
    connect(r.key, &QToolButton::clicked, this, [this, idx] {
        const Layer* l = m_s->active3DLayer();
        const QString id = keyPropId(m_keyRows[idx]);
        if (l && !id.isEmpty() && !l->props.locked) m_s->doc()->toggleScene3DKey(l->id, id);
        refreshInspector();
    });
    return w;
}

QString Scene3DPanel::keyPropId(const KeyRow& r) const {
    if (r.material) { const QUuid m = currentMaterialId(); return m.isNull() ? QString() : materialPropId(m, r.what); }
    const QUuid n = m_s->selectedNode();
    return n.isNull() ? QString() : nodePropId(n, r.what);
}

bool Scene3DPanel::addSelectedClipToTimeline() {
    const Layer* l = m_s->active3DLayer();
    QListWidgetItem* it = m_clipList->currentItem();
    if (!l || !it || l->props.locked) return false;
    const QUuid cid(it->data(Qt::UserRole).toString());
    const int frame = m_s->doc()->currentFrame();
    return m_s->doc()->editScene3D(l->id, [&](Scene3D& sc) {
        if (!sc.clip(cid)) return false;
        ClipPlacement p; p.id = QUuid::createUuid(); p.clip = cid; p.start = frame;
        sc.placements.push_back(p);
        return true;
    }, tr("Add Clip to Timeline"), {}, false);
}

QUuid Scene3DPanel::selectedPlacement() const {
    QListWidgetItem* it = m_placeList->currentItem();
    return it ? QUuid(it->data(Qt::UserRole).toString()) : QUuid();
}

void Scene3DPanel::editPlacement(const QString& text, const QString& key, const std::function<void(ClipPlacement&)>& fn) {
    const QUuid pid = selectedPlacement();
    if (pid.isNull()) return;
    editScene(text, key.isEmpty() ? key : key + ":" + pid.toString(QUuid::WithoutBraces), [&](Scene3D& sc) {
        for (ClipPlacement& p : sc.placements) {
            if (p.id != pid) continue;
            const ClipPlacement before = p;
            fn(p);
            return !(p == before);
        }
        return false;
    });
}

void Scene3DPanel::refreshClips() {
    const Layer* l = m_s->active3DLayer();
    const Scene3D* sc = l ? l->scene3d.get() : nullptr;
    const bool had = m_updating;
    m_updating = true;
    const QString keepClip = m_clipList->currentItem() ? m_clipList->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString keepPl = m_placeList->currentItem() ? m_placeList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_clipList->clear(); m_placeList->clear();
    m_gClips->setVisible(sc != nullptr && (!sc->clips.empty()));
    if (sc) {
        for (const AnimClip& c : sc->clips) {
            auto* it = new QListWidgetItem(tr("%1  (%2 frames)").arg(c.name).arg(c.length), m_clipList);
            it->setData(Qt::UserRole, c.id.toString());
            if (c.id.toString() == keepClip) m_clipList->setCurrentItem(it);
        }
        if (!m_clipList->currentItem() && m_clipList->count() > 0) m_clipList->setCurrentRow(0);
        for (const ClipPlacement& p : sc->placements) {
            const AnimClip* c = sc->clip(p.clip);
            auto* it = new QListWidgetItem(tr("%1 @ frame %2").arg(c ? c->name : tr("(missing clip)")).arg(p.start), m_placeList);
            it->setData(Qt::UserRole, p.id.toString());
            if (p.id.toString() == keepPl) m_placeList->setCurrentItem(it);
        }
        if (!m_placeList->currentItem() && m_placeList->count() > 0) m_placeList->setCurrentRow(0);
    }
    const ClipPlacement* cur = nullptr;
    if (sc) for (const ClipPlacement& p : sc->placements) if (p.id == selectedPlacement()) cur = &p;
    for (QWidget* w : {static_cast<QWidget*>(m_pStart), static_cast<QWidget*>(m_pSpeed), static_cast<QWidget*>(m_pOffset), static_cast<QWidget*>(m_pLoop),
                       static_cast<QWidget*>(m_pEnabled), static_cast<QWidget*>(m_placeRemove)}) w->setEnabled(cur != nullptr);
    if (cur) {
        if (!m_pStart->hasFocus()) m_pStart->setValue(cur->start);
        if (!m_pSpeed->hasFocus()) m_pSpeed->setValue(cur->speed);
        if (!m_pOffset->hasFocus()) m_pOffset->setValue(cur->offset);
        m_pLoop->setChecked(cur->loop); m_pEnabled->setChecked(cur->enabled);
    }
    m_clipAdd->setEnabled(m_clipList->currentItem() != nullptr);
    m_clipRename->setEnabled(m_clipList->currentItem() != nullptr);
    m_clipDelete->setEnabled(m_clipList->currentItem() != nullptr);
    m_updating = had;
}

QUuid Scene3DPanel::currentMaterialId() const { return QUuid(m_matCombo->currentData().toString()); }

void Scene3DPanel::editMaterial(const QString& text, const QString& key, const std::function<bool(Material&)>& fn) {
    const QUuid mid = currentMaterialId();
    if (mid.isNull()) return;
    editScene(text, key.isEmpty() ? key : key + ":" + mid.toString(QUuid::WithoutBraces), [&](Scene3D& s) { Material* m = s.material(mid); return m && fn(*m); });
}

void Scene3DPanel::rebuildTree() {
    const Layer* l = m_s->active3DLayer();
    m_updating = true;
    m_tree->clear();
    if (l) {
        const Scene3D& sc = *l->scene3d;
        std::function<void(const QUuid&, QTreeWidgetItem*, int)> add = [&](const QUuid& parent, QTreeWidgetItem* ui, int depth) {
            if (depth > 128) return;
            for (const QUuid& id : sc.childrenOf(parent)) {
                const Node3D* n = sc.node(id);
                auto* it = ui ? new QTreeWidgetItem(ui) : new QTreeWidgetItem(m_tree);
                it->setText(0, glyph(n->kind, n->lightType) + n->name + (id == sc.activeCamera ? tr("  (scene camera)") : QString()));
                it->setData(0, Qt::UserRole, id.toString());
                it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
                it->setCheckState(0, n->visible ? Qt::Checked : Qt::Unchecked);
                if (id == m_s->selectedNode()) m_tree->setCurrentItem(it);
                add(id, it, depth + 1);
                it->setExpanded(true);
            }
        };
        add(QUuid(), nullptr, 0);
    }
    m_updating = false;
}

void Scene3DPanel::refreshInspector() {
    const Layer* l = m_s->active3DLayer();
    const auto shown = l ? l->sceneAt(m_s->doc()->currentFrame()) : nullptr;     // what the user sees at this frame
    const Scene3D* sc = shown.get();
    const Node3D* n = sc ? sc->node(m_s->selectedNode()) : nullptr;
    m_updating = true;
    auto setSpin = [](QDoubleSpinBox* b, double v) { if (!b->hasFocus() || !b->isVisible()) b->setValue(v); };
    m_gXform->setVisible(n != nullptr);
    m_gMat->setVisible(n && n->kind == NodeKind::Mesh && n->cardLayer.isNull());
    m_gCard->setVisible(n && !n->cardLayer.isNull());
    if (n && !n->cardLayer.isNull()) {
        const Layer* src = m_s->doc() ? m_s->doc()->findLayer(n->cardLayer) : nullptr;
        m_cardSource->setText(src ? src->props.name : tr("(the layer no longer exists)"));
        m_cardLit->setChecked(n->cardLit);
    }
    m_gLight->setVisible(n && n->kind == NodeKind::Light);
    m_gCam->setVisible(n && n->kind == NodeKind::Camera);
    m_gScene->setVisible(sc != nullptr);
    if (n) {
        for (int i = 0; i < 3; ++i) { setSpin(m_pos[i], n->position[i]); setSpin(m_rot[i], n->rotation[i]); setSpin(m_scl[i], n->scale[i]); }
        if (n->kind == NodeKind::Mesh && n->cardLayer.isNull()) {
            QUuid keep = currentMaterialId();
            m_matCombo->clear();
            QVector<QUuid> mats;
            if (!n->materialOverride.isNull()) mats << n->materialOverride;
            else if (auto it = sc->meshes.find(n->mesh); it != sc->meshes.end())
                for (const auto& p : it->second->parts) if (!p.material.isNull() && !mats.contains(p.material)) mats << p.material;
            for (const QUuid& id : mats) if (const Material* m = sc->material(id)) m_matCombo->addItem(m->name, id.toString());
            const int ki = m_matCombo->findData(keep.toString());
            if (ki >= 0) m_matCombo->setCurrentIndex(ki);
            const Material* m = sc->material(currentMaterialId());
            m_gMat->setEnabled(m != nullptr);
            if (m) {
                m_baseC = m->baseColor; m_emisC = m->emissive;
                paintColor(m_baseColor, m_baseC); paintColor(m_emissive, m_emisC);
                setSpin(m_rough, m->roughness); setSpin(m_metal, m->metallic);
                m_double->setChecked(m->doubleSided);
                m_clearTexture->setEnabled(!m->baseTexture.isNull());
                m_texture->setText(m->baseTexture.isNull() ? tr("Texture…") : tr("Change…"));
            }
        } else if (n->kind == NodeKind::Light) {
            m_lightType->setCurrentIndex(n->lightType == LightType::Directional ? 0 : n->lightType == LightType::Point ? 1 : 2);
            m_lightC = n->lightColor; paintColor(m_lightColor, m_lightC);
            setSpin(m_intensity, n->intensity); setSpin(m_range, n->range); setSpin(m_spot, n->spotAngle);
            m_range->setEnabled(n->lightType != LightType::Directional);
            m_spot->setEnabled(n->lightType == LightType::Spot);
        } else if (n->kind == NodeKind::Camera) {
            setSpin(m_fov, n->fov); setSpin(m_near, n->nearPlane); setSpin(m_far, n->farPlane);
            m_isSceneCam->setChecked(sc->activeCamera == n->id);
        }
    }
    for (const KeyRow& r : m_keyRows) {                      // stopwatch / key state of every animatable row
        const QString id = l ? keyPropId(r) : QString();
        const auto it = id.isEmpty() ? PropertyTracks::const_iterator() : l->tracks.constFind(id);
        const bool animated = !id.isEmpty() && it != l->tracks.constEnd() && it->animated();
        r.watch->setChecked(animated);
        r.key->setEnabled(!id.isEmpty());
        r.key->setText(animated && it->indexOfKey(m_s->doc()->currentFrame()) >= 0 ? QString::fromUtf8("◆") : QString::fromUtf8("◇"));
    }
    refreshClips();
    if (sc) {
        m_cameraCombo->clear();
        for (const auto& nd : sc->nodes) if (nd.kind == NodeKind::Camera) m_cameraCombo->addItem(nd.name, nd.id.toString());
        const int ci = m_cameraCombo->findData(sc->activeCamera.toString());
        m_cameraCombo->setCurrentIndex(ci);
        m_ambC = sc->ambient; m_bgC = sc->background;
        paintColor(m_ambientColor, m_ambC); paintColor(m_bgColor, m_bgC);
        setSpin(m_ambientInt, sc->ambientIntensity);
    }
    m_updating = false;
}

}  // namespace mynd
