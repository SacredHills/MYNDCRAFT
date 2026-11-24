#pragma once
#include <QString>
#include <QStringList>

#include "Scene3D/Scene3D.h"

namespace mynd {

// Importers for external models. Each returns a self-contained Scene3D (one root Group node named after the file,
// with the model's hierarchy below it) or an error. Nothing from the file is trusted: every offset, index and count
// is range-checked, sizes are capped, and problems that do not stop the import are returned as warnings.
struct ImportResult {
    std::shared_ptr<Scene3D> scene;
    QStringList warnings;
    QString error;
    QUuid root;                 // the root Group node in `scene`
    explicit operator bool() const { return scene != nullptr; }
};

// `fps` converts animation times (seconds) to composition frames.
qint64 maxModelFileBytes();                                                                       // largest model file accepted (4 GB)
ImportResult importModelFile(const QString& path, double fps = 24.0);                      // picks the importer from the extension
ImportResult importObjData(const QByteArray& data, const QString& baseDir, const QString& name);
ImportResult importGltfData(const QByteArray& data, const QString& baseDir, const QString& name, double fps = 24.0);   // .gltf JSON or binary .glb
ImportResult importFbxData(const QByteArray& data, const QString& baseDir, const QString& name);    // binary FBX 7.x

QStringList supportedModelExtensions();        // lower-case, without the dot
QString modelFileFilter();                     // for file dialogs

// Copies everything in `src` into `dst` with fresh ids, under `parent`. Returns the new id of src's root node (the first node
// without a parent), or a null id when src is empty.
QUuid mergeImport(Scene3D& dst, const Scene3D& src, const QUuid& parent = {});

}  // namespace mynd
