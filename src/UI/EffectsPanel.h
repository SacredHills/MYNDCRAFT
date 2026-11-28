#pragma once
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QWidget>

#include "Scene/LayerFx.h"
#include "UI/EditorSession.h"

class QVBoxLayout;

namespace mynd {

// Masks, track matte and effects of the active layer. Everything here is a normal undoable edit of Layer::fx.
class EffectsPanel : public QWidget {
    Q_OBJECT
public:
    // `embedded`: no scroll area of its own (it sits inside the Properties panel under the layer's other properties).
    explicit EffectsPanel(EditorSession* s, bool embedded = false, QWidget* parent = nullptr);

    // What the buttons do (also used by tests and menus).
    bool addEffect(EffectType t);
    bool addMask(const Mask& m);
    bool addRectangleMask();
    bool addEllipseMask();
    bool addMaskFromSelectedShapes();       // the selected shapes of a vector layer become masks of that layer
    bool clearMasks();
    bool clearEffects();
    bool setMatte(const QUuid& layer, MatteMode mode);
    void edit(const QString& text, const QString& mergeKey, const std::function<void(LayerFx&)>& fn);

    // Stopwatch of one number (see LayerFx.h for the ids): turn keyframing on at the current frame / off, freezing the value shown.
    bool setNumberAnimated(const QString& propId, bool on);

private:
    struct ValueRow { QString id; QDoubleSpinBox* spin; QToolButton* watch; QToolButton* key; };
    QWidget* valueRow(const QString& label, const QString& propId, double lo, double hi, int decimals, const QString& suffix);
    void updateValues();
    QVector<ValueRow> m_rows;
    void watchDocument();
    QVector<QMetaObject::Connection> m_conns;
    Document* m_watched = nullptr;
    void rebuild();
    void refreshIfChanged();
    EditorSession* m_s;
    QWidget* m_body = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QUuid m_shownLayer;
    LayerFx m_shown;
    bool m_rebuilding = false;
    bool m_embedded = false;
};

}  // namespace mynd
