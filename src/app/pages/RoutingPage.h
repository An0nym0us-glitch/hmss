#pragma once

#include "PageCommon.h"

class RoutingMatrixWidget;

class RoutingPage : public Page {
    Q_OBJECT
public:
    explicit RoutingPage(EngineController* ctl, QWidget* parent = nullptr);
    void setSnapshot(const ab::EngineSnapshot& s) override;
    void updateMeters(const MeterFrame& f) override;

private:
    void onToggle(const QString& in, const QString& out, bool enable);
    void onGain(const QString& in, const QString& out, float gain, const QPoint& pos);

    RoutingMatrixWidget* matrix_;
    QLabel* hint_;
    SnapshotPtr snap_;
};
