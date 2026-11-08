#include "RoutingMatrixWidget.h"

#include "../Theme.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <cmath>

namespace {

QString detailFor(const ab::DeviceView& v)
{
    QString state;
    if (!v.present)
        state = QObject::tr("Disconnected");
    else if (v.runState == ab::RunState::Running)
        state = QObject::tr("Streaming");
    else if (v.runState == ab::RunState::Error)
        state = QObject::tr("Error");
    else if (v.runState == ab::RunState::WaitingForDevice)
        state = QObject::tr("Waiting");
    else
        state = QString::fromUtf8(ab::toString(v.info.state));
    return QString::fromStdString(ab::deviceCategory(v.info)) + QStringLiteral(" · ") + state;
}

} // namespace

RoutingMatrixWidget::RoutingMatrixWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize RoutingMatrixWidget::sizeHint() const
{
    return QSize(kHeaderW + static_cast<int>(cols_.size()) * kCellW + 8,
                 kHeaderH + static_cast<int>(rows_.size()) * kCellH + 8);
}

void RoutingMatrixWidget::setSnapshot(const ab::EngineSnapshot& s, bool showDisconnected)
{
    monitoring_ = s.monitoring;
    rows_.clear();
    cols_.clear();
    int idx = 0;
    for (const auto& v : s.inputs) {
        if (!v.present && !showDisconnected && !v.config.enabled)
            continue;
        Row r;
        r.id = v.info.id;
        r.name = QString::fromStdString(v.info.displayName);
        r.detail = detailFor(v);
        r.present = v.present;
        r.enabled = v.config.enabled;
        r.running = v.runState == ab::RunState::Running;
        r.slot = v.slot;
        r.colorIndex = idx++;
        r.info = v.info;
        rows_.push_back(r);
    }
    for (const auto& v : s.outputs) {
        if (!v.present && !showDisconnected && !v.config.enabled)
            continue;
        Row r;
        r.id = v.info.id;
        r.name = QString::fromStdString(v.info.displayName);
        r.detail = detailFor(v);
        r.present = v.present;
        r.enabled = v.config.enabled;
        r.running = v.runState == ab::RunState::Running;
        r.slot = v.slot;
        r.info = v.info;
        cols_.push_back(r);
    }
    cells_.assign(rows_.size() * cols_.size(), Cell{});
    levels_.assign(cells_.size(), 0.0f);
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        for (int c = 0; c < static_cast<int>(cols_.size()); ++c) {
            Cell& cl = cell(r, c);
            cl.risk = ab::AudioRoutingMatrix::feedbackRisk(rows_[r].info, cols_[c].info);
            for (const auto& route : s.routes)
                if (route.inputId == rows_[r].id && route.outputId == cols_[c].id) {
                    cl.routed = true;
                    cl.gain = route.gain;
                }
        }
    updateGeometry();
    resize(sizeHint());
    update();
}

void RoutingMatrixWidget::updateLevels(const MeterFrame& f)
{
    bool changed = false;
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        for (int c = 0; c < static_cast<int>(cols_.size()); ++c) {
            float target = 0.0f;
            if (cell(r, c).routed && rows_[r].slot >= 0 && cols_[c].slot >= 0) {
                const float pk = f.routePeak[rows_[r].slot][cols_[c].slot];
                // Map -60..0 dBFS to 0..1.
                target = pk > 0.001f ? std::max(0.0f, std::min(1.0f, (20.0f * std::log10(pk) + 60.0f) / 60.0f)) : 0.0f;
            }
            float& lv = levels_[static_cast<std::size_t>(r * cols_.size() + c)];
            const float nv = target > lv ? target : lv * 0.85f;
            if (std::fabs(nv - lv) > 0.005f)
                changed = true;
            lv = nv;
        }
    if (changed)
        update();
}

QRectF RoutingMatrixWidget::cellRect(int r, int c) const
{
    return QRectF(kHeaderW + c * kCellW + 6, kHeaderH + r * kCellH + 5, kCellW - 12, kCellH - 10);
}

bool RoutingMatrixWidget::hitCell(const QPoint& p, int* r, int* c) const
{
    if (p.x() < kHeaderW || p.y() < kHeaderH)
        return false;
    const int cc = (p.x() - kHeaderW) / kCellW;
    const int rr = (p.y() - kHeaderH) / kCellH;
    if (rr < 0 || cc < 0 || rr >= static_cast<int>(rows_.size()) || cc >= static_cast<int>(cols_.size()))
        return false;
    if (!cellRect(rr, cc).contains(p))
        return false;
    *r = rr;
    *c = cc;
    return true;
}

void RoutingMatrixWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QFont base = font();
    QFont bold = base;
    bold.setWeight(QFont::DemiBold);
    QFont small = base;
    small.setPointSizeF(base.pointSizeF() * 0.85);

    // Column headers (outputs).
    for (int c = 0; c < static_cast<int>(cols_.size()); ++c) {
        const Row& col = cols_[c];
        QRectF h(kHeaderW + c * kCellW + 4, 4, kCellW - 8, kHeaderH - 10);
        const bool hot = c == hoverC_;
        p.setPen(Qt::NoPen);
        p.setBrush(hot ? Theme::cardHover() : Theme::card());
        p.drawRoundedRect(h, 8, 8);
        const QColor tc = col.present ? Theme::text() : Theme::dimtext();
        p.drawPixmap(QPointF(h.left() + 8, h.top() + 8),
                     Theme::iconPixmap(ab::deviceCategory(col.info).find("phones") != std::string::npos
                                           ? Theme::Icon::Headphones
                                           : Theme::Icon::Speaker,
                                       col.enabled ? Theme::accent() : Theme::subtext(), 16));
        p.setFont(bold);
        p.setPen(tc);
        QRectF nameRect(h.left() + 8, h.top() + 28, h.width() - 16, h.height() - 30);
        p.drawText(nameRect, Qt::AlignLeft | Qt::AlignTop,
                   Theme::wrapElide(col.name, bold, static_cast<int>(nameRect.width()), 3));
        p.setFont(small);
        p.setPen(col.running ? Theme::success() : Theme::dimtext());
        p.drawText(QRectF(h.left() + 28, h.top() + 7, h.width() - 34, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   col.enabled ? (col.running ? tr("Streaming") : (col.present ? tr("Enabled") : tr("Disconnected")))
                               : tr("Off"));
    }

    // Rows (inputs).
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const Row& row = rows_[r];
        QRectF h(4, kHeaderH + r * kCellH + 4, kHeaderW - 12, kCellH - 8);
        p.setPen(Qt::NoPen);
        p.setBrush(r == hoverR_ ? Theme::cardHover() : Theme::card());
        p.drawRoundedRect(h, 8, 8);
        p.setBrush(row.enabled ? Theme::channelColor(row.colorIndex) : Theme::dimtext());
        p.drawRoundedRect(QRectF(h.left() + 8, h.top() + 10, 4, h.height() - 20), 2, 2);
        p.setFont(bold);
        p.setPen(row.present ? Theme::text() : Theme::dimtext());
        p.drawText(QRectF(h.left() + 20, h.top() + 5, h.width() - 28, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(bold).elidedText(row.name, Qt::ElideRight, static_cast<int>(h.width() - 28)));
        p.setFont(small);
        p.setPen(Theme::subtext());
        const QString d = row.enabled ? row.detail : row.detail + tr(" · Off");
        p.drawText(QRectF(h.left() + 20, h.top() + 25, h.width() - 28, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(small).elidedText(d, Qt::ElideRight, static_cast<int>(h.width() - 28)));
    }

    // Cells.
    p.setFont(bold);
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r)
        for (int c = 0; c < static_cast<int>(cols_.size()); ++c) {
            const Cell& cl = cell(r, c);
            const QRectF rc = cellRect(r, c);
            const bool hover = r == hoverR_ && c == hoverC_;
            const bool live = rows_[r].running && cols_[c].running;
            const QColor col = Theme::channelColor(rows_[r].colorIndex);
            if (cl.routed) {
                QColor fill = col;
                fill.setAlpha(live ? 70 : 35);
                p.setPen(QPen(live ? col : col.darker(160), 1.5));
                p.setBrush(fill);
                p.drawRoundedRect(rc, 9, 9);
                // Live level glow along the bottom of the cell.
                const float lv = levels_[static_cast<std::size_t>(r * cols_.size() + c)];
                if (lv > 0.01f) {
                    QColor g = col;
                    g.setAlpha(200);
                    p.setPen(Qt::NoPen);
                    p.setBrush(g);
                    p.drawRoundedRect(QRectF(rc.left() + 8, rc.bottom() - 7, (rc.width() - 16) * lv, 3), 1.5, 1.5);
                }
                p.setPen(live ? Theme::text() : Theme::subtext());
                const QString label = std::fabs(cl.gain - 1.0f) < 0.001f ? tr("ON") : Theme::gainText(cl.gain);
                p.drawText(rc.adjusted(0, -3, 0, -3), Qt::AlignCenter, label);
            } else {
                p.setPen(QPen(hover ? QColor(0x4a, 0x53, 0x66) : QColor(0x2b, 0x30, 0x3c), 1.2, Qt::DashLine));
                p.setBrush(hover ? QColor(0x20, 0x25, 0x2f) : Qt::transparent);
                p.drawRoundedRect(rc, 9, 9);
                if (hover) {
                    p.setPen(Theme::subtext());
                    p.drawText(rc, Qt::AlignCenter, tr("+ Connect"));
                }
            }
            if (cl.risk != ab::AudioRoutingMatrix::FeedbackRisk::None)
                p.drawPixmap(QPointF(rc.right() - 18, rc.top() + 3), Theme::iconPixmap(Theme::Icon::Warning, Theme::warning(), 14));
        }

    if (isEmpty()) {
        p.setPen(Theme::subtext());
        p.drawText(rect(), Qt::AlignCenter, tr("No devices detected."));
    }
}

void RoutingMatrixWidget::mouseMoveEvent(QMouseEvent* e)
{
    int r = -1, c = -1;
    if (!hitCell(e->pos(), &r, &c)) {
        r = -1;
        c = -1;
    }
    if (r != hoverR_ || c != hoverC_) {
        hoverR_ = r;
        hoverC_ = c;
        setCursor(r >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void RoutingMatrixWidget::leaveEvent(QEvent*)
{
    hoverR_ = hoverC_ = -1;
    update();
}

void RoutingMatrixWidget::mousePressEvent(QMouseEvent* e)
{
    int r, c;
    if (!hitCell(e->pos(), &r, &c))
        return;
    const QString in = QString::fromStdString(rows_[r].id);
    const QString out = QString::fromStdString(cols_[c].id);
    if (e->button() == Qt::LeftButton)
        emit routeToggleRequested(in, out, !cell(r, c).routed);
    else if (e->button() == Qt::RightButton && cell(r, c).routed)
        emit routeGainRequested(in, out, cell(r, c).gain, e->globalPos());
}

bool RoutingMatrixWidget::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        int r, c;
        if (hitCell(he->pos(), &r, &c)) {
            const Cell& cl = cell(r, c);
            QString t = QString("<b>%1</b><br>→ <b>%2</b><br>").arg(rows_[r].name.toHtmlEscaped(), cols_[c].name.toHtmlEscaped());
            t += cl.routed ? tr("Route ON (level %1). Click to disconnect, right-click to set the level.").arg(Theme::gainText(cl.gain))
                           : tr("Click to connect.");
            if (!rows_[r].enabled || !cols_[c].enabled)
                t += tr("<br><i>Enable both devices to hear this route.</i>");
            if (cl.risk == ab::AudioRoutingMatrix::FeedbackRisk::SameDevice)
                t += tr("<br><span style='color:#f5a524'>Feedback risk: microphone and speaker belong to the same device.</span>");
            else if (cl.risk == ab::AudioRoutingMatrix::FeedbackRisk::SameComputer)
                t += tr("<br><span style='color:#f5a524'>Feedback risk: built-in microphone and built-in speakers of this PC.</span>");
            QToolTip::showText(he->globalPos(), t, this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(e);
}
