#include "UI/AnimationActions.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
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
    connect(copyFrames, &QAction::triggered, this, [this] { if (m_s->selectedKeys().isEmpty()) m_s->copyFrames(false); else m_s->copyKeys(false); });
    cutFrames = mk("cutFrames", tr("Cut Frames"), QKeySequence::Cut);
    connect(cutFrames, &QAction::triggered, this, [this] { if (m_s->selectedKeys().isEmpty()) m_s->copyFrames(true); else m_s->copyKeys(true); });
    pasteFrames = mk("pasteFrames", tr("Paste Frames"), QKeySequence::Paste);
    connect(pasteFrames, &QAction::triggered, this, [this] { if (m_s->keyClipActive()) m_s->pasteKeys(); else m_s->pasteFrames(false); });
    pasteLinked = mk("pasteLinked", tr("Paste Frames (Reuse Same Drawings)"), QKeySequence("Ctrl+Shift+V"));
    connect(pasteLinked, &QAction::triggered, this, [this] { m_s->pasteFrames(true); });
    connect(m_s, &EditorSession::clipChanged, this, &AnimationActions::refresh);
    connect(m_s, &EditorSession::keySelectionChanged, this, &AnimationActions::refresh);
    connect(m_s, &EditorSession::keyClipChanged, this, &AnimationActions::refresh);

    autoKey = mk("autoKey", tr("Auto-Key"), QKeySequence("Ctrl+Shift+K"));
    autoKey->setCheckable(true);
    connect(autoKey, &QAction::toggled, this, [this](bool on) { m_s->setAutoKey(on); });
    connect(m_s, &EditorSession::autoKeyChanged, this, [this] { QSignalBlocker b(autoKey); autoKey->setChecked(m_s->autoKey()); });
    prevKey = mk("prevKey", tr("Previous Keyframe"), QKeySequence("Ctrl+,"));
    connect(prevKey, &QAction::triggered, this, [this] { m_s->gotoKey(-1); });
    nextKey = mk("nextKey", tr("Next Keyframe"), QKeySequence("Ctrl+."));
    connect(nextKey, &QAction::triggered, this, [this] { m_s->gotoKey(+1); });
    deleteKeys = mk("deleteKeys", tr("Delete Keyframes"));
    connect(deleteKeys, &QAction::triggered, this, [this] { m_s->deleteSelectedKeys(); });
    auto ease = [this](QAction*& a, const char* name, const QString& text, EasePreset p, const QKeySequence& k) {
        a = mk(name, text, k);
        connect(a, &QAction::triggered, this, [this, p] { m_s->easeSelectedKeys(p); });
    };
    ease(keyLinear, "keyLinear", tr("Linear"), EasePreset::Linear, {});
    ease(keyHold, "keyHold", tr("Hold"), EasePreset::Hold, {});
    ease(keyEaseIn, "keyEaseIn", tr("Easy Ease In"), EasePreset::EaseIn, QKeySequence("Shift+F9"));
    ease(keyEaseOut, "keyEaseOut", tr("Easy Ease Out"), EasePreset::EaseOut, QKeySequence("Ctrl+Shift+F9"));
    ease(keyEaseBoth, "keyEaseBoth", tr("Easy Ease"), EasePreset::EaseInOut, QKeySequence("F9"));
    connect(m_s, &EditorSession::frameSelectionChanged, this, &AnimationActions::refresh);

    createTweenAction = mk("createTween", tr("Create Tween…"));
    connect(createTweenAction, &QAction::triggered, this, [this] {
        const Layer* l = m_s->activeLayer();
        Document* d = m_s->doc();
        if (!l || !d) return;
        int a = d->currentFrame(), b = std::min(d->durationFrames() - 1, a + 24);
        if (m_s->hasFrameSelection()) { int x, y; m_s->selectedFrameRange(&x, &y); if (y > x) { a = x; b = y; } }
        QDialog dlg(m_parent);
        dlg.setWindowTitle(tr("Create Tween"));
        auto* f = new QFormLayout(&dlg);
        auto* kind = new QComboBox; kind->addItem(tr("Motion Tween (Easy Ease)")); kind->addItem(tr("Classic Tween (ease slider)"));
        auto* from = new QSpinBox; from->setRange(1, d->durationFrames()); from->setValue(a + 1);
        auto* to = new QSpinBox; to->setRange(1, d->durationFrames()); to->setValue(b + 1);
        auto* ease = new QSpinBox; ease->setRange(-100, 100); ease->setValue(0);
        ease->setToolTip(tr("Classic tween: -100 starts slowly (ease in), +100 ends slowly (ease out)"));
        auto* rot = new QComboBox; rot->addItems({tr("None"), tr("Clockwise"), tr("Counter-clockwise")});
        auto* spins = new QSpinBox; spins->setRange(0, 50); spins->setValue(1);
        f->addRow(tr("Type"), kind); f->addRow(tr("From frame"), from); f->addRow(tr("To frame"), to);
        f->addRow(tr("Ease (classic)"), ease); f->addRow(tr("Rotate"), rot); f->addRow(tr("Full turns"), spins);
        auto* hint = new QLabel(tr("Adds keyframes for Position, Scale, Rotation and Opacity at both ends.\nTurn on Auto-Key and change the layer at the end frame to define the motion."));
        hint->setWordWrap(true);
        f->addRow(hint);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        f->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        EditorSession::TweenOptions o;
        o.kind = kind->currentIndex() == 0 ? EditorSession::TweenOptions::Motion : EditorSession::TweenOptions::Classic;
        o.ease = ease->value();
        o.rotateDir = rot->currentIndex() == 0 ? 0 : rot->currentIndex() == 1 ? 1 : -1;
        o.spins = spins->value();
        if (!m_s->createTween(l->id, from->value() - 1, to->value() - 1, o)) emit m_s->message(tr("Choose a start frame before the end frame, inside the composition."));
    });
    createShapeTweenAction = mk("createShapeTween", tr("Create Shape Tween…"));
    connect(createShapeTweenAction, &QAction::triggered, this, [this] {
        const Layer* l = m_s->activeLayer();
        Document* d = m_s->doc();
        if (!l || !d) return;
        if (l->type != LayerType::Vector || l->shapes.isEmpty()) { emit m_s->message(tr("Shape tweens need a vector layer with shapes on it.")); return; }
        if (l->hasShapeTween()) { emit m_s->message(tr("This layer already has a shape tween.")); return; }
        int a = d->currentFrame(), b = std::min(d->durationFrames() - 1, a + 24);
        QDialog dlg(m_parent);
        dlg.setWindowTitle(tr("Create Shape Tween"));
        auto* f = new QFormLayout(&dlg);
        auto* from = new QSpinBox; from->setRange(1, d->durationFrames()); from->setValue(a + 1);
        auto* to = new QSpinBox; to->setRange(1, d->durationFrames()); to->setValue(b + 1);
        auto* easy = new QCheckBox(tr("Easy Ease"));
        easy->setChecked(true);
        f->addRow(tr("From frame"), from); f->addRow(tr("To frame"), to); f->addRow(easy);
        auto* hint = new QLabel(tr("The current shapes are the START. A copy becomes the END shape and opens for editing: reshape it with the\ntools, then choose Edit End Shape again to return. Points are paired in order."));
        hint->setWordWrap(true);
        f->addRow(hint);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        f->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        if (!m_s->createShapeTween(l->id, from->value() - 1, to->value() - 1, easy->isChecked()))
            emit m_s->message(tr("Choose a start frame before the end frame, inside the composition."));
    });
    editEndShapeAction = mk("editEndShape", tr("Edit End Shape"));
    editEndShapeAction->setCheckable(true);
    connect(editEndShapeAction, &QAction::triggered, this, [this] {
        if (const Layer* l = m_s->activeLayer()) m_s->toggleEditEndShape(l->id);
    });
    addShapeKeyAction = mk("addShapeKey", tr("Add Shape Key at Current Frame"));
    connect(addShapeKeyAction, &QAction::triggered, this, [this] {
        const Layer* l = m_s->activeLayer();
        if (l && m_s->doc() && !m_s->addShapePose(l->id, m_s->doc()->currentFrame()))
            emit m_s->message(tr("Pick a frame without a shape-morph key, on a layer that has a shape tween."));
    });
    removeShapeTweenAction = mk("removeShapeTween", tr("Remove Shape Tween"));
    connect(removeShapeTweenAction, &QAction::triggered, this, [this] {
        if (const Layer* l = m_s->activeLayer()) m_s->removeShapeTween(l->id);
    });
    removeTweenAction = mk("removeTween", tr("Remove Tween at Current Frame"));
    connect(removeTweenAction, &QAction::triggered, this, [this] {
        const Layer* l = m_s->activeLayer();
        if (!l || !m_s->doc()) return;
        // the span around the playhead: from the nearest transform key at/before it to the nearest one after it
        const int cur = m_s->doc()->currentFrame();
        int lo = -1, hi = -1;
        for (const char* id : {"transform.position", "transform.scale", "transform.rotation", "opacity"}) {
            auto it = l->tracks.constFind(id);
            if (it == l->tracks.constEnd()) continue;
            for (const Keyframe& k : it->keys) {
                if (k.frame <= cur) lo = std::max(lo, k.frame);
                else if (hi < 0 || k.frame < hi) hi = k.frame;
            }
        }
        if (lo < 0 || hi < 0) { emit m_s->message(tr("No tween keyframes around this frame.")); return; }
        if (!m_s->removeTween(l->id, lo, hi)) emit m_s->message(tr("No tween keyframes around this frame."));
    });

    extend = mk("extend", tr("Extend Exposure by 1 Frame"), QKeySequence("Ctrl+="));
    connect(extend, &QAction::triggered, this, [=, this] {
        const int f = frame();
        if (!m_s->editActiveAnimation(tr("Extend Exposure"), [&](AnimationState& s) { return s.extendExposure(f, 1); }))
            emit m_s->message(tr("There is no exposure at this frame to extend."));
    });
    holdDrawing = mk("holdDrawing", tr("Extend Here"));
    connect(holdDrawing, &QAction::triggered, this, [=, this] {
        const int f = frame();
        const int total = m_s->doc() ? m_s->doc()->durationFrames() : 0;
        const bool ok = m_s->editActiveAnimation(tr("Extend Here"), [&](AnimationState& s) {
            int start = -1;
            QUuid dr;
            if (const Exposure* e = s.at(f); e && !e->drawing.isNull()) { start = e->start; dr = e->drawing; }
            else if (int ps = s.neighborStart(f, -1); ps >= 0) { if (const Exposure* pe = s.at(ps)) { start = pe->start; dr = pe->drawing; } }
            if (start < 0 || dr.isNull()) return false;
            int stop = total;
            for (const Exposure& e : s.exposures)
                if (e.start > start && !e.drawing.isNull()) { stop = std::min(stop, e.start); break; }
            if (stop <= start) return false;
            const Exposure* cur = s.at(start);
            const PoseMarker mk2 = cur ? cur->marker : PoseMarker::None;
            s.setRange(start, stop - start, dr, mk2);
            return true;
        });
        if (!ok) emit m_s->message(tr("There is no drawing to hold here."));
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
            connect(m_s->doc(), &Document::layerPropsChanged, this, &AnimationActions::refresh);
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
    {
        const Layer* vl = m_s->activeLayer();
        const bool vec = vl && vl->type == LayerType::Vector && !vl->props.locked;
        createShapeTweenAction->setEnabled(vec && !vl->shapes.isEmpty() && !vl->hasShapeTween());
        editEndShapeAction->setEnabled(vec && vl->hasShapeTween());
        editEndShapeAction->setChecked(vl && vl->editingEnd());
        removeShapeTweenAction->setEnabled(vec && vl->hasShapeTween());
        addShapeKeyAction->setEnabled(vec && vl->hasShapeTween());
    }
    const bool anim = onAnimLayer();
    for (QAction* a : {newDrawing, duplicateDrawing, newDrawingNextFrame, insertBlank, deleteFrame, loopExposures, holdDrawing}) a->setEnabled(anim);
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
    const bool keysSel = !m_s->selectedKeys().isEmpty();
    copyFrames->setEnabled(keysSel || m_s->activeAnimationLayer() != nullptr);
    cutFrames->setEnabled(keysSel || anim);
    pasteFrames->setEnabled(m_s->keyClipActive() ? m_s->activeLayer() != nullptr : (anim && !m_s->frameClip().isEmpty()));
    pasteLinked->setEnabled(!m_s->keyClipActive() && anim && !m_s->frameClip().isEmpty());
    for (QAction* a : {deleteKeys, keyLinear, keyHold, keyEaseIn, keyEaseOut, keyEaseBoth}) a->setEnabled(keysSel);
    prevKey->setEnabled(m_s->activeLayer() != nullptr); nextKey->setEnabled(m_s->activeLayer() != nullptr);
    autoKey->setEnabled(m_s->doc() != nullptr);
    const bool hasDoc = m_s->doc() != nullptr;
    for (QAction* a : {firstFrame, prevFrame, nextFrame, lastFrame, playPause, stopPlayback, workIn, workOut, workClear}) a->setEnabled(hasDoc);
    prevDrawing->setEnabled(anim); nextDrawing->setEnabled(anim);
}

void AnimationActions::fillMenu(QMenu* m) {
    m->addActions({newLayer, newDrawing, duplicateDrawing, newDrawingNextFrame});
    m->addSeparator();
    m->addActions({copyFrames, cutFrames, pasteFrames, pasteLinked});
    m->addSeparator();
    m->addActions({extend, holdDrawing, shorten, insertBlank, deleteFrame, makeBlank, duplicateExposure, loopExposures});
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
    m->addSeparator();
    m->addActions({autoKey, prevKey, nextKey});
    QMenu* interp = m->addMenu(tr("Keyframe Interpolation"));
    interp->addActions({keyLinear, keyHold, keyEaseIn, keyEaseOut, keyEaseBoth});
    m->addAction(deleteKeys);
    m->addSeparator();
    m->addActions({createTweenAction, removeTweenAction, createShapeTweenAction, addShapeKeyAction, editEndShapeAction});
    QMenu* poseMenu = m->addMenu(tr("Edit Shape"));
    connect(poseMenu, &QMenu::aboutToShow, poseMenu, [this, poseMenu] {
        poseMenu->clear();
        const Layer* l = m_s->activeLayer();
        if (!l || !l->hasShapeTween()) { poseMenu->addAction(tr("(no shape tween on this layer)"))->setEnabled(false); return; }
        for (int k = 0; k < l->poseCount(); ++k) {
            QAction* a = poseMenu->addAction(k == 0 ? tr("Shape 1 (start; stop editing)") : tr("Shape %1").arg(k + 1));
            a->setCheckable(true); a->setChecked(k == l->editingPose);
            connect(a, &QAction::triggered, this, [this, k] { if (const Layer* ly = m_s->activeLayer()) m_s->editShapePose(ly->id, k); });
        }
    });
    m->addAction(removeShapeTweenAction);
}

void AnimationActions::fillLayerKeyMenu(QMenu* m, const QUuid& layer) {
    refresh();
    const Layer* l = m_s->doc() ? m_s->doc()->findLayer(layer) : nullptr;
    const bool ok = l && !l->props.locked;
    QAction* all = m->addAction(tr("Add Keyframes Here"));
    all->setEnabled(ok);
    connect(all, &QAction::triggered, m, [this, layer] { m_s->addKeysHere(layer); });
    QMenu* one = m->addMenu(tr("Add Keyframe For"));
    one->setEnabled(ok);
    for (const PropertyDef& d : layerPropertyDefs()) {
        if (d.id == "shape.morph" && !(l && l->hasShapeTween())) continue;
        const QString id = d.id;
        connect(one->addAction(d.label), &QAction::triggered, m, [this, layer, id] { m_s->addKeysHere(layer, id); });
    }
    QAction* paste = m->addAction(tr("Paste Keyframes at Current Frame"));
    paste->setEnabled(ok && !m_s->keyClip().isEmpty());
    connect(paste, &QAction::triggered, m, [this] { m_s->pasteKeys(); });
    m->addSeparator();
    m->addActions({prevKey, nextKey});
    m->addAction(deleteKeys);
}

void AnimationActions::fillKeyframeMenu(QMenu* m) {
    refresh();
    connect(m->addAction(tr("Copy Keyframes")), &QAction::triggered, m, [this] { m_s->copyKeys(false); });
    connect(m->addAction(tr("Cut Keyframes")), &QAction::triggered, m, [this] { m_s->copyKeys(true); });
    QAction* paste = m->addAction(tr("Paste Keyframes at Current Frame"));
    paste->setEnabled(!m_s->keyClip().isEmpty() && m_s->activeLayer());
    connect(paste, &QAction::triggered, m, [this] { m_s->pasteKeys(); });
    m->addSeparator();
    m->addActions({keyEaseBoth, keyEaseIn, keyEaseOut});
    m->addSeparator();
    m->addActions({keyLinear, keyHold});
    m->addSeparator();
    m->addAction(deleteKeys);
}

void AnimationActions::fillContextMenu(QMenu* m) {
    m->addActions({copyFrames, cutFrames, pasteFrames, pasteLinked});
    m->addSeparator();
    m->addActions({newDrawing, duplicateDrawing});
    m->addSeparator();
    m->addActions({extend, holdDrawing, shorten, insertBlank, deleteFrame, makeBlank, duplicateExposure, loopExposures});
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
