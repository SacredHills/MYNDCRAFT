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

#include "Scene3D/Anim3D.h"

namespace mynd {

namespace {

QString rasterAssetName(const QUuid& id) {
    return "assets/raster/" + id.toString(QUuid::WithoutBraces) + ".png";
}

QString drawingAssetName(const QUuid& layer, const QUuid& drawing) {
    return "assets/drawings/" + layer.toString(QUuid::WithoutBraces) + "/" + drawing.toString(QUuid::WithoutBraces) + ".png";
}

QString meshAssetName(const QUuid& layer, const QUuid& mesh) {
    return "assets/scene3d/" + layer.toString(QUuid::WithoutBraces) + "/meshes/" + mesh.toString(QUuid::WithoutBraces) + ".bin";
}
QString textureAssetName(const QUuid& layer, const QUuid& tex) {
    return "assets/scene3d/" + layer.toString(QUuid::WithoutBraces) + "/textures/" + tex.toString(QUuid::WithoutBraces) + ".png";
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
    if (l.fx.any()) o["fx"] = l.fx.toJson();
    if (l.type == LayerType::Raster) o["asset"] = rasterAssetName(l.id);
    if (l.type == LayerType::Vector) {
        // While the end shapes are being edited the two lists are swapped in memory; the file always has start/end the right way round.
        o["shapes"] = shapesToJson(l.editingEnd ? l.morphShapes : l.shapes);
        if (!(l.editingEnd ? l.shapes : l.morphShapes).isEmpty()) o["morphShapes"] = shapesToJson(l.editingEnd ? l.shapes : l.morphShapes);
    }
    if (l.type == LayerType::Text) o["text"] = l.textContent.toJson();
    if (l.type == LayerType::Scene3D && l.scene3d) o["scene3d"] = l.scene3d->toJson();
    if (l.type == LayerType::Animation) o["animation"] = l.anim.toJson();
    {
        // Keyframes of deleted 3D nodes/materials are dropped rather than saved as orphans.
        const PropertyTracks tr = l.scene3d ? pruneSceneTracks(*l.scene3d, l.tracks) : l.tracks;
        if (!tr.isEmpty()) o["tracks"] = tracksToJson(tr);
    }
    if (l.type == LayerType::Symbol) {
        o["symbolId"] = l.symbolId.toString(QUuid::WithoutBraces);
        o["play"] = symbolPlayKey(l.symbolPlay);
        o["firstFrame"] = l.symbolFirstFrame;
    }
    if (l.type == LayerType::Group) {
        QJsonArray ch;
        for (const auto& c : l.children) ch.append(layerToJson(*c));
        o["children"] = ch;
    }
    return o;
}

void collectRaster(const Layer& g, QVector<const Layer*>& out) {
    for (const auto& c : g.children) {
        if (c->type == LayerType::Raster || c->type == LayerType::Animation || c->type == LayerType::Scene3D) out.append(c.get());
        collectRaster(*c, out);
    }
}

// Everything a project file contains, captured on the UI thread (images are shared copy-on-write handles, so this is cheap) and
// encoded / compressed / written later, possibly on another thread.
struct SnapEntry { QString name; QByteArray data; QImage image; bool png = false; bool compress = false; };

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

bool buildEntries(const Document& doc, std::vector<SnapEntry>& entries, QString* error) {
    Q_UNUSED(error);
    struct { std::vector<SnapEntry>* v; bool add(const QString& n, const QByteArray& d, bool c) { v->push_back({n, d, {}, false, c}); return true; } bool addPng(const QString& n, const QImage& i) { v->push_back({n, {}, i, true, false}); return true; } } w{&entries};

    QJsonObject manifest;
    manifest["format"] = kProjectFormatName;
    manifest["version"] = kProjectFormatVersion;
    manifest["app"] = QString("MyndCraft %1").arg(QCoreApplication::applicationVersion());
    manifest["docId"] = doc.docId().toString(QUuid::WithoutBraces);
    manifest["composition"] = "composition.json";

    const CompositionSettings c = doc.sceneComp();     // always the main scene, even while a symbol is open
    QJsonObject comp;
    comp["width"] = c.width;
    comp["height"] = c.height;
    comp["fpsNum"] = c.fps.num;
    comp["fpsDen"] = c.fps.den;
    comp["background"] = c.background.name(QColor::HexArgb);
    comp["gridSize"] = c.gridSize;
    comp["durationFrames"] = c.durationFrames;
    comp["workEnabled"] = c.workEnabled;
    comp["workIn"] = c.workIn;
    comp["workOut"] = c.workOut;
    QJsonArray guides;
    for (const auto& g : c.guides)
        guides.append(QJsonObject{{"horizontal", g.orientation == Qt::Horizontal}, {"pos", g.pos}});
    comp["guides"] = guides;
    QJsonArray root;
    for (const auto& ch : doc.sceneRoot()->children) root.append(layerToJson(*ch));
    comp["layers"] = root;
    QJsonArray syms;
    for (const SymbolDef& d : doc.symbols()) {
        QJsonObject so;
        so["id"] = d.id.toString(QUuid::WithoutBraces);
        so["name"] = d.name;
        so["kind"] = symbolKindKey(d.kind);
        so["durationFrames"] = d.durationFrames;
        QJsonArray lay;
        if (d.root) for (const auto& ch : d.root->children) lay.append(layerToJson(*ch));
        so["layers"] = lay;
        syms.append(so);
    }
    if (!syms.isEmpty()) comp["symbols"] = syms;

    bool ok = w.add("manifest.json", QJsonDocument(manifest).toJson(), true) &&
              w.add("composition.json", QJsonDocument(comp).toJson(QJsonDocument::Compact), true);
    QVector<const Layer*> rasters;
    collectRaster(*doc.sceneRoot(), rasters);
    for (const SymbolDef& d : doc.symbols()) if (d.root) collectRaster(*d.root, rasters);
    for (const Layer* l : rasters) {
        if (!ok) break;
        if (l->type == LayerType::Scene3D) {
            if (!l->scene3d) continue;
            for (const auto& [mid, mesh] : l->scene3d->meshes) { ok = w.add(meshAssetName(l->id, mid), mesh->geometryBlob(), true); if (!ok) break; }
            for (const auto& [tid, img] : l->scene3d->textures) {
                if (!ok) break;
                ok = w.addPng(textureAssetName(l->id, tid), img);
            }
            continue;
        }
        if (l->type == LayerType::Animation) {
            for (const auto& dm : l->anim.drawings) {   // only drawings the layer still lists; undo-orphans are dropped
                auto it = l->drawingImages.find(dm.id);
                if (it == l->drawingImages.end()) { ok = false; break; }
                ok = w.addPng(drawingAssetName(l->id, dm.id), it->second);
                if (!ok) break;
            }
            continue;
        }
        ok = w.addPng(rasterAssetName(l->id), l->raster);
    }
    return ok;
}

bool writeEntries(const std::vector<SnapEntry>& entries, const QString& path, QString* error) {
    int zerr = 0;
    QByteArray p = QFile::encodeName(path);
    zip_t* z = zip_open(p.constData(), ZIP_CREATE | ZIP_TRUNCATE, &zerr);
    if (!z) { if (error) *error = QString("Cannot create archive (libzip error %1)").arg(zerr); return false; }
    ZipWriter w; w.z = z;
    bool ok = true;
    for (const SnapEntry& e : entries) {
        if (e.png) {
            QByteArray png; QBuffer buf(&png); buf.open(QIODevice::WriteOnly);
            if (!e.image.save(&buf, "PNG")) { ok = false; break; }
            ok = w.add(e.name, png, false);
        } else {
            ok = w.add(e.name, e.data, e.compress);
        }
        if (!ok) break;
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

bool writeZip(const Document& doc, const QString& path, QString* error) {
    std::vector<SnapEntry> entries;
    if (!buildEntries(doc, entries, error)) { if (error && error->isEmpty()) *error = "Failed to add data to project archive"; return false; }
    return writeEntries(entries, path, error);
}

}  // namespace

struct ProjectSnapshot { std::vector<SnapEntry> entries; bool ok = false; };

std::shared_ptr<ProjectSnapshot> ProjectIO::snapshot(const Document& doc) {
    auto s = std::make_shared<ProjectSnapshot>();
    s->ok = buildEntries(doc, s->entries, nullptr);
    return s;
}

bool ProjectIO::saveSnapshot(const ProjectSnapshot& snap, const QString& path, QString* error) {
    if (!snap.ok) { if (error) *error = "Failed to capture the project"; return false; }
    const QString tmp = path + ".tmp";
    QFile::remove(tmp);
    if (!writeEntries(snap.entries, tmp, error)) { QFile::remove(tmp); return false; }
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) { if (error) *error = "Cannot move finished file into place: " + path; QFile::remove(tmp); return false; }
    return true;
}

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
    // v3 -> v4 also added shape tweens (morphShapes, the "shape.morph" property).
    // v4 -> v5 added 3D scene layers (scene3d JSON + mesh/texture assets), optional as well.
    // v3 -> v4 added the symbol library and symbol-instance layers, also optional.
    // v2 -> v3 added per-layer property tracks (transform/opacity keyframes), also optional.
    // v1 -> v2 added animation layers, drawings and timeline settings. Every new field is optional with
    // a default, so a v1 document needs no transformation. Each future breaking change adds a step here.
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
    if (o.contains("fx")) { QStringList fw; l->fx = LayerFx::fromJson(o["fx"].toObject(), &fw); for (const QString& w : fw) warnings << QString("Layer '%1': %2").arg(l->props.name, w); }
    if (o.contains("tracks")) {
        QStringList tw;
        if (!tracksFromJson(o["tracks"].toObject(), l->tracks, err, &tw)) return nullptr;
        for (const QString& w : tw) warnings << QString("Layer '%1': %2").arg(l->props.name, w);
    }

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
        if (o.contains("morphShapes") && !shapesFromJson(o["morphShapes"].toArray(), l->morphShapes, err)) return nullptr;
    } else if (t == LayerType::Text) {
        if (!TextContent::fromJson(o["text"].toObject(), l->textContent, err)) return nullptr;
    } else if (t == LayerType::Animation) {
        if (!AnimationState::fromJson(o["animation"].toObject(), l->anim, err)) return nullptr;
        for (const auto& dm : l->anim.drawings) {
            bool found = false;
            const QByteArray name = drawingAssetName(l->id, dm.id).toUtf8();
            QByteArray data = readEntry(z, name.constData(), &found);
            QImage img;
            if (found && img.loadFromData(data, "PNG") && !img.isNull()) {
                img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                if (img.size() != doc.canvasSize()) {
                    warnings << QString("Layer '%1' drawing '%2': size differs from canvas, cropped/padded").arg(l->props.name, dm.name);
                    QImage fixed = doc.newRasterImage();
                    QPainter p(&fixed); p.drawImage(0, 0, img); p.end();
                    img = fixed;
                }
            } else {
                warnings << QString("Layer '%1' drawing '%2': missing or unreadable image; drawing is blank").arg(l->props.name, dm.name);
                img = doc.newRasterImage();
            }
            l->drawingImages[dm.id] = img;
        }
    } else if (t == LayerType::Scene3D) {
        const QJsonObject so = o["scene3d"].toObject();
        QString serr;
        if (!Scene3D::fromJson(so, *l->scene3d, &serr)) { *err = "3D scene '" + l->props.name + "': " + serr; return nullptr; }
        for (const auto& mv : so["meshes"].toArray()) {
            const QJsonObject mo = mv.toObject();
            const QUuid mid(mo["id"].toString());
            bool found = false;
            const QByteArray blob = readEntry(z, meshAssetName(l->id, mid).toUtf8().constData(), &found);
            std::shared_ptr<Mesh> mesh = found ? Mesh::fromJson(mo, blob, &serr) : nullptr;
            if (mesh) l->scene3d->meshes[mesh->id] = mesh;
            else warnings << QString("3D scene '%1': mesh '%2' is missing or damaged (%3); objects using it are not drawn").arg(l->props.name, mo["name"].toString(), found ? serr : QString("file missing"));
        }
        for (const auto& tv : so["textures"].toArray()) {
            const QUuid tid(tv.toString());
            bool found = false;
            const QByteArray png = readEntry(z, textureAssetName(l->id, tid).toUtf8().constData(), &found);
            QImage img;
            if (found && img.loadFromData(png, "PNG") && !img.isNull()) l->scene3d->textures[tid] = img;
            else warnings << QString("3D scene '%1': a texture is missing or unreadable; surfaces using it are drawn untextured").arg(l->props.name);
        }
        l->scene3d->touch();
        for (const QString& w : l->scene3d->validate()) {
            if (w.contains("ancestor") || w.contains("duplicate")) { *err = "3D scene '" + l->props.name + "': " + w; return nullptr; }
            warnings << QString("3D scene '%1': %2").arg(l->props.name, w);
        }
        const PropertyTracks kept = pruneSceneTracks(*l->scene3d, l->tracks);
        if (kept.size() != l->tracks.size()) {
            warnings << QString("3D scene '%1': animation of %2 missing node(s)/material(s) was dropped").arg(l->props.name).arg(l->tracks.size() - kept.size());
            l->tracks = kept;
        }
    } else if (t == LayerType::Symbol) {
        l->symbolId = QUuid(o["symbolId"].toString());
        if (!symbolPlayFromKey(o["play"].toString("loop"), l->symbolPlay)) {
            warnings << QString("Layer '%1': unknown symbol playback mode, using Loop").arg(l->props.name);
            l->symbolPlay = SymbolPlay::Loop;
        }
        l->symbolFirstFrame = std::max(0, o["firstFrame"].toInt(0));
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
    cs.durationFrames = qBound(1, comp["durationFrames"].toInt(48), kMaxDurationFrames);
    cs.workEnabled = comp["workEnabled"].toBool(false);
    cs.workOut = qBound(0, comp["workOut"].toInt(cs.durationFrames - 1), cs.durationFrames - 1);
    cs.workIn = qBound(0, comp["workIn"].toInt(0), cs.workOut);
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
    QVector<SymbolDef> symbols;
    for (const auto& sv : comp["symbols"].toArray()) {
        const QJsonObject so = sv.toObject();
        SymbolDef d;
        d.id = QUuid(so["id"].toString());
        if (d.id.isNull()) { r.error = "Symbol without valid id"; return r; }
        d.name = so["name"].toString("Symbol");
        if (!symbolKindFromKey(so["kind"].toString("graphic"), d.kind)) { r.warnings << QString("Symbol '%1': unknown type, using Graphic").arg(d.name); d.kind = SymbolKind::Graphic; }
        d.durationFrames = qBound(1, so["durationFrames"].toInt(24), kMaxDurationFrames);
        d.root = std::make_shared<Layer>(LayerType::Group, "Symbol");
        for (const auto& v : so["layers"].toArray()) {
            QString lerr;
            LayerPtr l = layerFromJson(v.toObject(), z, *doc, r.warnings, &lerr, 0);
            if (!l) { r.error = "Corrupt layer data in symbol '" + d.name + "': " + lerr; return r; }
            l->parent = d.root.get();
            d.root->children.push_back(l);
        }
        symbols.append(d);
    }
    doc->rawSetSymbols(symbols);
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
    QSet<QUuid> symbolIds;
    for (const SymbolDef& d : doc.symbols()) {
        if (symbolIds.contains(d.id)) problems << "duplicate symbol id " + d.id.toString();
        symbolIds.insert(d.id);
    }
    auto checkLayers = [&](const Layer* root) {
    Document::forEachLayer(root, [&](Layer* l) {
        if (l->type == LayerType::Symbol && !symbolIds.contains(l->symbolId))
            problems << "instance '" + l->props.name + "' refers to a missing symbol";
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
    };
    checkLayers(doc.sceneRoot());
    for (const SymbolDef& d : doc.symbols()) {
        if (d.root) checkLayers(d.root.get());
        for (const SymbolDef& e : doc.symbols())
            if (d.id != e.id && doc.symbolUses(d.id, e.id) && doc.symbolUses(e.id, d.id)) problems << "symbols '" + d.name + "' and '" + e.name + "' contain each other";
        if (doc.symbolUses(d.id, d.id)) problems << "symbol '" + d.name + "' contains itself";
    }
    return problems;
}

}  // namespace mynd
