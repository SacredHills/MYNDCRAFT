#include "UI/Workspaces.h"

#include <QToolBar>

#include "Core/Settings.h"

using namespace ads;

namespace mynd {

WorkspaceManager::WorkspaceManager(QMainWindow* w, CDockManager* mgr, const QMap<QString, CDockWidget*>& docks, QObject* parent)
    : QObject(parent), m_win(w), m_mgr(mgr), m_docks(docks) {}

// Layout bytes = "ADS1" + the docking system's saved state. Layouts saved by the earlier Qt main-window docking have no such prefix and
// cannot be read any more: those workspaces start from the factory arrangement.
static const QByteArray kMagic = "ADS1";

// Floating panels are not stored in workspaces: restoring a floating window from saved state leaves it without a link to the dock manager
// in the docking library (then removing it crashes), so they are docked as tabs before a layout is captured or cleared.
void WorkspaceManager::dockFloatingPanels() {
    CDockAreaWidget* anchor = nullptr;
    for (CDockWidget* d : m_docks) if (d && !d->isFloating() && !d->isClosed() && d->dockAreaWidget()) { anchor = d->dockAreaWidget(); break; }
    if (!anchor) return;
    for (CDockWidget* d : m_docks) if (d && d->isFloating()) m_mgr->addDockWidgetTabToArea(d, anchor);
}

QByteArray WorkspaceManager::captureLayout() {
    dockFloatingPanels();
    return kMagic + m_mgr->saveState();
}

bool WorkspaceManager::restoreLayout(const QByteArray& bytes) {
    if (!bytes.startsWith(kMagic)) return false;
    // A saved layout can only be put back onto panels the docking system knows. On the first start of the app none has been placed
    // yet, so build the standard arrangement first; restoring then moves them where they were.
    for (CDockWidget* d : m_docks) if (d && !m_mgr->dockWidgetsMap().contains(d->objectName())) { applyPreset("Drawing"); break; }
    const bool ok = m_mgr->restoreState(bytes.mid(kMagic.size()));
    if (!ok) return false;
    for (CDockWidget* d : m_docks) {                       // the canvas can never be gone
        if (d && d->objectName() == "dock.canvas" && (d->isClosed() || (m_win->isVisible() && !d->isVisible()))) { return false; }   // before the first show nothing is visible yet
    }
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
        emit aboutToChange();
        applyPreset(m_current);
        emit changed();
    } else if (!m_current.isEmpty()) {
        const QByteArray st = settings::workspaceState(m_current);
        if (!st.isEmpty() && !restoreLayout(st)) applyPreset("Drawing");
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

// Takes every panel out of the layout (floating ones too), so a layout is always built from the same starting point.
void WorkspaceManager::clearLayout() {
    dockFloatingPanels();
    for (CDockWidget* d : m_docks) if (d) m_mgr->removeDockWidget(d);
}

bool WorkspaceManager::apply(const QString& name) {
    if (!presetNames().contains(name) && !customNames().contains(name)) return false;
    emit aboutToChange();
    if (name != m_current) saveCurrent();                       // keep the arrangement of the one we are leaving
    if (presetNames().contains(name)) {
        const QByteArray mine = settings::presetLayout(name);
        if (mine.isEmpty() || !restoreLayout(mine)) applyPreset(name);
    } else {
        const QByteArray st = settings::workspaceState(name);
        if (st.isEmpty()) return false;
        if (!restoreLayout(st)) applyPreset("Drawing");          // saved by the old docking system: start from the Drawing arrangement
    }
    m_current = name;
    settings::setLastWorkspace(name);
    emit changed();
    return true;
}

void WorkspaceManager::applyPreset(const QString& name) {
    CDockWidget* cvs = m_docks["canvas"];
    CDockWidget* opt = m_docks["options"];
    CDockWidget* lay = m_docks["layers"];
    CDockWidget* his = m_docks["history"];
    CDockWidget* tim = m_docks["timeline"];
    CDockWidget* exs = m_docks["exposure"];
    CDockWidget* oni = m_docks["onion"];
    CDockWidget* prp = m_docks["properties"];
    CDockWidget* s3d = m_docks.value("scene3d");
    CDockWidget* v3d = m_docks.value("viewport3d");
    clearLayout();
    if (QToolBar* tb = m_win->findChild<QToolBar*>("tools")) { tb->show(); m_win->addToolBar(Qt::LeftToolBarArea, tb); }

    QSet<CDockWidget*> shown;
    auto place = [&](CDockWidget* d) { shown.insert(d); };
    const int W = 1500, H = 900;                                  // reference size: splitter sizes are proportions
    CDockAreaWidget* cA = m_mgr->addDockWidget(CenterDockWidgetArea, cvs);
    place(cvs);
    if (name == "Drawing") {
        // Everything stacked on the right; history hidden.
        auto* sA = m_mgr->addDockWidget(RightDockWidgetArea, opt, cA);
        auto* oA = sA;
        auto* lA = m_mgr->addDockWidget(BottomDockWidgetArea, lay, oA);
        place(opt); place(lay);
        m_mgr->setSplitterSizes(cA, {W - 300, 300});
        m_mgr->setSplitterSizes(sA, {230, 490});
        Q_UNUSED(lA);
    } else if (name == "Animation") {
        // Options + onion skin + properties (tabbed) and colors on the left; exposure sheet over layers on the right; timeline below.
        auto* oA = m_mgr->addDockWidget(LeftDockWidgetArea, opt, cA);
        m_mgr->addDockWidgetTabToArea(oni, oA);
        m_mgr->addDockWidgetTabToArea(prp, oA);
        auto* eA = m_mgr->addDockWidget(RightDockWidgetArea, exs, cA);
        auto* lA = m_mgr->addDockWidget(BottomDockWidgetArea, lay, eA);
        m_mgr->addDockWidgetTabToArea(his, lA);
        auto* tA = m_mgr->addDockWidget(BottomDockWidgetArea, tim);
        for (auto* d : {opt, oni, prp, exs, lay, his, tim}) place(d);
        m_mgr->setSplitterSizes(cA, {250, W - 520, 270});
        m_mgr->setSplitterSizes(eA, {330, 220});
        m_mgr->setSplitterSizes(tA, {H - 260, 210});
        prp->setAsCurrentTab(); lay->setAsCurrentTab();
    } else if (name == "3D") {
        // scene hierarchy + inspector (tabbed with tool options) on the left; canvas and 3D view in the middle; layers on the right; timeline below.
        auto* sA = m_mgr->addDockWidget(LeftDockWidgetArea, s3d, cA);
        m_mgr->addDockWidgetTabToArea(opt, sA);
        m_mgr->addDockWidgetTabToArea(prp, sA);
        auto* vA = m_mgr->addDockWidget(RightDockWidgetArea, v3d, cA);
        auto* lA = m_mgr->addDockWidget(RightDockWidgetArea, lay, vA);
        m_mgr->addDockWidgetTabToArea(his, lA);
        auto* tA = m_mgr->addDockWidget(BottomDockWidgetArea, tim);
        for (auto* d : {s3d, opt, prp, v3d, lay, his, tim}) place(d);
        const int mid = (W - 300 - 260) / 2;
        m_mgr->setSplitterSizes(cA, {300, mid, mid, 260});
        m_mgr->setSplitterSizes(tA, {H - 250, 200});
        s3d->setAsCurrentTab(); lay->setAsCurrentTab();
    } else {  // Illustration: options + colors on the left, canvas, layers + history (tabbed) on the right
        m_mgr->addDockWidget(LeftDockWidgetArea, opt, cA);
        auto* lA = m_mgr->addDockWidget(RightDockWidgetArea, lay, cA);
        m_mgr->addDockWidgetTabToArea(his, lA);
        for (auto* d : {opt, lay, his}) place(d);
        m_mgr->setSplitterSizes(cA, {260, W - 560, 300});
        lay->setAsCurrentTab();
    }
    // Effects & Masks sits as a tab next to the layers panel in every workspace.
    if (CDockWidget* fxd = m_docks.value("effects")) { m_mgr->addDockWidgetTabToArea(fxd, lay->dockAreaWidget()); shown.insert(fxd); lay->setAsCurrentTab(); }
    // Panels this workspace does not show are parked (closed) next to the layers panel: the Window menu brings them back there.
    CDockAreaWidget* park = lay->dockAreaWidget();
    for (CDockWidget* d : {opt, lay, his, tim, exs, oni, prp, s3d, v3d}) {
        if (!d || shown.contains(d)) continue;
        m_mgr->addDockWidgetTabToArea(d, park);
        d->toggleView(false);
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
