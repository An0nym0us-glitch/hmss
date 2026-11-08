// Common building blocks for AudioBridge pages.
#pragma once

#include "../EngineController.h"
#include "../MeterHub.h"
#include "../Theme.h"

#include <QFrame>
#include <QLabel>
#include <QWidget>

class QVBoxLayout;
class QScrollArea;

// Base class for the pages in the main stack.
class Page : public QWidget {
    Q_OBJECT
public:
    Page(EngineController* ctl, QWidget* parent = nullptr) : QWidget(parent), ctl_(ctl) { setObjectName("page"); }
    virtual void setSnapshot(const ab::EngineSnapshot& s) = 0;
    virtual void updateMeters(const MeterFrame& f) = 0;
    virtual void setShowDisconnected(bool show) { showDisconnected_ = show; }

protected:
    EngineController* ctl_;
    bool showDisconnected_ = true;
};

namespace ui {

QLabel* chip(const QString& text, bool accent = false);
QFrame* card(QWidget* parent = nullptr, bool flat = false);
QFrame* separator();
// Title + subtitle row; `right` (optional) is placed at the right edge.
QWidget* pageHeader(const QString& title, const QString& subtitle, QWidget* right = nullptr);
// Vertically scrolling container; returns the layout to fill.
QScrollArea* scrollContainer(QVBoxLayout** contentLayout);

struct StatusInfo {
    QString text;
    QColor color;
};
StatusInfo status(const ab::DeviceView& v, bool monitoring, bool enabled);
QLabel* statusPill();
void setStatusPill(QLabel* pill, const StatusInfo& s);

Theme::Icon iconFor(const ab::DeviceInfo& d);
QString formatLine(const ab::DeviceView& v);

} // namespace ui
