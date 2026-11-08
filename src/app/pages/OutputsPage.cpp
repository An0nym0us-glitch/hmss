#include "OutputsPage.h"

#include "../widgets/LevelMeter.h"
#include "../widgets/ToggleSwitch.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

OutputCard::OutputCard(EngineController* ctl, const ab::OutputView& v, QWidget* parent)
    : QFrame(parent), ctl_(ctl), id_(v.info.id)
{
    setObjectName("card");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    auto* head = new QHBoxLayout;
    head->setSpacing(12);
    auto* icon = new QLabel;
    icon->setFixedSize(36, 36);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("background: rgba(139,108,255,40); border-radius: 10px;");
    icon->setPixmap(Theme::iconPixmap(ui::iconFor(v.info), Theme::accent2(), 20));
    head->addWidget(icon, 0, Qt::AlignTop);
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
    jack_ = new QLabel;
    jack_->setToolTip(tr("Physical connector state reported by the audio driver"));
    titles->addWidget(jack_);
    head->addLayout(titles, 1);
    pill_ = ui::statusPill();
    head->addWidget(pill_, 0, Qt::AlignTop);
    enable_ = new ToggleSwitch;
    enable_->setToolTip(tr("Use this output in AudioBridge"));
    head->addWidget(enable_, 0, Qt::AlignTop);
    root->addLayout(head);

    // Feedback guard banner.
    feedback_ = new QWidget;
    auto* fb = new QHBoxLayout(feedback_);
    fb->setContentsMargins(0, 0, 0, 0);
    auto* fbText = new QLabel(tr("Possible acoustic feedback detected - this output was muted automatically to protect "
                                 "your ears and speakers. Lower the microphone gain or move the microphone away from "
                                 "the speaker, then unmute."));
    fbText->setObjectName("bannerDanger");
    fbText->setWordWrap(true);
    auto* fbButton = new QPushButton(tr("Unmute output"));
    fbButton->setObjectName("danger");
    fb->addWidget(fbText, 1);
    fb->addWidget(fbButton);
    connect(fbButton, &QPushButton::clicked, this, [this] { ctl_->clearFeedbackTrip(id_); });
    root->addWidget(feedback_);

    virtualWarn_ = new QLabel;
    virtualWarn_->setObjectName("banner");
    virtualWarn_->setWordWrap(true);
    root->addWidget(virtualWarn_);
    winWarn_ = ui::windowsVolumeWarning(&winWarnText_);
    root->addWidget(winWarn_);

    controls_ = new QWidget;
    auto* grid = new QGridLayout(controls_);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(10);
    meter_ = new LevelMeter(Qt::Horizontal);
    grid->addWidget(new QLabel(tr("Output level")), 0, 0);
    grid->addWidget(meter_, 0, 1, 1, 5);

    volume_ = new QSlider(Qt::Horizontal);
    volume_->setRange(0, 1000);
    volume_->setToolTip(tr("Output volume"));
    volumeLabel_ = new QLabel;
    volumeLabel_->setMinimumWidth(64);
    grid->addWidget(new QLabel(tr("Volume")), 1, 0);
    grid->addWidget(volume_, 1, 1);
    grid->addWidget(volumeLabel_, 1, 2);
    mute_ = new QPushButton("M");
    mute_->setObjectName("mute");
    mute_->setCheckable(true);
    mute_->setToolTip(tr("Mute this output"));
    test_ = new QPushButton(Theme::icon(Theme::Icon::Play, Theme::text(), 14), tr("Test"));
    test_->setObjectName("small");
    test_->setToolTip(tr("Plays a short chime on this output only, so you can hear which physical speaker it is. "
                         "Starts monitoring if needed."));
    auto* mt = new QHBoxLayout;
    mt->setSpacing(6);
    mt->addWidget(mute_);
    mt->addWidget(test_);
    grid->addLayout(mt, 1, 3);
    delay_ = new QSpinBox;
    delay_->setRange(0, 500);
    delay_->setSuffix(tr(" ms"));
    delay_->setToolTip(tr("Extra delay for this output. Use it to line up a fast wired speaker with a slower "
                          "Bluetooth speaker when both play the same microphone."));
    auto* dl = new QHBoxLayout;
    dl->addWidget(new QLabel(tr("Sync delay")));
    dl->addWidget(delay_);
    grid->addLayout(dl, 1, 4, 1, 2, Qt::AlignRight);
    grid->setColumnStretch(1, 1);
    stats_ = new QLabel;
    stats_->setObjectName("dim");
    grid->addWidget(stats_, 2, 0, 1, 6);
    auto* bp = new QHBoxLayout;
    bp->setSpacing(10);
    bypass_ = new ToggleSwitch;
    bp->addWidget(bypass_);
    auto* bpl = new QLabel(tr("Bypass Windows audio effects (Nahimic, Dolby, …)"));
    bpl->setToolTip(tr("Opens the device in Windows RAW mode, so effects such as Nahimic are not applied to "
                       "AudioBridge's audio. Only works if the driver supports it; the result is shown here."));
    bp->addWidget(bpl);
    bypassNote_ = new QLabel;
    bypassNote_->setObjectName("dim");
    bypassNote_->setWordWrap(true);
    bp->addWidget(bypassNote_, 1);
    grid->addLayout(bp, 3, 0, 1, 6);
    root->addWidget(controls_);

    message_ = new QLabel;
    message_->setObjectName("dim");
    message_->setWordWrap(true);
    root->addWidget(message_);

    connect(enable_, &QAbstractButton::toggled, this, [this](bool on) { edit([on](ab::OutputConfig& c) { c.enabled = on; }); });
    connect(volume_, &QSlider::valueChanged, this, [this](int v) {
        volumeLabel_->setText(Theme::gainText(Theme::faderToGain(v)));
        edit([v](ab::OutputConfig& c) { c.volume = Theme::faderToGain(v); });
    });
    connect(mute_, &QPushButton::toggled, this, [this](bool on) { edit([on](ab::OutputConfig& c) { c.mute = on; }); });
    connect(test_, &QPushButton::clicked, this, [this] { ctl_->playTestTone(id_); });
    connect(bypass_, &QAbstractButton::toggled, this,
            [this](bool on) { edit([on](ab::OutputConfig& c) { c.bypassEffects = on; }); });
    connect(delay_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int ms) { edit([ms](ab::OutputConfig& c) { c.delayMs = static_cast<float>(ms); }); });
}

void OutputCard::update(const ab::OutputView& v, const ab::EngineSnapshot& s)
{
    const bool monitoring = s.monitoring;
    const ab::OutputConfig& c = v.config;
    slot_ = v.slot;
    running_ = v.runState == ab::RunState::Running;
    deviceLatency_ = v.deviceLatencyMs;
    name_->setText(QString::fromStdString(v.info.displayName));
    name_->setToolTip(tr("Windows name: %1\nEndpoint ID: %2")
                          .arg(QString::fromStdString(v.info.friendlyName), QString::fromStdString(v.info.id)));
    QStringList parts;
    parts << QString::fromStdString(ab::deviceCategory(v.info)) << QString::fromUtf8(ab::toString(v.info.bus));
    if (v.info.isDefault)
        parts << tr("Windows default");
    const QString fmt = ui::formatLine(v);
    if (!fmt.isEmpty())
        parts << fmt;
    chips_->setText(parts.join(QStringLiteral("  ·  ")));
    ui::setStatusPill(pill_, ui::status(v, monitoring, c.enabled));
    enable_->setCheckedSilently(c.enabled);
    enable_->setEnabled(v.present || c.enabled);
    controls_->setVisible(c.enabled);
    if (!volume_->isSliderDown()) {
        const bool b = volume_->blockSignals(true);
        volume_->setValue(Theme::gainToFader(c.volume));
        volume_->blockSignals(b);
    }
    volumeLabel_->setText(Theme::gainText(c.volume));
    {
        const bool b = mute_->blockSignals(true);
        mute_->setChecked(c.mute);
        mute_->blockSignals(b);
    }
    if (!delay_->hasFocus()) {
        const bool b = delay_->blockSignals(true);
        delay_->setValue(static_cast<int>(std::lround(c.delayMs)));
        delay_->blockSignals(b);
    }
    meter_->setActive(running_);

    const QString jack = QString::fromStdString(ab::jackSummary(v.info));
    jack_->setText(jack);
    jack_->setVisible(!jack.isEmpty());
    jack_->setStyleSheet(ab::hasPluggedJack(v.info) ? QStringLiteral("color: #2ecc71; font-weight: 600;")
                                                    : QStringLiteral("color: #95a0b2;"));
    const QString virt = ui::virtualDeviceWarning(v.info);
    virtualWarn_->setText(virt);
    virtualWarn_->setVisible(!virt.isEmpty());
    const QString vol = ui::windowsVolumeProblem(v.info);
    winWarnText_->setText(vol);
    winWarn_->setVisible(!vol.isEmpty() && c.enabled);
    bypass_->setCheckedSilently(c.bypassEffects);
    bypassNote_->setText(v.streamOpen && !v.notes.empty() ? QString::fromStdString(v.notes) : QString());

    QString msg = QString::fromStdString(v.message);
    if (!v.present && c.enabled)
        msg = tr("This output is disconnected. Its routes are kept and it reconnects automatically when the same "
                 "device comes back. AudioBridge never switches your audio to a different device on its own.");
    else if (c.enabled && msg.isEmpty() && ui::routeCount(s, v.info.id) == 0)
        msg = tr("No microphone is connected to this output yet, so it stays silent. Connect one in the Routing Matrix.");
    message_->setText(msg);
    message_->setVisible(!msg.isEmpty());
    if (!running_)
        feedback_->setVisible(false);
}

void OutputCard::updateMeter(const MeterFrame& f)
{
    if (slot_ < 0)
        return;
    feedback_->setVisible(running_ && f.outFeedback[slot_]);
    if (!controls_->isVisible())
        return;
    meter_->pushLevel(running_ ? f.outPeak[slot_] : 0.0f, running_ && f.outClip[slot_]);
    if (running_) {
        stats_->setText(tr("Latency ≈ %1 ms (route buffer %2 ms + device %3 ms)  ·  clock correction %4 ppm  ·  "
                           "underruns %5  ·  limiter %6")
                            .arg(std::lround(f.outLatencyMs[slot_] + deviceLatency_))
                            .arg(std::lround(f.outLatencyMs[slot_]))
                            .arg(std::lround(deviceLatency_))
                            .arg(std::lround(f.outDriftPpm[slot_]))
                            .arg(f.outUnderruns[slot_])
                            .arg(f.outLimiterDb[slot_] > 0.05f ? QString("-%1 dB").arg(f.outLimiterDb[slot_], 0, 'f', 1)
                                                               : tr("idle")));
    } else {
        stats_->clear();
    }
}

OutputsPage::OutputsPage(EngineController* ctl, QWidget* parent) : Page(ctl, parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(28, 24, 28, 20);
    v->setSpacing(16);
    auto* refresh = new QPushButton(Theme::icon(Theme::Icon::Refresh, Theme::text(), 16), tr("Refresh devices"));
    connect(refresh, &QPushButton::clicked, this, [this] { ctl_->refreshDevices(); });
    v->addWidget(ui::pageHeader(tr("Outputs"),
                                tr("Every speaker, headphone, AUX, USB, Bluetooth and HDMI output Windows reports. "
                                   "The laptop's AUX / headphone port is the output showing “3.5 mm jack: plugged in” "
                                   "(on Realtek laptops it is usually the “Speakers” device). Press Test on an output "
                                   "to hear which physical speaker it is."),
                                refresh));
    v->addWidget(ui::scrollContainer(&list_), 1);
    empty_ = new QLabel(tr("No playback devices found."));
    empty_->setObjectName("muted");
    empty_->setAlignment(Qt::AlignCenter);
    list_->addWidget(empty_);
    list_->addStretch(1);
}

void OutputsPage::setSnapshot(const ab::EngineSnapshot& s)
{
    std::vector<const ab::OutputView*> shown;
    for (const auto& ov : s.outputs)
        if (ov.present || ov.config.enabled || showDisconnected_)
            shown.push_back(&ov);
    std::vector<std::string> ids;
    for (auto* ov : shown)
        ids.push_back(ov->info.id);
    if (ids != ids_) {
        for (auto* c : cards_)
            c->deleteLater();
        cards_.clear();
        int idx = 0;
        for (auto* ov : shown) {
            auto* card = new OutputCard(ctl_, *ov);
            list_->insertWidget(idx++, card);
            cards_.push_back(card);
        }
        ids_ = ids;
    }
    for (std::size_t i = 0; i < cards_.size(); ++i)
        cards_[i]->update(*shown[i], s);
    empty_->setVisible(cards_.empty());
}

void OutputsPage::updateMeters(const MeterFrame& f)
{
    for (auto* c : cards_)
        c->updateMeter(f);
}
