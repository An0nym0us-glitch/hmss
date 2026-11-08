#include "MixerPage.h"

#include "../widgets/LevelMeter.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

ChannelStrip::ChannelStrip(Kind kind, EngineController* ctl, const std::string& id, const QString& name,
                           const QColor& color, QWidget* parent)
    : QFrame(parent), kind_(kind), ctl_(ctl), id_(id)
{
    setObjectName(kind == Kind::Master ? "cardFlat" : "card");
    setFixedWidth(kind == Kind::Master ? 132 : 128);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(8);

    auto* bar = new QFrame;
    bar->setFixedHeight(3);
    bar->setStyleSheet(QString("background: %1; border-radius: 1px;").arg(color.name()));
    v->addWidget(bar);
    auto* title = new QLabel(name);
    title->setWordWrap(false); // lines are pre-wrapped below
    title->setToolTip(name);
    title->setStyleSheet("font-weight: 600;");
    title->setFixedHeight(36);
    title->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    QFont tf = title->font();
    tf.setWeight(QFont::DemiBold);
    title->setText(Theme::wrapElide(name, tf, width() - 26, 2));
    v->addWidget(title);
    subtitle_ = new QLabel;
    subtitle_->setObjectName("dim");
    subtitle_->setAlignment(Qt::AlignHCenter);
    v->addWidget(subtitle_);

    if (kind == Kind::Input) {
        gain_ = new QSlider(Qt::Horizontal);
        gain_->setRange(-240, 360);
        gain_->setToolTip(tr("Microphone gain"));
        gainLabel_ = new QLabel;
        gainLabel_->setAlignment(Qt::AlignHCenter);
        gainLabel_->setObjectName("dim");
        auto* gl = new QLabel(tr("GAIN"));
        gl->setObjectName("dim");
        gl->setAlignment(Qt::AlignHCenter);
        v->addWidget(gl);
        v->addWidget(gain_);
        v->addWidget(gainLabel_);
        connect(gain_, &QSlider::valueChanged, this, [this](int val) {
            gainLabel_->setText(Theme::dbText(val / 10.0f));
            ctl_->updateInput(id_, [val](ab::InputConfig& c) { c.gainDb = val / 10.0f; });
        });
    }

    auto* fr = new QHBoxLayout;
    fr->setSpacing(10);
    fr->addStretch(1);
    fader_ = new QSlider(Qt::Vertical);
    fader_->setRange(0, 1000);
    fader_->setMinimumHeight(200);
    fader_->setToolTip(kind == Kind::Master ? tr("Master volume (applies to every output)") : tr("Volume"));
    meter_ = new LevelMeter(Qt::Vertical);
    meter_->setFixedWidth(kind == Kind::Master ? 14 : 10);
    fr->addWidget(fader_);
    fr->addWidget(meter_);
    fr->addStretch(1);
    v->addLayout(fr, 1);
    faderLabel_ = new QLabel;
    faderLabel_->setAlignment(Qt::AlignHCenter);
    v->addWidget(faderLabel_);

    auto* btns = new QHBoxLayout;
    btns->addStretch(1);
    mute_ = new QPushButton(kind == Kind::Master ? tr("MUTE") : QStringLiteral("M"));
    mute_->setObjectName(kind == Kind::Master ? "danger" : "mute");
    mute_->setCheckable(true);
    mute_->setToolTip(kind == Kind::Master ? tr("Mute all outputs") : tr("Mute"));
    btns->addWidget(mute_);
    if (kind == Kind::Input) {
        solo_ = new QPushButton("S");
        solo_->setObjectName("solo");
        solo_->setCheckable(true);
        solo_->setToolTip(tr("Solo"));
        btns->addWidget(solo_);
        connect(solo_, &QPushButton::toggled, this,
                [this](bool on) { ctl_->updateInput(id_, [on](ab::InputConfig& c) { c.solo = on; }); });
    }
    btns->addStretch(1);
    v->addLayout(btns);

    connect(fader_, &QSlider::valueChanged, this, [this](int pos) {
        const float g = Theme::faderToGain(pos);
        faderLabel_->setText(Theme::gainText(g));
        if (kind_ == Kind::Input)
            ctl_->updateInput(id_, [g](ab::InputConfig& c) { c.volume = g; });
        else if (kind_ == Kind::Output)
            ctl_->updateOutput(id_, [g](ab::OutputConfig& c) { c.volume = g; });
        else
            ctl_->setMasterVolume(g);
    });
    connect(mute_, &QPushButton::toggled, this, [this](bool on) {
        if (kind_ == Kind::Input)
            ctl_->updateInput(id_, [on](ab::InputConfig& c) { c.mute = on; });
        else if (kind_ == Kind::Output)
            ctl_->updateOutput(id_, [on](ab::OutputConfig& c) { c.mute = on; });
        else
            ctl_->setGlobalMute(on);
    });
}

void ChannelStrip::setState(float volume, float gainDb, bool mute, bool solo, bool running, int slot,
                            const QString& subtitle)
{
    slot_ = slot;
    running_ = running;
    subtitle_->setText(QFontMetrics(subtitle_->font()).elidedText(subtitle, Qt::ElideRight, width() - 26));
    subtitle_->setToolTip(subtitle);
    auto setSlider = [](QSlider* s, int v) {
        if (!s || s->isSliderDown())
            return;
        const bool b = s->blockSignals(true);
        s->setValue(v);
        s->blockSignals(b);
    };
    auto setButton = [](QPushButton* b, bool on) {
        if (!b)
            return;
        const bool o = b->blockSignals(true);
        b->setChecked(on);
        b->blockSignals(o);
    };
    setSlider(fader_, Theme::gainToFader(volume));
    faderLabel_->setText(Theme::gainText(volume));
    if (gain_) {
        setSlider(gain_, static_cast<int>(std::lround(gainDb * 10.0f)));
        gainLabel_->setText(Theme::dbText(gainDb));
    }
    setButton(mute_, mute);
    setButton(solo_, solo);
    meter_->setActive(running || kind_ == Kind::Master);
}

void ChannelStrip::updateMeter(const MeterFrame& f)
{
    if (kind_ == Kind::Master) {
        meter_->pushLevel(f.masterPeak, f.masterPeak >= 0.999f);
        return;
    }
    if (slot_ < 0 || !running_) {
        meter_->pushLevel(0.0f, false);
        return;
    }
    if (kind_ == Kind::Input)
        meter_->pushLevel(f.inPeak[slot_], f.inClip[slot_]);
    else
        meter_->pushLevel(f.outPeak[slot_], f.outClip[slot_]);
}

MixerPage::MixerPage(EngineController* ctl, QWidget* parent) : Page(ctl, parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(28, 24, 28, 20);
    v->setSpacing(16);
    v->addWidget(ui::pageHeader(tr("Mixer"),
                                tr("Channel strips for every enabled microphone and output. Gain is applied before the "
                                   "fader; the master fader scales every output. Meters show dBFS with peak hold; the "
                                   "red LED marks clipping (click to clear).")));
    auto* sa = new QScrollArea;
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidgetResizable(true);
    sa->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stripHost_ = new QWidget;
    stripHost_->setObjectName("scrollContent");
    strips_ = new QHBoxLayout(stripHost_);
    strips_->setContentsMargins(0, 0, 0, 8);
    strips_->setSpacing(10);
    sa->setWidget(stripHost_);
    v->addWidget(sa, 1);
    empty_ = new QLabel(tr("No devices enabled yet. Enable microphones on the Inputs page and speakers on the Outputs page."));
    empty_->setObjectName("muted");
    v->addWidget(empty_);
}

void MixerPage::setSnapshot(const ab::EngineSnapshot& s)
{
    std::vector<std::string> key;
    for (const auto& i : s.inputs)
        if (i.config.enabled)
            key.push_back("i" + i.info.id + "|" + i.info.displayName);
    for (const auto& o : s.outputs)
        if (o.config.enabled)
            key.push_back("o" + o.info.id + "|" + o.info.displayName);

    if (key != layoutKey_ || !master_) {
        for (auto* st : all_)
            st->deleteLater();
        all_.clear();
        if (master_)
            master_->deleteLater();
        QLayoutItem* item;
        while ((item = strips_->takeAt(0)) != nullptr) {
            if (item->widget() && item->widget() != stripHost_)
                item->widget()->deleteLater();
            delete item;
        }
        auto addLabel = [&](const QString& text) {
            auto* l = new QLabel(text);
            l->setObjectName("sectionTitle");
            l->setAlignment(Qt::AlignTop);
            return l;
        };
        bool any = false;
        int colorIdx = 0;
        for (const auto& i : s.inputs) {
            const int ci = colorIdx++;
            if (!i.config.enabled)
                continue;
            if (!any)
                strips_->addWidget(addLabel(tr("Inputs")), 0, Qt::AlignTop);
            any = true;
            auto* st = new ChannelStrip(ChannelStrip::Kind::Input, ctl_, i.info.id,
                                        QString::fromStdString(i.info.displayName), Theme::channelColor(ci));
            strips_->addWidget(st);
            all_.push_back(st);
        }
        bool anyOut = false;
        for (const auto& o : s.outputs) {
            if (!o.config.enabled)
                continue;
            if (!anyOut) {
                auto* sep = new QFrame;
                sep->setFixedWidth(1);
                sep->setStyleSheet(QString("background: %1;").arg(Theme::border().name()));
                strips_->addSpacing(8);
                strips_->addWidget(sep);
                strips_->addSpacing(8);
                strips_->addWidget(addLabel(tr("Outputs")), 0, Qt::AlignTop);
            }
            anyOut = true;
            auto* st = new ChannelStrip(ChannelStrip::Kind::Output, ctl_, o.info.id,
                                        QString::fromStdString(o.info.displayName), Theme::accent2());
            strips_->addWidget(st);
            all_.push_back(st);
        }
        strips_->addStretch(1);
        master_ = new ChannelStrip(ChannelStrip::Kind::Master, ctl_, std::string(), tr("Master"), Theme::accent());
        strips_->addWidget(master_);
        layoutKey_ = key;
        empty_->setVisible(all_.empty());
    }

    for (auto* st : all_) {
        for (const auto& i : s.inputs)
            if (i.info.id == st->id() && i.config.enabled)
                st->setState(i.config.volume, i.config.gainDb, i.config.mute, i.config.solo,
                             i.runState == ab::RunState::Running, i.slot,
                             QString::fromStdString(ab::deviceCategory(i.info)));
        for (const auto& o : s.outputs)
            if (o.info.id == st->id() && o.config.enabled)
                st->setState(o.config.volume, 0.0f, o.config.mute, false, o.runState == ab::RunState::Running, o.slot,
                             QString::fromStdString(ab::deviceCategory(o.info)));
    }
    master_->setState(s.settings.masterVolume, 0.0f, s.settings.globalMute, false, s.monitoring, -1,
                      s.monitoring ? (s.paused ? tr("Paused") : tr("Monitoring")) : tr("Stopped"));
}

void MixerPage::updateMeters(const MeterFrame& f)
{
    for (auto* st : all_)
        st->updateMeter(f);
    if (master_)
        master_->updateMeter(f);
}
