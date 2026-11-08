// Runs ab::AudioEngine on a dedicated control thread and exposes it to the GUI.
//
// GUI -> engine: every call is posted to the engine thread (never blocks the GUI;
//                opening a Bluetooth device can take seconds).
// engine -> GUI: coalesced snapshots (device list, states, configs, routes) via
//                the snapshotChanged() signal, delivered queued on the GUI thread.
// Meters:        the GUI reads the lock-free AudioGraph atomics directly (MeterHub).
#pragma once

#include "core/AudioEngine.h"

#include <QObject>
#include <QThread>
#include <QTimer>

#include <functional>
#include <memory>

using SnapshotPtr = std::shared_ptr<const ab::EngineSnapshot>;
Q_DECLARE_METATYPE(SnapshotPtr)
Q_DECLARE_METATYPE(ab::EngineConfig)

class EngineController : public QObject {
    Q_OBJECT
public:
    explicit EngineController(std::unique_ptr<ab::IAudioBackend> backend, QObject* parent = nullptr);
    ~EngineController() override;

    // Starts the engine thread and initialises the backend there.
    void start(const ab::EngineConfig& initialConfig, bool startMonitoring);
    // Stops monitoring, returns the final configuration (blocking; used at shutdown).
    ab::EngineConfig shutdown();

    // Valid until shutdown(); stop reading meters before calling shutdown().
    const ab::AudioGraph& graph() const { return *graph_; }
    SnapshotPtr snapshot() const { return snapshot_; }

    // --- asynchronous commands (GUI thread)
    void startMonitoring();
    void stopMonitoring();
    void setPaused(bool paused);
    void refreshDevices();
    void updateInput(const std::string& id, std::function<void(ab::InputConfig&)> edit);
    void updateOutput(const std::string& id, std::function<void(ab::OutputConfig&)> edit);
    void updateSettings(std::function<void(ab::EngineSettings&)> edit);
    void setRoute(const std::string& in, const std::string& out, bool enabled);
    void setRouteGain(const std::string& in, const std::string& out, float gain);
    void setMasterVolume(float v);
    void setGlobalMute(bool m);
    void clearFeedbackTrip(const std::string& outputId);

signals:
    void snapshotChanged(SnapshotPtr snapshot);
    void configChanged(ab::EngineConfig config);  // for persistence (debounced by the receiver)
    void errorMessage(QString message);           // user-facing problems (e.g. too many devices)
    void initializationFailed(QString message);

private:
    void post(std::function<void()> fn);
    void afterChange();     // engine thread
    void publish();         // engine thread

    QThread thread_;
    QObject* worker_ = nullptr;           // lives in thread_; owns the timers
    std::unique_ptr<ab::AudioEngine> engine_;
    const ab::AudioGraph* graph_ = nullptr;
    SnapshotPtr snapshot_;                // GUI thread copy
    std::uint64_t publishedRevision_ = 0; // engine thread
    QTimer* housekeeping_ = nullptr;
    QTimer* publishTimer_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
};
