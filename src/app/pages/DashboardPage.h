#pragma once

#include "PageCommon.h"

#include <QElapsedTimer>

class QPushButton;
class QGridLayout;
class QSlider;
class LevelMeter;
class RoutingDiagramWidget;

class DashboardPage : public Page {
    Q_OBJECT
public:
    explicit DashboardPage(EngineController* ctl, QWidget* parent = nullptr);
    void setSnapshot(const ab::EngineSnapshot& s) override;
    void updateMeters(const MeterFrame& f) override;
    void setBanner(const QString& text, bool danger);

private:
    QLabel* makeStat(QGridLayout* grid, int col, const QString& label, QLabel** sub);

    QPushButton* startStop_;
    QPushButton* pause_;
    QLabel* state_;
    QLabel* banner_;
    QLabel* hint_;
    QLabel* inputsValue_;
    QLabel* inputsSub_;
    QLabel* outputsValue_;
    QLabel* outputsSub_;
    QLabel* routesValue_;
    QLabel* routesSub_;
    QLabel* latencyValue_;
    QLabel* latencySub_;
    QLabel* cpuValue_;
    QLabel* cpuSub_;
    RoutingDiagramWidget* diagram_;
    QSlider* master_;
    QLabel* masterLabel_;
    QPushButton* muteAll_;
    LevelMeter* activity_;
    SnapshotPtr snap_;
    QElapsedTimer cpuClock_;
    double lastCpuSeconds_ = 0.0;
    int frame_ = 0;
};
