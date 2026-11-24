#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace mynd::settings {

bool autosaveEnabled();
void setAutosaveEnabled(bool on);
int autosaveIntervalSeconds();
void setAutosaveIntervalSeconds(int s);
double defaultGridSize();
void setDefaultGridSize(double g);

QStringList recentFiles();
void addRecentFile(const QString& path);
void clearRecentFiles();

// Named custom workspace layouts (QMainWindow::saveState blobs).
QStringList workspaceNames();
QByteArray workspaceState(const QString& name);
void saveWorkspaceState(const QString& name, const QByteArray& state);
void removeWorkspace(const QString& name);

QByteArray windowGeometry();
void setWindowGeometry(const QByteArray& g);
QByteArray presetLayout(const QString& name);   // user's rearranged layout of a built-in workspace
void savePresetLayout(const QString& name, const QByteArray& state);
void removePresetLayout(const QString& name);
QString lastWorkspace();
void setLastWorkspace(const QString& n);

QString autosaveDir();

}  // namespace mynd::settings
