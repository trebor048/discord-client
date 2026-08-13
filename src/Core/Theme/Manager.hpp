#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QObject>
#include <QPalette>
#include <QString>

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Tokens.hpp"

class QJsonObject;

namespace Acheron {
namespace Core {
namespace Theme {

class Manager : public QObject
{
    Q_OBJECT
public:
    static Manager &instance();

    QColor color(Token token) const;
    QFont font(FontRole role) const;

    bool hasOverride(Token token) const;
    void setOverride(Token token, const QColor &color);
    void clearOverride(Token token);
    void resetAll();
    void setOverrides(const QHash<Token, QColor> &overrides);

    bool hasFontOverride(FontRole role) const;
    void setFontOverride(FontRole role, const QFont &font);
    void clearFontOverride(FontRole role);

    QPalette buildPalette() const;
    void apply();
    void applyFonts();

    // load/save only does overrides, export does everything as resolved
    bool load();
    bool save() const;
    bool exportTo(const QString &path) const;
    bool importFrom(const QString &path);

signals:
    void themeChanged();
    void metricsChanged();

private:
    Manager() = default;
    Q_DISABLE_COPY(Manager)

    static QString defaultThemePath();
    QJsonObject toObject(bool includeDefaults) const;
    void loadFromObject(const QJsonObject &obj);

    QHash<Token, QColor> overrides;
    QHash<FontRole, QFont> fontOverrides;
};

} // namespace Theme
} // namespace Core
} // namespace Acheron
