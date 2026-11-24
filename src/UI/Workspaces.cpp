#include "UI/Workspaces.h"

#include <QToolBar>

#include "Core/Settings.h"

namespace mynd {

WorkspaceManager::WorkspaceManager(QMainWindow* w, const QMap<QString, QDockWidget*>& docks, QObject* parent)
    : QObject(parent), m_win(w), m_docks(docks) {
    // Let panels sit under/over each other anywhere, not just one column per side.
    m_win->setDockNestingEnabled(true);
    m_win->setDockOptions(m_win->dockOptions() | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AnimatedDocks);
}

void WorkspaceManager::saveCurrent() {
    if (m_current.isEmpty() || !m_win->isVisible()) return;   // a window that was never shown has no real layout to keep
    const QByteArray st = m_win->saveState(2);
    if (presetNames().contains(m_current)) settings::savePresetLayout(m_current, st);
    else settings::saveWorkspaceState(m_current, st);
}

void WorkspaceManager::resetCurrent() {
    if (presetNames().contains(m_current)) {
        settings::removePresetLayout(m_current);
        applyPreset(m_current);
        emit changed();
    } else if (!m_current.isEmpty()) {
        const QByteArray st = settings::workspaceState(m_current);
        if (!st.isEmpty() && !m_win->restoreState(st, 2)) m_win->restoreState(st, 1);
    }
}

QStringList WorkspaceManager::presetNames() { return {"Drawing", "Illustration", "Animation"}; }
QStringList WorkspaceManager::customNames() const { return settings::workspaceNames(); }

bool WorkspaceManager::apply(const QString& name) {
    if (!presetNames().contains(name) && !customNames().contains(name)) return false;
    if (name != m_current) saveCurrent();                       // keep the arrangement of the one we are leaving
    if (presetNames().contains(name)) {
        const QByteArray mine = settings::presetLayout(name);
        if (mine.isEmpty() || !m_win->restoreState(mine, 2)) applyPreset(name);   // (layouts saved before the Properties dock existed are version 1: dropped)
    } else if (customNames().contains(name)) {
        const QByteArray st = settings::workspaceState(name);
        if (st.isEmpty()) return false;
        if (!m_win->restoreState(st, 2)) {
            if (!m_win->restoreState(st, 1)) return false;
            if (QDockWidget* p = m_docks.value("properties")) p->hide();   // an older layout does not know this panel
        }
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
    QDockWidget* tim = m_docks["timeline"];
    QDockWidget* exs = m_docks["exposure"];
    QDockWidget* oni = m_docks["onion"];
    QDockWidget* prp = m_docks["properties"];
    for (QDockWidget* d : {opt, sty, lay, his, tim, exs, oni, prp}) { if (d) m_win->removeDockWidget(d); }
    if (name != "Animation") for (QDockWidget* d : {tim, exs, oni, prp}) if (d) d->hide();   // still available from the Window menu
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
    } else if (name == "Animation") {
        // Options + onion skin (tabbed) and colors on the left; exposure sheet over layers on the right; timeline below.
        m_win->addDockWidget(Qt::LeftDockWidgetArea, opt);
        m_win->addDockWidget(Qt::LeftDockWidgetArea, oni);
        m_win->tabifyDockWidget(opt, oni);
        m_win->addDockWidget(Qt::LeftDockWidgetArea, prp);
        m_win->tabifyDockWidget(oni, prp);
        m_win->addDockWidget(Qt::LeftDockWidgetArea, sty);
        m_win->splitDockWidget(opt, sty, Qt::Vertical);
        m_win->addDockWidget(Qt::RightDockWidgetArea, exs);
        m_win->addDockWidget(Qt::RightDockWidgetArea, lay);
        m_win->splitDockWidget(exs, lay, Qt::Vertical);
        m_win->addDockWidget(Qt::RightDockWidgetArea, his);
        m_win->tabifyDockWidget(lay, his);
        m_win->addDockWidget(Qt::BottomDockWidgetArea, tim);
        for (auto* d : {opt, oni, prp, sty, exs, lay, his, tim}) d->show();
        prp->raise(); lay->raise();
        m_win->resizeDocks({tim}, {210}, Qt::Vertical);
        m_win->resizeDocks({opt, exs}, {250, 270}, Qt::Horizontal);
        m_win->resizeDocks({exs, lay}, {330, 220}, Qt::Vertical);
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
    settings::saveWorkspaceState(name, m_win->saveState(2));
    m_current = name;
    settings::setLastWorkspace(name);
    emit changed();
}

bool WorkspaceManager::deleteCustom(const QString& name) {
    if (!customNames().contains(name)) return false;
    settings::removeWorkspace(name);
    if (m_current == name) { m_current.clear(); apply("Drawing"); }   // don't re-save the layout we just deleted
    emit changed();
    return true;
}

}  // namespace mynd
