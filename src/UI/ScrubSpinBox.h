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
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o != lineEdit() || lineEdit()->hasFocus() || !isEnabled() || isReadOnly())
            return QDoubleSpinBox::eventFilter(o, e);
        switch (e->type()) {
        case QEvent::MouseButtonPress: {
            auto* m = static_cast<QMouseEvent*>(e);
            if (m->button() != Qt::LeftButton) break;
            m_pressX = m->globalPosition().x();
            m_startValue = value();
            m_pressed = true; m_dragging = false;
            return true;  // not focused yet: focus (and the text cursor) only on a click without a drag
        }
        case QEvent::MouseMove: {
            if (!m_pressed) break;
            auto* m = static_cast<QMouseEvent*>(e);
            const double dx = m->globalPosition().x() - m_pressX;
            if (!m_dragging && std::abs(dx) < 3) return true;
            m_dragging = true;
            scrubBy(dx, m->modifiers());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_pressed) break;
            m_pressed = false;
            if (!m_dragging) { lineEdit()->setFocus(Qt::MouseFocusReason); selectAll(); }
            m_dragging = false;
            return true;
        }
        default: break;
        }
        return QDoubleSpinBox::eventFilter(o, e);
    }

private:
    bool m_pressed = false, m_dragging = false;
    double m_pressX = 0, m_startValue = 0, m_pxPerStep = 1;
};

}  // namespace mynd
