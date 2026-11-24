#pragma once
#include <QJsonObject>
#include <QStringList>
#include <memory>

#include "Scene/Document.h"

namespace mynd {

constexpr int kProjectFormatVersion = 5;
constexpr const char* kProjectFormatName = "myndcraft-studio";

struct LoadResult {
    std::unique_ptr<Document> doc;
    QString error;           // non-empty if doc is null
    QStringList warnings;    // recoverable problems (missing assets, etc.)
};

struct ProjectSnapshot;

class ProjectIO {
public:
    // Writes atomically (temp file + rename) and keeps the previous file as "<path>.bak".
    static bool save(Document& doc, const QString& path, QString* error, bool keepBackup = true);
    // Same container, but does not touch the document's path/clean state (used by autosave).
    // Two-step save for background writing: snapshot() on the UI thread (cheap: images are shared copy-on-write), then
    // saveSnapshot() on any thread (PNG encoding, compression, disk I/O).
    static std::shared_ptr<ProjectSnapshot> snapshot(const Document& doc);
    static bool saveSnapshot(const ProjectSnapshot& snap, const QString& path, QString* error);
    static bool saveCopy(const Document& doc, const QString& path, QString* error);
    static LoadResult load(const QString& path);
    // Apply schema migrations from `fromVersion` up to kProjectFormatVersion.
    static bool migrate(QJsonObject& compositionJson, int fromVersion, QString* error);
    // Structural validation of a loaded document; returns problems found.
    static QStringList validate(const Document& doc);
};

}  // namespace mynd
