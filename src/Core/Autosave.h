#pragma once
#include <QObject>
#include <QThread>
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
    ~Autosaver() override { waitForWriter(); }
    void waitForWriter();
    void attach(Document* doc);
    void detach(bool removeFile);   // removeFile=true on clean close/save
    void setInterval(int seconds);
    void setEnabled(bool on);
    bool saveNow();                 // synchronous (tests, and anything that must have the file right now)
    bool saveInBackground();        // what the timer uses: the UI thread only snapshots, a worker thread encodes and writes
    bool busy() const { return m_busy; }
    QString currentAutosavePath() const;

    static QList<RecoveryEntry> findRecoverable(const QString& dir = {});
    static void discard(const RecoveryEntry& e);

private:
    Document* m_doc = nullptr;
    QTimer m_timer;
    bool m_enabled = true;
    bool m_busy = false;
    QThread* m_thread = nullptr;
};

}  // namespace mynd
