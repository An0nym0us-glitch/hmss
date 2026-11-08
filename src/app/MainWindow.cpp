#include "MainWindow.h"

#include "Theme.h"
#include "pages/DashboardPage.h"
#include "pages/InputsPage.h"
#include "pages/MixerPage.h"
#include "pages/OutputsPage.h"
#include "pages/RoutingPage.h"
#include "pages/SettingsPage.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

MainWindow::MainWindow(EngineController* ctl, const AppPreferences& prefs, const QString& developmentNote,
                       QWidget* parent)
    : QMainWindow(parent), ctl_(ctl), prefs_(prefs)
{
    setWindowTitle(QStringLiteral("AudioBridge"));
    setWindowIcon(QIcon(QStringLiteral(":/AudioBridge.png")));
    resize(1280, 820);
    setMinimumSize(980, 640);
    meters_.reset(new MeterHub(ctl_->graph()));
    buildUi(developmentNote);
    buildTray();

    connect(ctl_, &EngineController::snapshotChanged, this, &MainWindow::onSnapshot);
    connect(ctl_, &EngineController::errorMessage, this,
            [this](const QString& m) { QMessageBox::warning(this, tr("AudioBridge"), m); });
    connect(ctl_, &EngineController::initializationFailed, this, [this](const QString& m) {
        initFailed_ = true;
        dashboard_->setBanner(tr("Windows audio could not be initialised: %1").arg(m), true);
        QMessageBox::critical(this, tr("AudioBridge"), tr("Windows audio could not be initialised:\n%1").arg(m));
    });
    // Persist configuration changes (debounced: sliders produce many updates).
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(800);
    connect(&saveTimer_, &QTimer::timeout, this, [this] {
        if (haveConfig_ && !initFailed_)
            AppSettings::saveEngineConfig(pendingConfig_);
    });
    connect(ctl_, &EngineController::configChanged, this, [this](const ab::EngineConfig& c) {
        pendingConfig_ = c;
        haveConfig_ = true;
        saveTimer_.start();
    });

    meterTimer_.setInterval(33); // ~30 fps
    connect(&meterTimer_, &QTimer::timeout, this, &MainWindow::onMeterTick);
    meterTimer_.start();
    applyPreferences(prefs_);
}

MainWindow::~MainWindow()
{
    shutdown();
}

void MainWindow::buildUi(const QString& developmentNote)
{
    auto* central = new QWidget;
    central->setObjectName("central");
    auto* h = new QHBoxLayout(central);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // Sidebar.
    auto* side = new QWidget;
    side->setObjectName("sidebar");
    side->setFixedWidth(232);
    auto* sv = new QVBoxLayout(side);
    sv->setContentsMargins(14, 18, 14, 16);
    sv->setSpacing(4);
    auto* brand = new QHBoxLayout;
    auto* logo = new QLabel;
    logo->setPixmap(QIcon(QStringLiteral(":/AudioBridge.png")).pixmap(36, 36));
    brand->addWidget(logo);
    auto* bt = new QVBoxLayout;
    bt->setSpacing(0);
    auto* name = new QLabel(QStringLiteral("AudioBridge"));
    name->setObjectName("appName");
    auto* tag = new QLabel(tr("Audio router & mixer"));
    tag->setObjectName("appTag");
    bt->addWidget(name);
    bt->addWidget(tag);
    brand->addLayout(bt, 1);
    sv->addLayout(brand);
    sv->addSpacing(18);

    struct NavItem {
        QString text;
        Theme::Icon icon;
    };
    const NavItem items[] = {{tr("Dashboard"), Theme::Icon::Dashboard}, {tr("Inputs"), Theme::Icon::Microphone},
                             {tr("Outputs"), Theme::Icon::Speaker},     {tr("Routing Matrix"), Theme::Icon::Matrix},
                             {tr("Mixer"), Theme::Icon::Mixer},         {tr("Settings"), Theme::Icon::Settings}};
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    int idx = 0;
    for (const auto& it : items) {
        auto* b = new QPushButton(Theme::icon(it.icon, QColor(0xc4, 0xca, 0xd6), 18), QStringLiteral("  ") + it.text);
        b->setObjectName("nav");
        b->setCheckable(true);
        b->setIconSize(QSize(18, 18));
        b->setCursor(Qt::PointingHandCursor);
        group->addButton(b, idx);
        nav_.push_back(b);
        sv->addWidget(b);
        const int i = idx++;
        connect(b, &QPushButton::clicked, this, [this, i] { showPage(i); });
    }
    sv->addStretch(1);

    auto* statusCard = new QFrame;
    statusCard->setObjectName("cardFlat");
    auto* sc = new QVBoxLayout(statusCard);
    sc->setContentsMargins(12, 10, 12, 12);
    sc->setSpacing(8);
    sideState_ = new QLabel;
    sideState_->setObjectName("muted");
    sc->addWidget(sideState_);
    sideStart_ = new QPushButton;
    sideStart_->setMinimumHeight(34);
    connect(sideStart_, &QPushButton::clicked, this, [this] {
        if (snap_ && snap_->monitoring)
            ctl_->stopMonitoring();
        else
            ctl_->startMonitoring();
    });
    sc->addWidget(sideStart_);
    sv->addWidget(statusCard);
    h->addWidget(side);

    // Pages.
    stack_ = new QStackedWidget;
    dashboard_ = new DashboardPage(ctl_);
    settings_ = new SettingsPage(ctl_);
    pages_ = {dashboard_, new InputsPage(ctl_), new OutputsPage(ctl_), new RoutingPage(ctl_), new MixerPage(ctl_), settings_};
    for (Page* p : pages_)
        stack_->addWidget(p);
    h->addWidget(stack_, 1);
    setCentralWidget(central);

    if (!developmentNote.isEmpty())
        dashboard_->setBanner(developmentNote, false);

    settings_->setPreferences(prefs_);
    connect(settings_, &SettingsPage::preferencesChanged, this, [this](const AppPreferences& p) {
        const bool startupChanged = p.launchAtStartup != prefs_.launchAtStartup;
        prefs_ = p;
        AppSettings::savePreferences(p);
        if (startupChanged && !AppSettings::applyLaunchAtStartup(p.launchAtStartup) && p.launchAtStartup)
            QMessageBox::warning(this, tr("AudioBridge"), tr("Could not register AudioBridge as a startup program."));
        applyPreferences(p);
    });
    connect(settings_, &SettingsPage::resetRequested, this, [this] {
        AppSettings::resetAll();
        prefs_ = AppPreferences();
        settings_->setPreferences(prefs_);
        applyPreferences(prefs_);
        // Disable every device and remove every route; engine settings back to defaults.
        if (snap_) {
            for (const auto& r : snap_->routes)
                ctl_->setRoute(r.inputId, r.outputId, false);
            for (const auto& i : snap_->inputs)
                ctl_->updateInput(i.info.id, [](ab::InputConfig& c) {
                    const std::string id = c.id, name = c.lastKnownName;
                    c = ab::InputConfig();
                    c.id = id;
                    c.lastKnownName = name;
                });
            for (const auto& o : snap_->outputs)
                ctl_->updateOutput(o.info.id, [](ab::OutputConfig& c) {
                    const std::string id = c.id, name = c.lastKnownName;
                    c = ab::OutputConfig();
                    c.id = id;
                    c.lastKnownName = name;
                });
        }
        ctl_->updateSettings([](ab::EngineSettings& s) { s = ab::EngineSettings(); });
    });

    nav_[0]->setChecked(true);
    stack_->setCurrentIndex(0);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    tray_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/AudioBridge.png")), this);
    tray_->setToolTip(QStringLiteral("AudioBridge"));
    auto* menu = new QMenu(this);
    menu->addAction(tr("Open AudioBridge"), this, &MainWindow::showFromTray);
    trayStart_ = menu->addAction(tr("Start monitoring"), this, [this] {
        if (snap_ && snap_->monitoring)
            ctl_->stopMonitoring();
        else
            ctl_->startMonitoring();
    });
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, [this] {
        quitting_ = true;
        close();
    });
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
            showFromTray();
    });
    tray_->show();
}

void MainWindow::showFromTray()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::applyPreferences(const AppPreferences& p)
{
    for (Page* pg : pages_)
        pg->setShowDisconnected(p.showDisconnected);
    if (snap_)
        for (Page* pg : pages_)
            pg->setSnapshot(*snap_);
}

void MainWindow::showPage(int index)
{
    if (index == stack_->currentIndex())
        return;
    QWidget* w = stack_->widget(index);
    stack_->setCurrentIndex(index);
    nav_[static_cast<std::size_t>(index)]->setChecked(true);
    // Subtle fade-in; the effect is removed afterwards so it costs nothing at rest.
    auto* eff = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(eff);
    auto* anim = new QPropertyAnimation(eff, "opacity", w);
    anim->setDuration(140);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, w, [w] { w->setGraphicsEffect(nullptr); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::onSnapshot(SnapshotPtr s)
{
    snap_ = s;
    for (Page* p : pages_)
        p->setSnapshot(*s);
    const QString state = s->monitoring ? (s->paused ? tr("● Paused") : tr("● Monitoring live")) : tr("● Stopped");
    sideState_->setText(state);
    sideState_->setStyleSheet(QString("color: %1; font-weight: 600;")
                                  .arg((s->monitoring ? (s->paused ? Theme::warning() : Theme::success()) : Theme::subtext()).name()));
    sideStart_->setText(s->monitoring ? tr("Stop monitoring") : tr("Start monitoring"));
    sideStart_->setIcon(Theme::icon(s->monitoring ? Theme::Icon::Stop : Theme::Icon::Play,
                                    s->monitoring ? Theme::text() : QColor(Qt::white), 14));
    sideStart_->setObjectName(s->monitoring ? "danger" : "primary");
    sideStart_->style()->unpolish(sideStart_);
    sideStart_->style()->polish(sideStart_);
    if (trayStart_)
        trayStart_->setText(s->monitoring ? tr("Stop monitoring") : tr("Start monitoring"));
    if (tray_)
        tray_->setToolTip(QStringLiteral("AudioBridge - ") + (s->monitoring ? tr("monitoring") : tr("stopped")));
}

void MainWindow::onMeterTick()
{
    meters_->poll();
    if (isVisible() && !isMinimized()) {
        if (auto* p = qobject_cast<Page*>(stack_->currentWidget()))
            p->updateMeters(meters_->frame());
    }
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!quitting_ && prefs_.minimizeToTray && tray_ && tray_->isVisible()) {
        hide();
        if (!trayHintShown_) {
            tray_->showMessage(QStringLiteral("AudioBridge"),
                               tr("AudioBridge keeps running in the notification area. Right-click the icon to quit."),
                               QSystemTrayIcon::Information, 4000);
            trayHintShown_ = true;
        }
        e->ignore();
        return;
    }
    shutdown();
    e->accept();
    qApp->quit();
}

void MainWindow::shutdown()
{
    if (shutDown_)
        return;
    shutDown_ = true;
    meterTimer_.stop(); // no more graph reads after this point
    saveTimer_.stop();
    const ab::EngineConfig cfg = ctl_->shutdown();
    // Never overwrite the stored configuration with an empty one if the audio system
    // could not be initialised this session.
    if (!initFailed_)
        AppSettings::saveEngineConfig(cfg);
    if (tray_)
        tray_->hide();
}
