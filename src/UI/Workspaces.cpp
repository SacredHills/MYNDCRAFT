#include "UI/Workspaces.h"

#include <QDataStream>
#include <QIODevice>
#include <QToolBar>

#include "Core/Settings.h"

namespace mynd {

WorkspaceManager::WorkspaceManager(QMainWindow* w, const QMap<QString, QDockWidget*>& docks, QObject* parent)
    : QObject(parent), m_win(w), m_docks(docks) {
    // Let panels sit under/over each other anywhere, not just one column per side.
    m_win->setDockNestingEnabled(true);
    m_win->setDockOptions(m_win->dockOptions() | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AnimatedDocks);
}

// Layout bytes = our own wrapper (magic + names of panels that are hidden) around QMainWindow::saveState(). Qt does not hide a panel that a
// saved layout does not mention, and a hidden panel is not always remembered, so the hidden ones are stored explicitly.
static const quint32 kLayoutMagic = 0x4d594c32;   // "MYL2"

QByteArray WorkspaceManager::captureLayout() const {
    QStringList hidden;
    for (QDockWidget* d : m_docks) if (d && !d->isVisible() && !d->isFloating()) hidden << d->objectName();
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds << kLayoutMagic << hidden << m_win->saveState(2);
    return out;
}

bool WorkspaceManager::restoreLayout(const QByteArray& bytes) {
    QStringList hidden;
    QByteArray st = bytes;
    QDataStream ds(bytes);
    quint32 magic = 0;
    ds >> magic;
    const bool wrapped = magic == kLayoutMagic;
    if (wrapped) { ds >> hidden >> st; if (ds.status() != QDataStream::Ok) return false; }
    clearDocks();
    bool ok = m_win->restoreState(st, 2);
    if (!ok && !wrapped) {
        ok = m_win->restoreState(st, 1);
        if (ok) if (QDockWidget* p = m_docks.value("properties")) p->hide();   // an older layout does not know this panel
    }
    if (!ok) return false;
    for (QDockWidget* d : m_docks) if (d && hidden.contains(d->objectName())) d->hide();
    return true;
}

void WorkspaceManager::saveCurrent() {
    if (m_current.isEmpty() || !m_win->isVisible()) return;   // a window that was never shown has no real layout to keep
    const QByteArray st = captureLayout();
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
        if (!st.isEmpty()) restoreLayout(st);
    }
}

QStringList WorkspaceManager::presetNames() { return {"Drawing", "Illustration", "Animation", "3D"}; }
QStringList WorkspaceManager::customNames() const { return settings::workspaceNames(); }

void WorkspaceManager::reapplyCurrent() {
    const QString n = m_current;
    if (n.isEmpty()) return;
    m_current.clear();          // do not save the (possibly broken) arrangement over the stored one
    apply(n);
}

// Starting from "nothing docked, everything hidden" makes every workspace come back identical instead of depending on the one before.
void WorkspaceManager::clearDocks() {
    for (QDockWidget* d : m_docks) {
        if (!d) continue;
        d->setFloating(false);
        m_win->removeDockWidget(d);
        d->hide();
    }
}

bool WorkspaceManager::apply(const QString& name) {
    if (!presetNames().contains(name) && !customNames().contains(name)) return false;
    if (name != m_current) saveCurrent();                       // keep the arrangement of the one we are leaving
    if (presetNames().contains(name)) {
        const QByteArray mine = settings::presetLayout(name);
        if (mine.isEmpty() || !restoreLayout(mine)) applyPreset(name);
    } else if (customNames().contains(name)) {
        const QByteArray st = settings::workspaceState(name);
        if (st.isEmpty()) return false;
        if (!restoreLayout(st)) return false;
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
    QDockWidget* s3d = m_docks.value("scene3d");
    QDockWidget* v3d = m_docks.value("viewport3d");
    for (QDockWidget* d : {opt, sty, lay, his, tim, exs, oni, prp, s3d, v3d}) { if (d) m_win->removeDockWidget(d); }
    if (name != "3D") for (QDockWidget* d : {s3d, v3d}) if (d) d->hide();       // still available from the Window menu
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
    } else if (name == "3D") {
        // 3D view on the right with the layers below it; scene hierarchy + inspector (tabbed with tool options) on the left; timeline below.
        m_win->addDockWidget(Qt::LeftDockWidgetArea, s3d);
        m_win->addDockWidget(Qt::LeftDockWidgetArea, opt);
        m_win->tabifyDockWidget(s3d, opt);
        m_win->addDockWidget(Qt::LeftDockWidgetArea, prp);
        m_win->tabifyDockWidget(opt, prp);
        m_win->addDockWidget(Qt::RightDockWidgetArea, v3d);
        m_win->addDockWidget(Qt::RightDockWidgetArea, lay);
        m_win->splitDockWidget(v3d, lay, Qt::Vertical);
        m_win->addDockWidget(Qt::RightDockWidgetArea, his);
        m_win->tabifyDockWidget(lay, his);
        m_win->addDockWidget(Qt::BottomDockWidgetArea, tim);
        for (auto* d : {s3d, opt, prp, v3d, lay, his, tim}) d->show();
        s3d->raise(); lay->raise();
        m_win->resizeDocks({tim}, {200}, Qt::Vertical);
        m_win->resizeDocks({s3d, v3d}, {300, 520}, Qt::Horizontal);
        m_win->resizeDocks({v3d, lay}, {420, 160}, Qt::Vertical);
        if (sty) sty->hide();
        if (exs) exs->hide();
        if (oni) oni->hide();
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
    settings::saveWorkspaceState(name, captureLayout());
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
