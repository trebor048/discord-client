#pragma once

#include <QColor>
#include <QHash>
#include <QPushButton>
#include <QWidget>

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Tokens.hpp"

class QComboBox;
class QFontComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace Acheron {
namespace UI {

/// Appearance & theming hub.
///
/// Rebuilt around making the client easy to theme:
///   * one-click theme presets and quick accent swatches that regenerate the
///     whole palette from a seed color,
///   * a live preview mock that restyles in real time,
///   * every palette token editable via a color picker (alpha-aware) or a hex
///     field, always kept in sync with Core::Theme::Manager,
///   * an "extra CSS" editor for restyling anything the tokens do not cover,
///     with export/import so themes are shareable as a single JSON file.
///
/// Layout/scaling/motion/font controls from the original page are preserved
/// with identical semantics and signals.
class AppearancePage : public QWidget
{
    Q_OBJECT
public:
    explicit AppearancePage(QWidget *parent = nullptr);

signals:
    void channelListModeChanged(bool classic);
    void compactModeChanged(bool compact);
    void compactInputChanged(bool compact);
    void showTimestampsChanged(bool enabled);

private:
    /// Re-sync every token swatch/hex field and the accent seed from the
    /// Manager (called after themeChanged and after local edits).
    void refreshTokenControls();
    void refreshFontControls();
    void refreshAccentSeed();

    void generateInto(const QColor &seed, int schemeIndex, bool dark);
    /// Open the color picker for a single token and apply the result.
    void pickTokenColor(Core::Theme::Token token);
    /// Apply a hex string (validated) to a single token; no-op when invalid.
    void applyTokenHex(Core::Theme::Token token, const QString &hex);

    QHash<Core::Theme::Token, QPushButton *> tokenSwatches;
    QHash<Core::Theme::Token, QLineEdit *> tokenHexEdits;
    QHash<Core::Theme::FontRole, QFontComboBox *> familyCombos;
    QHash<Core::Theme::FontRole, QSpinBox *> sizeSpins;

    QColor seedColor;
    QPushButton *seedSwatch = nullptr;
    QComboBox *schemeCombo = nullptr;
    QComboBox *modeCombo = nullptr;
    QWidget *themePreview = nullptr;
    QPlainTextEdit *extraCssEdit = nullptr;
};

} // namespace UI
} // namespace Acheron
