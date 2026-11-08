// AudioBridge - real-time multi-device audio routing and mixing for Windows.
#include "AppSettings.h"
#include "EngineController.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QPalette>
#include <QStandardPaths>
#include <QStyleFactory>

#ifdef _WIN32
#include "backend/wasapi/WasapiBackend.h"
#elif defined(AUDIOBRIDGE_DEV_SIMULATION)
#include "backend/sim/SimulatedBackend.h"
#endif

namespace {

std::unique_ptr<ab::IAudioBackend> createBackend(QString* note)
{
#ifdef _WIN32
    Q_UNUSED(note);
    return std::unique_ptr<ab::IAudioBackend>(new ab::WasapiBackend());
#elif defined(AUDIOBRIDGE_DEV_SIMULATION)
    *note = QObject::tr("DEVELOPMENT BUILD - this non-Windows build uses SIMULATED devices with generated test "
                        "tones. The Windows build uses real WASAPI devices.");
    auto sim = std::make_unique<ab::SimulatedBackend>(true);
    auto dev = [&](const char* id, const char* name, ab::DeviceFlow flow, ab::DeviceBus bus, ab::FormFactor ff, int rate,
                   int ch, double hz, const char* container) {
        ab::SimDeviceSpec s;
        s.id = id;
        s.name = name;
        s.flow = flow;
        s.bus = bus;
        s.formFactor = ff;
        s.sampleRate = rate;
        s.channels = ch;
        s.toneHz = hz;
        s.toneAmplitude = 0.12f;
        s.noiseAmplitude = 0.01f;
        s.containerId = container;
        s.burstyDelivery = bus == ab::DeviceBus::Bluetooth;
        if (std::string(id) == "sim.out.aux") {
            // Like a Realtek laptop codec: built-in speakers + 3.5 mm combo jack (cable plugged in).
            ab::JackInfo internal;
            internal.integrated = true;
            internal.connected = true;
            ab::JackInfo jack;
            jack.connector = "3.5 mm";
            jack.location = "left side";
            jack.connected = true;
            s.jacks = {internal, jack};
        }
        sim->addDevice(s);
    };
    using F = ab::DeviceFlow;
    using B = ab::DeviceBus;
    using FF = ab::FormFactor;
    dev("sim.mic.bt", "Headset Microphone (SIMULATED Bluetooth Earbuds)", F::Capture, B::Bluetooth, FF::Headset, 16000, 1, 330, "{BT}");
    dev("sim.mic.usb", "Microphone (SIMULATED USB Audio Device)", F::Capture, B::USB, FF::Microphone, 48000, 1, 520, "{USB1}");
    dev("sim.mic.int", "Microphone Array (SIMULATED Built-in)", F::Capture, B::Internal, FF::Microphone, 48000, 2, 740, "{PC}");
    dev("sim.out.aux", "Speakers (SIMULATED Realtek Audio)", F::Render, B::Internal, FF::Headphones, 48000, 2, 0, "{PC}");
    dev("sim.out.bt", "Headphones (SIMULATED Bluetooth Earbuds Stereo)", F::Render, B::Bluetooth, FF::Headphones, 44100, 2, 0, "{BT}");
    dev("sim.out.usb", "Speakers (SIMULATED USB Audio Device)", F::Render, B::USB, FF::Speakers, 48000, 2, 0, "{USB2}");
    dev("sim.out.hdmi", "SIMULATED HDMI Display Audio", F::Render, B::Display, FF::DigitalAudioDisplayDevice, 48000, 2, 0, "{MON}");
    dev("sim.out.nahimic", "Speakers (Nahimic mirroring Wave Speaker) SIMULATED", F::Render, B::Virtual, FF::Speakers, 48000, 2, 0, "{NAH}");
    dev("sim.mic.nahimic", "Microphone (Nahimic mirroring Wave Microphone) SIMULATED", F::Capture, B::Virtual, FF::Microphone, 48000, 2, 0, "{NAH}");
    return sim;
#else
    *note = QObject::tr("This platform has no audio backend.");
    return nullptr;
#endif
}

void applyDarkPalette(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, Theme::window());
    p.setColor(QPalette::WindowText, Theme::text());
    p.setColor(QPalette::Base, Theme::panel());
    p.setColor(QPalette::AlternateBase, Theme::card());
    p.setColor(QPalette::ToolTipBase, Theme::card());
    p.setColor(QPalette::ToolTipText, Theme::text());
    p.setColor(QPalette::Text, Theme::text());
    p.setColor(QPalette::Button, Theme::card());
    p.setColor(QPalette::ButtonText, Theme::text());
    p.setColor(QPalette::Highlight, Theme::accent());
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, Theme::accent());
    p.setColor(QPalette::Disabled, QPalette::Text, Theme::dimtext());
    p.setColor(QPalette::Disabled, QPalette::ButtonText, Theme::dimtext());
    p.setColor(QPalette::Disabled, QPalette::WindowText, Theme::dimtext());
    app.setPalette(p);
    app.setStyleSheet(Theme::styleSheet());
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("AudioBridge"));
    QCoreApplication::setApplicationName(QStringLiteral("AudioBridge"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AUDIOBRIDGE_VERSION));
    app.setQuitOnLastWindowClosed(false); // the tray keeps the app alive
    applyDarkPalette(app);

    // One instance only: two instances would compete for the same devices.
    QLockFile lock(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("AudioBridge.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        QMessageBox::information(nullptr, QStringLiteral("AudioBridge"),
                                 QObject::tr("AudioBridge is already running. Look for its icon in the notification area."));
        return 0;
    }

    QString note;
    std::unique_ptr<ab::IAudioBackend> backend = createBackend(&note);
    if (!backend) {
        QMessageBox::critical(nullptr, QStringLiteral("AudioBridge"), note);
        return 1;
    }
    const bool minimizedArg = app.arguments().contains(QStringLiteral("--minimized"));
    const AppPreferences prefs = AppSettings::loadPreferences();

    EngineController controller(std::move(backend));
    MainWindow window(&controller, prefs, note);
    controller.start(AppSettings::loadEngineConfig(), prefs.startMonitoringOnLaunch);
    if (!(minimizedArg || prefs.startMinimized) || !QSystemTrayIcon::isSystemTrayAvailable())
        window.show();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &window, [&window] { window.shutdown(); });
    return app.exec();
}
