#pragma once
#include <QWidget>

#include "Scene/LayerFx.h"
#include "UI/EditorSession.h"

class QVBoxLayout;

namespace mynd {

// Masks, track matte and effects of the active layer. Everything here is a normal undoable edit of Layer::fx.
class EffectsPanel : public QWidget {
    Q_OBJECT
public:
    explicit EffectsPanel(EditorSession* s, QWidget* parent = nullptr);

    // What the buttons do (also used by tests and menus).
    bool addEffect(EffectType t);
    bool addMask(const Mask& m);
    bool addRectangleMask();
    bool addEllipseMask();
    bool addMaskFromSelectedShapes();       // the selected shapes of a vector layer become masks of that layer
    bool clearMasks();
    bool setMatte(const QUuid& layer, MatteMode mode);
    void edit(const QString& text, const QString& mergeKey, const std::function<void(LayerFx&)>& fn);

private:
    void rebuild();
    void refreshIfChanged();
    EditorSession* m_s;
    QWidget* m_body = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QUuid m_shownLayer;
    LayerFx m_shown;
    bool m_rebuilding = false;
};

}  // namespace mynd
