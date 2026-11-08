// Inputs: every detected microphone / recording endpoint with its real Windows name,
// category, connection status and a full channel strip.
#pragma once

#include "PageCommon.h"

#include <map>

class QCheckBox;
class QSlider;
class QPushButton;
class ToggleSwitch;
class LevelMeter;
class QVBoxLayout;

class InputCard : public QFrame {
    Q_OBJECT
public:
    InputCard(EngineController* ctl, const ab::InputView& v, int colorIndex, QWidget* parent = nullptr);
    void update(const ab::InputView& v, bool monitoring, bool voiceProcessingSupported);
    void updateMeter(const MeterFrame& f);
    const std::string& id() const { return id_; }

private:
    template <class F> void edit(F f) { ctl_->updateInput(id_, f); }

    EngineController* ctl_;
    std::string id_;
    int slot_ = -1;
    bool running_ = false;
    QLabel* name_;
    QLabel* icon_;
    QLabel* pill_;
    QLabel* chips_;
    ToggleSwitch* enable_;
    QWidget* controls_;
    LevelMeter* meter_;
    QSlider* gain_;
    QLabel* gainLabel_;
    QSlider* volume_;
    QLabel* volumeLabel_;
    QPushButton* mute_;
    QPushButton* solo_;
    ToggleSwitch* mono_;
    ToggleSwitch* hpf_;
    ToggleSwitch* ns_;
    QSlider* nsStrength_;
    ToggleSwitch* gate_;
    QSlider* gateThreshold_;
    QLabel* gateLabel_;
    ToggleSwitch* voice_;
    QLabel* voiceLabel_;
    QLabel* message_;
    QLabel* btBanner_;
};

class InputsPage : public Page {
    Q_OBJECT
public:
    explicit InputsPage(EngineController* ctl, QWidget* parent = nullptr);
    void setSnapshot(const ab::EngineSnapshot& s) override;
    void updateMeters(const MeterFrame& f) override;

private:
    QVBoxLayout* list_;
    QLabel* empty_;
    std::vector<InputCard*> cards_;
    std::vector<std::string> ids_;
};
