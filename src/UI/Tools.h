#pragma once
#include <QCursor>
#include <QKeyEvent>
#include <QPainter>
#include <memory>

#include "Drawing/BrushEngine.h"
#include "UI/EditorSession.h"

namespace mynd {

class CanvasView;

struct PointerEvent {
    QPointF doc;
    QPointF view;
    double pressure = 1.0;
    double xTilt = 0, yTilt = 0;  // degrees, from the tablet (captured; not yet mapped to brush dynamics)
    Qt::KeyboardModifiers mods;
    Qt::MouseButton button = Qt::LeftButton;
    bool eraserEnd = false;       // the stylus eraser end is in use
    bool shift() const { return mods & Qt::ShiftModifier; }
    bool alt() const { return mods & Qt::AltModifier; }
    bool ctrl() const { return mods & Qt::ControlModifier; }
};

class Tool {
public:
    Tool(EditorSession* s, CanvasView* c) : m_s(s), m_c(c) {}
    virtual ~Tool() = default;
    virtual ToolId id() const = 0;
    virtual void press(const PointerEvent&) {}
    virtual void move(const PointerEvent&) {}
    virtual void release(const PointerEvent&) {}
    virtual void doubleClick(const PointerEvent&) {}
    virtual bool keyPress(QKeyEvent*) { return false; }
    virtual void cancel() {}
    virtual void paintOverlay(QPainter&, const QTransform& docToView) {}
    virtual QCursor cursor() const { return Qt::CrossCursor; }
    virtual bool busy() const { return false; }  // a gesture is in progress

protected:
    EditorSession* m_s;
    CanvasView* m_c;
};

std::unique_ptr<Tool> createTool(ToolId id, EditorSession* s, CanvasView* c);

}  // namespace mynd
