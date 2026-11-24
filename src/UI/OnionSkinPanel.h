#pragma once
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

#include "UI/EditorSession.h"

namespace mynd {

// Onion skin settings: how many previous/next drawings, opacity, tint colors, whether held drawings count
// as separate steps, and whether it applies to the active layer only.
class OnionSkinPanel : public QWidget {
    Q_OBJECT
public:
    explicit OnionSkinPanel(EditorSession* s, QWidget* parent = nullptr);
private:
    void load();
    void store();
    EditorSession* m_s;
    QCheckBox *m_enabled, *m_held, *m_active;
    QSpinBox *m_before, *m_after;
    QSlider* m_opacity;
    QPushButton *m_beforeColor, *m_afterColor;
    bool m_busy = false;
};

}  // namespace mynd
