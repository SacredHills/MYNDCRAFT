#pragma once
#include <QJsonObject>
#include <QStringList>
#include <memory>

#include "Scene/Document.h"

namespace mynd {

constexpr int kProjectFormatVersion = 3;
constexpr const char* kProjectFormatName = "myndcraft-studio";

struct LoadResult {
    std::unique_ptr<Document> doc;
    QString error;           // non-empty if doc is null
    QStringList warnings;    // recoverable problems (missing assets, etc.)
};

class ProjectIO {
public:
    // Writes atomically (temp file + rename) and keeps the previous file as "<path>.bak".
    static bool save(Document& doc, const QString& path, QString* error, bool keepBackup = true);
    // Same container, but does not touch the document's path/clean state (used by autosave).
    static bool saveCopy(const Document& doc, const QString& path, QString* error);
    static LoadResult load(const QString& path);
    // Apply schema migrations from `fromVersion` up to kProjectFormatVersion.
    static bool migrate(QJsonObject& compositionJson, int fromVersion, QString* error);
    // Structural validation of a loaded document; returns problems found.
    static QStringList validate(const Document& doc);
};

}  // namespace mynd
