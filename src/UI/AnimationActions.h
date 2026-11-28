#pragma once
#include <QAction>
#include <QMenu>
#include <QObject>

#include "UI/EditorSession.h"

namespace mynd {

// Every timeline / exposure-sheet / drawing command as a QAction bound to the editor session. One set of
// actions feeds the Animation menu, the timeline and exposure-sheet toolbars and their context menus, so
// all of them behave identically. All edits are undoable.
class AnimationActions : public QObject {
    Q_OBJECT
public:
    AnimationActions(EditorSession* s, QWidget* dialogParent);

    // Layers & drawings
    QAction *newLayer, *newDrawing, *duplicateDrawing, *newDrawingNextFrame;
    // Exposure timing
    QAction *copyFrames, *cutFrames, *pasteFrames, *pasteLinked;
    QAction *extend, *shorten, *insertBlank, *deleteFrame, *makeBlank, *duplicateExposure, *loopExposures, *holdDrawing;
    QAction *markKey, *markBreakdown, *markInbetween, *markClear;
    // Navigation / transport
    QAction *prevDrawing, *nextDrawing, *firstFrame, *prevFrame, *nextFrame, *lastFrame;
    QAction *playPause, *stopPlayback, *loopPlayback, *onionToggle;
    QAction *workIn, *workOut, *workClear;
    // Property keyframes
    QAction *autoKey, *prevKey, *nextKey, *deleteKeys;
    QAction *createTweenAction, *removeTweenAction, *createShapeTweenAction, *addShapeKeyAction, *editEndShapeAction, *removeShapeTweenAction;
    QAction *keyLinear, *keyHold, *keyEaseIn, *keyEaseOut, *keyEaseBoth;

    // Adds every animation command (grouped, with separators) to `menu`.
    void fillMenu(QMenu* menu);
    // Context menu for a timeline cell / exposure: timing commands, markers and drawing substitution.
    void fillContextMenu(QMenu* menu);
    // Context menu for a keyframe in the timeline: copy/paste, interpolation, delete.
    void fillKeyframeMenu(QMenu* menu);
    // Context menu for a layer's own row in the timeline (any layer type): add keyframes at the current frame for everything or for one
    // property, paste, jump between keys, delete the selected keys.
    void fillLayerKeyMenu(QMenu* menu, const QUuid& layer);

    void goToFrame(int frame);
    QAction* actionFor(const QString& name) const { return m_byName.value(name); }

private:
    void refresh();
    void addSubstituteMenus(QMenu* menu);
    bool onAnimLayer() const;
    EditorSession* m_s;
    QWidget* m_parent;
    QHash<QString, QAction*> m_byName;
    QAction* mk(const QString& name, const QString& text, const QKeySequence& key = {});
    void setMarker(PoseMarker m, const QString& text);
};

}  // namespace mynd
