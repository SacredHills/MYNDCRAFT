#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QToolButton>
#include <QWidget>

#include "UI/AnimationActions.h"
#include "UI/EditorSession.h"

namespace mynd {

// Transform and opacity of the active layer. Each property has a stopwatch (turn animation on/off), value
// fields, a keyframe button (add/remove a key at the playhead) and previous/next keyframe arrows. With
// Auto-Key on, editing a value creates a keyframe at the current frame.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    PropertiesPanel(EditorSession* s, AnimationActions* a, QWidget* parent = nullptr);
    // Test access.
    QDoubleSpinBox* field(const QString& prop, int component) const;
    QToolButton* stopwatch(const QString& prop) const { return m_rows.value(prop).watch; }
    QToolButton* keyButton(const QString& prop) const { return m_rows.value(prop).key; }
    QComboBox* instanceMode() const { return m_instMode; }
    QSpinBox* instanceFirstFrame() const { return m_instFirst; }
    QGroupBox* instanceBox() const { return m_instBox; }
    class EffectsPanel* effects() const { return m_effects; }
    QGroupBox* audioBox() const { return m_audioBox; }
    QDoubleSpinBox* audioStart() const { return m_audStart; }
    QDoubleSpinBox* audioTrimIn() const { return m_audIn; }
    QDoubleSpinBox* audioTrimOut() const { return m_audOut; }
    QCheckBox* audioLoop() const { return m_audLoop; }

private:
    struct Row {
        QToolButton *watch = nullptr, *key = nullptr, *prev = nullptr, *next = nullptr;
        QVector<QDoubleSpinBox*> spins;
        QLabel* label = nullptr;
    };
    void refresh();
    void edited(const QString& prop);
    void gotoPropertyKey(const QString& prop, int dir);
    EditorSession* m_s;
    AnimationActions* m_a;
    QLabel* m_layerName;
    class EffectsPanel* m_effects = nullptr;
    QCheckBox *m_auto, *m_link;
    QMap<QString, Row> m_rows;
    QGroupBox* m_instBox = nullptr;
    QLabel* m_instName = nullptr;
    QComboBox* m_instMode = nullptr;
    QSpinBox* m_instFirst = nullptr;
    void instanceEdited();
    void audioEdited();
    QWidget* m_xformBox = nullptr;
    QGroupBox* m_audioBox = nullptr;
    QLabel* m_audioInfo = nullptr;
    QDoubleSpinBox *m_audStart = nullptr, *m_audIn = nullptr, *m_audOut = nullptr;
    QCheckBox* m_audLoop = nullptr;
    bool m_busy = false;
    QElapsedTimer m_lastEdit;
    int m_editCounter = 0;
};

}  // namespace mynd
