#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

#include "Core/Theme/Tokens.hpp"

namespace Acheron {
namespace Core {
namespace Theme {
namespace Icons {

namespace Name {
inline constexpr auto AtSign = "at-sign";
inline constexpr auto Eye = "eye";
inline constexpr auto FileText = "file-text";
inline constexpr auto Handshake = "handshake";
inline constexpr auto IdCard = "id-card";
inline constexpr auto Lock = "lock";
inline constexpr auto MessageCircle = "message-circle";
inline constexpr auto Pencil = "pencil";
inline constexpr auto Search = "search";
inline constexpr auto Settings = "settings";
inline constexpr auto Spool = "spool";
inline constexpr auto X = "x";
} // namespace Name

QPixmap pixmap(const QString &name, int px, const QColor &color, qreal dpr = 1.0);
QPixmap pixmap(const QString &name, int px, Token token, qreal dpr = 1.0);

// Device presence glyph (monitor/smartphone/globe) colored by the user's status
// (green=online, yellow=idle, red=dnd, gray=offline). Returns a null pixmap
// when `device` is empty (caller should render a status dot instead).
inline QPixmap presencePixmap(const QString &status, const QString &device,
                              const QString &deviceStatus, int px, qreal dpr = 1.0)
{
    const QString colorStatus = deviceStatus.isEmpty() ? status : deviceStatus;
    QColor color;
    if (colorStatus == QLatin1String("online"))
        color = QColor(0x23, 0xA5, 0x5A);
    else if (colorStatus == QLatin1String("idle"))
        color = QColor(0xF0, 0xB2, 0x32);
    else if (colorStatus == QLatin1String("dnd"))
        color = QColor(0xF2, 0x3F, 0x43);
    else
        color = QColor(0x80, 0x84, 0x8E); // offline / unknown

    if (device.isEmpty())
        return {};

    QString iconName;
    if (device == QLatin1String("monitor") || device == QLatin1String("desktop"))
        iconName = QStringLiteral("monitor");
    else if (device == QLatin1String("mobile") || device == QLatin1String("phone"))
        iconName = QStringLiteral("smartphone");
    else
        iconName = QStringLiteral("globe");

    return pixmap(iconName, px, color, dpr);
}

QIcon icon(const QString &name, const QColor &color);
QIcon icon(const QString &name, Token token);

} // namespace Icons
} // namespace Theme
} // namespace Core
} // namespace Acheron
