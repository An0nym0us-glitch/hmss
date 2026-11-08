#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTextLayout>

#include <cmath>

namespace Theme {

QColor channelColor(int index)
{
    static const QColor colors[] = {QColor(0x4c, 0x8d, 0xff), QColor(0x2e, 0xcc, 0x9a), QColor(0xff, 0x9f, 0x43),
                                    QColor(0xb3, 0x7c, 0xff), QColor(0xff, 0x6b, 0x9a), QColor(0x3d, 0xd5, 0xf3),
                                    QColor(0xf9, 0xd4, 0x4a), QColor(0x8a, 0xe2, 0x34)};
    const int n = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
    return colors[((index % n) + n) % n];
}

QString styleSheet()
{
    // Colours are injected from the palette so they stay in one place.
    QString qss = R"(
* { font-family: "Segoe UI Variable Text", "Segoe UI", "Inter", "Noto Sans", sans-serif; font-size: 10pt; color: @text; }
QMainWindow, QWidget#central { background: @window; }
QWidget#sidebar { background: @sidebar; border-right: 1px solid @border; }
QWidget#page, QScrollArea, QScrollArea > QWidget > QWidget#scrollContent { background: transparent; border: none; }
QLabel { background: transparent; }
QLabel#pageTitle { font-size: 20pt; font-weight: 600; }
QLabel#pageSubtitle { color: @subtext; font-size: 10pt; }
QLabel#appName { font-size: 13pt; font-weight: 700; }
QLabel#appTag { color: @subtext; font-size: 8pt; }
QLabel#sectionTitle { font-size: 11pt; font-weight: 600; }
QLabel#muted, QLabel.muted { color: @subtext; }
QLabel#dim { color: @dimtext; font-size: 9pt; }
QLabel#statValue { font-size: 20pt; font-weight: 600; }
QLabel#statLabel { color: @subtext; font-size: 9pt; }
QLabel#deviceName { font-size: 11pt; font-weight: 600; }
QLabel#chip { background: #262b36; border-radius: 9px; padding: 2px 8px; color: #b9c0cc; font-size: 8.5pt; }
QLabel#chipAccent { background: rgba(76,141,255,0.16); border-radius: 9px; padding: 2px 8px; color: #9cc0ff; font-size: 8.5pt; }
QLabel#banner { background: rgba(245,165,36,0.12); border: 1px solid rgba(245,165,36,0.35); border-radius: 8px; padding: 8px 10px; color: #f3cf8b; }
QLabel#bannerInfo { background: rgba(76,141,255,0.10); border: 1px solid rgba(76,141,255,0.30); border-radius: 8px; padding: 8px 10px; color: #b6cdfa; }
QLabel#bannerDanger { background: rgba(255,77,94,0.12); border: 1px solid rgba(255,77,94,0.45); border-radius: 8px; padding: 8px 10px; color: #ffb3bb; }
QFrame#card { background: @card; border: 1px solid @border; border-radius: 12px; }
QFrame#cardFlat { background: @panel; border: 1px solid @border; border-radius: 12px; }
QFrame#separator { background: @border; max-height: 1px; min-height: 1px; border: none; }
QPushButton { background: #252a35; border: 1px solid #323846; border-radius: 8px; padding: 6px 14px; }
QPushButton:hover { background: #2c3240; border-color: #3c4456; }
QPushButton:pressed { background: #20252f; }
QPushButton:disabled { color: @dimtext; background: #1c2029; border-color: #262b36; }
QPushButton#primary { background: @accent; border: 1px solid #5d9bff; color: white; font-weight: 600; }
QPushButton#primary:hover { background: #5b98ff; }
QPushButton#primary:pressed { background: #3f7ce6; }
QPushButton#danger { background: rgba(255,77,94,0.18); border: 1px solid rgba(255,77,94,0.55); color: #ffc2c8; font-weight: 600; }
QPushButton#danger:hover { background: rgba(255,77,94,0.28); }
QPushButton#nav { background: transparent; border: none; border-radius: 8px; text-align: left; padding: 9px 12px; color: #c4cad6; font-size: 10.5pt; }
QPushButton#nav:hover { background: #1c2028; color: @text; }
QPushButton#nav:checked { background: #222836; color: white; font-weight: 600; }
QPushButton#mute, QPushButton#solo { min-width: 30px; max-width: 30px; min-height: 26px; max-height: 26px; padding: 0; font-weight: 700; border-radius: 6px; }
QPushButton#mute:checked { background: @danger; border-color: #ff7381; color: white; }
QPushButton#solo:checked { background: @warning; border-color: #ffc163; color: #1b1300; }
QPushButton#small { padding: 3px 10px; border-radius: 6px; font-size: 9pt; }
QSlider::groove:horizontal { height: 4px; background: #2a303c; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: #e9edf5; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; border: 2px solid @accent; }
QSlider::handle:horizontal:hover { background: white; }
QSlider::groove:vertical { width: 4px; background: #2a303c; border-radius: 2px; }
QSlider::add-page:vertical { background: @accent; border-radius: 2px; }
QSlider::handle:vertical { background: #e9edf5; height: 12px; width: 26px; margin: 0 -11px; border-radius: 4px; border: 1px solid #9aa6bb; }
QComboBox, QSpinBox, QDoubleSpinBox { background: #222731; border: 1px solid #323846; border-radius: 6px; padding: 4px 8px; min-height: 22px; }
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: #3f475a; }
QComboBox QAbstractItemView { background: #1f232c; border: 1px solid #323846; selection-background-color: #2f3a52; outline: 0; }
QComboBox::drop-down { border: none; width: 20px; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #4a5263; background: #1f232c; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; }
QGroupBox { background: @card; border: 1px solid @border; border-radius: 12px; margin-top: 18px; padding: 14px 14px 10px 14px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 14px; top: 2px; padding: 0 4px; color: @text; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #2c3240; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #3a4253; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #2c3240; border-radius: 4px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0; height: 0; }
QToolTip { background: #232834; color: @text; border: 1px solid #3a4253; padding: 6px; border-radius: 6px; }
QMenu { background: #1f232c; border: 1px solid #323846; padding: 6px; border-radius: 8px; }
QMenu::item { padding: 6px 18px; border-radius: 5px; }
QMenu::item:selected { background: #2b3446; }
QMessageBox { background: @panel; }
)";
    qss.replace("@window", window().name());
    qss.replace("@sidebar", sidebar().name());
    qss.replace("@panel", panel().name());
    qss.replace("@card", card().name());
    qss.replace("@border", border().name());
    qss.replace("@subtext", subtext().name());
    qss.replace("@dimtext", dimtext().name());
    qss.replace("@text", text().name());
    qss.replace("@accent", accent().name());
    qss.replace("@warning", warning().name());
    qss.replace("@danger", danger().name());
    return qss;
}

QPixmap iconPixmap(Icon which, const QColor& color, int size, qreal dpr)
{
    QPixmap pm(static_cast<int>(size * dpr), static_cast<int>(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0); // icons are designed on a 24x24 grid
    QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (which) {
    case Icon::Dashboard:
        p.drawRoundedRect(QRectF(3.5, 3.5, 7, 7), 2, 2);
        p.drawRoundedRect(QRectF(13.5, 3.5, 7, 7), 2, 2);
        p.drawRoundedRect(QRectF(3.5, 13.5, 7, 7), 2, 2);
        p.drawRoundedRect(QRectF(13.5, 13.5, 7, 7), 2, 2);
        break;
    case Icon::Microphone: {
        p.drawRoundedRect(QRectF(8.5, 2.5, 7, 12), 3.5, 3.5);
        QPainterPath arc;
        arc.moveTo(5, 11);
        arc.cubicTo(5, 19, 19, 19, 19, 11);
        p.drawPath(arc);
        p.drawLine(QPointF(12, 18), QPointF(12, 21.5));
        p.drawLine(QPointF(8.5, 21.5), QPointF(15.5, 21.5));
        break;
    }
    case Icon::Speaker: {
        QPainterPath body;
        body.moveTo(3.5, 9);
        body.lineTo(7.5, 9);
        body.lineTo(12.5, 4.5);
        body.lineTo(12.5, 19.5);
        body.lineTo(7.5, 15);
        body.lineTo(3.5, 15);
        body.closeSubpath();
        p.drawPath(body);
        p.drawArc(QRectF(10, 8, 7, 8), -60 * 16, 120 * 16);
        p.drawArc(QRectF(10, 4.5, 11, 15), -60 * 16, 120 * 16);
        break;
    }
    case Icon::Headphones: {
        QPainterPath band;
        band.moveTo(4, 15);
        band.cubicTo(4, 3, 20, 3, 20, 15);
        p.drawPath(band);
        p.drawRoundedRect(QRectF(3.5, 13.5, 4.5, 7), 1.5, 1.5);
        p.drawRoundedRect(QRectF(16, 13.5, 4.5, 7), 1.5, 1.5);
        break;
    }
    case Icon::Matrix:
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                const bool on = (r + c) % 2 == 0;
                p.setBrush(on ? color : Qt::NoBrush);
                p.drawEllipse(QPointF(5.5 + c * 6.5, 5.5 + r * 6.5), 2.2, 2.2);
            }
        break;
    case Icon::Mixer:
        p.drawLine(QPointF(6, 3.5), QPointF(6, 20.5));
        p.drawLine(QPointF(12, 3.5), QPointF(12, 20.5));
        p.drawLine(QPointF(18, 3.5), QPointF(18, 20.5));
        p.setBrush(color);
        p.drawRoundedRect(QRectF(3.5, 13, 5, 3), 1, 1);
        p.drawRoundedRect(QRectF(9.5, 6, 5, 3), 1, 1);
        p.drawRoundedRect(QRectF(15.5, 10, 5, 3), 1, 1);
        break;
    case Icon::Settings: {
        p.drawEllipse(QPointF(12, 12), 3.2, 3.2);
        for (int i = 0; i < 8; ++i) {
            const double a = i * 3.14159265 / 4.0;
            p.drawLine(QPointF(12 + 6.2 * std::cos(a), 12 + 6.2 * std::sin(a)),
                       QPointF(12 + 8.8 * std::cos(a), 12 + 8.8 * std::sin(a)));
        }
        p.drawEllipse(QPointF(12, 12), 6.2, 6.2);
        break;
    }
    case Icon::Play: {
        QPainterPath t;
        t.moveTo(7.5, 4.5);
        t.lineTo(19, 12);
        t.lineTo(7.5, 19.5);
        t.closeSubpath();
        p.setBrush(color);
        p.drawPath(t);
        break;
    }
    case Icon::Stop:
        p.setBrush(color);
        p.drawRoundedRect(QRectF(5.5, 5.5, 13, 13), 2.5, 2.5);
        break;
    case Icon::Pause:
        p.setBrush(color);
        p.drawRoundedRect(QRectF(6, 5, 4, 14), 1.5, 1.5);
        p.drawRoundedRect(QRectF(14, 5, 4, 14), 1.5, 1.5);
        break;
    case Icon::Refresh: {
        p.drawArc(QRectF(4.5, 4.5, 15, 15), 40 * 16, 290 * 16);
        QPainterPath head;
        head.moveTo(19.5, 3.5);
        head.lineTo(19.5, 9);
        head.lineTo(14, 9);
        p.drawPath(head);
        break;
    }
    case Icon::Bluetooth: {
        QPainterPath b;
        b.moveTo(6.5, 7.5);
        b.lineTo(17, 16.5);
        b.lineTo(12, 21);
        b.lineTo(12, 3);
        b.lineTo(17, 7.5);
        b.lineTo(6.5, 16.5);
        p.drawPath(b);
        break;
    }
    case Icon::Usb: {
        p.drawLine(QPointF(12, 3), QPointF(12, 18));
        p.drawEllipse(QPointF(12, 19.5), 2, 2);
        p.drawLine(QPointF(12, 14), QPointF(7, 10));
        p.drawLine(QPointF(7, 10), QPointF(7, 7));
        p.drawLine(QPointF(12, 12), QPointF(17, 9));
        p.drawLine(QPointF(17, 9), QPointF(17, 6.5));
        p.drawRect(QRectF(15.5, 4.5, 3, 2));
        p.drawEllipse(QPointF(7, 6), 1.3, 1.3);
        break;
    }
    case Icon::Monitor:
        p.drawRoundedRect(QRectF(3, 4, 18, 12), 2, 2);
        p.drawLine(QPointF(12, 16), QPointF(12, 20));
        p.drawLine(QPointF(8, 20.5), QPointF(16, 20.5));
        break;
    case Icon::Laptop:
        p.drawRoundedRect(QRectF(5, 5, 14, 10), 1.5, 1.5);
        p.drawLine(QPointF(2.5, 18.5), QPointF(21.5, 18.5));
        p.drawLine(QPointF(5, 15), QPointF(3, 18.5));
        p.drawLine(QPointF(19, 15), QPointF(21, 18.5));
        break;
    case Icon::Warning: {
        QPainterPath t;
        t.moveTo(12, 3.5);
        t.lineTo(21.5, 20);
        t.lineTo(2.5, 20);
        t.closeSubpath();
        p.drawPath(t);
        p.drawLine(QPointF(12, 9.5), QPointF(12, 14));
        p.setBrush(color);
        p.drawEllipse(QPointF(12, 17), 0.6, 0.6);
        break;
    }
    case Icon::Info:
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
        p.setBrush(color);
        p.drawEllipse(QPointF(12, 7.8), 0.6, 0.6);
        break;
    case Icon::Link:
        p.drawRoundedRect(QRectF(2.5, 8.5, 10, 7), 3.5, 3.5);
        p.drawRoundedRect(QRectF(11.5, 8.5, 10, 7), 3.5, 3.5);
        break;
    }
    return pm;
}

QIcon icon(Icon which, const QColor& color, int size)
{
    QIcon ic;
    ic.addPixmap(iconPixmap(which, color, size, 1.0));
    ic.addPixmap(iconPixmap(which, color, size, 2.0));
    return ic;
}

int gainToFader(float gain)
{
    if (gain <= 0.001f)
        return 0;
    const double db = 20.0 * std::log10(static_cast<double>(gain));
    const double pos = (db + 60.0) / 66.0 * 1000.0;
    return static_cast<int>(std::lround(std::max(1.0, std::min(1000.0, pos))));
}

float faderToGain(int pos)
{
    if (pos <= 0)
        return 0.0f;
    const double db = -60.0 + 66.0 * pos / 1000.0;
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

QString dbText(float db, int decimals)
{
    if (db <= -100.0f)
        return QStringLiteral("-∞ dB");
    return QString("%1%2 dB").arg(db > 0.05f ? "+" : "").arg(static_cast<double>(db), 0, 'f', decimals);
}

QString gainText(float gain)
{
    if (gain <= 0.0001f)
        return QStringLiteral("-∞ dB");
    return dbText(20.0f * std::log10(gain));
}

} // namespace Theme

namespace Theme {

QString wrapElide(const QString& text, const QFont& font, int width, int maxLines)
{
    QTextLayout layout(text, font);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(opt);
    layout.beginLayout();
    QStringList lines;
    const QFontMetrics fm(font);
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        if (lines.size() == maxLines - 1) {
            lines << fm.elidedText(text.mid(line.textStart()).simplified(), Qt::ElideRight, width);
            break;
        }
        lines << text.mid(line.textStart(), line.textLength()).trimmed();
    }
    layout.endLayout();
    return lines.join(QLatin1Char('\n'));
}

} // namespace Theme
