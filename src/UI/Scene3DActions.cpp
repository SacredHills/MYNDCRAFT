#include <QTimer>
#include <QProgressDialog>
#include <QtConcurrent>
#include <QFutureWatcher>
#include "UI/Scene3DActions.h"

#include <QFileDialog>
#include <QFileInfo>
#include <functional>
#include <QMessageBox>
#include <QSet>

#include "Scene3D/Render3D.h"

namespace mynd {

namespace {
// A camera node that looks at `target` from `eye`.
void aim(Node3D& n, const QVector3D& eye, const QVector3D& target) {
    QMatrix4x4 view;
    view.lookAt(eye, target, QVector3D(0, 1, 0));
    decomposeMatrix(view.inverted(), &n.position, &n.rotation, &n.scale);
    n.scale = QVector3D(1, 1, 1);
}
}  // namespace

Scene3DActions::Scene3DActions(EditorSession* s, QWidget* dialogParent) : QObject(dialogParent), m_s(s), m_parent(dialogParent) {
    auto mk = [this](const QString& text, const QKeySequence& key = {}) {
        auto* a = new QAction(text, this);
        if (!key.isEmpty()) { a->setShortcut(key); a->setShortcutContext(Qt::WindowShortcut); }
        return a;
    };
    newScene = mk(tr("New 3D Scene Layer"));
    importModel = mk(tr("Import 3D Model…"), QKeySequence("Ctrl+Shift+I"));
    addCube = mk(tr("Cube")); addSphere = mk(tr("Sphere")); addPlane = mk(tr("Plane")); addCylinder = mk(tr("Cylinder"));
    addPoint = mk(tr("Point Light")); addDirectional = mk(tr("Directional Light")); addSpot = mk(tr("Spot Light"));
    addCamera = mk(tr("Camera")); addGroup = mk(tr("Empty Group"));
    duplicateNode = mk(tr("Duplicate Node")); deleteNode = mk(tr("Delete Node"));
    useAsSceneCamera = mk(tr("Use as Scene Camera"));
    frameSelection = mk(tr("Frame Selection in 3D View"), QKeySequence("Shift+F"));
    make3D = mk(tr("3&D Layer (show layer in 3D space)"));
    make3D->setCheckable(true);
    make3D->setToolTip(tr("Turns the selected 2D layer into a card in a 3D scene so it can be moved, rotated and scaled in 3D space"));

    connect(newScene, &QAction::triggered, this, [this] { m_s->doc()->undoStack()->beginMacro(tr("New 3D Scene")); const QUuid id = m_s->doc()->addScene3DLayer(); m_s->setActiveLayer(id); m_s->doc()->undoStack()->endMacro(); emit sceneLayerActivated(); });
    connect(importModel, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(m_parent, tr("Import 3D Model"), QString(), modelFileFilter());
        if (path.isEmpty()) return;
        importModelFileAsync(path);
    });
    connect(make3D, &QAction::triggered, this, [this](bool on) {
        const QUuid id = m_s->activeLayerId();
        QString err;
        if (on) { if (makeLayer3D(id, &err).isNull() && !err.isEmpty()) QMessageBox::information(m_parent, tr("3D Layer"), err); }
        else removeLayer3D(id);
        refresh();
    });
    connect(addCube, &QAction::triggered, this, [this] { createPrimitive(Primitive::Cube); });
    connect(addSphere, &QAction::triggered, this, [this] { createPrimitive(Primitive::Sphere); });
    connect(addPlane, &QAction::triggered, this, [this] { createPrimitive(Primitive::Plane); });
    connect(addCylinder, &QAction::triggered, this, [this] { createPrimitive(Primitive::Cylinder); });
    connect(addPoint, &QAction::triggered, this, [this] { createLight(LightType::Point); });
    connect(addDirectional, &QAction::triggered, this, [this] { createLight(LightType::Directional); });
    connect(addSpot, &QAction::triggered, this, [this] { createLight(LightType::Spot); });
    connect(addCamera, &QAction::triggered, this, [this] { createCamera(); });
    connect(addGroup, &QAction::triggered, this, [this] { createGroup(); });
    connect(duplicateNode, &QAction::triggered, this, [this] { duplicateSelected(); });
    connect(deleteNode, &QAction::triggered, this, [this] { deleteSelected(); });
    connect(useAsSceneCamera, &QAction::triggered, this, [this] { setSceneCamera(m_s->selectedNode()); });
    connect(m_s, &EditorSession::activeLayerChanged, this, &Scene3DActions::refresh);
    connect(m_s, &EditorSession::nodeSelectionChanged, this, &Scene3DActions::refresh);
    connect(m_s, &EditorSession::documentChanged, this, &Scene3DActions::refresh);
    refresh();
}

void Scene3DActions::fillMenu(QMenu* m) {
    m->addAction(newScene);
    m->addAction(importModel);
    m->addSeparator();
    QMenu* prim = m->addMenu(tr("Add Primitive"));
    prim->addActions({addCube, addSphere, addPlane, addCylinder});
    QMenu* light = m->addMenu(tr("Add Light"));
    light->addActions({addPoint, addDirectional, addSpot});
    m->addAction(addCamera);
    m->addAction(addGroup);
    m->addSeparator();
    m->addAction(make3D);
    m->addSeparator();
    m->addActions({duplicateNode, deleteNode, useAsSceneCamera, frameSelection});
}

void Scene3DActions::refresh() {
    const bool doc = m_s->doc() != nullptr;
    const Layer* l = m_s->active3DLayer();
    const Node3D* n = l ? l->scene3d->node(m_s->selectedNode()) : nullptr;
    newScene->setEnabled(doc);
    importModel->setEnabled(doc);
    for (QAction* a : {addCube, addSphere, addPlane, addCylinder, addPoint, addDirectional, addSpot, addCamera, addGroup}) a->setEnabled(doc);
    duplicateNode->setEnabled(n != nullptr);
    deleteNode->setEnabled(n != nullptr);
    useAsSceneCamera->setEnabled(n && n->kind == NodeKind::Camera);
    frameSelection->setEnabled(l != nullptr);
    const Layer* al = m_s->activeLayer();
    const bool is3D = al && isLayer3D(al->id);
    make3D->setEnabled(doc && al && (al->type != LayerType::Scene3D || is3D));
    make3D->blockSignals(true);
    make3D->setChecked(is3D);
    make3D->blockSignals(false);
}

bool Scene3DActions::isLayer3D(const QUuid& layer, QUuid* sceneLayer, QUuid* node) const {
    Document* d = m_s->doc();
    if (!d || layer.isNull()) return false;
    bool found = false;
    Document::forEachLayer(d->root(), [&](Layer* l) {
        if (found || l->type != LayerType::Scene3D || !l->scene3d) return;
        for (const Node3D& n : l->scene3d->nodes) if (n.cardLayer == layer) { found = true; if (sceneLayer) *sceneLayer = l->id; if (node) *node = n.id; return; }
    });
    return found;
}

QUuid Scene3DActions::makeLayer3D(const QUuid layerId, QString* error) {
    Document* d = m_s->doc();
    Layer* src = d ? d->findLayer(layerId) : nullptr;
    if (!src) { if (error) *error = tr("There is no layer to make 3D."); return {}; }
    if (src->type == LayerType::Scene3D) { if (error) *error = tr("A 3D scene layer is already 3D."); return {}; }
    if (src->props.locked) { if (error) *error = tr("The layer is locked."); return {}; }
    QUuid existing;
    if (isLayer3D(layerId, nullptr, &existing)) return existing;
    // which scene layer: the nearest one above the layer in the same parent, else the nearest below, else a new one right above
    Layer* parent = src->parent ? src->parent : d->root();
    const int at = d->indexInParent(src);
    Layer* scene = nullptr;
    for (int i = at + 1; i < int(parent->children.size()) && !scene; ++i) if (parent->children[size_t(i)]->type == LayerType::Scene3D) scene = parent->children[size_t(i)].get();
    for (int i = at - 1; i >= 0 && !scene; --i) if (parent->children[size_t(i)]->type == LayerType::Scene3D) scene = parent->children[size_t(i)].get();
    d->undoStack()->beginMacro(tr("Make \"%1\" a 3D Layer").arg(src->props.name));
    QUuid sceneId = scene ? scene->id : QUuid();
    if (sceneId.isNull()) sceneId = d->addScene3DLayer(tr("3D Scene"), parent == d->root() ? QUuid() : parent->id, at + 1);
    const double aspect = d->comp().height > 0 ? double(d->comp().width) / d->comp().height : 1.0;
    QUuid made;
    d->editScene3D(sceneId, [&](Scene3D& s) {
        auto mesh = makeCardPlane(2.0 * aspect, 2.0);               // 2 units tall, canvas-shaped; at distance 2.414 a 45° camera frames it exactly
        mesh->parts = {{0, int(mesh->indices.size()), QUuid()}};
        s.meshes[mesh->id] = mesh;
        Node3D n; n.kind = NodeKind::Mesh; n.name = s.uniqueName(tr("Card: %1").arg(src->props.name)); n.mesh = mesh->id; n.cardLayer = layerId;
        made = s.addNode(n);
        if (s.firstCamera().isNull()) {
            Node3D cam; cam.kind = NodeKind::Camera; cam.name = s.uniqueName(tr("Camera")); cam.fov = 45;
            cam.position = QVector3D(0, 0, float(1.0 / std::tan(22.5 * M_PI / 180.0)));
            s.activeCamera = s.addNode(cam);
        }
        return true;
    }, tr("Add Card"));
    d->undoStack()->endMacro();
    m_s->setActiveLayer(sceneId);
    m_s->setSelectedNode(made);
    emit sceneLayerActivated();
    return made;
}

bool Scene3DActions::removeLayer3D(const QUuid layerId) {
    Document* d = m_s->doc();
    if (!d) return false;
    QVector<QUuid> scenes;
    Document::forEachLayer(d->root(), [&](Layer* l) {
        if (l->type != LayerType::Scene3D || !l->scene3d) return;
        for (const Node3D& n : l->scene3d->nodes) if (n.cardLayer == layerId) { scenes << l->id; return; }
    });
    if (scenes.isEmpty()) return false;
    d->undoStack()->beginMacro(tr("Make Layer 2D Again"));
    for (const QUuid& sid : scenes)
        d->editScene3D(sid, [&](Scene3D& s) {
            QVector<QUuid> victims;
            for (const Node3D& n : s.nodes) if (n.cardLayer == layerId) victims << n.id;
            for (const QUuid& v : victims) s.removeNode(v);
            return !victims.isEmpty();
        }, tr("Remove Card"));
    d->undoStack()->endMacro();
    m_s->setActiveLayer(layerId);
    return true;
}

QUuid Scene3DActions::ensureSceneLayer() {
    if (!m_s->doc()) return {};
    if (Layer* l = m_s->active3DLayer()) return l->id;
    const QUuid id = m_s->doc()->addScene3DLayer();
    m_s->setActiveLayer(id);
    emit sceneLayerActivated();
    return id;
}

QUuid Scene3DActions::add(const Node3D& proto, const QString& text, std::shared_ptr<const Mesh> mesh) {
    Document* d = m_s->doc();
    if (!d) return {};
    d->undoStack()->beginMacro(text);
    const QUuid lid = ensureSceneLayer();
    QUuid made;
    d->editScene3D(lid, [&](Scene3D& s) {
        Node3D n = proto;
        n.name = s.uniqueName(n.name);
        if (mesh) {
            auto copy = std::make_shared<Mesh>(*mesh);
            Material mat; mat.id = QUuid::createUuid(); mat.name = n.name; mat.baseColor = QColor(200, 200, 205);
            s.materials.push_back(mat);
            copy->parts = {{0, int(copy->indices.size()), mat.id}};
            s.meshes[copy->id] = copy;
            n.mesh = copy->id;
        }
        made = s.addNode(n);
        return true;
    }, text);
    d->undoStack()->endMacro();
    if (!made.isNull()) m_s->setSelectedNode(made);
    return made;
}

QUuid Scene3DActions::createPrimitive(Primitive p) {
    Node3D n; n.kind = NodeKind::Mesh;
    std::shared_ptr<Mesh> m;
    switch (p) {
    case Primitive::Cube: m = makeCube(1.0); n.name = tr("Cube"); break;
    case Primitive::Sphere: m = makeSphere(0.5); n.name = tr("Sphere"); break;
    case Primitive::Plane: m = makePlane(4.0); n.name = tr("Plane"); break;
    case Primitive::Cylinder: m = makeCylinder(0.5, 1.0); n.name = tr("Cylinder"); break;
    }
    if (p != Primitive::Plane) n.position = QVector3D(0, 0.5f, 0);
    return add(n, tr("Add %1").arg(n.name), m);
}

QUuid Scene3DActions::createLight(LightType t) {
    Node3D n; n.kind = NodeKind::Light; n.lightType = t;
    switch (t) {
    case LightType::Directional: n.name = tr("Directional Light"); n.rotation = QVector3D(-45, -30, 0); n.position = QVector3D(0, 4, 0); break;
    case LightType::Point: n.name = tr("Point Light"); n.position = QVector3D(2, 3, 2); n.range = 0; break;
    case LightType::Spot: n.name = tr("Spot Light"); n.position = QVector3D(0, 4, 3); n.rotation = QVector3D(-50, 0, 0); n.spotAngle = 40; break;
    }
    return add(n, tr("Add %1").arg(n.name));
}

QUuid Scene3DActions::createGroup() {
    Node3D n; n.kind = NodeKind::Group; n.name = tr("Group");
    return add(n, tr("Add Group"));
}

QUuid Scene3DActions::createCamera() {
    Node3D n; n.kind = NodeKind::Camera; n.name = tr("Camera");
    OrbitCamera oc;
    oc.pitchDeg = 15; oc.yawDeg = 20;
    if (const Layer* l = m_s->active3DLayer()) {
        QVector3D mn, mx;
        if (l->scene3d->worldBounds(&mn, &mx)) oc.frame(mn, mx);
    }
    aim(n, oc.eye(), oc.target);
    n.fov = oc.fov;
    return add(n, tr("Add Camera"));
}

QUuid Scene3DActions::importModelFile(const QString& path, QString* error, QStringList* warnings) {
    if (!m_s->doc()) { if (error) *error = tr("No document is open."); return {}; }
    ImportResult r = mynd::importModelFile(path, m_s->doc()->comp().fps.fps());
    return finishImport(path, r, error, warnings);
}

// Reading and decoding the model runs on a worker thread, so the program stays usable and a progress box with Cancel is shown; only the
// final merge into the scene (fast) happens on the UI thread.
void Scene3DActions::importModelFileAsync(const QString& path) {
    if (!m_s->doc()) return;
    if (m_importing) { emit m_s->message(tr("A model is already being imported.")); return; }
    m_importing = true;
    auto control = std::make_shared<ImportControl>();
    const double fps = m_s->doc()->comp().fps.fps();
    auto* watcher = new QFutureWatcher<ImportResult>(this);
    auto* prog = new QProgressDialog(tr("Importing %1…").arg(QFileInfo(path).fileName()), tr("Cancel"), 0, 100, m_parent);
    prog->setWindowTitle(tr("Import 3D Model"));
    prog->setWindowModality(Qt::WindowModal);
    prog->setMinimumDuration(300);
    prog->setAutoClose(false); prog->setAutoReset(false);
    auto* tick = new QTimer(prog);
    connect(tick, &QTimer::timeout, prog, [prog, control] { prog->setValue(control->percent.load()); });
    tick->start(100);
    connect(prog, &QProgressDialog::canceled, this, [control] { control->cancel = true; });
    connect(watcher, &QFutureWatcher<ImportResult>::finished, this, [this, watcher, prog, path, control] {
        ImportResult r = watcher->result();
        prog->close(); prog->deleteLater(); watcher->deleteLater();
        m_importing = false;
        QString err; QStringList warn;
        if (!m_s->doc()) return;
        if (finishImport(path, r, &err, &warn).isNull()) {
            if (control->cancel) emit m_s->message(tr("Import cancelled"));
            else QMessageBox::warning(m_parent, tr("Import 3D Model"), err);
            return;
        }
        if (!warn.isEmpty()) QMessageBox::information(m_parent, tr("Import 3D Model"), tr("Imported with notes:\n\n• %1").arg(warn.join("\n• ")));
        else emit m_s->message(tr("Imported %1").arg(QFileInfo(path).fileName()));
    });
    watcher->setFuture(QtConcurrent::run([path, fps, control] { return mynd::importModelFile(path, fps, control.get()); }));
}

QUuid Scene3DActions::finishImport(const QString& path, ImportResult& r, QString* error, QStringList* warnings) {
    if (!r) { if (error) *error = r.error; return {}; }
    if (warnings) *warnings = r.warnings;
    Document* d = m_s->doc();
    d->undoStack()->beginMacro(tr("Import %1").arg(QFileInfo(path).fileName()));
    const QUuid lid = ensureSceneLayer();
    QUuid root;
    d->editScene3D(lid, [&](Scene3D& s) {
        const QString rootName = s.uniqueName(r.scene->node(r.root) ? r.scene->node(r.root)->name : tr("Model"));
        root = mergeImport(s, *r.scene);
        if (Node3D* rn = s.node(root)) rn->name = rootName;
        if (s.firstCamera().isNull()) {
            QVector3D mn, mx;
            Node3D cam; cam.kind = NodeKind::Camera; cam.name = tr("Camera");
            OrbitCamera oc; oc.pitchDeg = 15; oc.yawDeg = 25;
            if (s.worldBounds(&mn, &mx)) oc.frame(mn, mx);
            aim(cam, oc.eye(), oc.target);
            cam.nearPlane = std::max(0.001, oc.distance * 0.01); cam.farPlane = std::max(100.0, oc.distance * 100);
            s.addNode(cam);
        }
        bool hasLight = false;
        for (const auto& n : s.nodes) hasLight |= n.kind == NodeKind::Light;
        if (!hasLight) {
            Node3D sun; sun.kind = NodeKind::Light; sun.lightType = LightType::Directional; sun.name = tr("Directional Light");
            sun.rotation = QVector3D(-40, -35, 0); sun.intensity = 1.0;
            QVector3D mn, mx;
            if (s.worldBounds(&mn, &mx)) sun.position = (mn + mx) * 0.5f + QVector3D(0, (mx - mn).length(), 0);
            s.addNode(sun);
        }
        return true;
    }, tr("Import Model"));
    d->undoStack()->endMacro();
    if (!root.isNull()) m_s->setSelectedNode(root);
    return root;
}

bool Scene3DActions::deleteSelected() {
    Layer* l = m_s->active3DLayer();
    const QUuid id = m_s->selectedNode();
    if (!l || id.isNull() || l->props.locked) return false;
    const bool ok = m_s->doc()->editScene3D(l->id, [&](Scene3D& s) { if (!s.node(id)) return false; s.removeNode(id); return true; }, tr("Delete Node"));
    if (ok) m_s->setSelectedNode({});
    return ok;
}

QUuid Scene3DActions::duplicateSelected() {
    Layer* l = m_s->active3DLayer();
    const QUuid id = m_s->selectedNode();
    if (!l || id.isNull() || l->props.locked) return {};
    QUuid made;
    m_s->doc()->editScene3D(l->id, [&](Scene3D& s) {
        if (!s.node(id)) return false;
        std::map<QUuid, QUuid> map;
        std::vector<QUuid> order;
        std::function<void(const QUuid&)> walk = [&](const QUuid& i) { order.push_back(i); for (const QUuid& c : s.childrenOf(i)) walk(c); };
        walk(id);
        for (const QUuid& i : order) map[i] = QUuid::createUuid();
        for (const QUuid& i : order) {
            Node3D n = *s.node(i);
            n.id = map[i];
            if (i == id) n.name = s.uniqueName(n.name);
            else n.parent = map[n.parent];
            s.nodes.push_back(n);
        }
        made = map[id];
        return true;
    }, tr("Duplicate Node"));
    if (!made.isNull()) m_s->setSelectedNode(made);
    return made;
}

bool Scene3DActions::setSceneCamera(const QUuid& node) {
    Layer* l = m_s->active3DLayer();
    if (!l || l->props.locked) return false;
    return m_s->doc()->editScene3D(l->id, [&](Scene3D& s) {
        const Node3D* n = s.node(node);
        if (!n || n->kind != NodeKind::Camera || s.activeCamera == node) return false;
        s.activeCamera = node;
        return true;
    }, tr("Set Scene Camera"));
}

bool Scene3DActions::reparentNode(const QUuid& node, const QUuid& newParent) {
    Layer* l = m_s->active3DLayer();
    if (!l || l->props.locked) return false;
    return m_s->doc()->editScene3D(l->id, [&](Scene3D& s) { return s.reparent(node, newParent); }, tr("Reparent Node"));
}

bool Scene3DActions::moveNode(const QUuid& node, const QUuid& newParent, const QUuid& ref, bool after) {
    Layer* l = m_s->active3DLayer();
    if (!l || l->props.locked) return false;
    return m_s->doc()->editScene3D(l->id, [&](Scene3D& s) { return s.moveNode(node, newParent, ref, after); }, tr("Move Node"));
}

}  // namespace mynd
