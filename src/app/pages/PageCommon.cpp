#include "PageCommon.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QPushButton>
#include <QUrl>
#include <QScrollArea>
#include <QVBoxLayout>

namespace ui {

QLabel* chip(const QString& text, bool accent)
{
    auto* l = new QLabel(text);
    l->setObjectName(accent ? "chipAccent" : "chip");
    return l;
}

QFrame* card(QWidget* parent, bool flat)
{
    auto* f = new QFrame(parent);
    f->setObjectName(flat ? "cardFlat" : "card");
    return f;
}

QFrame* separator()
{
    auto* f = new QFrame;
    f->setObjectName("separator");
    f->setFrameShape(QFrame::NoFrame);
    return f;
}

QWidget* pageHeader(const QString& title, const QString& subtitle, QWidget* right)
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* v = new QVBoxLayout;
    v->setSpacing(2);
    auto* t = new QLabel(title);
    t->setObjectName("pageTitle");
    auto* s = new QLabel(subtitle);
    s->setObjectName("pageSubtitle");
    s->setWordWrap(true);
    v->addWidget(t);
    v->addWidget(s);
    h->addLayout(v, 1);
    if (right)
        h->addWidget(right, 0, Qt::AlignRight | Qt::AlignVCenter);
    return w;
}

QScrollArea* scrollContainer(QVBoxLayout** contentLayout)
{
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget;
    content->setObjectName("scrollContent");
    auto* v = new QVBoxLayout(content);
    v->setContentsMargins(0, 0, 6, 0);
    v->setSpacing(12);
    sa->setWidget(content);
    *contentLayout = v;
    return sa;
}

StatusInfo status(const ab::DeviceView& v, bool monitoring, bool enabled)
{
    if (!v.present)
        return {QObject::tr("Disconnected"), Theme::danger()};
    if (v.info.state != ab::DeviceState::Active)
        return {QString::fromUtf8(ab::toString(v.info.state)), Theme::warning()};
    if (!enabled)
        return {QObject::tr("Available"), Theme::subtext()};
    switch (v.runState) {
    case ab::RunState::Running: return {QObject::tr("Streaming"), Theme::success()};
    case ab::RunState::Error: return {QObject::tr("Error - retrying"), Theme::danger()};
    case ab::RunState::WaitingForDevice: return {QObject::tr("Waiting for device"), Theme::warning()};
    case ab::RunState::Off:
    default: return {monitoring ? QObject::tr("Starting") : QObject::tr("Ready"), Theme::accent()};
    }
}

QLabel* statusPill()
{
    auto* l = new QLabel;
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(90);
    return l;
}

void setStatusPill(QLabel* pill, const StatusInfo& s)
{
    QColor bg = s.color;
    bg.setAlpha(40);
    pill->setText(QStringLiteral("●  ") + s.text);
    pill->setStyleSheet(QString("QLabel { background: rgba(%1,%2,%3,%4); color: %5; border-radius: 10px; padding: 3px 10px; "
                                "font-size: 8.5pt; font-weight: 600; }")
                            .arg(bg.red())
                            .arg(bg.green())
                            .arg(bg.blue())
                            .arg(bg.alpha())
                            .arg(s.color.lighter(115).name()));
}

Theme::Icon iconFor(const ab::DeviceInfo& d)
{
    if (d.bus == ab::DeviceBus::Bluetooth)
        return Theme::Icon::Bluetooth;
    if (d.bus == ab::DeviceBus::USB)
        return Theme::Icon::Usb;
    if (d.bus == ab::DeviceBus::Display)
        return Theme::Icon::Monitor;
    if (d.flow == ab::DeviceFlow::Capture)
        return Theme::Icon::Microphone;
    if (d.formFactor == ab::FormFactor::Headphones || d.formFactor == ab::FormFactor::Headset)
        return Theme::Icon::Headphones;
    return Theme::Icon::Speaker;
}

QString formatLine(const ab::DeviceView& v)
{
    if (v.streamOpen)
        return QString::fromStdString(v.streamFormat.describe());
    if (v.info.mixFormatValid)
        return QString::fromStdString(v.info.mixFormat.describe());
    return QString();
}

QWidget* windowsVolumeWarning(QLabel** text)
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* t = new QLabel;
    t->setObjectName("banner");
    t->setWordWrap(true);
    auto* b = new QPushButton(QObject::tr("Windows sound settings"));
    QObject::connect(b, &QPushButton::clicked, [] {
#ifdef Q_OS_WIN
        QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:sound")));
#endif
    });
    h->addWidget(t, 1);
    h->addWidget(b);
    *text = t;
    return w;
}

QString windowsVolumeProblem(const ab::DeviceInfo& d)
{
    if (d.state != ab::DeviceState::Active || d.windowsVolume < 0.0f)
        return QString();
    if (d.windowsMuted)
        return QObject::tr("Windows has this device MUTED. AudioBridge sends audio to it, but Windows silences it. "
                           "Unmute it in the Windows volume mixer / sound settings.");
    if (d.windowsVolume < 0.05f)
        return QObject::tr("Windows volume for this device is at %1 %. Raise it in Windows sound settings.")
            .arg(static_cast<int>(d.windowsVolume * 100.0f + 0.5f));
    return QString();
}

QString virtualDeviceWarning(const ab::DeviceInfo& d)
{
    if (!ab::isEnhancementVirtualDevice(d))
        return QString();
    const bool nahimic = QString::fromStdString(d.friendlyName + d.adapterName).contains("nahimic", Qt::CaseInsensitive);
    if (d.flow == ab::DeviceFlow::Render)
        return QObject::tr("This is a virtual device created by %1. It does not play sound on any physical speaker. "
                           "For the laptop's AUX / headphone port, use the Realtek output marked "
                           "“3.5 mm jack: plugged in” (or press Test on each output to hear which one it is).")
            .arg(nahimic ? QStringLiteral("Nahimic") : QObject::tr("audio-enhancement software"));
    return QObject::tr("This is a virtual recording device created by %1, not a physical microphone.")
        .arg(nahimic ? QStringLiteral("Nahimic") : QObject::tr("audio-enhancement software"));
}

int routeCount(const ab::EngineSnapshot& s, const std::string& id)
{
    int n = 0;
    for (const auto& r : s.routes)
        if (r.inputId == id || r.outputId == id)
            ++n;
    return n;
}

} // namespace ui
