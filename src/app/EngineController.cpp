#include "EngineController.h"

#include <QMetaObject>

EngineController::EngineController(std::unique_ptr<ab::IAudioBackend> backend, QObject* parent)
    : QObject(parent), engine_(new ab::AudioEngine(std::move(backend)))
{
    graph_ = &engine_->graph();
    qRegisterMetaType<SnapshotPtr>("SnapshotPtr");
    qRegisterMetaType<ab::EngineConfig>("ab::EngineConfig");
    snapshot_ = std::make_shared<ab::EngineSnapshot>();
    connect(this, &EngineController::snapshotChanged, this, [this](SnapshotPtr s) { snapshot_ = std::move(s); });
    thread_.setObjectName("AudioBridge engine");
}

EngineController::~EngineController()
{
    if (thread_.isRunning())
        shutdown();
}

void EngineController::post(std::function<void()> fn)
{
    if (!worker_)
        return;
    QMetaObject::invokeMethod(
        worker_, [this, fn = std::move(fn)] {
            if (engine_) // no-op once the engine has been shut down
                fn();
        },
        Qt::QueuedConnection);
}

void EngineController::start(const ab::EngineConfig& initialConfig, bool startMonitoring)
{
    worker_ = new QObject();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_.start();

    post([this, initialConfig, startMonitoring] {
        housekeeping_ = new QTimer(worker_);
        housekeeping_->setInterval(250);
        connect(housekeeping_, &QTimer::timeout, worker_, [this] {
            if (!engine_)
                return;
            engine_->housekeeping();
            afterChange();
        });
        publishTimer_ = new QTimer(worker_);
        publishTimer_->setSingleShot(true);
        publishTimer_->setInterval(40); // coalesce bursts of changes (slider drags)
        connect(publishTimer_, &QTimer::timeout, worker_, [this] {
            if (engine_)
                publish();
        });
        refreshTimer_ = new QTimer(worker_);
        refreshTimer_->setSingleShot(true);
        refreshTimer_->setInterval(300); // debounce Windows device-change storms
        connect(refreshTimer_, &QTimer::timeout, worker_, [this] {
            if (!engine_)
                return;
            engine_->refreshDevices();
            afterChange();
        });
        // OS notifications arrive on a system thread: just schedule a debounced refresh.
        engine_->setDeviceChangeHook([this] {
            QMetaObject::invokeMethod(refreshTimer_, static_cast<void (QTimer::*)()>(&QTimer::start),
                                      Qt::QueuedConnection);
        });

        std::string err;
        if (!engine_->initialize(&err)) {
            emit initializationFailed(QString::fromStdString(err));
            return;
        }
        engine_->loadConfig(initialConfig);
        if (startMonitoring)
            engine_->startMonitoring();
        housekeeping_->start();
        publish();
    });
}

ab::EngineConfig EngineController::shutdown()
{
    ab::EngineConfig cfg;
    if (!thread_.isRunning())
        return cfg;
    QMetaObject::invokeMethod(
        worker_,
        [this, &cfg] {
            if (housekeeping_)
                housekeeping_->stop();
            if (publishTimer_)
                publishTimer_->stop();
            if (refreshTimer_)
                refreshTimer_->stop();
            cfg = engine_->config();
            engine_->stopMonitoring();
            // Destroy on the thread that initialised COM for the backend. The backend
            // detaches its device-change listener first, so the hook can no longer fire.
            engine_.reset();
        },
        Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
    worker_ = nullptr;
    return cfg;
}

void EngineController::afterChange()
{
    if (engine_->revision() != publishedRevision_ && publishTimer_ && !publishTimer_->isActive())
        publishTimer_->start();
}

void EngineController::publish()
{
    publishedRevision_ = engine_->revision();
    auto snap = std::make_shared<ab::EngineSnapshot>(engine_->snapshot());
    emit snapshotChanged(snap);
    emit configChanged(engine_->config());
}

void EngineController::startMonitoring()
{
    post([this] { engine_->startMonitoring(); afterChange(); });
}

void EngineController::stopMonitoring()
{
    post([this] { engine_->stopMonitoring(); afterChange(); });
}

void EngineController::setPaused(bool paused)
{
    post([this, paused] { engine_->setPaused(paused); afterChange(); });
}

void EngineController::refreshDevices()
{
    post([this] { engine_->refreshDevices(); afterChange(); });
}

void EngineController::updateInput(const std::string& id, std::function<void(ab::InputConfig&)> edit)
{
    post([this, id, edit] {
        ab::InputConfig c;
        if (!engine_->inputConfig(id, &c))
            return;
        edit(c);
        std::string err;
        if (!engine_->setInputConfig(c, &err))
            emit errorMessage(QString::fromStdString(err));
        afterChange();
    });
}

void EngineController::updateOutput(const std::string& id, std::function<void(ab::OutputConfig&)> edit)
{
    post([this, id, edit] {
        ab::OutputConfig c;
        if (!engine_->outputConfig(id, &c))
            return;
        edit(c);
        std::string err;
        if (!engine_->setOutputConfig(c, &err))
            emit errorMessage(QString::fromStdString(err));
        afterChange();
    });
}

void EngineController::updateSettings(std::function<void(ab::EngineSettings&)> edit)
{
    post([this, edit] {
        ab::EngineSettings s = engine_->settings();
        edit(s);
        engine_->setSettings(s);
        afterChange();
    });
}

void EngineController::setRoute(const std::string& in, const std::string& out, bool enabled)
{
    post([this, in, out, enabled] { engine_->setRoute(in, out, enabled); afterChange(); });
}

void EngineController::setRouteGain(const std::string& in, const std::string& out, float gain)
{
    post([this, in, out, gain] { engine_->setRouteGain(in, out, gain); afterChange(); });
}

void EngineController::setMasterVolume(float v)
{
    post([this, v] {
        engine_->setMasterVolume(v);
        // Master volume does not bump the revision (it is dragged continuously), but
        // it must be persisted.
        if (publishTimer_ && !publishTimer_->isActive())
            publishTimer_->start();
    });
}

void EngineController::setGlobalMute(bool m)
{
    post([this, m] { engine_->setGlobalMute(m); afterChange(); });
}

void EngineController::clearFeedbackTrip(const std::string& outputId)
{
    post([this, outputId] { engine_->clearFeedbackTrip(outputId); afterChange(); });
}

void EngineController::playTestTone(const std::string& outputId)
{
    post([this, outputId] {
        if (!engine_->isMonitoring())
            engine_->startMonitoring();
        if (!engine_->playTestTone(outputId))
            emit errorMessage(tr("This output is not streaming, so the test sound cannot be played. "
                                 "Check its status message on the Outputs page."));
        afterChange();
    });
}
