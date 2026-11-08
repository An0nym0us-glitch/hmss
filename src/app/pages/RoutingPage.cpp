#include "RoutingPage.h"

#include "../widgets/RoutingMatrixWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <memory>

RoutingPage::RoutingPage(EngineController* ctl, QWidget* parent) : Page(ctl, parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(28, 24, 28, 20);
    v->setSpacing(16);
    auto* clear = new QPushButton(tr("Disconnect all"));
    clear->setToolTip(tr("Remove every route"));
    connect(clear, &QPushButton::clicked, this, [this] {
        if (!snap_ || snap_->routes.empty())
            return;
        if (QMessageBox::question(this, tr("Disconnect all routes"), tr("Remove all %n route(s)?", "", static_cast<int>(snap_->routes.size()))) !=
            QMessageBox::Yes)
            return;
        for (const auto& r : snap_->routes)
            ctl_->setRoute(r.inputId, r.outputId, false);
    });
    v->addWidget(ui::pageHeader(tr("Routing Matrix"),
                                tr("Rows are microphones, columns are speakers. Click a cell to connect or disconnect; "
                                   "right-click a connected cell to set that route's level. Any microphone can feed any "
                                   "number of speakers, and every speaker mixes all microphones routed to it. Changes "
                                   "apply instantly, even while monitoring."),
                                clear));

    auto* legend = new QHBoxLayout;
    legend->setSpacing(18);
    auto addLegend = [&](const QPixmap& pm, const QString& text) {
        auto* ic = new QLabel;
        ic->setPixmap(pm);
        auto* l = new QLabel(text);
        l->setObjectName("muted");
        legend->addWidget(ic);
        legend->addWidget(l);
    };
    QPixmap on(14, 14);
    on.fill(Qt::transparent);
    {
        QPainter p(&on);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Theme::accent());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(1, 1, 12, 12), 3, 3);
    }
    addLegend(on, tr("Connected (bar = live level)"));
    addLegend(Theme::iconPixmap(Theme::Icon::Warning, Theme::warning(), 14),
              tr("Feedback risk (microphone and speaker on the same hardware)"));
    legend->addStretch(1);
    v->addLayout(legend);

    auto* sa = new QScrollArea;
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidgetResizable(false);
    sa->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    matrix_ = new RoutingMatrixWidget;
    sa->setWidget(matrix_);
    v->addWidget(sa, 1);

    hint_ = new QLabel;
    hint_->setObjectName("bannerInfo");
    hint_->setWordWrap(true);
    v->addWidget(hint_);

    connect(matrix_, &RoutingMatrixWidget::routeToggleRequested, this, &RoutingPage::onToggle);
    connect(matrix_, &RoutingMatrixWidget::routeGainRequested, this, &RoutingPage::onGain);
}

void RoutingPage::setSnapshot(const ab::EngineSnapshot& s)
{
    snap_ = std::make_shared<ab::EngineSnapshot>(s);
    matrix_->setSnapshot(s, showDisconnected_);
    int enabledIn = 0, enabledOut = 0;
    for (const auto& i : s.inputs)
        enabledIn += i.config.enabled ? 1 : 0;
    for (const auto& o : s.outputs)
        enabledOut += o.config.enabled ? 1 : 0;
    QString hint;
    if (enabledIn == 0 || enabledOut == 0)
        hint = tr("Tip: routes only carry audio between enabled devices. Enable microphones on the Inputs page and "
                  "speakers on the Outputs page (or simply connect a cell here - you will be asked to enable them).");
    else if (s.routes.empty())
        hint = tr("No routes yet. Click a cell to connect a microphone to a speaker.");
    else if (!s.monitoring)
        hint = tr("Routing is configured. Press Start monitoring on the Dashboard (or in the sidebar) to hear it.");
    hint_->setText(hint);
    hint_->setVisible(!hint.isEmpty());
}

void RoutingPage::updateMeters(const MeterFrame& f)
{
    matrix_->updateLevels(f);
}

void RoutingPage::onToggle(const QString& inQ, const QString& outQ, bool enable)
{
    if (!snap_)
        return;
    const std::string in = inQ.toStdString(), out = outQ.toStdString();
    const ab::InputView* iv = nullptr;
    const ab::OutputView* ov = nullptr;
    for (const auto& v : snap_->inputs)
        if (v.info.id == in)
            iv = &v;
    for (const auto& v : snap_->outputs)
        if (v.info.id == out)
            ov = &v;
    if (!iv || !ov)
        return;
    if (enable) {
        const auto risk = ab::AudioRoutingMatrix::feedbackRisk(iv->info, ov->info);
        if (risk != ab::AudioRoutingMatrix::FeedbackRisk::None) {
            const QString why = risk == ab::AudioRoutingMatrix::FeedbackRisk::SameDevice
                                    ? tr("“%1” and “%2” belong to the same physical device.")
                                    : tr("“%1” and “%2” are the built-in microphone and speakers of this computer.");
            QMessageBox box(QMessageBox::Warning, tr("Feedback risk"),
                            why.arg(QString::fromStdString(iv->info.displayName), QString::fromStdString(ov->info.displayName)) +
                                tr("\n\nThe speaker sound can reach the microphone and build up into loud howling. "
                                   "Use headphones or keep the volume low. The feedback guard will mute the output "
                                   "if sustained howling is detected.\n\nConnect anyway?"),
                            QMessageBox::Yes | QMessageBox::No, this);
            box.setDefaultButton(QMessageBox::No);
            if (box.exec() != QMessageBox::Yes)
                return;
        }
        // Offer to enable devices that are off, so the new route is audible.
        QStringList off;
        if (!iv->config.enabled)
            off << QString::fromStdString(iv->info.displayName);
        if (!ov->config.enabled)
            off << QString::fromStdString(ov->info.displayName);
        if (!off.isEmpty()) {
            const auto answer = QMessageBox::question(
                this, tr("Enable devices"),
                tr("This route uses device(s) that are currently off:\n\n%1\n\nEnable them now?").arg(off.join("\n")),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (answer == QMessageBox::Yes) {
                if (!iv->config.enabled)
                    ctl_->updateInput(in, [](ab::InputConfig& c) { c.enabled = true; });
                if (!ov->config.enabled)
                    ctl_->updateOutput(out, [](ab::OutputConfig& c) { c.enabled = true; });
            }
        }
    }
    ctl_->setRoute(in, out, enable);
}

void RoutingPage::onGain(const QString& inQ, const QString& outQ, float gain, const QPoint& pos)
{
    const std::string in = inQ.toStdString(), out = outQ.toStdString();
    QMenu menu(this);
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(10, 8, 10, 8);
    auto* title = new QLabel(tr("Route level"));
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 1000);
    slider->setMinimumWidth(180);
    slider->setValue(Theme::gainToFader(gain));
    auto* value = new QLabel(Theme::gainText(gain));
    value->setMinimumWidth(60);
    h->addWidget(title);
    h->addWidget(slider);
    h->addWidget(value);
    connect(slider, &QSlider::valueChanged, &menu, [this, in, out, value](int p) {
        const float g = Theme::faderToGain(p);
        value->setText(Theme::gainText(g));
        ctl_->setRouteGain(in, out, g);
    });
    auto* act = new QWidgetAction(&menu);
    act->setDefaultWidget(w);
    menu.addAction(act);
    menu.addSeparator();
    menu.addAction(tr("Reset to 0 dB"), this, [this, in, out] { ctl_->setRouteGain(in, out, 1.0f); });
    menu.addAction(tr("Disconnect"), this, [this, in, out] { ctl_->setRoute(in, out, false); });
    menu.exec(pos);
}
