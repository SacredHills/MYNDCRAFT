#include "Core/Settings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace mynd::settings {

static QSettings& s() {
    static QSettings inst(QSettings::NativeFormat, QSettings::UserScope, "MyndCraft", "MyndCraft");
    return inst;
}

bool autosaveEnabled() { return s().value("autosave/enabled", true).toBool(); }
void setAutosaveEnabled(bool on) { s().setValue("autosave/enabled", on); }
bool gpuEffects() { return s().value("render/gpuEffects", false).toBool(); }
void setGpuEffects(bool on) { s().setValue("render/gpuEffects", on); }
int autosaveIntervalSeconds() { return qBound(10, s().value("autosave/interval", 120).toInt(), 3600); }
void setAutosaveIntervalSeconds(int v) { s().setValue("autosave/interval", qBound(10, v, 3600)); }
double defaultGridSize() { return qBound(2.0, s().value("canvas/gridSize", 50.0).toDouble(), 1000.0); }
void setDefaultGridSize(double g) { s().setValue("canvas/gridSize", g); }

QStringList recentFiles() { return s().value("recent").toStringList(); }
void addRecentFile(const QString& p) {
    QStringList l = recentFiles();
    l.removeAll(p);
    l.prepend(p);
    while (l.size() > 10) l.removeLast();
    s().setValue("recent", l);
}
void clearRecentFiles() { s().remove("recent"); }

QStringList workspaceNames() {
    s().beginGroup("workspaces");
    QStringList k = s().childKeys();
    s().endGroup();
    return k;
}
QByteArray workspaceState(const QString& n) { return s().value("workspaces/" + n).toByteArray(); }
void saveWorkspaceState(const QString& n, const QByteArray& st) { s().setValue("workspaces/" + n, st); s().sync(); }
void removeWorkspace(const QString& n) { s().remove("workspaces/" + n); }

QByteArray windowGeometry() { return s().value("window/geometry").toByteArray(); }
void setWindowGeometry(const QByteArray& g) { s().setValue("window/geometry", g); s().sync(); }
QByteArray presetLayout(const QString& n) { return s().value("presetLayouts/" + n).toByteArray(); }
void savePresetLayout(const QString& n, const QByteArray& st) { s().setValue("presetLayouts/" + n, st); s().sync(); }
void removePresetLayout(const QString& n) { s().remove("presetLayouts/" + n); }
QString lastWorkspace() { return s().value("window/workspace", "Drawing").toString(); }
void setLastWorkspace(const QString& n) { s().setValue("window/workspace", n); s().sync(); }

QString autosaveDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/.myndcraft";
    QDir d(base + "/autosave");
    d.mkpath(".");
    return d.absolutePath();
}

}  // namespace mynd::settings
