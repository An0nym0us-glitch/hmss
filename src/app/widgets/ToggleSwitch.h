// Fluent-style on/off switch with an animated knob.
#pragma once

#include <QAbstractButton>
#include <QPropertyAnimation>

class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal knob READ knob WRITE setKnob)
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(42, 22); }
    qreal knob() const { return knob_; }
    void setKnob(qreal k) { knob_ = k; update(); }
    // Sets the state without emitting toggled() and without animation.
    void setCheckedSilently(bool on);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEvent*) override { hover_ = true; update(); }
    void leaveEvent(QEvent*) override { hover_ = false; update(); }

private:
    qreal knob_ = 0.0;
    bool hover_ = false;
    QPropertyAnimation anim_;
};
