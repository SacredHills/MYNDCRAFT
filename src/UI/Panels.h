#pragma once
#include <QCheckBox>
#include <QLabel>
#include <functional>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoView>
#include <QWidget>

#include "UI/EditorSession.h"

namespace mynd {

class LayerTree : public QTreeWidget {
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;
signals:
    void dropRequested(const QUuid& dragged, const QUuid& target, int indicator);
protected:
    void dropEvent(QDropEvent* e) override;
};

class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(EditorSession* s, QWidget* parent = nullptr);
    void moveActive(int delta);   // delta>0 raises the layer (within its parent)
    void deleteActive();
    void duplicateActive();
    void groupActive();
    void ungroupActive();
    LayerTree* tree() const { return m_tree; }
private:
    void rebuild();
    void syncSelection();
    void refreshControls();
    void onItemChanged(QTreeWidgetItem* it, int col);
    void onDrop(const QUuid& dragged, const QUuid& target, int indicator);
    QTreeWidgetItem* makeItem(const Layer& l);
    EditorSession* m_s;
    LayerTree* m_tree;
    QSlider* m_opacity;
    QLabel* m_opacityLabel;
    QComboBox* m_blend;
    bool m_busy = false;
    bool m_inItemEdit = false, m_rebuildQueued = false;
    QList<QMetaObject::Connection> m_docConns;
};

class StylePanel : public QWidget {
    Q_OBJECT
public:
    explicit StylePanel(EditorSession* s, QWidget* parent = nullptr);
private:
    void refresh();
    void pushStyle();
    EditorSession* m_s;
    QToolButton *m_fg, *m_bg, *m_fill, *m_stroke;
    QCheckBox *m_fillOn, *m_strokeOn;
    QDoubleSpinBox* m_width;
    bool m_busy = false;
};

class ToolOptionsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolOptionsPanel(EditorSession* s, QWidget* parent = nullptr);
private:
    struct Row { QLabel* label; QWidget* field; };
    void refresh();
    QWidget* sliderRow(const QString& label, double min, double max, int decimals, const QString& suffix,
                       const std::function<double(const BrushSettings&)>& get,
                       const std::function<void(BrushSettings&, double)>& set, double displayScale);
    EditorSession* m_s;
    QFormLayout* m_form;
    QLabel* m_title;
    QList<QPair<QWidget*, std::function<void()>>> m_refreshers;
    QCheckBox *m_pSize, *m_pOpacity;
    QSpinBox *m_sides, *m_tol;
    QList<QWidget*> m_brushRows, m_hardnessRows;
    QWidget *m_pressureRow, *m_sidesRow, *m_tolRow;
    bool m_busy = false;
};

class HistoryPanel : public QUndoView {
    Q_OBJECT
public:
    explicit HistoryPanel(EditorSession* s, QWidget* parent = nullptr);
};

}  // namespace mynd
