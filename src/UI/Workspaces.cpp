#include "UI/Workspaces.h"

#include <QToolBar>

#include "Core/Settings.h"

namespace mynd {

WorkspaceManager::WorkspaceManager(QMainWindow* w, const QMap<QString, QDockWidget*>& docks, QObject* parent)
    : QObject(parent), m_win(w), m_docks(docks) {}

QStringList WorkspaceManager::presetNames() { return {"Drawing", "Illustration"}; }
QStringList WorkspaceManager::customNames() const { return settings::workspaceNames(); }

bool WorkspaceManager::apply(const QString& name) {
    if (presetNames().contains(name)) {
        applyPreset(name);
    } else if (customNames().contains(name)) {
        const QByteArray st = settings::workspaceState(name);
        if (st.isEmpty() || !m_win->restoreState(st, 1)) return false;
    } else {
        return false;
    }
    m_current = name;
    settings::setLastWorkspace(name);
    emit changed();
    return true;
}

void WorkspaceManager::applyPreset(const QString& name) {
    QDockWidget* opt = m_docks["options"];
    QDockWidget* sty = m_docks["style"];
    QDockWidget* lay = m_docks["layers"];
    QDockWidget* his = m_docks["history"];
    for (QDockWidget* d : {opt, sty, lay, his}) { m_win->removeDockWidget(d); }
    if (QToolBar* tb = m_win->findChild<QToolBar*>("tools")) { tb->show(); m_win->addToolBar(Qt::LeftToolBarArea, tb); }

    if (name == "Drawing") {
        // Everything stacked on the right; history hidden.
        m_win->addDockWidget(Qt::RightDockWidgetArea, sty);
        m_win->splitDockWidget(sty, opt, Qt::Vertical);
        m_win->splitDockWidget(opt, lay, Qt::Vertical);
        for (auto* d : {sty, opt, lay}) d->show();
        m_win->resizeDocks({sty, opt, lay}, {110, 230, 380}, Qt::Vertical);
        m_win->resizeDocks({sty}, {300}, Qt::Horizontal);
        his->hide();
    } else {  // Illustration: options on the left, layers + history (tabbed) on the right
        m_win->addDockWidget(Qt::LeftDockWidgetArea, opt);
        m_win->addDockWidget(Qt::LeftDockWidgetArea, sty);
        m_win->splitDockWidget(opt, sty, Qt::Vertical);
        m_win->addDockWidget(Qt::RightDockWidgetArea, lay);
        m_win->addDockWidget(Qt::RightDockWidgetArea, his);
        m_win->tabifyDockWidget(lay, his);
        for (auto* d : {opt, sty, lay, his}) d->show();
        lay->raise();
        m_win->resizeDocks({opt, sty}, {240, 120}, Qt::Vertical);
        m_win->resizeDocks({opt, lay}, {260, 300}, Qt::Horizontal);
    }
}

void WorkspaceManager::saveCurrentAs(const QString& name) {
    settings::saveWorkspaceState(name, m_win->saveState(1));
    m_current = name;
    settings::setLastWorkspace(name);
    emit changed();
}

bool WorkspaceManager::deleteCustom(const QString& name) {
    if (!customNames().contains(name)) return false;
    settings::removeWorkspace(name);
    if (m_current == name) apply("Drawing");
    emit changed();
    return true;
}

}  // namespace mynd
