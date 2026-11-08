#include "PageCommon.h"

#include <QHBoxLayout>
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

} // namespace ui
