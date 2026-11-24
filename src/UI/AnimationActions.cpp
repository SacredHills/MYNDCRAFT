#include "UI/AnimationActions.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

namespace mynd {

QAction* AnimationActions::mk(const QString& name, const QString& text, const QKeySequence& key) {
    auto* a = new QAction(text, this);
    if (!key.isEmpty()) a->setShortcut(key);
    a->setShortcutContext(Qt::WindowShortcut);
    m_byName[name] = a;
    return a;
}

bool AnimationActions::onAnimLayer() const {
    const Layer* l = m_s->activeAnimationLayer();
    return l && !l->props.locked;
}

void AnimationActions::goToFrame(int frame) {
    if (m_s->doc()) m_s->doc()->setCurrentFrame(frame);
}

void AnimationActions::setMarker(PoseMarker m, const QString& text) {
    const int f = m_s->doc()->currentFrame();
    m_s->editActiveAnimation(text, [&](AnimationState& s) { return s.setMarker(f, m); });
}

AnimationActions::AnimationActions(EditorSession* s, QWidget* dialogParent) : QObject(dialogParent), m_s(s), m_parent(dialogParent) {
    auto frame = [this] { return m_s->doc() ? m_s->doc()->currentFrame() : 0; };

    newLayer = mk("newLayer", tr("New Animation Layer"));
    connect(newLayer, &QAction::triggered, this, [this] { m_s->addLayerAboveActive(LayerType::Animation, tr("Animation Layer")); });
    newDrawing = mk("newDrawing", tr("New Drawing at Current Frame"), QKeySequence("Ctrl+Shift+N"));
    connect(newDrawing, &QAction::triggered, this, [this] { m_s->newDrawing(false); });
    duplicateDrawing = mk("duplicateDrawing", tr("Duplicate Drawing at Current Frame"), QKeySequence("Ctrl+Shift+D"));
    connect(duplicateDrawing, &QAction::triggered, this, [this] { m_s->newDrawing(true); });
    newDrawingNextFrame = mk("newDrawingNextFrame", tr("Next Frame with New Drawing"), QKeySequence("Ctrl+Alt+N"));
    connect(newDrawingNextFrame, &QAction::triggered, this, [this] { m_s->newDrawingOnNextFrame(false); });

    copyFrames = mk("copyFrames", tr("Copy Frames"), QKeySequence::Copy);
    connect(copyFrames, &QAction::triggered, this, [this] { m_s->copyFrames(false); });
    cutFrames = mk("cutFrames", tr("Cut Frames"), QKeySequence::Cut);
    connect(cutFrames, &QAction::triggered, this, [this] { m_s->copyFrames(true); });
    pasteFrames = mk("pasteFrames", tr("Paste Frames"), QKeySequence::Paste);
    connect(pasteFrames, &QAction::triggered, this, [this] { m_s->pasteFrames(false); });
    pasteLinked = mk("pasteLinked", tr("Paste Frames (Reuse Same Drawings)"), QKeySequence("Ctrl+Shift+V"));
    connect(pasteLinked, &QAction::triggered, this, [this] { m_s->pasteFrames(true); });
    connect(m_s, &EditorSession::clipChanged, this, &AnimationActions::refresh);
    connect(m_s, &EditorSession::frameSelectionChanged, this, &AnimationActions::refresh);

    extend = mk("extend", tr("Extend Exposure by 1 Frame"), QKeySequence("Ctrl+="));
    connect(extend, &QAction::triggered, this, [=, this] {
        const int f = frame();
        if (!m_s->editActiveAnimation(tr("Extend Exposure"), [&](AnimationState& s) { return s.extendExposure(f, 1); }))
            emit m_s->message(tr("There is no exposure at this frame to extend."));
    });
    shorten = mk("shorten", tr("Shorten Exposure by 1 Frame"), QKeySequence("Ctrl+-"));
    connect(shorten, &QAction::triggered, this, [=, this] {
        const int f = frame();
        m_s->editActiveAnimation(tr("Shorten Exposure"), [&](AnimationState& s) { return s.shortenExposure(f, 1); });
    });
    insertBlank = mk("insertBlank", tr("Insert Blank Exposure"), QKeySequence("Ins"));
    connect(insertBlank, &QAction::triggered, this, [=, this] {
        const int f = frame();
        m_s->editActiveAnimation(tr("Insert Blank Exposure"), [&](AnimationState& s) { s.insertFrames(f, 1); s.setRange(f, 1, QUuid()); return true; });
    });
    deleteFrame = mk("deleteFrame", tr("Delete Frame (Close Gap)"), QKeySequence("Ctrl+Del"));
    connect(deleteFrame, &QAction::triggered, this, [=, this] {
        int a, b;
        m_s->selectedFrameRange(&a, &b);
        m_s->editActiveAnimation(b > a ? tr("Delete Frames") : tr("Delete Frame"), [&](AnimationState& s) { s.removeFrames(a, b - a + 1); return true; });
        m_s->clearFrameSelection();
    });
    makeBlank = mk("makeBlank", tr("Make Exposure Blank"));
    connect(makeBlank, &QAction::triggered, this, [=, this] {
        const int f = frame();
        m_s->editActiveAnimation(tr("Make Exposure Blank"), [&](AnimationState& s) {
            const Exposure* e = s.at(f);
            if (!e) return false;
            const Exposure c = *e;
            s.setRange(c.start, c.length, QUuid());
            return true;
        });
    });
    duplicateExposure = mk("duplicateExposure", tr("Repeat Exposure"));
    connect(duplicateExposure, &QAction::triggered, this, [=, this] {
        const int f = frame();
        m_s->editActiveAnimation(tr("Repeat Exposure"), [&](AnimationState& s) { return s.duplicateExposure(f); });
    });
    loopExposures = mk("loopExposures", tr("Loop Frames…"));
    connect(loopExposures, &QAction::triggered, this, [this] {
        Document* d = m_s->doc();
        if (!d || !m_s->activeAnimationLayer()) return;
        QDialog dlg(m_parent);
        dlg.setWindowTitle(tr("Loop Frames"));
        auto* form = new QFormLayout(&dlg);
        auto* from = new QSpinBox, *to = new QSpinBox, *times = new QSpinBox;
        from->setRange(1, d->durationFrames()); to->setRange(1, d->durationFrames());
        from->setValue(d->rangeStart() + 1); to->setValue(d->rangeEnd() + 1);
        if (const Exposure* e = m_s->activeAnimationLayer()->anim.at(d->currentFrame()); e && !d->comp().workEnabled) {
            from->setValue(e->start + 1); to->setValue(e->end());
        }
        times->setRange(1, 1000); times->setValue(1);
        form->addRow(tr("From frame"), from); form->addRow(tr("To frame"), to);
        form->addRow(tr("Extra repeats"), times);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        const int a = from->value() - 1, b = std::max(a, to->value() - 1), n = times->value();
        m_s->editActiveAnimation(tr("Loop Frames"), [&](AnimationState& s) { return s.loopRange(a, b, n); });
    });

    markKey = mk("markKey", tr("Mark as Key Pose"));
    connect(markKey, &QAction::triggered, this, [this] { setMarker(PoseMarker::Key, tr("Mark Key Pose")); });
    markBreakdown = mk("markBreakdown", tr("Mark as Breakdown"));
    connect(markBreakdown, &QAction::triggered, this, [this] { setMarker(PoseMarker::Breakdown, tr("Mark Breakdown")); });
    markInbetween = mk("markInbetween", tr("Mark as In-between"));
    connect(markInbetween, &QAction::triggered, this, [this] { setMarker(PoseMarker::Inbetween, tr("Mark In-between")); });
    markClear = mk("markClear", tr("Clear Pose Marker"));
    connect(markClear, &QAction::triggered, this, [this] { setMarker(PoseMarker::None, tr("Clear Pose Marker")); });

    prevDrawing = mk("prevDrawing", tr("Previous Drawing"), QKeySequence("Alt+,"));
    connect(prevDrawing, &QAction::triggered, this, [this] { m_s->gotoDrawing(-1); });
    nextDrawing = mk("nextDrawing", tr("Next Drawing"), QKeySequence("Alt+."));
    connect(nextDrawing, &QAction::triggered, this, [this] { m_s->gotoDrawing(+1); });
    firstFrame = mk("firstFrame", tr("First Frame"), QKeySequence("Home"));
    connect(firstFrame, &QAction::triggered, this, [this] { if (m_s->doc()) goToFrame(m_s->doc()->rangeStart()); });
    prevFrame = mk("prevFrame", tr("Previous Frame"), QKeySequence(","));
    connect(prevFrame, &QAction::triggered, this, [this] { if (m_s->doc()) goToFrame(m_s->doc()->currentFrame() - 1); });
    nextFrame = mk("nextFrame", tr("Next Frame"), QKeySequence("."));
    connect(nextFrame, &QAction::triggered, this, [this] { if (m_s->doc()) goToFrame(m_s->doc()->currentFrame() + 1); });
    lastFrame = mk("lastFrame", tr("Last Frame"), QKeySequence("End"));
    connect(lastFrame, &QAction::triggered, this, [this] { if (m_s->doc()) goToFrame(m_s->doc()->rangeEnd()); });
    playPause = mk("playPause", tr("Play / Pause"), QKeySequence("Shift+Return"));
    connect(playPause, &QAction::triggered, this, [this] { m_s->playback()->toggle(); });
    stopPlayback = mk("stopPlayback", tr("Stop"), QKeySequence("Alt+Return"));
    connect(stopPlayback, &QAction::triggered, this, [this] { m_s->playback()->stop(); });
    loopPlayback = mk("loopPlayback", tr("Loop Playback"));
    loopPlayback->setCheckable(true);
    loopPlayback->setChecked(m_s->playback()->loop());
    connect(loopPlayback, &QAction::triggered, this, [this](bool on) { m_s->playback()->setLoop(on); });
    connect(m_s->playback(), &Playback::loopChanged, this, [this](bool on) { loopPlayback->setChecked(on); });
    onionToggle = mk("onionToggle", tr("Onion Skin"), QKeySequence("Alt+O"));
    onionToggle->setCheckable(true);
    onionToggle->setChecked(m_s->onion().enabled);
    connect(onionToggle, &QAction::triggered, this, [this](bool on) { auto o = m_s->onion(); o.enabled = on; m_s->setOnion(o); });
    connect(m_s, &EditorSession::onionChanged, this, [this] { onionToggle->setChecked(m_s->onion().enabled); });

    workIn = mk("workIn", tr("Set Work Area Start"), QKeySequence("Alt+["));
    connect(workIn, &QAction::triggered, this, [this] {
        Document* d = m_s->doc(); if (!d) return;
        const int f = d->currentFrame();
        d->setWorkArea(true, f, std::max(f, d->comp().workEnabled ? d->comp().workOut : d->durationFrames() - 1));
    });
    workOut = mk("workOut", tr("Set Work Area End"), QKeySequence("Alt+]"));
    connect(workOut, &QAction::triggered, this, [this] {
        Document* d = m_s->doc(); if (!d) return;
        const int f = d->currentFrame();
        d->setWorkArea(true, std::min(f, d->comp().workEnabled ? d->comp().workIn : 0), f);
    });
    workClear = mk("workClear", tr("Clear Work Area"));
    connect(workClear, &QAction::triggered, this, [this] {
        if (Document* d = m_s->doc()) d->setWorkArea(false, d->comp().workIn, d->comp().workOut);
    });

    connect(m_s, &EditorSession::documentChanged, this, [this] {
        if (m_s->doc()) {
            connect(m_s->doc(), &Document::frameChanged, this, &AnimationActions::refresh);
            connect(m_s->doc(), &Document::animationChanged, this, &AnimationActions::refresh);
            connect(m_s->doc(), &Document::layerTreeChanged, this, &AnimationActions::refresh);
        }
        refresh();
    });
    connect(m_s, &EditorSession::activeLayerChanged, this, &AnimationActions::refresh);
    connect(m_s->playback(), &Playback::playingChanged, this, [this](bool p) {
        playPause->setText(p ? tr("Pause") : tr("Play"));
    });
    // Short labels for compact toolbars (menus keep the full text).
    const QHash<QAction*, QString> shortText = {
        {newDrawing, tr("New")}, {duplicateDrawing, tr("Copy")}, {extend, tr("+1")}, {shorten, tr("−1")},
        {insertBlank, tr("Blank")}, {deleteFrame, tr("Del")}, {duplicateExposure, tr("Repeat")}};
    for (auto it = shortText.begin(); it != shortText.end(); ++it) it.key()->setIconText(it.value());
    refresh();
}

void AnimationActions::refresh() {
    const bool anim = onAnimLayer();
    for (QAction* a : {newDrawing, duplicateDrawing, newDrawingNextFrame, insertBlank, deleteFrame, loopExposures}) a->setEnabled(anim);
    bool exposed = false;
    PoseMarker marker = PoseMarker::None;
    if (const Layer* l = m_s->activeAnimationLayer(); l && m_s->doc()) {
        if (const Exposure* e = l->anim.at(m_s->doc()->currentFrame())) { exposed = true; marker = e->marker; }
    }
    for (QAction* a : {extend, shorten, makeBlank, duplicateExposure, markKey, markBreakdown, markInbetween, markClear})
        a->setEnabled(anim && exposed);
    markKey->setCheckable(true); markBreakdown->setCheckable(true); markInbetween->setCheckable(true);
    markKey->setChecked(exposed && marker == PoseMarker::Key);
    markBreakdown->setChecked(exposed && marker == PoseMarker::Breakdown);
    markInbetween->setChecked(exposed && marker == PoseMarker::Inbetween);
    copyFrames->setEnabled(m_s->activeAnimationLayer() != nullptr);
    cutFrames->setEnabled(anim);
    pasteFrames->setEnabled(anim && !m_s->frameClip().isEmpty());
    pasteLinked->setEnabled(anim && !m_s->frameClip().isEmpty());
    const bool hasDoc = m_s->doc() != nullptr;
    for (QAction* a : {firstFrame, prevFrame, nextFrame, lastFrame, playPause, stopPlayback, workIn, workOut, workClear}) a->setEnabled(hasDoc);
    prevDrawing->setEnabled(anim); nextDrawing->setEnabled(anim);
}

void AnimationActions::fillMenu(QMenu* m) {
    m->addActions({newLayer, newDrawing, duplicateDrawing, newDrawingNextFrame});
    m->addSeparator();
    m->addActions({copyFrames, cutFrames, pasteFrames, pasteLinked});
    m->addSeparator();
    m->addActions({extend, shorten, insertBlank, deleteFrame, makeBlank, duplicateExposure, loopExposures});
    m->addSeparator();
    QMenu* pose = m->addMenu(tr("Pose Marker"));
    pose->addActions({markKey, markBreakdown, markInbetween, markClear});
    addSubstituteMenus(m);
    m->addSeparator();
    m->addActions({prevDrawing, nextDrawing, firstFrame, prevFrame, nextFrame, lastFrame});
    m->addSeparator();
    m->addActions({playPause, stopPlayback, loopPlayback, onionToggle});
    m->addSeparator();
    m->addActions({workIn, workOut, workClear});
}

void AnimationActions::fillContextMenu(QMenu* m) {
    m->addActions({copyFrames, cutFrames, pasteFrames, pasteLinked});
    m->addSeparator();
    m->addActions({newDrawing, duplicateDrawing});
    m->addSeparator();
    m->addActions({extend, shorten, insertBlank, deleteFrame, makeBlank, duplicateExposure, loopExposures});
    m->addSeparator();
    QMenu* pose = m->addMenu(tr("Pose Marker"));
    pose->addActions({markKey, markBreakdown, markInbetween, markClear});
    addSubstituteMenus(m);
}

void AnimationActions::addSubstituteMenus(QMenu* menu) {
    // Lists the layer's drawings when opened, so it is always current.
    auto addMenu = [&](const QString& title, bool wholeExposure) {
        QMenu* sub = menu->addMenu(title);
        connect(sub, &QMenu::aboutToShow, sub, [this, sub, wholeExposure] {
            sub->clear();
            const Layer* l = m_s->activeAnimationLayer();
            if (!l || l->props.locked) { sub->setEnabled(false); return; }
            const QUuid cur = m_s->doc() ? l->anim.drawingAt(m_s->doc()->currentFrame()) : QUuid();
            for (const auto& d : l->anim.drawings) {
                QAction* a = sub->addAction(d.name);
                a->setCheckable(true); a->setChecked(d.id == cur);
                connect(a, &QAction::triggered, sub, [this, id = d.id, wholeExposure] {
                    const int f = m_s->doc()->currentFrame();
                    m_s->editActiveAnimation(wholeExposure ? tr("Substitute Drawing") : tr("Use Drawing on Frame"), [&](AnimationState& s) {
                        if (wholeExposure) return s.replaceDrawing(f, id);
                        s.setRange(f, 1, id);
                        return true;
                    });
                });
            }
            if (sub->isEmpty()) sub->addAction(tr("(no drawings yet)"))->setEnabled(false);
        });
    };
    addMenu(tr("Substitute Drawing (whole exposure)"), true);
    addMenu(tr("Use Existing Drawing on This Frame"), false);
}

}  // namespace mynd
