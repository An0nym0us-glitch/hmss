#include "RoutingDiagramWidget.h"

#include "../Theme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

RoutingDiagramWidget::RoutingDiagramWidget(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    anim_.setInterval(33);
    connect(&anim_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    clock_.start();
}

void RoutingDiagramWidget::setSnapshot(const ab::EngineSnapshot& s)
{
    monitoring_ = s.monitoring;
    paused_ = s.paused;
    ins_.clear();
    outs_.clear();
    edges_.clear();
    auto routed = [&](const std::string& id) {
        for (const auto& r : s.routes)
            if (r.inputId == id || r.outputId == id)
                return true;
        return false;
    };
    int colorIdx = 0;
    for (const auto& v : s.inputs) {
        const int ci = colorIdx++;
        if (!v.config.enabled && !routed(v.info.id))
            continue;
        Node n;
        n.id = v.info.id;
        n.name = QString::fromStdString(v.info.displayName);
        n.running = v.runState == ab::RunState::Running;
        n.present = v.present;
        n.enabled = v.config.enabled;
        n.slot = v.slot;
        n.colorIndex = ci;
        n.bluetooth = v.info.bus == ab::DeviceBus::Bluetooth;
        ins_.push_back(n);
    }
    for (const auto& v : s.outputs) {
        if (!v.config.enabled && !routed(v.info.id))
            continue;
        Node n;
        n.id = v.info.id;
        n.name = QString::fromStdString(v.info.displayName);
        n.running = v.runState == ab::RunState::Running;
        n.present = v.present;
        n.enabled = v.config.enabled;
        n.slot = v.slot;
        n.bluetooth = v.info.bus == ab::DeviceBus::Bluetooth;
        outs_.push_back(n);
    }
    for (const auto& r : s.routes) {
        Edge e;
        e.in = e.out = -1;
        for (int i = 0; i < static_cast<int>(ins_.size()); ++i)
            if (ins_[i].id == r.inputId)
                e.in = i;
        for (int o = 0; o < static_cast<int>(outs_.size()); ++o)
            if (outs_[o].id == r.outputId)
                e.out = o;
        if (e.in >= 0 && e.out >= 0)
            edges_.push_back(e);
    }
    updateAnimation();
    update();
}

void RoutingDiagramWidget::updateLevels(const MeterFrame& f)
{
    auto toUnit = [](float pk) {
        return pk > 0.001f ? std::max(0.0f, std::min(1.0f, (20.0f * std::log10(pk) + 60.0f) / 60.0f)) : 0.0f;
    };
    for (auto& n : ins_)
        n.level = n.slot >= 0 ? std::max(toUnit(f.inPeak[n.slot]), n.level * 0.85f) : 0.0f;
    for (auto& n : outs_)
        n.level = n.slot >= 0 ? std::max(toUnit(f.outPeak[n.slot]), n.level * 0.85f) : 0.0f;
    for (auto& e : edges_) {
        const int is = ins_[e.in].slot, os = outs_[e.out].slot;
        const float lv = (is >= 0 && os >= 0) ? toUnit(f.routePeak[is][os]) : 0.0f;
        e.level = std::max(lv, e.level * 0.85f);
    }
    if (!anim_.isActive())
        update();
}

void RoutingDiagramWidget::updateAnimation()
{
    const bool want = isVisible() && monitoring_ && !paused_ && !edges_.empty();
    if (want && !anim_.isActive())
        anim_.start();
    else if (!want && anim_.isActive())
        anim_.stop();
}

void RoutingDiagramWidget::showEvent(QShowEvent*) { updateAnimation(); }
void RoutingDiagramWidget::hideEvent(QHideEvent*) { anim_.stop(); }

void RoutingDiagramWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF area = QRectF(rect()).adjusted(12, 12, -12, -12);

    if (ins_.empty() && outs_.empty()) {
        p.setPen(Theme::subtext());
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap,
                   tr("No active configuration yet.\nEnable a microphone and a speaker, then connect them in the Routing Matrix."));
        return;
    }

    const double nodeW = std::min(320.0, area.width() * 0.31);
    const double nodeH = 46.0, gap = 10.0;
    const double hubW = std::min(170.0, area.width() * 0.2);
    const double hubH = std::max(96.0, std::min(area.height() - 20, std::max(ins_.size(), outs_.size()) * 30.0 + 30));
    const QRectF hub(area.center().x() - hubW / 2, area.center().y() - hubH / 2, hubW, hubH);

    auto column = [&](int n, int i, double x) {
        const double total = n * nodeH + (n - 1) * gap;
        const double top = area.center().y() - total / 2;
        return QRectF(x, top + i * (nodeH + gap), nodeW, nodeH);
    };
    auto inRect = [&](int i) { return column(static_cast<int>(ins_.size()), i, area.left()); };
    auto outRect = [&](int o) { return column(static_cast<int>(outs_.size()), o, area.right() - nodeW); };
    auto hubY = [&](int n, int i) { return hub.top() + hubH * (i + 1) / (n + 1); };

    const double t = clock_.elapsed() / 1000.0;

    // Edges.
    for (const Edge& e : edges_) {
        const Node& a = ins_[e.in];
        const Node& b = outs_[e.out];
        const QRectF ra = inRect(e.in), rb = outRect(e.out);
        const QPointF p0(ra.right(), ra.center().y());
        // Small per-route offsets keep routes that share a device distinguishable.
        const double inOff = (e.out - (static_cast<int>(outs_.size()) - 1) / 2.0) * 4.0;
        const double outOff = (e.in - (static_cast<int>(ins_.size()) - 1) / 2.0) * 4.0;
        const QPointF p1(hub.left(), hubY(static_cast<int>(ins_.size()), e.in) + inOff);
        const QPointF p2(hub.right(), hubY(static_cast<int>(outs_.size()), e.out) + outOff);
        const QPointF p3(rb.left(), rb.center().y());
        QPainterPath path(p0);
        const double mx1 = (p0.x() + p1.x()) / 2, mx2 = (p2.x() + p3.x()) / 2;
        path.cubicTo(QPointF(mx1, p0.y()), QPointF(mx1, p1.y()), p1);
        path.lineTo(p2);
        path.cubicTo(QPointF(mx2, p2.y()), QPointF(mx2, p3.y()), p3);

        const bool live = monitoring_ && a.running && b.running && a.enabled && b.enabled;
        QColor c = Theme::channelColor(a.colorIndex);
        if (live) {
            c.setAlpha(static_cast<int>(110 + 145 * e.level));
            p.setPen(QPen(c, 2.0 + 1.5 * e.level, Qt::SolidLine, Qt::RoundCap));
        } else {
            QColor g = Theme::subtext();
            g.setAlpha(110);
            p.setPen(QPen(g, 1.4, Qt::DashLine, Qt::RoundCap));
        }
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        // Flowing dots while signal is present.
        if (live && !paused_ && e.level > 0.05f) {
            QColor dc = Theme::channelColor(a.colorIndex).lighter(130);
            dc.setAlpha(static_cast<int>(120 + 135 * e.level));
            p.setPen(Qt::NoPen);
            p.setBrush(dc);
            const double phase = std::fmod(t * 0.35, 0.1);
            for (double s = phase; s < 1.0; s += 0.1)
                p.drawEllipse(path.pointAtPercent(s), 2.4, 2.4);
        }
    }

    // Hub.
    QLinearGradient hg(hub.topLeft(), hub.bottomRight());
    hg.setColorAt(0, QColor(0x2a, 0x3d, 0x6e));
    hg.setColorAt(1, QColor(0x3d, 0x2f, 0x72));
    p.setPen(QPen(QColor(0x5a, 0x6f, 0xb0), 1.2));
    p.setBrush(hg);
    p.drawRoundedRect(hub, 14, 14);
    QFont f = font();
    f.setWeight(QFont::Bold);
    f.setPointSizeF(f.pointSizeF() * 1.1);
    p.setFont(f);
    p.setPen(Theme::text());
    p.drawText(hub.adjusted(0, 0, 0, -hubH * 0.18), Qt::AlignCenter, QStringLiteral("AudioBridge"));
    QFont sf = font();
    sf.setPointSizeF(sf.pointSizeF() * 0.85);
    p.setFont(sf);
    p.setPen(QColor(0xb8, 0xc4, 0xe6));
    p.drawText(hub.adjusted(0, hubH * 0.22, 0, 0), Qt::AlignCenter,
               tr("Mixer · %n route(s)", "", static_cast<int>(edges_.size())));

    // Nodes.
    auto drawNode = [&](const Node& n, const QRectF& r, bool input) {
        p.setPen(QPen(Theme::border(), 1));
        p.setBrush(Theme::card());
        p.drawRoundedRect(r, 10, 10);
        const QColor stripe = input ? Theme::channelColor(n.colorIndex) : Theme::accent2();
        p.setPen(Qt::NoPen);
        p.setBrush(n.enabled ? stripe : Theme::dimtext());
        p.drawRoundedRect(QRectF(input ? r.left() + 6 : r.right() - 10, r.top() + 9, 4, r.height() - 18), 2, 2);
        const QColor dot = !n.present ? Theme::danger()
                                      : (n.running ? Theme::success() : (n.enabled ? Theme::warning() : Theme::dimtext()));
        const double tx = input ? r.left() + 18 : r.left() + 10;
        p.setBrush(dot);
        p.drawEllipse(QPointF(tx + 4, r.top() + 15), 3.5, 3.5);
        QFont nf = font();
        nf.setWeight(QFont::DemiBold);
        p.setFont(nf);
        p.setPen(n.present ? Theme::text() : Theme::dimtext());
        const QRectF tr(tx + 14, r.top() + 5, r.width() - 40, 20);
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(nf).elidedText(n.name, Qt::ElideRight, static_cast<int>(tr.width())));
        // Level bar.
        const QRectF bar(tx, r.bottom() - 13, r.width() - 34, 4);
        p.setBrush(Theme::meterTrack());
        p.drawRoundedRect(bar, 2, 2);
        if (n.level > 0.01f) {
            p.setBrush(n.level > 0.95f ? Theme::danger() : Theme::success());
            p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * n.level, bar.height()), 2, 2);
        }
    };
    for (int i = 0; i < static_cast<int>(ins_.size()); ++i)
        drawNode(ins_[i], inRect(i), true);
    for (int o = 0; o < static_cast<int>(outs_.size()); ++o)
        drawNode(outs_[o], outRect(o), false);
}
