#include "UI/OnionSkinPanel.h"

#include <QColorDialog>
#include <QFormLayout>
#include <QLabel>

namespace mynd {

static void paintSwatch(QPushButton* b, const QColor& c) {
    b->setText(c.name());
    b->setStyleSheet(QString("QPushButton { background:%1; color:%2; }").arg(c.name(), c.lightness() > 128 ? "#000" : "#fff"));
}

OnionSkinPanel::OnionSkinPanel(EditorSession* s, QWidget* parent) : QWidget(parent), m_s(s) {
    auto* form = new QFormLayout(this);
    m_enabled = new QCheckBox(tr("Show onion skin"));
    form->addRow(m_enabled);
    m_before = new QSpinBox; m_before->setRange(0, 10);
    m_after = new QSpinBox; m_after->setRange(0, 10);
    form->addRow(tr("Previous drawings"), m_before);
    form->addRow(tr("Next drawings"), m_after);
    m_opacity = new QSlider(Qt::Horizontal); m_opacity->setRange(5, 100);
    form->addRow(tr("Opacity"), m_opacity);
    m_beforeColor = new QPushButton; m_afterColor = new QPushButton;
    form->addRow(tr("Previous tint"), m_beforeColor);
    form->addRow(tr("Next tint"), m_afterColor);
    m_held = new QCheckBox(tr("Count every held frame as a step"));
    m_active = new QCheckBox(tr("Active layer only"));
    form->addRow(m_held);
    form->addRow(m_active);
    auto* note = new QLabel(tr("Onion skin is an editing aid: it is not drawn during playback or in exports."));
    note->setWordWrap(true);
    form->addRow(note);

    for (QSpinBox* sb : {m_before, m_after}) connect(sb, &QSpinBox::valueChanged, this, [this] { store(); });
    for (QCheckBox* cb : {m_enabled, m_held, m_active}) connect(cb, &QCheckBox::toggled, this, [this] { store(); });
    connect(m_opacity, &QSlider::valueChanged, this, [this] { store(); });
    auto pick = [this](bool before) {
        Compositor::OnionSkin o = m_s->onion();
        const QColor c = QColorDialog::getColor(before ? o.beforeColor : o.afterColor, this, tr("Onion Skin Tint"));
        if (!c.isValid()) return;
        (before ? o.beforeColor : o.afterColor) = c;
        m_s->setOnion(o);
    };
    connect(m_beforeColor, &QPushButton::clicked, this, [pick] { pick(true); });
    connect(m_afterColor, &QPushButton::clicked, this, [pick] { pick(false); });
    connect(s, &EditorSession::onionChanged, this, &OnionSkinPanel::load);
    load();
}

void OnionSkinPanel::load() {
    m_busy = true;
    const auto& o = m_s->onion();
    m_enabled->setChecked(o.enabled);
    m_before->setValue(o.before); m_after->setValue(o.after);
    m_opacity->setValue(int(std::lround(o.opacity * 100)));
    m_held->setChecked(o.includeHeld); m_active->setChecked(o.activeLayerOnly);
    paintSwatch(m_beforeColor, o.beforeColor); paintSwatch(m_afterColor, o.afterColor);
    m_busy = false;
}

void OnionSkinPanel::store() {
    if (m_busy) return;
    Compositor::OnionSkin o = m_s->onion();
    o.enabled = m_enabled->isChecked();
    o.before = m_before->value(); o.after = m_after->value();
    o.opacity = m_opacity->value() / 100.0;
    o.includeHeld = m_held->isChecked(); o.activeLayerOnly = m_active->isChecked();
    m_s->setOnion(o);
}

}  // namespace mynd
