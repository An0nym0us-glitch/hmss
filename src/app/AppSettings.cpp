#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace AppSettings {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }

} // namespace

ab::EngineConfig loadEngineConfig()
{
    QSettings s;
    ab::EngineConfig c;
    s.beginGroup("engine");
    c.settings.sampleRate = s.value("sampleRate", 48000).toInt();
    c.settings.bufferMs = s.value("bufferMs", 20).toInt();
    c.settings.lowLatency = s.value("lowLatency", false).toBool();
    c.settings.limiter = s.value("limiter", true).toBool();
    c.settings.feedbackGuard = s.value("feedbackGuard", true).toBool();
    c.settings.masterVolume = s.value("masterVolume", 0.8).toFloat();
    c.settings.globalMute = s.value("globalMute", false).toBool();
    s.endGroup();

    const int ni = s.beginReadArray("inputs");
    for (int i = 0; i < ni; ++i) {
        s.setArrayIndex(i);
        ab::InputConfig ic;
        ic.id = ss(s.value("id").toString());
        if (ic.id.empty())
            continue;
        ic.lastKnownName = ss(s.value("name").toString());
        ic.enabled = s.value("enabled", false).toBool();
        ic.gainDb = s.value("gainDb", 0.0).toFloat();
        ic.volume = s.value("volume", 1.0).toFloat();
        ic.mute = s.value("mute", false).toBool();
        ic.solo = s.value("solo", false).toBool();
        ic.monoMix = s.value("monoMix", true).toBool();
        ic.highPass = s.value("highPass", false).toBool();
        ic.noiseSuppression = s.value("noiseSuppression", false).toBool();
        ic.noiseSuppressionStrength = s.value("noiseSuppressionStrength", 0.6).toFloat();
        ic.noiseGate = s.value("noiseGate", false).toBool();
        ic.gateThresholdDb = s.value("gateThresholdDb", -50.0).toFloat();
        ic.voiceProcessing = s.value("voiceProcessing", false).toBool();
        c.inputs.push_back(ic);
    }
    s.endArray();

    const int no = s.beginReadArray("outputs");
    for (int i = 0; i < no; ++i) {
        s.setArrayIndex(i);
        ab::OutputConfig oc;
        oc.id = ss(s.value("id").toString());
        if (oc.id.empty())
            continue;
        oc.lastKnownName = ss(s.value("name").toString());
        oc.enabled = s.value("enabled", false).toBool();
        oc.volume = s.value("volume", 1.0).toFloat();
        oc.mute = s.value("mute", false).toBool();
        oc.delayMs = s.value("delayMs", 0.0).toFloat();
        c.outputs.push_back(oc);
    }
    s.endArray();

    const int nr = s.beginReadArray("routes");
    for (int i = 0; i < nr; ++i) {
        s.setArrayIndex(i);
        ab::Route r;
        r.inputId = ss(s.value("input").toString());
        r.outputId = ss(s.value("output").toString());
        r.gain = s.value("gain", 1.0).toFloat();
        if (!r.inputId.empty() && !r.outputId.empty())
            c.routes.push_back(r);
    }
    s.endArray();
    return c;
}

void saveEngineConfig(const ab::EngineConfig& c)
{
    QSettings s;
    s.beginGroup("engine");
    s.setValue("sampleRate", c.settings.sampleRate);
    s.setValue("bufferMs", c.settings.bufferMs);
    s.setValue("lowLatency", c.settings.lowLatency);
    s.setValue("limiter", c.settings.limiter);
    s.setValue("feedbackGuard", c.settings.feedbackGuard);
    s.setValue("masterVolume", static_cast<double>(c.settings.masterVolume));
    s.setValue("globalMute", c.settings.globalMute);
    s.endGroup();

    s.remove("inputs");
    s.beginWriteArray("inputs", static_cast<int>(c.inputs.size()));
    for (int i = 0; i < static_cast<int>(c.inputs.size()); ++i) {
        const ab::InputConfig& ic = c.inputs[static_cast<std::size_t>(i)];
        s.setArrayIndex(i);
        s.setValue("id", qs(ic.id));
        s.setValue("name", qs(ic.lastKnownName));
        s.setValue("enabled", ic.enabled);
        s.setValue("gainDb", static_cast<double>(ic.gainDb));
        s.setValue("volume", static_cast<double>(ic.volume));
        s.setValue("mute", ic.mute);
        s.setValue("solo", ic.solo);
        s.setValue("monoMix", ic.monoMix);
        s.setValue("highPass", ic.highPass);
        s.setValue("noiseSuppression", ic.noiseSuppression);
        s.setValue("noiseSuppressionStrength", static_cast<double>(ic.noiseSuppressionStrength));
        s.setValue("noiseGate", ic.noiseGate);
        s.setValue("gateThresholdDb", static_cast<double>(ic.gateThresholdDb));
        s.setValue("voiceProcessing", ic.voiceProcessing);
    }
    s.endArray();

    s.remove("outputs");
    s.beginWriteArray("outputs", static_cast<int>(c.outputs.size()));
    for (int i = 0; i < static_cast<int>(c.outputs.size()); ++i) {
        const ab::OutputConfig& oc = c.outputs[static_cast<std::size_t>(i)];
        s.setArrayIndex(i);
        s.setValue("id", qs(oc.id));
        s.setValue("name", qs(oc.lastKnownName));
        s.setValue("enabled", oc.enabled);
        s.setValue("volume", static_cast<double>(oc.volume));
        s.setValue("mute", oc.mute);
        s.setValue("delayMs", static_cast<double>(oc.delayMs));
    }
    s.endArray();

    s.remove("routes");
    s.beginWriteArray("routes", static_cast<int>(c.routes.size()));
    for (int i = 0; i < static_cast<int>(c.routes.size()); ++i) {
        s.setArrayIndex(i);
        s.setValue("input", qs(c.routes[static_cast<std::size_t>(i)].inputId));
        s.setValue("output", qs(c.routes[static_cast<std::size_t>(i)].outputId));
        s.setValue("gain", static_cast<double>(c.routes[static_cast<std::size_t>(i)].gain));
    }
    s.endArray();
}

AppPreferences loadPreferences()
{
    QSettings s;
    AppPreferences p;
    s.beginGroup("app");
    p.startMonitoringOnLaunch = s.value("startMonitoringOnLaunch", false).toBool();
    p.startMinimized = s.value("startMinimized", false).toBool();
    p.minimizeToTray = s.value("minimizeToTray", true).toBool();
    p.launchAtStartup = s.value("launchAtStartup", false).toBool();
    p.showDisconnected = s.value("showDisconnected", true).toBool();
    s.endGroup();
    return p;
}

void savePreferences(const AppPreferences& p)
{
    QSettings s;
    s.beginGroup("app");
    s.setValue("startMonitoringOnLaunch", p.startMonitoringOnLaunch);
    s.setValue("startMinimized", p.startMinimized);
    s.setValue("minimizeToTray", p.minimizeToTray);
    s.setValue("launchAtStartup", p.launchAtStartup);
    s.setValue("showDisconnected", p.showDisconnected);
    s.endGroup();
}

bool applyLaunchAtStartup(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enabled) {
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        run.setValue("AudioBridge", QString("\"%1\" --minimized").arg(exe));
    } else {
        run.remove("AudioBridge");
    }
    run.sync();
    return run.status() == QSettings::NoError;
#else
    Q_UNUSED(enabled);
    return false;
#endif
}

void resetAll()
{
    QSettings s;
    s.clear();
    applyLaunchAtStartup(false);
}

} // namespace AppSettings
