#include "Core/Autosave.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "Core/ProjectIO.h"
#include "Core/Settings.h"

namespace mynd {

Autosaver::Autosaver(QObject* parent) : QObject(parent) {
    m_timer.setInterval(settings::autosaveIntervalSeconds() * 1000);
    connect(&m_timer, &QTimer::timeout, this, [this] { saveNow(); });
}

void Autosaver::attach(Document* doc) {
    m_doc = doc;
    if (m_enabled) m_timer.start();
}

void Autosaver::detach(bool removeFile) {
    m_timer.stop();
    if (m_doc && removeFile) {
        QFile::remove(currentAutosavePath());
        QFile::remove(currentAutosavePath() + ".json");
    }
    m_doc = nullptr;
}

void Autosaver::setInterval(int s) { m_timer.setInterval(qBound(10, s, 3600) * 1000); }
void Autosaver::setEnabled(bool on) {
    m_enabled = on;
    if (!on) m_timer.stop();
    else if (m_doc) m_timer.start();
}

QString Autosaver::currentAutosavePath() const {
    if (!m_doc) return {};
    return settings::autosaveDir() + "/" + m_doc->docId().toString(QUuid::WithoutBraces) + ".studio";
}

bool Autosaver::saveNow() {
    if (!m_doc || !m_doc->isModified()) return false;
    QString err;
    const QString path = currentAutosavePath();
    if (!ProjectIO::saveCopy(*m_doc, path, &err)) return false;
    QJsonObject meta{{"original", m_doc->filePath()}, {"time", QDateTime::currentDateTime().toString(Qt::ISODate)}};
    QFile f(path + ".json");
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(meta).toJson());
    return true;
}

QList<RecoveryEntry> Autosaver::findRecoverable(const QString& dir) {
    QList<RecoveryEntry> out;
    QDir d(dir.isEmpty() ? settings::autosaveDir() : dir);
    for (const QFileInfo& fi : d.entryInfoList({"*.studio"}, QDir::Files, QDir::Time)) {
        RecoveryEntry e;
        e.autosavePath = fi.absoluteFilePath();
        e.when = fi.lastModified();
        QFile f(e.autosavePath + ".json");
        if (f.open(QIODevice::ReadOnly)) e.originalPath = QJsonDocument::fromJson(f.readAll()).object()["original"].toString();
        out.append(e);
    }
    return out;
}

void Autosaver::discard(const RecoveryEntry& e) {
    QFile::remove(e.autosavePath);
    QFile::remove(e.autosavePath + ".json");
}

}  // namespace mynd
