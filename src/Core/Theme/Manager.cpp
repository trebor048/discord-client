#include "Core/Theme/Manager.hpp"

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Stylesheet.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>

#include <algorithm>

namespace Acheron {
namespace Core {
namespace Theme {

Manager &Manager::instance()
{
    static Manager inst;
    return inst;
}

QColor Manager::color(Token token) const
{
    auto it = overrides.constFind(token);
    if (it != overrides.constEnd())
        return it.value();
    return descriptor(token).defaultColor;
}

QFont Manager::font(FontRole role) const
{
    auto it = fontOverrides.constFind(role);
    if (it != fontOverrides.constEnd())
        return it.value();
    return fontDescriptor(role).defaultFont;
}

int Manager::roundness() const
{
    const int px = QSettings().value(QStringLiteral("appearance/roundness"), kDefaultRoundness).toInt();
    return std::clamp(px, 0, 48);
}

void Manager::setRoundness(int px)
{
    const int clamped = std::clamp(px, 0, 48);
    if (clamped == roundness())
        return;
    QSettings().setValue(QStringLiteral("appearance/roundness"), clamped);
    scheduleApply(true, false);
}

bool Manager::hasOverride(Token token) const
{
    return overrides.contains(token);
}

void Manager::scheduleApply(bool colors, bool fonts)
{
    pendingColorApply = pendingColorApply || colors;
    pendingFontApply = pendingFontApply || fonts;
    if (applyScheduled)
        return;
    applyScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        applyScheduled = false;
        const bool doColors = pendingColorApply;
        const bool doFonts = pendingFontApply;
        pendingColorApply = pendingFontApply = false;
        if (doColors)
            apply();
        if (doFonts)
            applyFonts();
    });
}

void Manager::setOverride(Token token, const QColor &color)
{
    if (!color.isValid())
        return;
    overrides.insert(token, color);
    scheduleApply(true, false);
}

void Manager::clearOverride(Token token)
{
    if (overrides.remove(token) > 0)
        scheduleApply(true, false);
}

void Manager::resetAll()
{
    overrides.clear();
    fontOverrides.clear();
    scheduleApply(true, true);
}

void Manager::setOverrides(const QHash<Token, QColor> &overrides)
{
    this->overrides = overrides;
    scheduleApply(true, false);
}

bool Manager::hasFontOverride(FontRole role) const
{
    return fontOverrides.contains(role);
}

void Manager::setFontOverride(FontRole role, const QFont &font)
{
    fontOverrides.insert(role, font);
    scheduleApply(false, true);
}

void Manager::clearFontOverride(FontRole role)
{
    if (fontOverrides.remove(role) > 0)
        scheduleApply(false, true);
}

QPalette Manager::buildPalette() const
{
    QPalette pal = qApp->style()->standardPalette();

    for (const TokenDescriptor &d : registry()) {
        if (d.role.has_value())
            pal.setColor(*d.role, color(d.token));
    }

    const QColor disabled = color(Token::DisabledText);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabled);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    pal.setColor(QPalette::Disabled, QPalette::PlaceholderText, disabled);

    return pal;
}

void Manager::apply()
{
    qApp->setPalette(buildPalette());
    qApp->setStyleSheet(buildStyleSheet());
    emit themeChanged();
}

void Manager::applyFonts()
{
    qApp->setFont(font(FontRole::Ui));
    qApp->setStyleSheet(buildStyleSheet());
    emit metricsChanged();
}

QString Manager::defaultThemePath()
{
    const QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(".");
    return dir.filePath("theme.json");
}

QJsonObject Manager::toObject(bool includeDefaults) const
{
    QJsonObject obj;
    obj["_version"] = 1;
    for (const TokenDescriptor &d : registry()) {
        if (!includeDefaults && !overrides.contains(d.token))
            continue;
        const QColor c = color(d.token);
        const QString hex = c.name(c.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
        obj[QString::fromUtf8(d.id)] = hex;
    }
    for (const FontDescriptor &d : fontRegistry()) {
        if (!includeDefaults && !fontOverrides.contains(d.role))
            continue;
        obj[QString::fromUtf8(d.id)] = font(d.role).toString();
    }
    return obj;
}

void Manager::loadFromObject(const QJsonObject &obj)
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.key().startsWith(QLatin1Char('_')))
            continue;
        if (!it.value().isString())
            continue;
        const QString value = it.value().toString();

        if (const TokenDescriptor *d = findById(it.key())) {
            const QColor c(value);
            if (c.isValid())
                overrides.insert(d->token, c);
        } else if (const FontDescriptor *fd = findFontById(it.key())) {
            QFont parsed;
            if (parsed.fromString(value))
                fontOverrides.insert(fd->role, parsed);
        }
    }
    scheduleApply(true, true);
}

bool Manager::load()
{
    overrides.clear();
    fontOverrides.clear();
    QFile file(defaultThemePath());
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    loadFromObject(doc.object());
    return true;
}

bool Manager::save() const
{
    QSaveFile file(defaultThemePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QJsonDocument doc(toObject(false));
    file.write(doc.toJson(QJsonDocument::Indented));
    return file.commit();
}

bool Manager::exportTo(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QJsonDocument doc(toObject(true));
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool Manager::importFrom(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    overrides.clear();
    fontOverrides.clear();
    loadFromObject(doc.object());
    return true;
}

} // namespace Theme
} // namespace Core
} // namespace Acheron
