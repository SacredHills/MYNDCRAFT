#pragma once
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QLabel>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

#include "UI/AnimationActions.h"
#include "UI/EditorSession.h"

namespace mynd {

// Frame ruler + one row per layer. Animation layers show their exposures (colored by pose marker, labelled
// with the drawing name); every other layer shows a bar over the whole duration. Click or drag on the
// ruler to scrub, click a cell to pick layer and frame, drag the end of an exposure to retime it.
class TimelineView : public QAbstractScrollArea {
    Q_OBJECT
public:
    TimelineView(EditorSession* s, AnimationActions* a, QWidget* parent = nullptr);
    int cellWidth() const { return m_cellW; }
    void setCellWidth(int w);
    // Geometry helpers (also used by tests): view-space position of a frame / row.
    QPoint cellCenter(int frame, int row) const;
    int rowCount() const { return m_rows.size(); }
    int rowOfLayer(const QUuid& id) const;
    QRect exposureRect(int row, int frame) const;       // rect of the exposure covering `frame` (null if none)
    QPoint rulerPoint(int frame) const;
    QSize sizeHint() const override { return {700, 160}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    struct Row { Layer* layer; int depth; };
    static constexpr int kNameW = 170, kRulerH = 24, kRowH = 24;
    void rebuildRows();
    void updateScrollBars();
    int frameAtX(int x) const;                           // frame under a viewport x (may be out of range)
    int xOfFrame(int frame) const { return kNameW + frame * m_cellW - horizontalScrollBar()->value(); }
    int rowAtY(int y) const;
    void ensureFrameVisible(int frame);
    void scrubTo(int x);
    bool hitExposureEdge(const QPoint& p, int* row, int* exposureIndex) const;

    EditorSession* m_s;
    AnimationActions* m_a;
    QVector<Row> m_rows;
    QMetaObject::Connection m_c1, m_c2, m_c3, m_c4, m_c5;
    int m_cellW = 16;
    enum Drag { None, Scrub, Resize, SelectRange } m_drag = None;
    int m_anchor = 0;
    QUuid m_dragLayer;
    // resize state
    QUuid m_resizeLayer; int m_resizeStart = 0, m_resizeEnd = 0, m_resizeNewEnd = 0, m_resizeFrame = 0;
};

class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    TimelinePanel(EditorSession* s, AnimationActions* a, QWidget* parent = nullptr);
    TimelineView* view() const { return m_view; }
private:
    void syncControls();
    QString timecode(int frame) const;
    EditorSession* m_s;
    AnimationActions* m_a;
    TimelineView* m_view;
    QSpinBox *m_frame, *m_duration;
    QLabel* m_time;
    QComboBox *m_fps, *m_speed;
    QSlider* m_zoom;
    bool m_busy = false;
};

}  // namespace mynd
