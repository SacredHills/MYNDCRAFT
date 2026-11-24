#pragma once
#include <QAbstractTableModel>
#include <QTableView>
#include <QWidget>

#include "UI/AnimationActions.h"
#include "UI/EditorSession.h"

namespace mynd {

// Frames x animation layers. A cell shows the drawing name where an exposure starts, a vertical bar while
// it is held, and "×" for an explicit blank exposure. Typing a drawing name into a cell exposes that
// drawing on that frame (creating it when the name is new); an empty cell clears the frame.
class ExposureModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ExposureModel(EditorSession* s, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return m_layers.size(); }
    QVariant data(const QModelIndex& i, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    bool setData(const QModelIndex& i, const QVariant& v, int role) override;
    Qt::ItemFlags flags(const QModelIndex& i) const override;
    QUuid layerAt(int column) const { return column >= 0 && column < m_layers.size() ? m_layers[column] : QUuid(); }
    int columnOf(const QUuid& id) const { return m_layers.indexOf(id); }
    void reload();
private:
    EditorSession* m_s;
    QVector<QUuid> m_layers;
};

class ExposureSheetPanel : public QWidget {
    Q_OBJECT
public:
    ExposureSheetPanel(EditorSession* s, AnimationActions* a, QWidget* parent = nullptr);
    QTableView* table() const { return m_table; }
    ExposureModel* model() const { return m_model; }
private:
    void syncSelection();
    EditorSession* m_s;
    AnimationActions* m_a;
    ExposureModel* m_model;
    QTableView* m_table;
    bool m_busy = false;
};

}  // namespace mynd
