#pragma once

#include "../AppSettings.h"
#include "PageCommon.h"

namespace Ui {
class SettingsPage;
}

class SettingsPage : public Page {
    Q_OBJECT
public:
    explicit SettingsPage(EngineController* ctl, QWidget* parent = nullptr);
    ~SettingsPage() override;
    void setSnapshot(const ab::EngineSnapshot& s) override;
    void updateMeters(const MeterFrame&) override {}
    void setPreferences(const AppPreferences& p);

signals:
    void preferencesChanged(AppPreferences prefs);
    void resetRequested();

private:
    void emitPrefs();
    Ui::SettingsPage* ui_;
    AppPreferences prefs_;
};
