// AudioBridge visual theme: dark, Windows 11 Fluent-inspired palette, stylesheet,
// vector icons (drawn with QPainter so they are crisp at every DPI and need no
// icon font), and small UI helpers (fader law, formatting).
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace Theme {

// Palette
inline QColor window() { return QColor(0x0e, 0x10, 0x14); }
inline QColor sidebar() { return QColor(0x13, 0x16, 0x1c); }
inline QColor panel() { return QColor(0x18, 0x1b, 0x22); }
inline QColor card() { return QColor(0x1d, 0x21, 0x2a); }
inline QColor cardHover() { return QColor(0x23, 0x28, 0x33); }
inline QColor border() { return QColor(0x2a, 0x2f, 0x3b); }
inline QColor text() { return QColor(0xe8, 0xea, 0xf0); }
inline QColor subtext() { return QColor(0x95, 0x9e, 0xae); }
inline QColor dimtext() { return QColor(0x63, 0x6b, 0x7a); }
inline QColor accent() { return QColor(0x4c, 0x8d, 0xff); }
inline QColor accent2() { return QColor(0x8b, 0x6c, 0xff); }
inline QColor success() { return QColor(0x2e, 0xcc, 0x71); }
inline QColor warning() { return QColor(0xf5, 0xa5, 0x24); }
inline QColor danger() { return QColor(0xff, 0x4d, 0x5e); }
inline QColor meterTrack() { return QColor(0x10, 0x13, 0x18); }

// Per-input colour used consistently in the diagram, matrix and mixer.
QColor channelColor(int index);

QString styleSheet();

enum class Icon { Dashboard, Microphone, Speaker, Matrix, Mixer, Settings, Play, Stop, Pause, Refresh,
                  Bluetooth, Usb, Monitor, Laptop, Warning, Info, Headphones, Link };
QIcon icon(Icon which, const QColor& color = text(), int size = 20);
QPixmap iconPixmap(Icon which, const QColor& color, int size, qreal dpr = 2.0);

// Fader law: slider position 0..1000 <-> linear gain. 0 = silence; otherwise
// -60 dB .. +6 dB linear in dB (like a console fader).
int gainToFader(float gain);
float faderToGain(int pos);
QString gainText(float gain); // "-6.0 dB", "-∞ dB"
// Word-wraps `text` into at most `maxLines` lines of `width` pixels; the last line is
// elided with "…" if the text does not fit. Returns lines joined with '\n'.
QString wrapElide(const QString& text, const QFont& font, int width, int maxLines);
QString dbText(float db, int decimals = 1);

} // namespace Theme
