// Audio level meter: dBFS scale (-60..0), peak bar with ballistic fall-off, peak-hold
// marker and a clip LED (latched for 3 s, click to clear). Fed with linear peak values
// by the UI meter timer; draws nothing more than needed (cheap at 30 fps).
#pragma once

#include <QElapsedTimer>
#include <QWidget>

class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(Qt::Orientation o = Qt::Horizontal, QWidget* parent = nullptr);

    // linearPeak: max |sample| since the previous call. clipped: full scale reached.
    void pushLevel(float linearPeak, bool clipped);
    void setActive(bool active); // greyed when the channel is not streaming
    void reset();
    bool hasClipLed() const { return showClip_; }
    void setShowClipLed(bool s) { showClip_ = s; update(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    static double dbToFrac(double db);
    Qt::Orientation orient_;
    double levelDb_ = -100.0;
    double holdDb_ = -100.0;
    qint64 holdTime_ = 0;
    qint64 clipTime_ = -100000;
    bool clipped_ = false;
    bool active_ = true;
    bool showClip_ = true;
    QElapsedTimer clock_;
    qint64 lastPush_ = 0;
};
