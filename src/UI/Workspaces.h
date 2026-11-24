#pragma once
#include <QDockWidget>
#include <QMainWindow>
#include <QMap>
#include <QObject>

namespace mynd {

// Panel layouts. Presets rearrange the dock panels; they never restrict what the project can
// contain. Users can save the current arrangement as a named custom workspace.
class WorkspaceManager : public QObject {
    Q_OBJECT
public:
    WorkspaceManager(QMainWindow* w, const QMap<QString, QDockWidget*>& docks, QObject* parent = nullptr);

    static QStringList presetNames();
    QStringList customNames() const;
    QStringList allNames() const { return presetNames() + customNames(); }
    QString current() const { return m_current; }

    bool apply(const QString& name);
    void reapplyCurrent();   // re-restore the current layout (used once the window is really shown)        // preset or custom
    void saveCurrentAs(const QString& name);
    bool deleteCustom(const QString& name);
    void saveCurrent();                     // remember the panel arrangement of the workspace being left
    void resetCurrent();                    // built-ins: back to the factory layout; custom: reload saved

signals:
    void changed();

private:
    void applyPreset(const QString& name);
    QByteArray captureLayout() const;
    bool restoreLayout(const QByteArray& bytes);
    void clearDocks();
    QMainWindow* m_win;
    QMap<QString, QDockWidget*> m_docks;
    QString m_current;
};

}  // namespace mynd
