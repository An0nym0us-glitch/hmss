#include "SettingsPage.h"

#include "ui_SettingsPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#ifndef AUDIOBRIDGE_VERSION
#define AUDIOBRIDGE_VERSION "1.0.0"
#endif

SettingsPage::SettingsPage(EngineController* ctl, QWidget* parent) : Page(ctl, parent), ui_(new Ui::SettingsPage)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(28, 24, 28, 20);
    v->setSpacing(16);
    v->addWidget(ui::pageHeader(tr("Settings"), tr("Audio engine, safety and application preferences.")));
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    content->setObjectName("scrollContent");
    ui_->setupUi(content);
    sa->setWidget(content);
    v->addWidget(sa, 1);
    ui_->engineNote->setObjectName("dim");
    ui_->devicesNote->setObjectName("dim");

    for (int r : {44100, 48000, 96000})
        ui_->sampleRateCombo->addItem(QString("%1 kHz").arg(r / 1000.0), r);
    for (int ms : {10, 15, 20, 30, 40, 60, 80, 120, 200})
        ui_->bufferCombo->addItem(ms == 20 ? tr("%1 ms (recommended)").arg(ms)
                                           : (ms >= 80 ? tr("%1 ms (very robust)").arg(ms) : tr("%1 ms").arg(ms)),
                                  ms);

    connect(ui_->sampleRateCombo, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        const int rate = ui_->sampleRateCombo->itemData(idx).toInt();
        ctl_->updateSettings([rate](ab::EngineSettings& s) { s.sampleRate = rate; });
    });
    connect(ui_->bufferCombo, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        const int ms = ui_->bufferCombo->itemData(idx).toInt();
        ctl_->updateSettings([ms](ab::EngineSettings& s) { s.bufferMs = ms; });
    });
    connect(ui_->lowLatencyCheck, &QCheckBox::clicked, this,
            [this](bool on) { ctl_->updateSettings([on](ab::EngineSettings& s) { s.lowLatency = on; }); });
    connect(ui_->limiterCheck, &QCheckBox::clicked, this,
            [this](bool on) { ctl_->updateSettings([on](ab::EngineSettings& s) { s.limiter = on; }); });
    connect(ui_->feedbackCheck, &QCheckBox::clicked, this,
            [this](bool on) { ctl_->updateSettings([on](ab::EngineSettings& s) { s.feedbackGuard = on; }); });
    connect(ui_->refreshButton, &QPushButton::clicked, this, [this] { ctl_->refreshDevices(); });
    for (QCheckBox* cb : {ui_->showDisconnectedCheck, ui_->startMonitoringCheck, ui_->launchAtStartupCheck,
                          ui_->startMinimizedCheck, ui_->minimizeToTrayCheck})
        connect(cb, &QCheckBox::clicked, this, [this] { emitPrefs(); });
    connect(ui_->resetButton, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Reset all settings"),
                                  tr("Forget all device settings, routes and preferences?")) == QMessageBox::Yes)
            emit resetRequested();
    });

#ifndef Q_OS_WIN
    ui_->launchAtStartupCheck->setEnabled(false);
    ui_->launchAtStartupCheck->setToolTip(tr("Only available on Windows"));
#endif
}

SettingsPage::~SettingsPage()
{
    delete ui_;
}

void SettingsPage::setPreferences(const AppPreferences& p)
{
    prefs_ = p;
    ui_->showDisconnectedCheck->setChecked(p.showDisconnected);
    ui_->startMonitoringCheck->setChecked(p.startMonitoringOnLaunch);
    ui_->launchAtStartupCheck->setChecked(p.launchAtStartup);
    ui_->startMinimizedCheck->setChecked(p.startMinimized);
    ui_->minimizeToTrayCheck->setChecked(p.minimizeToTray);
}

void SettingsPage::emitPrefs()
{
    prefs_.showDisconnected = ui_->showDisconnectedCheck->isChecked();
    prefs_.startMonitoringOnLaunch = ui_->startMonitoringCheck->isChecked();
    prefs_.launchAtStartup = ui_->launchAtStartupCheck->isChecked();
    prefs_.startMinimized = ui_->startMinimizedCheck->isChecked();
    prefs_.minimizeToTray = ui_->minimizeToTrayCheck->isChecked();
    emit preferencesChanged(prefs_);
}

void SettingsPage::setSnapshot(const ab::EngineSnapshot& s)
{
    const ab::EngineSettings& st = s.settings;
    int ri = ui_->sampleRateCombo->findData(st.sampleRate);
    if (ri < 0) {
        ui_->sampleRateCombo->addItem(QString("%1 kHz").arg(st.sampleRate / 1000.0), st.sampleRate);
        ri = ui_->sampleRateCombo->count() - 1;
    }
    ui_->sampleRateCombo->setCurrentIndex(ri);
    int bi = ui_->bufferCombo->findData(st.bufferMs);
    if (bi < 0) {
        ui_->bufferCombo->addItem(tr("%1 ms").arg(st.bufferMs), st.bufferMs);
        bi = ui_->bufferCombo->count() - 1;
    }
    ui_->bufferCombo->setCurrentIndex(bi);
    ui_->lowLatencyCheck->setChecked(st.lowLatency);
    ui_->limiterCheck->setChecked(st.limiter);
    ui_->feedbackCheck->setChecked(st.feedbackGuard);

    ui_->aboutLabel->setText(
        tr("<b>AudioBridge %1</b> — real-time multi-device audio router and mixer.<br>"
           "Audio backend: %2<br><br>"
           "<b>Bluetooth microphones:</b> Windows exposes a Bluetooth headset microphone through the hands-free "
           "profile (HFP). While it is open, the earbuds' own playback drops to telephone quality. This does not "
           "affect other outputs such as the AUX jack, USB speakers or a separate Bluetooth speaker.<br><br>"
           "<b>Virtual microphone:</b> this version does not create a virtual microphone for Discord, OBS or games. "
           "That requires a Windows audio driver (or a separately installed virtual audio cable); AudioBridge can "
           "already send audio to such a device if it is installed, because it appears as a normal output.")
            .arg(QStringLiteral(AUDIOBRIDGE_VERSION), QString::fromStdString(s.backendName)));
}
