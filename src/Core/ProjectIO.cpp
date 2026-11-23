#include "Core/ProjectIO.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <zip.h>

namespace mynd {

namespace {

QString rasterAssetName(const QUuid& id) {
    return "assets/raster/" + id.toString(QUuid::WithoutBraces) + ".png";
}

QJsonObject layerToJson(const Layer& l) {
    QJsonObject o;
    o["id"] = l.id.toString(QUuid::WithoutBraces);
    o["type"] = layerTypeKey(l.type);
    o["name"] = l.props.name;
    o["visible"] = l.props.visible;
    o["locked"] = l.props.locked;
    o["opacity"] = l.props.opacity;
    o["blend"] = blendModeKey(l.props.blend);
    if (l.type == LayerType::Raster) o["asset"] = rasterAssetName(l.id);
    if (l.type == LayerType::Vector) o["shapes"] = shapesToJson(l.shapes);
    if (l.type == LayerType::Group) {
        QJsonArray ch;
        for (const auto& c : l.children) ch.append(layerToJson(*c));
        o["children"] = ch;
    }
    return o;
}

void collectRaster(const Layer& g, QVector<const Layer*>& out) {
    for (const auto& c : g.children) {
        if (c->type == LayerType::Raster) out.append(c.get());
        collectRaster(*c, out);
    }
}

struct ZipWriter {
    zip_t* z = nullptr;
    QVector<QByteArray*> keep;  // zip_source_buffer needs buffers alive until zip_close
    ~ZipWriter() { for (auto* b : keep) delete b; }
    bool add(const QString& name, const QByteArray& data, bool compress) {
        auto* copy = new QByteArray(data);
        keep.append(copy);
        zip_source_t* src = zip_source_buffer(z, copy->constData(), copy->size(), 0);
        if (!src) return false;
        const QByteArray n = name.toUtf8();
        zip_int64_t idx = zip_file_add(z, n.constData(), src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (idx < 0) { zip_source_free(src); return false; }
        zip_set_file_compression(z, idx, compress ? ZIP_CM_DEFLATE : ZIP_CM_STORE, 0);
        return true;
    }
};

bool writeZip(const Document& doc, const QString& path, QString* error) {
    int zerr = 0;
    QByteArray p = QFile::encodeName(path);
    zip_t* z = zip_open(p.constData(), ZIP_CREATE | ZIP_TRUNCATE, &zerr);
    if (!z) { if (error) *error = QString("Cannot create archive (libzip error %1)").arg(zerr); return false; }
    ZipWriter w; w.z = z;

    QJsonObject manifest;
    manifest["format"] = kProjectFormatName;
    manifest["version"] = kProjectFormatVersion;
    manifest["app"] = QString("MyndCraft %1").arg(QCoreApplication::applicationVersion());
    manifest["docId"] = doc.docId().toString(QUuid::WithoutBraces);
    manifest["composition"] = "composition.json";

    const CompositionSettings& c = doc.comp();
    QJsonObject comp;
    comp["width"] = c.width;
    comp["height"] = c.height;
    comp["fpsNum"] = c.fps.num;
    comp["fpsDen"] = c.fps.den;
    comp["background"] = c.background.name(QColor::HexArgb);
    comp["gridSize"] = c.gridSize;
    QJsonArray guides;
    for (const auto& g : c.guides)
        guides.append(QJsonObject{{"horizontal", g.orientation == Qt::Horizontal}, {"pos", g.pos}});
    comp["guides"] = guides;
    QJsonArray root;
    for (const auto& ch : doc.root()->children) root.append(layerToJson(*ch));
    comp["layers"] = root;

    bool ok = w.add("manifest.json", QJsonDocument(manifest).toJson(), true) &&
              w.add("composition.json", QJsonDocument(comp).toJson(QJsonDocument::Compact), true);
    QVector<const Layer*> rasters;
    collectRaster(*doc.root(), rasters);
    for (const Layer* l : rasters) {
        if (!ok) break;
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        if (!l->raster.save(&buf, "PNG")) { ok = false; break; }
        ok = w.add(rasterAssetName(l->id), png, false);
    }
    if (!ok) {
        zip_discard(z);
        if (error) *error = "Failed to add data to project archive";
        return false;
    }
    if (zip_close(z) != 0) {
        if (error) *error = QString("Failed to finalize archive: %1").arg(zip_strerror(z));
        zip_discard(z);
        return false;
    }
    return true;
}

}  // namespace

bool ProjectIO::saveCopy(const Document& doc, const QString& path, QString* error) {
    const QString tmp = path + ".tmp";
    QFile::remove(tmp);
    if (!writeZip(doc, tmp, error)) { QFile::remove(tmp); return false; }
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        if (error) *error = "Cannot move finished file into place: " + path;
        QFile::remove(tmp);
        return false;
    }
    return true;
}

bool ProjectIO::save(Document& doc, const QString& path, QString* error, bool keepBackup) {
    const QString tmp = path + ".tmp";
    QFile::remove(tmp);
    if (!writeZip(doc, tmp, error)) { QFile::remove(tmp); return false; }
    if (keepBackup && QFileInfo::exists(path)) {
        QFile::remove(path + ".bak");
        QFile::copy(path, path + ".bak");
    }
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        if (error) *error = "Cannot write to " + path;
        QFile::remove(tmp);
        return false;
    }
    doc.setFilePath(path);
    doc.markSaved();
    return true;
}

bool ProjectIO::migrate(QJsonObject&, int fromVersion, QString* error) {
    // Version 1 is the first released schema, so there are no migration steps yet. Each future
    // schema change adds a step here: `if (v == 1) { ...transform...; v = 2; }`.
    if (fromVersion < 1 || fromVersion > kProjectFormatVersion) {
        if (error) *error = QString("Unsupported project version %1").arg(fromVersion);
        return false;
    }
    return true;
}

namespace {

QByteArray readEntry(zip_t* z, const char* name, bool* found, qint64 maxSize = 1LL << 30) {
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(z, name, ZIP_FL_ENC_GUESS, &st) != 0) { *found = false; return {}; }
    *found = true;
    if (st.size > quint64(maxSize)) { *found = false; return {}; }
    zip_file_t* f = zip_fopen(z, name, 0);
    if (!f) { *found = false; return {}; }
    QByteArray data(int(st.size), Qt::Uninitialized);
    zip_int64_t n = zip_fread(f, data.data(), st.size);
    zip_fclose(f);
    if (n != zip_int64_t(st.size)) { *found = false; return {}; }
    return data;
}

LayerPtr layerFromJson(const QJsonObject& o, zip_t* z, const Document& doc, QStringList& warnings,
                       QString* err, int depth) {
    if (depth > 64) { *err = "Layer nesting too deep"; return nullptr; }
    LayerType t;
    if (!layerTypeFromKey(o["type"].toString(), t)) {
        *err = "Unknown layer type '" + o["type"].toString() + "'";
        return nullptr;
    }
    auto l = std::make_shared<Layer>(t, o["name"].toString());
    const QUuid id(o["id"].toString());
    if (id.isNull()) { *err = "Layer without valid id"; return nullptr; }
    l->id = id;
    l->props.visible = o["visible"].toBool(true);
    l->props.locked = o["locked"].toBool(false);
    l->props.opacity = qBound(0.0, o["opacity"].toDouble(1.0), 1.0);
    bool ok = false;
    l->props.blend = blendModeFromKey(o["blend"].toString("normal"), &ok);
    if (!ok) warnings << QString("Layer '%1': unknown blend mode, using Normal").arg(l->props.name);

    if (t == LayerType::Raster) {
        QString asset = o["asset"].toString();
        bool found = false;
        QByteArray data = asset.isEmpty() ? QByteArray() : readEntry(z, asset.toUtf8().constData(), &found);
        QImage img;
        if (found && img.loadFromData(data, "PNG") && !img.isNull()) {
            img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (img.size() != doc.canvasSize()) {
                warnings << QString("Layer '%1': raster size differs from canvas, cropped/padded").arg(l->props.name);
                QImage fixed = doc.newRasterImage();
                QPainter p(&fixed); p.drawImage(0, 0, img); p.end();
                img = fixed;
            }
            l->raster = img;
        } else {
            warnings << QString("Layer '%1': missing or unreadable raster asset '%2'; layer is blank")
                            .arg(l->props.name, asset);
            l->raster = doc.newRasterImage();
        }
    } else if (t == LayerType::Vector) {
        if (!shapesFromJson(o["shapes"].toArray(), l->shapes, err)) return nullptr;
    } else {
        for (const auto& v : o["children"].toArray()) {
            LayerPtr c = layerFromJson(v.toObject(), z, doc, warnings, err, depth + 1);
            if (!c) return nullptr;
            c->parent = l.get();
            l->children.push_back(c);
        }
    }
    return l;
}

}  // namespace

LoadResult ProjectIO::load(const QString& path) {
    LoadResult r;
    int zerr = 0;
    const QByteArray p = QFile::encodeName(path);
    if (!QFileInfo::exists(path)) { r.error = "File not found: " + path; return r; }
    zip_t* z = zip_open(p.constData(), ZIP_RDONLY, &zerr);
    if (!z) { r.error = "Not a valid MyndCraft project (cannot open archive)."; return r; }
    struct Closer { zip_t* z; ~Closer() { zip_discard(z); } } closer{z};

    bool found = false;
    QByteArray mdata = readEntry(z, "manifest.json", &found, 1 << 20);
    if (!found) { r.error = "Project manifest is missing; the file is corrupt or not a MyndCraft project."; return r; }
    QJsonParseError pe;
    QJsonObject manifest = QJsonDocument::fromJson(mdata, &pe).object();
    if (pe.error != QJsonParseError::NoError || manifest["format"].toString() != kProjectFormatName) {
        r.error = "File is not a MyndCraft project.";
        return r;
    }
    const int version = manifest["version"].toInt(0);
    if (version > kProjectFormatVersion) {
        r.error = QString("This project was saved by a newer version of MyndCraft (format %1; this build reads up to %2).")
                      .arg(version).arg(kProjectFormatVersion);
        return r;
    }
    QByteArray cdata = readEntry(z, "composition.json", &found, 1LL << 30);
    if (!found) { r.error = "composition.json is missing from the project."; return r; }
    QJsonObject comp = QJsonDocument::fromJson(cdata, &pe).object();
    if (pe.error != QJsonParseError::NoError) { r.error = "composition.json is corrupt: " + pe.errorString(); return r; }
    QString merr;
    if (!migrate(comp, version, &merr)) { r.error = merr; return r; }

    CompositionSettings cs;
    cs.width = comp["width"].toInt(0);
    cs.height = comp["height"].toInt(0);
    if (cs.width < 1 || cs.height < 1 || cs.width > kMaxCanvasDim || cs.height > kMaxCanvasDim) {
        r.error = QString("Invalid canvas size %1x%2.").arg(cs.width).arg(cs.height);
        return r;
    }
    cs.fps = {comp["fpsNum"].toInt(24), comp["fpsDen"].toInt(1)};
    if (!cs.fps.isValid()) { r.warnings << "Invalid frame rate; using 24 fps"; cs.fps = {24, 1}; }
    QColor bg(comp["background"].toString());
    cs.background = bg.isValid() ? bg : QColor(Qt::white);
    cs.gridSize = comp["gridSize"].toDouble(50);
    for (const auto& g : comp["guides"].toArray()) {
        const QJsonObject go = g.toObject();
        cs.guides.append({go["horizontal"].toBool(true) ? Qt::Horizontal : Qt::Vertical, go["pos"].toDouble()});
    }

    auto doc = std::make_unique<Document>(cs);
    const QUuid docId(manifest["docId"].toString());
    if (!docId.isNull()) doc->setDocId(docId);
    auto root = std::make_shared<Layer>(LayerType::Group, "Root");
    for (const auto& v : comp["layers"].toArray()) {
        QString lerr;
        LayerPtr l = layerFromJson(v.toObject(), z, *doc, r.warnings, &lerr, 0);
        if (!l) { r.error = "Corrupt layer data: " + lerr; return r; }
        l->parent = root.get();
        root->children.push_back(l);
    }
    doc->setRoot(root);
    doc->setFilePath(path);
    const QStringList problems = validate(*doc);
    if (!problems.isEmpty()) { r.error = "Project failed validation: " + problems.join("; "); return r; }
    doc->markSaved();
    r.doc = std::move(doc);
    return r;
}

QStringList ProjectIO::validate(const Document& doc) {
    QStringList problems;
    QSet<QUuid> seen;
    Document::forEachLayer(doc.root(), [&](Layer* l) {
        if (seen.contains(l->id)) problems << "duplicate layer id " + l->id.toString();
        seen.insert(l->id);
        if (l->type == LayerType::Raster && l->raster.size() != doc.canvasSize())
            problems << "raster size mismatch in layer " + l->props.name;
        if (l->type != LayerType::Group && !l->children.empty())
            problems << "non-group layer with children: " + l->props.name;
        QSet<QUuid> shapeIds;
        for (const auto& s : l->shapes) {
            if (shapeIds.contains(s.id)) problems << "duplicate shape id in layer " + l->props.name;
            shapeIds.insert(s.id);
        }
    });
    return problems;
}

}  // namespace mynd
