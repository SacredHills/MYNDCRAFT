#pragma once
#include <QComboBox>
#include <QWidget>

#include "Scene/Property.h"
#include "UI/EditorSession.h"

namespace mynd {

// Graph editor: the keyframes of one animated property of the active layer drawn as curves of value (or
// speed) against time. Keys can be selected and dragged in time and value; the Bezier handles of the
// selected keys can be dragged to reshape the easing. Everything goes through undoable track edits, so
// changes show up immediately in the viewport and in playback.
class GraphCanvas : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Value, Speed };
    explicit GraphCanvas(EditorSession* s, QWidget* parent = nullptr);
    void setProperty(const QString& id);
    QString property() const { return m_prop; }
    void setMode(Mode m) { m_mode = m; update(); }
    Mode mode() const { return m_mode; }
    void fit();
    // Widget position of a key (component `comp`) or of one of its handles; for tests.
    QPointF keyPoint(int frame, int comp) const;
    bool handlePoint(int frame, bool outgoing, QPointF* out) const;
    // Adds a keyframe at `frame` holding the curve's current value (the curve does not change shape there).
    bool addKeyAt(int frame);
    bool deleteSelected();
    QSize sizeHint() const override { return {600, 200}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    struct Hit { int key = -1; int comp = 0; int handle = 0; };   // handle: 0 = the key itself, +1 out, -1 in
    const Layer* layer() const;
    const PropertyTrack* track() const;
    QPointF toView(double frame, double value) const { return {m_left + (frame - m_f0) * m_fs, m_bottomY - (value - m_v0) * m_vs}; }
    double frameAt(double x) const { return m_f0 + (x - m_left) / m_fs; }
    double valueAt(double y) const { return m_v0 + (m_bottomY - y) / m_vs; }
    int primaryComp(const Keyframe& a, const Keyframe& b) const;
    bool handleGeometry(int keyIdx, int side, QPointF* viewPt) const;
    Hit hitTest(const QPointF& p) const;
    void applyDrag(const QPointF& p);
    void layoutMetrics();
    EditorSession* m_s;
    QString m_prop;
    Mode m_mode = Mode::Value;
    double m_f0 = 0, m_fs = 10, m_v0 = 0, m_vs = 1;
    int m_left = 52, m_top = 22, m_bottomY = 0;
    // drag state
    Hit m_drag;
    bool m_dragging = false, m_panning = false;
    QPointF m_pressPt, m_panLast;
    PropertyTracks m_orig;
    QString m_mergeKey;
    QVector<KeyRef> m_pendingSel;
    bool m_hasPendingSel = false;
};

class GraphEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit GraphEditorPanel(EditorSession* s, QWidget* parent = nullptr);
    GraphCanvas* canvas() const { return m_canvas; }
    QComboBox* propertyBox() const { return m_props; }

private:
    void rebuildProps();
    EditorSession* m_s;
    GraphCanvas* m_canvas;
    QComboBox* m_props;
    QMetaObject::Connection m_c1, m_c2;
};

}  // namespace mynd
