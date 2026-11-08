#include "DashboardPage.h"

#include "../widgets/LevelMeter.h"
#include "../widgets/RoutingDiagramWidget.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QThread>
#include <QVBoxLayout>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <ctime>
#endif

namespace {

// CPU time consumed by this process (all threads), in seconds.
double processCpuSeconds()
{
#ifdef Q_OS_WIN
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
        return 0.0;
    auto toSec = [](const FILETIME& f) {
        return static_cast<double>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) * 1e-7;
    };
    return toSec(k) + toSec(u);
#else
    timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

} // namespace

DashboardPage::DashboardPage(EngineController* ctl, QWidget* parent) : Page(ctl, parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(28, 24, 28, 20);
    v->setSpacing(16);

    auto* actions = new QWidget;
    auto* ah = new QHBoxLayout(actions);
    ah->setContentsMargins(0, 0, 0, 0);
    ah->setSpacing(10);
    state_ = new QLabel;
    state_->setObjectName("muted");
    pause_ = new QPushButton;
    pause_->setMinimumHeight(38);
    startStop_ = new QPushButton;
    startStop_->setMinimumHeight(38);
    startStop_->setMinimumWidth(170);
    ah->addWidget(state_);
    ah->addSpacing(8);
    ah->addWidget(pause_);
    ah->addWidget(startStop_);
    v->addWidget(ui::pageHeader(tr("Dashboard"), tr("Live overview of your audio configuration."), actions));

    banner_ = new QLabel;
    banner_->setObjectName("banner");
    banner_->setWordWrap(true);
    banner_->hide();
    v->addWidget(banner_);

    auto* stats = new QWidget;
    auto* grid = new QGridLayout(stats);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    inputsValue_ = makeStat(grid, 0, tr("Microphones"), &inputsSub_);
    outputsValue_ = makeStat(grid, 1, tr("Outputs"), &outputsSub_);
    routesValue_ = makeStat(grid, 2, tr("Active routes"), &routesSub_);
    latencyValue_ = makeStat(grid, 3, tr("Latency"), &latencySub_);
    cpuValue_ = makeStat(grid, 4, tr("CPU usage"), &cpuSub_);
    v->addWidget(stats);

    auto* diagramCard = ui::card();
    auto* dv = new QVBoxLayout(diagramCard);
    dv->setContentsMargins(16, 12, 16, 12);
    auto* dt = new QLabel(tr("Signal flow"));
    dt->setObjectName("sectionTitle");
    dv->addWidget(dt);
    diagram_ = new RoutingDiagramWidget;
    dv->addWidget(diagram_, 1);
    v->addWidget(diagramCard, 1);

    auto* masterCard = ui::card();
    auto* mh = new QHBoxLayout(masterCard);
    mh->setContentsMargins(16, 12, 16, 12);
    mh->setSpacing(14);
    auto* ml = new QLabel(tr("Master"));
    ml->setObjectName("sectionTitle");
    mh->addWidget(ml);
    master_ = new QSlider(Qt::Horizontal);
    master_->setRange(0, 1000);
    master_->setMinimumWidth(200);
    master_->setToolTip(tr("Master volume (applies to every output)"));
    mh->addWidget(master_, 2);
    masterLabel_ = new QLabel;
    masterLabel_->setMinimumWidth(64);
    mh->addWidget(masterLabel_);
    muteAll_ = new QPushButton(tr("Mute all"));
    muteAll_->setCheckable(true);
    muteAll_->setObjectName("danger");
    mh->addWidget(muteAll_);
    auto* al = new QLabel(tr("Activity"));
    al->setObjectName("muted");
    mh->addWidget(al);
    activity_ = new LevelMeter(Qt::Horizontal);
    activity_->setMinimumWidth(160);
    activity_->setFixedHeight(10);
    activity_->setToolTip(tr("Loudest output right now"));
    mh->addWidget(activity_, 1);
    v->addWidget(masterCard);

    connect(startStop_, &QPushButton::clicked, this, [this] {
        if (snap_ && snap_->monitoring)
            ctl_->stopMonitoring();
        else
            ctl_->startMonitoring();
    });
    connect(pause_, &QPushButton::clicked, this, [this] {
        if (snap_)
            ctl_->setPaused(!snap_->paused);
    });
    connect(master_, &QSlider::valueChanged, this, [this](int p) {
        const float g = Theme::faderToGain(p);
        masterLabel_->setText(Theme::gainText(g));
        ctl_->setMasterVolume(g);
    });
    connect(muteAll_, &QPushButton::toggled, this, [this](bool on) { ctl_->setGlobalMute(on); });

    cpuClock_.start();
    lastCpuSeconds_ = processCpuSeconds();
}

QLabel* DashboardPage::makeStat(QGridLayout* grid, int col, const QString& label, QLabel** sub)
{
    auto* c = ui::card();
    auto* v = new QVBoxLayout(c);
    v->setContentsMargins(16, 12, 16, 12);
    v->setSpacing(2);
    auto* l = new QLabel(label);
    l->setObjectName("statLabel");
    auto* val = new QLabel("–");
    val->setObjectName("statValue");
    *sub = new QLabel;
    (*sub)->setObjectName("dim");
    v->addWidget(l);
    v->addWidget(val);
    v->addWidget(*sub);
    grid->addWidget(c, 0, col);
    grid->setColumnStretch(col, 1);
    return val;
}

void DashboardPage::setBanner(const QString& text, bool danger)
{
    banner_->setObjectName(danger ? "bannerDanger" : "banner");
    banner_->style()->unpolish(banner_); // re-apply the stylesheet for the new object name
    banner_->style()->polish(banner_);
    banner_->updateGeometry();
    banner_->setText(text);
    banner_->setVisible(!text.isEmpty());
}

void DashboardPage::setSnapshot(const ab::EngineSnapshot& s)
{
    snap_ = std::make_shared<ab::EngineSnapshot>(s);
    if (s.monitoring) {
        startStop_->setText(tr("Stop monitoring"));
        startStop_->setIcon(Theme::icon(Theme::Icon::Stop, Theme::text(), 16));
        startStop_->setObjectName("danger");
    } else {
        startStop_->setText(tr("Start monitoring"));
        startStop_->setIcon(Theme::icon(Theme::Icon::Play, QColor(Qt::white), 16));
        startStop_->setObjectName("primary");
    }
    startStop_->style()->unpolish(startStop_);
    startStop_->style()->polish(startStop_);
    pause_->setEnabled(s.monitoring);
    pause_->setText(s.paused ? tr("Resume") : tr("Pause"));
    pause_->setIcon(Theme::icon(s.paused ? Theme::Icon::Play : Theme::Icon::Pause, Theme::text(), 16));
    pause_->setToolTip(tr("Pause keeps every device open but silences the audio; resume is instant."));
    state_->setText(s.monitoring ? (s.paused ? tr("Paused") : tr("Monitoring live")) : tr("Stopped"));

    int inDetected = 0, inEnabled = 0, inRunning = 0, outDetected = 0, outEnabled = 0, outRunning = 0;
    for (const auto& i : s.inputs) {
        inDetected += i.present ? 1 : 0;
        inEnabled += i.config.enabled ? 1 : 0;
        inRunning += i.runState == ab::RunState::Running ? 1 : 0;
    }
    for (const auto& o : s.outputs) {
        outDetected += o.present ? 1 : 0;
        outEnabled += o.config.enabled ? 1 : 0;
        outRunning += o.runState == ab::RunState::Running ? 1 : 0;
    }
    int active = 0;
    for (const auto& r : s.routes) {
        bool a = false, b = false;
        for (const auto& i : s.inputs)
            if (i.info.id == r.inputId)
                a = i.runState == ab::RunState::Running;
        for (const auto& o : s.outputs)
            if (o.info.id == r.outputId)
                b = o.runState == ab::RunState::Running;
        active += (a && b) ? 1 : 0;
    }
    inputsValue_->setText(QString::number(inEnabled));
    inputsSub_->setText(tr("enabled · %1 detected · %2 streaming").arg(inDetected).arg(inRunning));
    outputsValue_->setText(QString::number(outEnabled));
    outputsSub_->setText(tr("enabled · %1 detected · %2 streaming").arg(outDetected).arg(outRunning));
    routesValue_->setText(QString::number(active));
    routesSub_->setText(tr("of %n configured", "", static_cast<int>(s.routes.size())));
    latencySub_->setText(tr("%1 kHz engine · %2 ms buffer").arg(s.settings.sampleRate / 1000.0).arg(s.settings.bufferMs));
    if (!s.monitoring)
        latencyValue_->setText("–");

    if (!master_->isSliderDown()) {
        const bool b = master_->blockSignals(true);
        master_->setValue(Theme::gainToFader(s.settings.masterVolume));
        master_->blockSignals(b);
    }
    masterLabel_->setText(Theme::gainText(s.settings.masterVolume));
    {
        const bool b = muteAll_->blockSignals(true);
        muteAll_->setChecked(s.settings.globalMute);
        muteAll_->blockSignals(b);
    }
    diagram_->setSnapshot(s);
}

void DashboardPage::updateMeters(const MeterFrame& f)
{
    diagram_->updateLevels(f);
    activity_->pushLevel(f.masterPeak, f.masterPeak >= 0.999f);
    if (++frame_ % 30 != 0 || !snap_)
        return; // the numbers below update once per second
    // Latency: worst output (route buffer + device buffer).
    if (snap_->monitoring) {
        double worst = 0.0;
        for (const auto& o : snap_->outputs)
            if (o.slot >= 0 && o.runState == ab::RunState::Running)
                worst = std::max(worst, static_cast<double>(f.outLatencyMs[o.slot]) + o.deviceLatencyMs);
        latencyValue_->setText(worst > 0.0 ? tr("%1 ms").arg(std::lround(worst)) : QStringLiteral("–"));
    }
    const double now = processCpuSeconds();
    const double wall = cpuClock_.restart() / 1000.0;
    const int cores = std::max(1, QThread::idealThreadCount());
    if (wall > 0.0) {
        const double pct = 100.0 * (now - lastCpuSeconds_) / wall / cores;
        cpuValue_->setText(QString("%1 %").arg(pct, 0, 'f', pct < 10 ? 1 : 0));
        cpuSub_->setText(tr("of total CPU (%1 logical cores)").arg(cores));
    }
    lastCpuSeconds_ = now;
}
