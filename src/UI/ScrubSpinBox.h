#pragma once
#include <QApplication>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <cmath>

namespace mynd {

// A number field that can also be dragged with the mouse: press on it and drag left/right to change the value.
// Shift = fine (x0.1), Ctrl = coarse (x10). A click without dragging focuses the field for typing as usual.
// The value changes through setValue(), so every step emits valueChanged like typing does; the editors that
// listen merge those into one undo step through their merge keys.
class ScrubSpinBox : public QDoubleSpinBox {
public:
    explicit ScrubSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        lineEdit()->installEventFilter(this);
        setCursor(Qt::SizeHorCursor);
        setToolTip(tr("Drag to change. Shift = fine, Ctrl = coarse. Click to type."));
    }
    // Pixels of drag per singleStep().
    void setPixelsPerStep(double p) { m_pxPerStep = p > 0 ? p : 1; }
    bool scrubbing() const { return m_dragging; }

    // Applies a horizontal drag of `dx` pixels from the value at the start of the drag.
    void scrubBy(double dx, Qt::KeyboardModifiers mods) {
        double mul = 1.0;
        if (mods & Qt::ShiftModifier) mul = 0.1;
        else if (mods & Qt::ControlModifier) mul = 10.0;
        double step = singleStep();
        if (step <= 0) step = std::pow(10.0, -decimals());
        const double v = m_startValue + dx / m_pxPerStep * step * mul;
        const double q = std::pow(10.0, decimals());
        setValue(std::round(v * q) / q);
    }

protected:
    // Mouse events arrive either on the spin box itself (its border) or on the line edit inside it: both do the same.
    void mousePressEvent(QMouseEvent* e) override { if (!press(e)) QDoubleSpinBox::mousePressEvent(e); }
    void mouseMoveEvent(QMouseEvent* e) override { if (!move(e)) QDoubleSpinBox::mouseMoveEvent(e); }
    void mouseReleaseEvent(QMouseEvent* e) override { if (!release(e)) QDoubleSpinBox::mouseReleaseEvent(e); }
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == lineEdit() && isEnabled() && !isReadOnly()) {
            switch (e->type()) {
            case QEvent::MouseButtonPress: if (press(static_cast<QMouseEvent*>(e))) return true; break;
            case QEvent::MouseMove: if (move(static_cast<QMouseEvent*>(e))) return true; break;
            case QEvent::MouseButtonRelease: if (release(static_cast<QMouseEvent*>(e))) return true; break;
            default: break;
            }
        }
        return QDoubleSpinBox::eventFilter(o, e);
    }

private:
    bool press(QMouseEvent* m) {
        if (m->button() != Qt::LeftButton || !isEnabled() || isReadOnly()) return false;
        // On the up/down buttons (when shown) the normal spin box behaviour stays.
        if (buttonSymbols() != NoButtons && !lineEdit()->geometry().contains(mapFromGlobal(m->globalPosition().toPoint()))) return false;
        m_pressX = m->globalPosition().x();
        m_startValue = value();
        m_pressed = true; m_dragging = false;
        m_clickPos = lineEdit()->mapFromGlobal(m->globalPosition().toPoint());
        return true;       // a plain click is replayed on release; a drag never reaches the text editor
    }
    bool move(QMouseEvent* m) {
        if (!m_pressed) return false;
        const double dx = m->globalPosition().x() - m_pressX;
        if (!m_dragging && std::abs(dx) < 3) return true;
        if (!m_dragging) { m_dragging = true; lineEdit()->deselect(); }
        scrubBy(dx, m->modifiers());
        return true;
    }
    bool release(QMouseEvent*) {
        if (!m_pressed) return false;
        m_pressed = false;
        if (!m_dragging) {                               // a click: edit the text, cursor where it was clicked
            const bool had = lineEdit()->hasFocus();
            lineEdit()->setFocus(Qt::MouseFocusReason);
            if (had) lineEdit()->setCursorPosition(lineEdit()->cursorPositionAt(m_clickPos)); else selectAll();
        }
        m_dragging = false;
        return true;
    }
    bool m_pressed = false, m_dragging = false;
    QPoint m_clickPos;
    double m_pressX = 0, m_startValue = 0, m_pxPerStep = 1;
};

}  // namespace mynd
