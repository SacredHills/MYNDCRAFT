#pragma once
#include <QImage>
#include <QWidget>
#include <memory>

#include "UI/EditorSession.h"
#include "UI/Tools.h"

namespace mynd {

// Interactive document view: zoom / pan / rotate, grid, rulers, guides, snapping, tool routing.
class CanvasView : public QWidget {
    Q_OBJECT
public:
    explicit CanvasView(EditorSession* session, QWidget* parent = nullptr);
    ~CanvasView() override;

    void setDocument(Document* d);
    Document* doc() const { return m_doc; }
    EditorSession* session() const { return m_session; }

    // View transform
    double zoom() const { return m_zoom; }
    double rotation() const { return m_rotation; }
    QTransform docToView() const;
    QPointF viewToDoc(const QPointF& v) const;
    void zoomAt(double factor, const QPointF& viewAnchor);
    void setZoom(double z);
    void rotateBy(double degrees);
    void resetRotation();
    void panBy(const QPointF& viewDelta);
    void fitToView();
    void actualPixels();

    QPointF snap(const QPointF& docPt) const;
    QColor sampleColor(const QPoint& docPt) const;
    QPointF hoverDocPos() const { return m_hover; }
    bool hasHover() const { return m_hasHover; }
    Tool* currentTool() const { return m_tool.get(); }
    // Rendered composite at document resolution (cache). Used by tests and the eyedropper.
    QImage compositeImage() const { return m_cache; }
    void rebuildCache();

signals:
    void viewChanged();
    void cursorMoved(const QPointF& docPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void tabletEvent(QTabletEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void pointer(QEvent::Type type, const QPointF& viewPos, Qt::MouseButton button,
                 Qt::KeyboardModifiers mods, double pressure, double xt, double yt, bool eraserEnd);
    void onContentChanged(const QRect& r);
    void switchTool(ToolId id);
    void updateCursor();
    QRectF rulerRectTop() const;
    QRectF rulerRectLeft() const;
    void drawRulers(QPainter& p);
    void drawGrid(QPainter& p, const QTransform& t);
    void drawGuides(QPainter& p, const QTransform& t);

    EditorSession* m_session;
    Document* m_doc = nullptr;
    std::unique_ptr<Tool> m_tool;
    QImage m_cache;
    QMetaObject::Connection m_conn;
    double m_zoom = 1.0, m_rotation = 0.0;
    QPointF m_center;  // document point shown at the widget center
    bool m_autoFit = true;
    bool m_panning = false, m_spaceDown = false;
    QPointF m_lastPanPos;
    QPointF m_hover;
    bool m_hasHover = false;
    bool m_penDown = false;  // a tablet stroke is active (suppresses synthesized mouse events)
    int m_dragGuide = -1;    // index of guide being dragged, -1 none
    bool m_creatingGuide = false;
    Qt::Orientation m_newGuideOrient = Qt::Horizontal;
    static constexpr int kRuler = 20;
};

}  // namespace mynd
