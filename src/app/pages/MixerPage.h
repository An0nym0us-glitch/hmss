// Console-style mixer: one vertical channel strip per enabled microphone and per
// enabled output, plus the master section.
#pragma once

#include "PageCommon.h"

class QSlider;
class QPushButton;
class QHBoxLayout;
class LevelMeter;

class ChannelStrip : public QFrame {
    Q_OBJECT
public:
    enum class Kind { Input, Output, Master };
    ChannelStrip(Kind kind, EngineController* ctl, const std::string& id, const QString& name, const QColor& color,
                 QWidget* parent = nullptr);
    void setState(float volume, float gainDb, bool mute, bool solo, bool running, int slot, const QString& subtitle);
    void updateMeter(const MeterFrame& f);
    const std::string& id() const { return id_; }

private:
    Kind kind_;
    EngineController* ctl_;
    std::string id_;
    int slot_ = -1;
    bool running_ = false;
    QLabel* subtitle_;
    QSlider* gain_ = nullptr;
    QLabel* gainLabel_ = nullptr;
    QSlider* fader_;
    QLabel* faderLabel_;
    LevelMeter* meter_;
    QPushButton* mute_;
    QPushButton* solo_ = nullptr;
};

class MixerPage : public Page {
    Q_OBJECT
public:
    explicit MixerPage(EngineController* ctl, QWidget* parent = nullptr);
    void setSnapshot(const ab::EngineSnapshot& s) override;
    void updateMeters(const MeterFrame& f) override;

private:
    QHBoxLayout* strips_;
    QWidget* stripHost_;
    QLabel* empty_;
    std::vector<ChannelStrip*> all_;
    ChannelStrip* master_ = nullptr;
    std::vector<std::string> layoutKey_;
};
