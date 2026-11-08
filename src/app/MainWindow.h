#pragma once

#include "AppSettings.h"
#include "EngineController.h"
#include "MeterHub.h"

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>

#include <memory>
#include <vector>

class QStackedWidget;
class QPushButton;
class QLabel;
class Page;
class DashboardPage;
class SettingsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(EngineController* ctl, const AppPreferences& prefs, const QString& developmentNote,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    // Stops UI timers, shuts the engine down and stores the configuration.
    void shutdown();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildUi(const QString& developmentNote);
    void buildTray();
    void showPage(int index);
    void onSnapshot(SnapshotPtr s);
    void onMeterTick();
    void applyPreferences(const AppPreferences& p);
    void showFromTray();

    EngineController* ctl_;
    AppPreferences prefs_;
    std::unique_ptr<MeterHub> meters_;
    QTimer meterTimer_;
    QTimer saveTimer_;
    ab::EngineConfig pendingConfig_;
    bool haveConfig_ = false;
    QStackedWidget* stack_ = nullptr;
    std::vector<Page*> pages_;
    std::vector<QPushButton*> nav_;
    DashboardPage* dashboard_ = nullptr;
    SettingsPage* settings_ = nullptr;
    QPushButton* sideStart_ = nullptr;
    QLabel* sideState_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayStart_ = nullptr;
    bool quitting_ = false;
    bool trayHintShown_ = false;
    bool shutDown_ = false;
    bool initFailed_ = false;
    SnapshotPtr snap_;
};
