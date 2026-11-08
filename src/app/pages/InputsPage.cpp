#include "InputsPage.h"

#include "../widgets/LevelMeter.h"
#include "../widgets/ToggleSwitch.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {

QSlider* hslider(int min, int max)
{
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setMinimumWidth(120);
    return s;
}

QWidget* labeled(ToggleSwitch* sw, const QString& text, const QString& tip)
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    h->addWidget(sw);
    auto* l = new QLabel(text);
    h->addWidget(l);
    w->setToolTip(tip);
    return w;
}

void setSliderSilently(QSlider* s, int v)
{
    if (s->isSliderDown())
        return; // never fight the user's drag
    const bool b = s->blockSignals(true);
    s->setValue(v);
    s->blockSignals(b);
}

void setCheckedSilently(QPushButton* b, bool on)
{
    const bool old = b->blockSignals(true);
    b->setChecked(on);
    b->blockSignals(old);
}

} // namespace

InputCard::InputCard(EngineController* ctl, const ab::InputView& v, int colorIndex, QWidget* parent)
    : QFrame(parent), ctl_(ctl), id_(v.info.id)
{
    setObjectName("card");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    // Header: icon, real device name, chips, status, enable switch.
    auto* head = new QHBoxLayout;
    head->setSpacing(12);
    icon_ = new QLabel;
    icon_->setFixedSize(36, 36);
    icon_->setAlignment(Qt::AlignCenter);
    const QColor cc = Theme::channelColor(colorIndex);
    icon_->setStyleSheet(QString("background: rgba(%1,%2,%3,40); border-radius: 10px;").arg(cc.red()).arg(cc.green()).arg(cc.blue()));
    icon_->setPixmap(Theme::iconPixmap(ui::iconFor(v.info), cc, 20));
    head->addWidget(icon_, 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(4);
    name_ = new QLabel;
    name_->setObjectName("deviceName");
    name_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    name_->setWordWrap(true);
    titles->addWidget(name_);
    chips_ = new QLabel;
    chips_->setObjectName("muted");
    chips_->setWordWrap(true);
    titles->addWidget(chips_);
    head->addLayout(titles, 1);
    pill_ = ui::statusPill();
    head->addWidget(pill_, 0, Qt::AlignTop);
    enable_ = new ToggleSwitch;
    enable_->setToolTip(tr("Use this microphone in AudioBridge"));
    head->addWidget(enable_, 0, Qt::AlignTop);
    root->addLayout(head);

    btBanner_ = new QLabel(tr("Bluetooth note: opening a Bluetooth headset microphone switches the earbuds to the "
                              "hands-free profile (HFP), which reduces the earbuds' own playback quality to telephone "
                              "quality while the microphone is in use. Outputs on other devices (AUX jack, USB, another "
                              "Bluetooth speaker) are not affected."));
    btBanner_->setObjectName("bannerInfo");
    btBanner_->setWordWrap(true);
    root->addWidget(btBanner_);
    virtualWarn_ = new QLabel;
    virtualWarn_->setObjectName("banner");
    virtualWarn_->setWordWrap(true);
    root->addWidget(virtualWarn_);
    winWarn_ = ui::windowsVolumeWarning(&winWarnText_);
    root->addWidget(winWarn_);

    // Controls (visible when enabled).
    controls_ = new QWidget;
    auto* grid = new QGridLayout(controls_);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(10);

    meter_ = new LevelMeter(Qt::Horizontal);
    meter_->setMinimumHeight(10);
    grid->addWidget(new QLabel(tr("Input level")), 0, 0);
    grid->addWidget(meter_, 0, 1, 1, 5);

    gain_ = hslider(-240, 360); // tenths of dB
    gain_->setToolTip(tr("Microphone gain (pre-fader), -24 dB to +36 dB."));
    gainLabel_ = new QLabel;
    gainLabel_->setMinimumWidth(64);
    grid->addWidget(new QLabel(tr("Gain")), 1, 0);
    grid->addWidget(gain_, 1, 1);
    grid->addWidget(gainLabel_, 1, 2);

    volume_ = hslider(0, 1000);
    volume_->setToolTip(tr("Channel volume (fader)"));
    volumeLabel_ = new QLabel;
    volumeLabel_->setMinimumWidth(64);
    grid->addWidget(new QLabel(tr("Volume")), 1, 3);
    grid->addWidget(volume_, 1, 4);
    grid->addWidget(volumeLabel_, 1, 5);

    auto* ms = new QHBoxLayout;
    mute_ = new QPushButton("M");
    mute_->setObjectName("mute");
    mute_->setCheckable(true);
    mute_->setToolTip(tr("Mute this microphone"));
    solo_ = new QPushButton("S");
    solo_->setObjectName("solo");
    solo_->setCheckable(true);
    solo_->setToolTip(tr("Solo: while any microphone is soloed, only soloed microphones are heard"));
    ms->addWidget(mute_);
    ms->addWidget(solo_);
    grid->addLayout(ms, 1, 6);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(4, 1);

    // Processing row.
    auto* proc = new QHBoxLayout;
    proc->setSpacing(18);
    mono_ = new ToggleSwitch;
    proc->addWidget(labeled(mono_, tr("Mono"), tr("Sum all capture channels to mono (recommended for microphones)")));
    hpf_ = new ToggleSwitch;
    proc->addWidget(labeled(hpf_, tr("Rumble filter"), tr("80 Hz high-pass filter: removes handling noise, desk thumps and hum")));
    ns_ = new ToggleSwitch;
    proc->addWidget(labeled(ns_, tr("Noise suppression"),
                            tr("Spectral noise suppression for steady background noise (fans, hiss). Adds ~11 ms latency.")));
    nsStrength_ = hslider(0, 100);
    nsStrength_->setMaximumWidth(110);
    nsStrength_->setToolTip(tr("Noise suppression strength"));
    proc->addWidget(nsStrength_);
    gate_ = new ToggleSwitch;
    proc->addWidget(labeled(gate_, tr("Noise gate"), tr("Silences the microphone while the level stays below the threshold")));
    gateThreshold_ = hslider(-80, -10);
    gateThreshold_->setMaximumWidth(110);
    gateLabel_ = new QLabel;
    gateLabel_->setObjectName("dim");
    proc->addWidget(gateThreshold_);
    proc->addWidget(gateLabel_);
    proc->addStretch(1);
    grid->addLayout(proc, 2, 0, 1, 7);

    auto* voiceRow = new QHBoxLayout;
    voiceRow->setSpacing(16);
    voice_ = new ToggleSwitch;
    voiceRow->addWidget(labeled(voice_, tr("Windows voice processing"),
                                tr("Opens the microphone in Windows communications mode so the driver/OS can apply its "
                                   "own echo cancellation, noise suppression and gain control. Availability depends on "
                                   "the device driver; the result is shown below.")));
    bypass_ = new ToggleSwitch;
    voiceRow->addWidget(labeled(bypass_, tr("Bypass Windows audio effects"),
                                tr("Opens the microphone in Windows RAW mode so effects such as Nahimic are not applied. "
                                   "Only works if the driver supports it.")));
    voiceLabel_ = new QLabel;
    voiceLabel_->setObjectName("dim");
    voiceLabel_->setWordWrap(true);
    voiceRow->addWidget(voiceLabel_, 1);
    grid->addLayout(voiceRow, 3, 0, 1, 7);
    root->addWidget(controls_);

    message_ = new QLabel;
    message_->setObjectName("dim");
    message_->setWordWrap(true);
    root->addWidget(message_);

    // Wiring: every control edits the engine-side configuration.
    connect(enable_, &QAbstractButton::toggled, this, [this](bool on) { edit([on](ab::InputConfig& c) { c.enabled = on; }); });
    connect(gain_, &QSlider::valueChanged, this, [this](int v) {
        gainLabel_->setText(Theme::dbText(v / 10.0f));
        edit([v](ab::InputConfig& c) { c.gainDb = v / 10.0f; });
    });
    connect(volume_, &QSlider::valueChanged, this, [this](int v) {
        volumeLabel_->setText(Theme::gainText(Theme::faderToGain(v)));
        edit([v](ab::InputConfig& c) { c.volume = Theme::faderToGain(v); });
    });
    connect(mute_, &QPushButton::toggled, this, [this](bool on) { edit([on](ab::InputConfig& c) { c.mute = on; }); });
    connect(solo_, &QPushButton::toggled, this, [this](bool on) { edit([on](ab::InputConfig& c) { c.solo = on; }); });
    connect(mono_, &QAbstractButton::toggled, this, [this](bool on) { edit([on](ab::InputConfig& c) { c.monoMix = on; }); });
    connect(hpf_, &QAbstractButton::toggled, this, [this](bool on) { edit([on](ab::InputConfig& c) { c.highPass = on; }); });
    connect(ns_, &QAbstractButton::toggled, this, [this](bool on) {
        nsStrength_->setEnabled(on);
        edit([on](ab::InputConfig& c) { c.noiseSuppression = on; });
    });
    connect(nsStrength_, &QSlider::valueChanged, this,
            [this](int v) { edit([v](ab::InputConfig& c) { c.noiseSuppressionStrength = v / 100.0f; }); });
    connect(gate_, &QAbstractButton::toggled, this, [this](bool on) {
        gateThreshold_->setEnabled(on);
        edit([on](ab::InputConfig& c) { c.noiseGate = on; });
    });
    connect(gateThreshold_, &QSlider::valueChanged, this, [this](int v) {
        gateLabel_->setText(Theme::dbText(static_cast<float>(v), 0));
        edit([v](ab::InputConfig& c) { c.gateThresholdDb = static_cast<float>(v); });
    });
    connect(voice_, &QAbstractButton::toggled, this, [this](bool on) { edit([on](ab::InputConfig& c) { c.voiceProcessing = on; }); });
    connect(bypass_, &QAbstractButton::toggled, this, [this](bool on) { edit([on](ab::InputConfig& c) { c.bypassEffects = on; }); });
}

void InputCard::update(const ab::InputView& v, const ab::EngineSnapshot& s)
{
    const bool monitoring = s.monitoring;
    const bool voiceProcessingSupported = s.voiceProcessingSupported;
    const ab::InputConfig& c = v.config;
    slot_ = v.slot;
    running_ = v.runState == ab::RunState::Running;
    name_->setText(QString::fromStdString(v.info.displayName));
    name_->setToolTip(tr("Windows name: %1\nEndpoint ID: %2")
                          .arg(QString::fromStdString(v.info.friendlyName), QString::fromStdString(v.info.id)));
    QStringList parts;
    parts << QString::fromStdString(ab::deviceCategory(v.info));
    parts << QString::fromUtf8(ab::toString(v.info.bus));
    if (v.info.isDefault)
        parts << tr("Windows default");
    if (v.info.isDefaultCommunications && !v.info.isDefault)
        parts << tr("Default communications");
    const QString fmt = ui::formatLine(v);
    if (!fmt.isEmpty())
        parts << fmt;
    chips_->setText(parts.join(QStringLiteral("  ·  ")));
    ui::setStatusPill(pill_, ui::status(v, monitoring, c.enabled));
    enable_->setCheckedSilently(c.enabled);
    enable_->setEnabled(v.present || c.enabled);
    controls_->setVisible(c.enabled);
    btBanner_->setVisible(c.enabled && v.info.bus == ab::DeviceBus::Bluetooth);

    setSliderSilently(gain_, static_cast<int>(std::lround(c.gainDb * 10.0f)));
    gainLabel_->setText(Theme::dbText(c.gainDb));
    setSliderSilently(volume_, Theme::gainToFader(c.volume));
    volumeLabel_->setText(Theme::gainText(c.volume));
    setCheckedSilently(mute_, c.mute);
    setCheckedSilently(solo_, c.solo);
    mono_->setCheckedSilently(c.monoMix);
    hpf_->setCheckedSilently(c.highPass);
    ns_->setCheckedSilently(c.noiseSuppression);
    setSliderSilently(nsStrength_, static_cast<int>(std::lround(c.noiseSuppressionStrength * 100.0f)));
    nsStrength_->setEnabled(c.noiseSuppression);
    gate_->setCheckedSilently(c.noiseGate);
    setSliderSilently(gateThreshold_, static_cast<int>(std::lround(c.gateThresholdDb)));
    gateThreshold_->setEnabled(c.noiseGate);
    gateLabel_->setText(Theme::dbText(c.gateThresholdDb, 0));
    voice_->setCheckedSilently(c.voiceProcessing);
    bypass_->setCheckedSilently(c.bypassEffects);
    const QString virt = ui::virtualDeviceWarning(v.info);
    virtualWarn_->setText(virt);
    virtualWarn_->setVisible(!virt.isEmpty());
    const QString vol = ui::windowsVolumeProblem(v.info);
    winWarnText_->setText(vol);
    winWarn_->setVisible(!vol.isEmpty() && c.enabled);
    voice_->setEnabled(voiceProcessingSupported);
    if (!voiceProcessingSupported)
        voiceLabel_->setText(tr("Not available on this platform."));
    else if ((c.voiceProcessing || c.bypassEffects) && v.streamOpen)
        voiceLabel_->setText(v.notes.empty() ? tr("Requested.") : QString::fromStdString(v.notes));
    else
        voiceLabel_->setText((c.voiceProcessing || c.bypassEffects) ? tr("Applied when monitoring starts.") : QString());
    meter_->setActive(running_);

    QString msg = QString::fromStdString(v.message);
    if (!v.present && c.enabled)
        msg = tr("This microphone is disconnected. Your settings and routes are kept; AudioBridge reconnects it "
                 "automatically when Windows reports the same device again.");
    else if (c.enabled && msg.isEmpty() && ui::routeCount(s, v.info.id) == 0)
        msg = tr("This microphone is not connected to any output yet, so you will see its level but hear nothing. "
                 "Connect it to a speaker in the Routing Matrix.");
    message_->setText(msg);
    message_->setVisible(!msg.isEmpty());
    setEnabled(true);
    setStyleSheet(v.present ? QString() : QStringLiteral("QLabel#deviceName { color: #8a92a2; }"));
}

void InputCard::updateMeter(const MeterFrame& f)
{
    if (slot_ >= 0 && controls_->isVisible())
        meter_->pushLevel(running_ ? f.inPeak[slot_] : 0.0f, running_ && f.inClip[slot_]);
}

InputsPage::InputsPage(EngineController* ctl, QWidget* parent) : Page(ctl, parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(28, 24, 28, 20);
    v->setSpacing(16);
    auto* refresh = new QPushButton(Theme::icon(Theme::Icon::Refresh, Theme::text(), 16), tr("Refresh devices"));
    connect(refresh, &QPushButton::clicked, this, [this] { ctl_->refreshDevices(); });
    v->addWidget(ui::pageHeader(tr("Inputs"),
                                tr("Every microphone and recording device Windows reports, with its real name. "
                                   "Enable the ones you want to use; several can run at the same time."),
                                refresh));
    v->addWidget(ui::scrollContainer(&list_), 1);
    empty_ = new QLabel(tr("No recording devices found. Connect a microphone or check Windows Sound settings."));
    empty_->setObjectName("muted");
    empty_->setAlignment(Qt::AlignCenter);
    list_->addWidget(empty_);
    list_->addStretch(1);
}

void InputsPage::setSnapshot(const ab::EngineSnapshot& s)
{
    std::vector<const ab::InputView*> shown;
    for (const auto& iv : s.inputs)
        if (iv.present || iv.config.enabled || showDisconnected_)
            shown.push_back(&iv);
    std::vector<std::string> ids;
    for (auto* iv : shown)
        ids.push_back(iv->info.id);
    if (ids != ids_) {
        for (auto* c : cards_)
            c->deleteLater();
        cards_.clear();
        int idx = 0;
        for (auto* iv : shown) {
            // Colour index matches the routing matrix / diagram (order in the snapshot).
            int colorIndex = 0;
            for (std::size_t k = 0; k < s.inputs.size(); ++k)
                if (s.inputs[k].info.id == iv->info.id)
                    colorIndex = static_cast<int>(k);
            auto* card = new InputCard(ctl_, *iv, colorIndex);
            list_->insertWidget(idx++, card);
            cards_.push_back(card);
        }
        ids_ = ids;
    }
    for (std::size_t i = 0; i < cards_.size(); ++i)
        cards_[i]->update(*shown[i], s);
    empty_->setVisible(cards_.empty());
}

void InputsPage::updateMeters(const MeterFrame& f)
{
    for (auto* c : cards_)
        c->updateMeter(f);
}
