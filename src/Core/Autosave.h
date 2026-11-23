#pragma once
#include <QObject>
#include <QTimer>

#include "Scene/Document.h"

namespace mynd {

struct RecoveryEntry {
    QString autosavePath;
    QString originalPath;  // empty for never-saved documents
    QDateTime when;
};

// Periodically writes an autosave copy of the attached document (when modified) and removes it
// on clean close/save. Entries left behind after a crash are offered for recovery at startup.
class Autosaver : public QObject {
    Q_OBJECT
public:
    explicit Autosaver(QObject* parent = nullptr);
    void attach(Document* doc);
    void detach(bool removeFile);   // removeFile=true on clean close/save
    void setInterval(int seconds);
    void setEnabled(bool on);
    bool saveNow();                 // exposed for tests
    QString currentAutosavePath() const;

    static QList<RecoveryEntry> findRecoverable(const QString& dir = {});
    static void discard(const RecoveryEntry& e);

private:
    Document* m_doc = nullptr;
    QTimer m_timer;
    bool m_enabled = true;
};

}  // namespace mynd
