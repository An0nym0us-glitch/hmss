// Outputs: every playback endpoint with its real Windows name and output controls.
#pragma once

#include "PageCommon.h"

class QSlider;
class QSpinBox;
class QPushButton;
class ToggleSwitch;
class LevelMeter;
class QVBoxLayout;

class OutputCard : public QFrame {
    Q_OBJECT
public:
    OutputCard(EngineController* ctl, const ab::OutputView& v, QWidget* parent = nullptr);
    void update(const ab::OutputView& v, const ab::EngineSnapshot& s);
    void updateMeter(const MeterFrame& f);

private:
    template <class F> void edit(F f) { ctl_->updateOutput(id_, f); }

    EngineController* ctl_;
    std::string id_;
    int slot_ = -1;
    bool running_ = false;
    double deviceLatency_ = 0.0;
    QLabel* name_;
    QLabel* pill_;
    QLabel* chips_;
    ToggleSwitch* enable_;
    QWidget* controls_;
    LevelMeter* meter_;
    QSlider* volume_;
    QLabel* volumeLabel_;
    QPushButton* mute_;
    QSpinBox* delay_;
    QLabel* stats_;
    QLabel* message_;
    QWidget* feedback_;
    QLabel* jack_;
    QLabel* virtualWarn_;
    QWidget* winWarn_;
    QLabel* winWarnText_;
    QPushButton* test_;
    ToggleSwitch* bypass_;
    QLabel* bypassNote_;
};

class OutputsPage : public Page {
    Q_OBJECT
public:
    explicit OutputsPage(EngineController* ctl, QWidget* parent = nullptr);
    void setSnapshot(const ab::EngineSnapshot& s) override;
    void updateMeters(const MeterFrame& f) override;

private:
    QVBoxLayout* list_;
    QLabel* empty_;
    std::vector<OutputCard*> cards_;
    std::vector<std::string> ids_;
};
