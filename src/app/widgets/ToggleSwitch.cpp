#include "ToggleSwitch.h"

#include "../Theme.h"

#include <QPainter>

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent), anim_(this, "knob")
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    anim_.setDuration(140);
    anim_.setEasingCurve(QEasingCurve::OutCubic);
    connect(this, &QAbstractButton::toggled, this, [this](bool on) {
        anim_.stop();
        anim_.setStartValue(knob_);
        anim_.setEndValue(on ? 1.0 : 0.0);
        anim_.start();
    });
}

void ToggleSwitch::setCheckedSilently(bool on)
{
    if (isChecked() == on)
        return;
    const bool b = blockSignals(true);
    setChecked(on);
    blockSignals(b);
    anim_.stop();
    knob_ = on ? 1.0 : 0.0;
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal h = r.height();
    QColor off = hover_ ? QColor(0x3a, 0x41, 0x50) : QColor(0x30, 0x36, 0x43);
    QColor on = Theme::accent();
    if (!isEnabled()) {
        off = QColor(0x25, 0x29, 0x32);
        on = QColor(0x34, 0x4a, 0x72);
    }
    QColor bg;
    bg.setRgbF(off.redF() + (on.redF() - off.redF()) * knob_, off.greenF() + (on.greenF() - off.greenF()) * knob_,
               off.blueF() + (on.blueF() - off.blueF()) * knob_);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, h / 2, h / 2);
    const qreal d = h - 6;
    const qreal x = r.left() + 3 + (r.width() - d - 6) * knob_;
    p.setBrush(isEnabled() ? QColor(0xf4, 0xf6, 0xfa) : QColor(0x8a, 0x90, 0x9c));
    p.drawEllipse(QRectF(x, r.top() + 3, d, d));
}
