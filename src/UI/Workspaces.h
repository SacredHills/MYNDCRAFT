#pragma once
#include <QMainWindow>
#include <QMap>
#include <QObject>

#include "DockManager.h"
#include "DockWidget.h"

namespace mynd {

// Panel layouts on top of the Qt Advanced Docking System. Every panel, the canvas included, can be docked beside, above, below or tabbed with any
// other panel, or floated. Presets arrange the panels; they never restrict what the project can contain. Users can save the current arrangement
// as a named custom workspace.
class WorkspaceManager : public QObject {
    Q_OBJECT
public:
    WorkspaceManager(QMainWindow* w, ads::CDockManager* mgr, const QMap<QString, ads::CDockWidget*>& docks, QObject* parent = nullptr);

    static QStringList presetNames();
    QStringList customNames() const;
    QStringList allNames() const { return presetNames() + customNames(); }
    QString current() const { return m_current; }

    bool apply(const QString& name);        // preset or custom
    void reapplyCurrent();                  // re-restore the current layout (used once the window is really shown)
    void saveCurrentAs(const QString& name);
    bool deleteCustom(const QString& name);
    void saveCurrent();                     // remember the panel arrangement of the workspace being left
    void resetCurrent();                    // built-ins: back to the factory layout; custom: reload saved

signals:
    void aboutToChange();   // a layout is about to be applied (panels not in it get closed by the docking system)
    void changed();

private:
    void applyPreset(const QString& name);
    void clearLayout();
    QByteArray captureLayout();
    void dockFloatingPanels();
    bool restoreLayout(const QByteArray& bytes);
    QMainWindow* m_win;
    ads::CDockManager* m_mgr;
    QMap<QString, ads::CDockWidget*> m_docks;
    QString m_current;
};

}  // namespace mynd
