// Live connection diagram: real input devices on the left, the AudioBridge mixer in
// the middle, real output devices on the right, and one curve per configured route.
// Curves are solid and animated (flowing dots, brightness = live level) when audio
// actually flows, dashed when the route exists but a device is off or disconnected.
#pragma once

#include "../MeterHub.h"
#include "core/AudioEngine.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <vector>

class RoutingDiagramWidget : public QWidget {
    Q_OBJECT
public:
    explicit RoutingDiagramWidget(QWidget* parent = nullptr);
    void setSnapshot(const ab::EngineSnapshot& s);
    void updateLevels(const MeterFrame& f);
    QSize sizeHint() const override { return QSize(760, 320); }
    QSize minimumSizeHint() const override { return QSize(420, 220); }

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    struct Node {
        std::string id;
        QString name;
        bool running = false, present = false, enabled = false;
        int slot = -1;
        int colorIndex = 0;
        float level = 0.0f;
        bool bluetooth = false;
    };
    struct Edge {
        int in = 0, out = 0;
        float level = 0.0f;
    };
    void updateAnimation();

    std::vector<Node> ins_, outs_;
    std::vector<Edge> edges_;
    bool monitoring_ = false;
    bool paused_ = false;
    QTimer anim_;
    QElapsedTimer clock_;
};
