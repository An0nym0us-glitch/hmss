#include "LevelMeter.h"

#include "../Theme.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

LevelMeter::LevelMeter(Qt::Orientation o, QWidget* parent) : QWidget(parent), orient_(o)
{
    clock_.start();
    setToolTip(tr("Level meter (dBFS). The red LED latches when the signal reaches full scale; click to clear."));
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

QSize LevelMeter::sizeHint() const
{
    return orient_ == Qt::Horizontal ? QSize(220, 10) : QSize(12, 180);
}

QSize LevelMeter::minimumSizeHint() const
{
    return orient_ == Qt::Horizontal ? QSize(60, 8) : QSize(8, 60);
}

double LevelMeter::dbToFrac(double db)
{
    // -60..0 dB, slightly expanded at the top where it matters.
    if (db <= -60.0)
        return 0.0;
    if (db >= 0.0)
        return 1.0;
    const double x = (db + 60.0) / 60.0;
    return std::pow(x, 1.35);
}

void LevelMeter::pushLevel(float linearPeak, bool clipped)
{
    const qint64 now = clock_.elapsed();
    const double dt = (now - lastPush_) / 1000.0;
    lastPush_ = now;
    const double db = linearPeak > 1e-6f ? 20.0 * std::log10(static_cast<double>(linearPeak)) : -100.0;
    // Instant attack, 24 dB/s release (IEC-style PPM ballistics).
    const double fallen = levelDb_ - 24.0 * std::max(0.0, std::min(dt, 0.5));
    const double newLevel = std::max(db, fallen);
    if (db >= holdDb_ || now - holdTime_ > 1500) {
        holdDb_ = std::max(db, newLevel);
        holdTime_ = now;
    }
    if (clipped) {
        clipped_ = true;
        clipTime_ = now;
    } else if (clipped_ && now - clipTime_ > 3000) {
        clipped_ = false;
    }
    if (std::fabs(newLevel - levelDb_) > 0.05 || clipped)
        update();
    levelDb_ = newLevel;
}

void LevelMeter::setActive(bool active)
{
    if (active_ != active) {
        active_ = active;
        if (!active)
            reset();
        update();
    }
}

void LevelMeter::reset()
{
    levelDb_ = holdDb_ = -100.0;
    clipped_ = false;
    update();
}

void LevelMeter::mousePressEvent(QMouseEvent* e)
{
    clipped_ = false;
    holdDb_ = levelDb_;
    update();
    QWidget::mousePressEvent(e);
}

void LevelMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool horiz = orient_ == Qt::Horizontal;
    QRectF r = rect();
    const double led = showClip_ ? (horiz ? r.height() + 4 : r.width() + 4) : 0.0;
    QRectF track = horiz ? QRectF(r.left(), r.top(), r.width() - led, r.height())
                         : QRectF(r.left(), r.top() + led, r.width(), r.height() - led);
    const double radius = std::min(track.width(), track.height()) / 2.0;

    p.setPen(Qt::NoPen);
    p.setBrush(Theme::meterTrack());
    p.drawRoundedRect(track, radius, radius);

    const double frac = active_ ? dbToFrac(levelDb_) : 0.0;
    if (frac > 0.0) {
        QLinearGradient g = horiz ? QLinearGradient(track.topLeft(), track.topRight())
                                  : QLinearGradient(track.bottomLeft(), track.topLeft());
        g.setColorAt(0.0, QColor(0x25, 0xb8, 0x6b));
        g.setColorAt(dbToFrac(-18.0), QColor(0x2e, 0xcc, 0x71));
        g.setColorAt(dbToFrac(-9.0), QColor(0xe6, 0xd2, 0x3a));
        g.setColorAt(dbToFrac(-3.0), QColor(0xff, 0x9f, 0x43));
        g.setColorAt(1.0, QColor(0xff, 0x4d, 0x5e));
        QRectF fill = horiz ? QRectF(track.left(), track.top(), track.width() * frac, track.height())
                            : QRectF(track.left(), track.bottom() - track.height() * frac, track.width(),
                                     track.height() * frac);
        p.setBrush(g);
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(track, radius, radius);
        p.setClipPath(clip);
        p.drawRect(fill);
        // Peak hold marker.
        const double hf = dbToFrac(holdDb_);
        if (hf > 0.01) {
            p.setBrush(QColor(255, 255, 255, 200));
            if (horiz)
                p.drawRect(QRectF(track.left() + track.width() * hf - 1.5, track.top(), 2, track.height()));
            else
                p.drawRect(QRectF(track.left(), track.bottom() - track.height() * hf - 1, track.width(), 2));
        }
        p.restore();
    }
    // Scale ticks at -40, -20, -10, -3 dB (subtle).
    p.setBrush(QColor(255, 255, 255, 28));
    for (double db : {-40.0, -20.0, -10.0, -3.0}) {
        const double f = dbToFrac(db);
        if (horiz)
            p.drawRect(QRectF(track.left() + track.width() * f, track.top(), 1, track.height()));
        else
            p.drawRect(QRectF(track.left(), track.bottom() - track.height() * f, track.width(), 1));
    }

    if (showClip_) {
        QRectF ledRect = horiz ? QRectF(r.right() - led + 4, r.top(), led - 4, r.height())
                               : QRectF(r.left(), r.top(), r.width(), led - 4);
        p.setBrush(clipped_ ? Theme::danger() : QColor(0x3a, 0x1f, 0x24));
        p.drawRoundedRect(ledRect, 2.5, 2.5);
    }
}
