// Persistence of the engine configuration and application preferences (QSettings:
// HKCU\Software\AudioBridge\AudioBridge on Windows). Devices are stored by their
// Windows endpoint ID, so routes survive restarts, disconnects and renames.
#pragma once

#include "core/AudioEngine.h"

#include <QString>

struct AppPreferences {
    bool startMonitoringOnLaunch = false;
    bool startMinimized = false;
    bool minimizeToTray = true;      // closing the window keeps audio running in the tray
    bool launchAtStartup = false;    // HKCU\...\Run entry
    bool showDisconnected = true;    // list remembered devices that are not connected
};

namespace AppSettings {

ab::EngineConfig loadEngineConfig();
void saveEngineConfig(const ab::EngineConfig& cfg);

AppPreferences loadPreferences();
void savePreferences(const AppPreferences& p);

// Registers / unregisters AudioBridge in the user's Windows startup programs.
bool applyLaunchAtStartup(bool enabled);

// Removes every stored setting (devices, routes, preferences).
void resetAll();

} // namespace AppSettings
