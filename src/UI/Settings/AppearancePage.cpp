#include "AppearancePage.hpp"

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Generator.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"
#include "Core/Animation/AnimationConfig.hpp"
#include "Core/Appearance/AppearanceConfig.hpp"
#include "ScaleStepper.hpp"

#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace Acheron {
namespace UI {

using Core::Theme::FontRole;
using Core::Theme::Manager;
using Core::Theme::Token;
using Core::Theme::TokenDescriptor;

namespace {

/// "#RRGGBB" when opaque, "rgba(r,g,b,a)" when translucent: translucent CSS
/// colors must not be written as #AARRGGBB everywhere (Qt QSS supports it,
/// but plain rgba keeps widget-inline styles unambiguous).
QString cssColor(const QColor &c)
{
    if (c.alpha() == 255)
        return c.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue())
            .arg(c.alpha());
}

QString swatchStyle(const QColor &c)
{
    return QStringLiteral("QPushButton { background-color: %1; border: 1px solid rgba(128,128,128,160);"
                          " border-radius: 4px; }")
            .arg(cssColor(c));
}

/// Normalize typed hex input: accept "#RGB"/"#RRGGBB"/"#AARRGGBB" as well as
/// the bare 3/6/8-digit forms; returns empty when unparseable.
QString normalizeHex(const QString &input)
{
    QString s = input.trimmed();
    if (s.startsWith(QLatin1Char('#')))
        s = s.mid(1);
    if (s.size() == 3) { // #RGB -> #RRGGBB
        QString expanded;
        for (const QChar ch : s)
            expanded += QString(ch) + ch;
        s = expanded;
    }
    if (s.size() != 6 && s.size() != 8)
        return QString();
    QString hex = QLatin1Char('#') + s;
    return QColor(hex).isValid() ? hex : QString();
}

/// One-click theme presets. Each preset is a seed color + scheme + light/dark
/// mode, plus a handful of deliberate token tweaks (e.g. true-black OLED).
struct ThemePreset
{
    const char *name;
    const char *seed;
    bool dark;
    int scheme;
    std::vector<std::pair<Token, const char *>> tweaks; // token, hex override
};

const std::vector<ThemePreset> &presets()
{
    static const std::vector<ThemePreset> list = {
        { QT_TRANSLATE_NOOP("AppearancePage", "Midnight"), "#5865F2", true, 0, {} },
        { QT_TRANSLATE_NOOP("AppearancePage", "OLED"), "#5865F2", true, 0,
          { { Token::WindowBg, "#000000" }, { Token::BaseBg, "#000000" },
            { Token::AlternateBaseBg, "#0B0B0F" }, { Token::ButtonBg, "#111116" },
            { Token::Divider, "#1C1C24" } } },
        { QT_TRANSLATE_NOOP("AppearancePage", "Aurora"), "#2DD4BF", true, 1, {} },
        { QT_TRANSLATE_NOOP("AppearancePage", "Sunset"), "#FF6B35", true, 1, {} },
        { QT_TRANSLATE_NOOP("AppearancePage", "Blossom"), "#F472B6", true, 3, {} },
        { QT_TRANSLATE_NOOP("AppearancePage", "Daylight"), "#5865F2", false, 0, {} },
    };
    return list;
}

/// Quick accent choices shown as swatch chips under the seed color.
const std::vector<std::pair<const char *, const char *>> &accentChips()
{
    static const std::vector<std::pair<const char *, const char *>> chips = {
        { "Blurple", "#5865F2" }, { "Fuchsia", "#EB459E" }, { "Red", "#ED4245" },
        { "Orange", "#F26522" },  { "Yellow", "#FEE75C" },  { "Green", "#57F287" },
        { "Cyan", "#00B0F4" },    { "Violet", "#7C5CFA" },
    };
    return chips;
}

QLabel *sectionHeader(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 0.5);
    label->setFont(f);
    return label;
}

/// A miniature, purely decorative mock of the client (guild rail, channel
/// list, message, input bar) painted from the live theme tokens so theme
/// edits are visible instantly inside the settings page.
class ThemePreview : public QWidget
{
public:
    explicit ThemePreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(264, 172);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Manager &m = Manager::instance();
        const QColor windowBg = m.color(Token::WindowBg);
        const QColor baseBg = m.color(Token::BaseBg);
        const QColor altBg = m.color(Token::AlternateBaseBg);
        const QColor divider = m.color(Token::Divider);
        const QColor primary = m.color(Token::PrimaryText);
        const QColor placeholder = m.color(Token::PlaceholderText);
        const QColor accent = m.color(Token::Highlight);
        const QColor accentText = m.color(Token::HighlightedText);
        const QColor buttonBg = m.color(Token::ButtonBg);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF bounds = rect().adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(altBg);
        p.drawRoundedRect(bounds, 6, 6);

        // Guild rail.
        p.setBrush(windowBg);
        p.drawRoundedRect(QRectF(bounds.left(), bounds.top(), 30, bounds.height()), 6, 6);
        const qreal railCenter = bounds.left() + 15;
        p.setBrush(accent);
        p.drawEllipse(QPointF(railCenter, bounds.top() + 16), 9, 9);
        p.setBrush(divider);
        for (int i = 0; i < 3; ++i)
            p.drawEllipse(QPointF(railCenter, bounds.top() + 46 + i * 22), 7, 7);

        // Channel pane.
        p.setBrush(baseBg);
        p.drawRoundedRect(QRectF(bounds.left() + 30, bounds.top(), 74, bounds.height()), 6, 6);
        p.setPen(placeholder);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 6, QFont::Bold));
        p.drawText(QRectF(bounds.left() + 36, bounds.top() + 8, 62, 12), Qt::AlignLeft, QStringLiteral("SERVER"));
        p.setPen(accent);
        p.drawText(QRectF(bounds.left() + 36, bounds.top() + 24, 62, 12), Qt::AlignLeft,
                   QStringLiteral("# general"));
        p.setPen(placeholder);
        p.drawText(QRectF(bounds.left() + 36, bounds.top() + 38, 62, 12), Qt::AlignLeft,
                   QStringLiteral("# lounge"));
        p.drawText(QRectF(bounds.left() + 36, bounds.top() + 52, 62, 12), Qt::AlignLeft,
                   QStringLiteral("# memes"));

        // Message area.
        const qreal msgLeft = bounds.left() + 112;
        const qreal msgRight = bounds.right() - 8;
        p.setPen(Qt::NoPen);
        // Header with a mock accent button.
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(msgRight - 34, bounds.top() + 8, 26, 12), 6, 6);
        p.setPen(accentText);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 6, QFont::Bold));
        p.drawText(QRectF(msgRight - 34, bounds.top() + 8, 26, 12), Qt::AlignCenter,
                   QStringLiteral("A"));
        p.setPen(Qt::NoPen);

        // Avatar + message lines.
        p.setBrush(QColor(accent).lighter(130));
        p.drawEllipse(QPointF(msgLeft + 6, bounds.top() + 32), 8, 8);
        p.setPen(primary);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 6, QFont::Bold));
        p.drawText(QRectF(msgLeft + 20, bounds.top() + 24, 90, 12), Qt::AlignLeft,
                   QStringLiteral("Acheron"));
        p.setPen(placeholder);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 6));
        p.drawText(QRectF(msgLeft + 20, bounds.top() + 36, 120, 14), Qt::AlignLeft,
                   QStringLiteral("themed to your taste"));
        p.setPen(divider);
        p.drawLine(QPointF(msgLeft, bounds.top() + 58), QPointF(msgRight, bounds.top() + 58));

        p.setPen(Qt::NoPen);
        p.setBrush(buttonBg);
        p.drawRoundedRect(QRectF(msgLeft + 4, bounds.top() + 66, 70, 16), 8, 8);
        p.setBrush(m.color(Token::MentionBg));
        p.drawRoundedRect(QRectF(msgLeft + 80, bounds.top() + 66, 84, 16), 8, 8);
        p.setPen(m.color(Token::MentionText));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 5));
        p.drawText(QRectF(msgLeft + 80, bounds.top() + 66, 84, 16), Qt::AlignCenter,
                   QStringLiteral("@someone"));

        // Input bar.
        p.setPen(Qt::NoPen);
        p.setBrush(baseBg);
        p.drawRoundedRect(QRectF(msgLeft, bounds.bottom() - 26, msgRight - msgLeft, 18), 9, 9);
        p.setPen(QColor(divider).lighter(115));
        p.drawRoundedRect(QRectF(msgLeft, bounds.bottom() - 26, msgRight - msgLeft, 18), 9, 9);
        p.setPen(placeholder);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 6));
        p.drawText(QRectF(msgLeft + 8, bounds.bottom() - 26, 100, 18), Qt::AlignVCenter,
                   QStringLiteral("Message #general"));
    }
};

} // namespace

AppearancePage::AppearancePage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 8);
    outer->setSpacing(12);

    // ------------------------------------------------------------------
    // Themes: presets + accent generation + live preview
    // ------------------------------------------------------------------
    auto *themesGroup = new QGroupBox(tr("Theme"), this);
    auto *themesLayout = new QHBoxLayout(themesGroup);
    themesLayout->setSpacing(16);

    auto *leftCol = new QVBoxLayout();
    leftCol->setSpacing(8);

    auto *presetHeader = sectionHeader(tr("Presets"), themesGroup);
    leftCol->addWidget(presetHeader);

    auto *presetGrid = new QGridLayout();
    presetGrid->setHorizontalSpacing(8);
    presetGrid->setVerticalSpacing(6);
    const auto &presetList = presets();
    for (size_t i = 0; i < presetList.size(); ++i) {
        const ThemePreset &preset = presetList[i];
        auto *btn = new QPushButton(tr(preset.name), themesGroup);
        btn->setCursor(Qt::PointingHandCursor);
        presetGrid->addWidget(btn, static_cast<int>(i / 2), static_cast<int>(i % 2));
        connect(btn, &QPushButton::clicked, this, [this, &preset]() {
            const QColor seed(QString::fromLatin1(preset.seed));
            if (!seed.isValid())
                return;
            seedColor = seed;
            if (seedSwatch)
                seedSwatch->setStyleSheet(swatchStyle(seedColor));
            if (schemeCombo)
                schemeCombo->setCurrentIndex(std::clamp(preset.scheme, 0, Core::Theme::schemeCount - 1));
            if (modeCombo)
                modeCombo->setCurrentIndex(preset.dark ? 0 : 1);
            generateInto(seedColor, schemeCombo ? schemeCombo->currentIndex() : preset.scheme,
                         modeCombo ? modeCombo->currentIndex() == 0 : preset.dark);
            for (const auto &[token, hex] : preset.tweaks) {
                const QColor tweak(QString::fromLatin1(hex));
                if (tweak.isValid())
                    Manager::instance().setOverride(token, tweak);
            }
            Manager::instance().apply();
            Manager::instance().save();
        });
    }
    leftCol->addLayout(presetGrid);

    auto *accentHeader = sectionHeader(tr("Accent & palette"), themesGroup);
    leftCol->addWidget(accentHeader);

    // Seed swatch + scheme + light/dark.
    seedColor = Manager::instance().color(Token::Highlight);
    auto *accentRow = new QHBoxLayout();
    accentRow->setSpacing(8);
    seedSwatch = new QPushButton(themesGroup);
    seedSwatch->setFixedSize(40, 26);
    seedSwatch->setCursor(Qt::PointingHandCursor);
    seedSwatch->setToolTip(tr("Pick an accent color — the whole palette is generated from it"));
    seedSwatch->setStyleSheet(swatchStyle(seedColor));
    accentRow->addWidget(seedSwatch);

    schemeCombo = new QComboBox(themesGroup);
    for (int i = 0; i < Core::Theme::schemeCount; ++i)
        schemeCombo->addItem(Core::Theme::schemeName(static_cast<Core::Theme::Scheme>(i)));
    accentRow->addWidget(schemeCombo, 1);

    modeCombo = new QComboBox(themesGroup);
    modeCombo->addItem(tr("Dark"));
    modeCombo->addItem(tr("Light"));
    accentRow->addWidget(modeCombo);
    leftCol->addLayout(accentRow);

    // Quick accent chips.
    auto *chipRow = new QHBoxLayout();
    chipRow->setSpacing(6);
    for (const auto &[name, hex] : accentChips()) {
        auto *chip = new QPushButton(themesGroup);
        chip->setFixedSize(20, 20);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setToolTip(tr(name));
        chip->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; border: 1px solid"
                                           " rgba(128,128,128,140); border-radius: 10px; }")
                                    .arg(QLatin1String(hex)));
        connect(chip, &QPushButton::clicked, this, [this, hex]() {
            seedColor = QColor(QString::fromLatin1(hex));
            if (seedColor.isValid()) {
                seedSwatch->setStyleSheet(swatchStyle(seedColor));
                generateInto(seedColor, schemeCombo->currentIndex(), modeCombo->currentIndex() == 0);
            }
        });
        chipRow->addWidget(chip);
    }
    chipRow->addStretch(1);
    leftCol->addLayout(chipRow);

    // Generate / randomize.
    auto *genRow = new QHBoxLayout();
    genRow->setSpacing(8);
    auto *genBtn = new QPushButton(tr("Generate theme"), themesGroup);
    auto *randBtn = new QPushButton(tr("Surprise me"), themesGroup);
    genRow->addWidget(genBtn);
    genRow->addWidget(randBtn);
    genRow->addStretch(1);
    leftCol->addLayout(genRow);
    leftCol->addStretch(1);

    themePreview = new ThemePreview(themesGroup);
    themesLayout->addLayout(leftCol, 1);
    themesLayout->addWidget(themePreview, 0, Qt::AlignTop);

    outer->addWidget(themesGroup);

    connect(seedSwatch, &QPushButton::clicked, this, [this]() {
        const QColor seed = seedColor.isValid() ? seedColor : Manager::instance().color(Token::Highlight);
        const QColor picked = QColorDialog::getColor(seed, this, tr("Accent color"));
        if (!picked.isValid())
            return;
        seedColor = picked;
        seedSwatch->setStyleSheet(swatchStyle(seedColor));
        generateInto(seedColor, schemeCombo->currentIndex(), modeCombo->currentIndex() == 0);
    });

    connect(genBtn, &QPushButton::clicked, this, [this]() {
        if (!seedColor.isValid())
            seedColor = Manager::instance().color(Token::Highlight);
        generateInto(seedColor, schemeCombo->currentIndex(), modeCombo->currentIndex() == 0);
    });

    connect(randBtn, &QPushButton::clicked, this, [this]() {
        auto *rng = QRandomGenerator::global();
        const qreal h = rng->generateDouble() * 360.0;
        const qreal s = 0.55 + rng->generateDouble() * 0.40;
        const qreal l = 0.42 + rng->generateDouble() * 0.22;
        seedColor = QColor::fromHslF(static_cast<float>(h / 360.0), static_cast<float>(s),
                                     static_cast<float>(l));
        seedSwatch->setStyleSheet(swatchStyle(seedColor));
        const int scheme = static_cast<int>(rng->generateDouble() * Core::Theme::schemeCount);
        schemeCombo->setCurrentIndex(scheme);
        generateInto(seedColor, scheme, modeCombo->currentIndex() == 0);
    });

    // ------------------------------------------------------------------
    // Palette: every color token, grouped, editable by picker or hex
    // ------------------------------------------------------------------
    auto *paletteGroup = new QGroupBox(tr("Colors"), this);
    auto *paletteLayout = new QVBoxLayout(paletteGroup);
    paletteLayout->setSpacing(6);

    QString currentGroup;
    for (const TokenDescriptor &d : Core::Theme::registry()) {
        const QString group = QString::fromUtf8(d.group);
        if (group != currentGroup) {
            currentGroup = group;
            paletteLayout->addWidget(sectionHeader(group, paletteGroup));
        }

        auto *row = new QHBoxLayout();
        row->setSpacing(10);

        auto *nameLabel = new QLabel(QString::fromUtf8(d.label), paletteGroup);
        row->addWidget(nameLabel);
        row->addStretch(1);

        auto *hexEdit = new QLineEdit(paletteGroup);
        hexEdit->setFixedWidth(96);
        hexEdit->setPlaceholderText(d.supportsAlpha ? QStringLiteral("#RRGGBBAA") : QStringLiteral("#RRGGBB"));
        row->addWidget(hexEdit);

        auto *swatch = new QPushButton(paletteGroup);
        swatch->setFixedSize(40, 24);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setToolTip(tr("Pick a color"));
        row->addWidget(swatch);

        auto *reset = new QToolButton(paletteGroup);
        reset->setText(tr("Reset"));
        reset->setToolTip(tr("Restore the default color for this token"));
        row->addWidget(reset);

        paletteLayout->addLayout(row);

        const Token token = d.token;
        tokenSwatches.insert(token, swatch);
        tokenHexEdits.insert(token, hexEdit);

        const QColor initial = Manager::instance().color(token);
        hexEdit->setText(cssColor(initial));
        swatch->setStyleSheet(swatchStyle(initial));

        connect(swatch, &QPushButton::clicked, this, [this, token]() { pickTokenColor(token); });

        connect(hexEdit, &QLineEdit::editingFinished, this, [this, token, hexEdit]() {
            applyTokenHex(token, hexEdit->text());
        });
        connect(reset, &QToolButton::clicked, this, [this, token]() {
            Manager::instance().clearOverride(token);
            Manager::instance().apply();
            Manager::instance().save();
        });
    }

    auto *paletteActions = new QHBoxLayout();
    auto *resetPalette = new QPushButton(tr("Reset all colors"), paletteGroup);
    paletteActions->addWidget(resetPalette);
    paletteActions->addStretch(1);
    paletteLayout->addLayout(paletteActions);

    connect(resetPalette, &QPushButton::clicked, this, [this]() {
        for (const TokenDescriptor &d : Core::Theme::registry())
            Manager::instance().clearOverride(d.token);
        Manager::instance().apply();
        Manager::instance().save();
    });

    outer->addWidget(paletteGroup);

    // ------------------------------------------------------------------
    // Typography
    // ------------------------------------------------------------------
    auto *fontGroup = new QGroupBox(tr("Typography"), this);
    auto *fontLayout = new QVBoxLayout(fontGroup);
    fontLayout->setSpacing(6);

    for (const Core::Theme::FontDescriptor &fd : Core::Theme::fontRegistry()) {
        const FontRole role = fd.role;

        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        row->addWidget(new QLabel(QString::fromUtf8(fd.label), fontGroup));
        row->addStretch(1);

        auto *family = new QFontComboBox(fontGroup);
        family->setCurrentFont(Manager::instance().font(role));
        row->addWidget(family);

        auto *size = new QSpinBox(fontGroup);
        size->setRange(6, 40);
        size->setSuffix(" pt");
        size->setValue(QFontInfo(Manager::instance().font(role)).pointSize());
        row->addWidget(size);

        auto *reset = new QToolButton(fontGroup);
        reset->setText(tr("Reset"));
        row->addWidget(reset);

        fontLayout->addLayout(row);

        familyCombos.insert(role, family);
        sizeSpins.insert(role, size);

        connect(family, &QFontComboBox::currentFontChanged, this, [this, role](const QFont &f) {
            QFont font = Manager::instance().font(role);
            font.setFamily(f.family());
            Manager::instance().setFontOverride(role, font);
            Manager::instance().applyFonts();
            Manager::instance().save();
        });

        connect(size, qOverload<int>(&QSpinBox::valueChanged), this, [this, role](int pt) {
            QFont font = Manager::instance().font(role);
            font.setPointSize(pt);
            Manager::instance().setFontOverride(role, font);
            Manager::instance().applyFonts();
            Manager::instance().save();
        });

        connect(reset, &QToolButton::clicked, this, [this, role]() {
            Manager::instance().clearFontOverride(role);
            Manager::instance().applyFonts();
            Manager::instance().save();
            refreshFontControls();
        });
    }

    outer->addWidget(fontGroup);

    // ------------------------------------------------------------------
    // Extra CSS: full control on top of the generated theme
    // ------------------------------------------------------------------
    auto *cssGroup = new QGroupBox(tr("Custom CSS"), this);
    auto *cssLayout = new QVBoxLayout(cssGroup);
    cssLayout->setSpacing(8);

    auto *cssHint = new QLabel(tr("Extra stylesheet applied after the generated theme. "
                                  "Usable for restyling anything the color tokens do not cover."),
                               cssGroup);
    cssHint->setWordWrap(true);
    cssHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    cssLayout->addWidget(cssHint);

    extraCssEdit = new QPlainTextEdit(cssGroup);
    extraCssEdit->setPlaceholderText(QStringLiteral("/* e.g. */\nQToolTip { border: 2px solid %1; }")
                                             .arg(Manager::instance().color(Token::Highlight).name()));
    QFont mono = extraCssEdit->font();
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setPointSizeF(std::max(8.0, mono.pointSizeF() - 0.5));
    extraCssEdit->setFont(mono);
    extraCssEdit->setPlainText(Manager::instance().extraCss());
    extraCssEdit->setMinimumHeight(110);
    cssLayout->addWidget(extraCssEdit);

    auto *cssTimer = new QTimer(cssGroup);
    cssTimer->setSingleShot(true);
    cssTimer->setInterval(350);
    connect(extraCssEdit, &QPlainTextEdit::textChanged, this, [this, cssTimer]() {
        cssTimer->start();
    });
    connect(cssTimer, &QTimer::timeout, this, [this]() {
        Manager::instance().setExtraCss(extraCssEdit->toPlainText());
        Manager::instance().apply();
    });

    auto *cssActions = new QHBoxLayout();
    cssActions->setSpacing(8);
    auto *cssImport = new QPushButton(tr("Import CSS…"), cssGroup);
    auto *cssExport = new QPushButton(tr("Export CSS…"), cssGroup);
    auto *cssClear = new QPushButton(tr("Clear"), cssGroup);
    cssActions->addWidget(cssImport);
    cssActions->addWidget(cssExport);
    cssActions->addWidget(cssClear);
    cssActions->addStretch(1);
    cssLayout->addLayout(cssActions);

    connect(cssImport, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import CSS"), QString(),
                                                          tr("Style sheets (*.qss *.css);;All files (*)"));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return;
        const QString css = QString::fromUtf8(f.readAll());
        f.close();
        extraCssEdit->setPlainText(css);
        Manager::instance().setExtraCss(css);
        Manager::instance().apply();
    });

    connect(cssExport, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export CSS"), QStringLiteral("theme.qss"),
                                                          tr("Style sheets (*.qss *.css)"));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        f.write(extraCssEdit->toPlainText().toUtf8());
        f.close();
    });

    connect(cssClear, &QPushButton::clicked, this, [this]() {
        extraCssEdit->clear();
        Manager::instance().setExtraCss(QString());
        Manager::instance().apply();
    });

    outer->addWidget(cssGroup);

    // ------------------------------------------------------------------
    // Channel list style
    // ------------------------------------------------------------------
    auto *channelGroup = new QGroupBox(tr("Channel list"), this);
    auto *channelLayout = new QHBoxLayout(channelGroup);
    channelLayout->addWidget(new QLabel(tr("Style:"), channelGroup));
    auto *channelListCombo = new QComboBox(channelGroup);
    channelListCombo->addItem(tr("Tree"));
    channelListCombo->addItem(tr("Classic"));
    channelListCombo->setCurrentIndex(
            QSettings().value("ui/channelListMode").toString() == "classic" ? 1 : 0);
    channelLayout->addWidget(channelListCombo, 1);
    outer->addWidget(channelGroup);

    connect(channelListCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const bool classic = index == 1;
                QSettings().setValue("ui/channelListMode", classic ? "classic" : "tree");
                emit channelListModeChanged(classic);
            });

    // ------------------------------------------------------------------
    // Messages
    // ------------------------------------------------------------------
    auto *messageGroup = new QGroupBox(tr("Messages"), this);
    auto *messageLayout = new QVBoxLayout(messageGroup);
    messageLayout->setSpacing(6);

    auto addMessageToggle = [this, messageGroup, messageLayout](const QString &text, bool checked,
                                                  std::function<void(bool)> onToggle) {
        auto *check = new QCheckBox(text, messageGroup);
        check->setChecked(checked);
        messageLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [onToggle](bool on) { onToggle(on); });
    };

    addMessageToggle(tr("Compact mode"), QSettings().value("ui/compactMessages", false).toBool(),
                     [this](bool compact) {
                         QSettings().setValue("ui/compactMessages", compact);
                         emit compactModeChanged(compact);
                     });
    addMessageToggle(tr("Always show timestamps"),
                     QSettings().value("ui/showTimestamps", false).toBool(),
                     [this](bool enabled) {
                         QSettings().setValue("ui/showTimestamps", enabled);
                         emit showTimestampsChanged(enabled);
                     });
    addMessageToggle(tr("Compact input bar"), QSettings().value("ui/compactInput", false).toBool(),
                     [this](bool compact) {
                         QSettings().setValue("ui/compactInput", compact);
                         emit compactInputChanged(compact);
                     });
    addMessageToggle(tr("Numbered unread badges"),
                     Core::Appearance::AppearanceConfig::instance().numberedUnread(),
                     [](bool on) {
                         Core::Appearance::AppearanceConfig::instance().setNumberedUnread(on);
                     });
    outer->addWidget(messageGroup);

    // ------------------------------------------------------------------
    // Window
    // ------------------------------------------------------------------
    auto *windowGroup = new QGroupBox(tr("Window"), this);
    auto *windowLayout = new QHBoxLayout(windowGroup);
    auto *customTitleBarToggle = new QCheckBox(tr("Custom title bar (macOS-style buttons)"), windowGroup);
    customTitleBarToggle->setChecked(Core::Appearance::AppearanceConfig::instance().customTitleBar());
    windowLayout->addWidget(customTitleBarToggle);
    windowLayout->addStretch(1);
    outer->addWidget(windowGroup);
    connect(customTitleBarToggle, &QCheckBox::toggled, this, [](bool on) {
        Core::Appearance::AppearanceConfig::instance().setCustomTitleBar(on);
    });

    // ------------------------------------------------------------------
    // Corners
    // ------------------------------------------------------------------
    auto *cornersGroup = new QGroupBox(tr("Corners"), this);
    auto *cornersLayout = new QHBoxLayout(cornersGroup);
    cornersLayout->addWidget(new QLabel(tr("Corner radius:"), cornersGroup));
    auto *roundnessSpin = new QSpinBox(cornersGroup);
    roundnessSpin->setRange(0, 48);
    roundnessSpin->setSuffix(" px");
    roundnessSpin->setValue(Manager::instance().roundness());
    cornersLayout->addWidget(roundnessSpin);
    auto *roundnessReset = new QToolButton(cornersGroup);
    roundnessReset->setText(tr("Reset"));
    cornersLayout->addWidget(roundnessReset);
    cornersLayout->addStretch(1);
    outer->addWidget(cornersGroup);

    connect(roundnessSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [](int px) { Manager::instance().setRoundness(px); });
    connect(roundnessReset, &QToolButton::clicked, this, [roundnessSpin]() {
        Manager::instance().setRoundness(Manager::kDefaultRoundness);
        roundnessSpin->setValue(Manager::instance().roundness());
    });

    // ------------------------------------------------------------------
    // Motion
    // ------------------------------------------------------------------
    auto *motionGroup = new QGroupBox(tr("Motion"), this);
    auto *motionLayout = new QFormLayout(motionGroup);
    motionLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto *speedSlider = new QSlider(Qt::Horizontal, motionGroup);
    speedSlider->setRange(0, 3);
    speedSlider->setSingleStep(1);
    speedSlider->setPageStep(1);
    speedSlider->setTickPosition(QSlider::TicksBelow);
    speedSlider->setTickInterval(1);
    const float currentSpeed = Core::AnimationConfig::instance().speed();
    speedSlider->setValue(currentSpeed <= 0.6f ? 0
                          : currentSpeed <= 1.4f ? 1
                          : currentSpeed <= 2.4f ? 2
                                                 : 3);
    motionLayout->addRow(tr("Animation speed:"), speedSlider);

    auto *speedHint = new QLabel(tr("Slow  ·  Normal  ·  Fast  ·  Turbo"), motionGroup);
    speedHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    motionLayout->addRow(QString(), speedHint);

    auto *reduceMotionCheck = new QCheckBox(tr("Reduce motion (disable transitions)"), motionGroup);
    reduceMotionCheck->setChecked(Core::AnimationConfig::instance().reduceMotion());
    motionLayout->addRow(reduceMotionCheck);
    outer->addWidget(motionGroup);

    connect(speedSlider, &QSlider::valueChanged, this, [](int index) {
        const float speed = index == 0 ? 0.5f : index == 1 ? 1.0f : index == 2 ? 1.75f : 2.5f;
        Core::AnimationConfig::instance().setSpeed(speed);
    });
    connect(reduceMotionCheck, &QCheckBox::toggled, this, [](bool on) {
        Core::AnimationConfig::instance().setReduceMotion(on);
    });

    // ------------------------------------------------------------------
    // Member list
    // ------------------------------------------------------------------
    auto *memberListGroup = new QGroupBox(tr("Member list"), this);
    auto *memberListLayout = new QHBoxLayout(memberListGroup);
    auto *slideOutToggle = new QCheckBox(tr("Slide-out member list (overlay)"), memberListGroup);
    slideOutToggle->setChecked(Core::Appearance::AppearanceConfig::instance().memberListMode()
                               == Core::Appearance::MemberListMode::SlideOut);
    memberListLayout->addWidget(slideOutToggle);
    memberListLayout->addStretch(1);
    outer->addWidget(memberListGroup);

    connect(slideOutToggle, &QCheckBox::toggled, this, [](bool on) {
        Core::Appearance::AppearanceConfig::instance().setMemberListMode(
                on ? Core::Appearance::MemberListMode::SlideOut
                   : Core::Appearance::MemberListMode::ResizeHandle);
    });

    // ------------------------------------------------------------------
    // Scaling
    // ------------------------------------------------------------------
    auto *scalingGroup = new QGroupBox(tr("Scaling"), this);
    auto *scalingLayout = new QFormLayout(scalingGroup);
    scalingLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    scalingLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    scalingLayout->setVerticalSpacing(8);

    auto addScaleRow = [this, scalingGroup, scalingLayout](const QString &label, float initial,
                                                           std::function<void(float)> setter,
                                                           std::pair<float, float> range = { 0, 0 }) {
        auto *stepper = new ScaleStepper(scalingGroup);
        if (range.first < range.second)
            stepper->setRange(range.first, range.second);
        stepper->setValue(initial);
        scalingLayout->addRow(label, stepper);
        connect(stepper, &ScaleStepper::valueChanged, this, [setter](float v) { setter(v); });
    };

    addScaleRow(tr("Member cards:"),
                Core::Appearance::AppearanceConfig::instance().memberCardScale(),
                [](float v) { Core::Appearance::AppearanceConfig::instance().setMemberCardScale(v); });
    addScaleRow(tr("Guild icons:"),
                Core::Appearance::AppearanceConfig::instance().guildIconScale(),
                [](float v) { Core::Appearance::AppearanceConfig::instance().setGuildIconScale(v); });
    addScaleRow(tr("Channel list:"),
                Core::Appearance::AppearanceConfig::instance().channelScale(),
                [](float v) { Core::Appearance::AppearanceConfig::instance().setChannelScale(v); },
                { Core::Appearance::AppearanceConfig::kChannelMinScale,
                  Core::Appearance::AppearanceConfig::kChannelMaxScale });
    outer->addWidget(scalingGroup);

    // ------------------------------------------------------------------
    // Bottom actions
    // ------------------------------------------------------------------
    auto *actions = new QHBoxLayout();
    auto *resetAll = new QPushButton(tr("Reset theme"), this);
    resetAll->setToolTip(tr("Restore the default palette, typography and custom CSS"));
    actions->addWidget(resetAll);
    actions->addStretch(1);
    auto *exportBtn = new QPushButton(tr("Export theme…"), this);
    auto *importBtn = new QPushButton(tr("Import theme…"), this);
    actions->addWidget(exportBtn);
    actions->addWidget(importBtn);
    outer->addLayout(actions);

    connect(resetAll, &QPushButton::clicked, this, [this]() {
        Manager::instance().resetAll();
        Manager::instance().setExtraCss(QString());
        Manager::instance().apply();
        Manager::instance().applyFonts();
        Manager::instance().save();
        extraCssEdit->clear();
        refreshFontControls();
        if (schemeCombo)
            schemeCombo->setCurrentIndex(0);
        if (modeCombo)
            modeCombo->setCurrentIndex(0);
    });

    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export theme"),
                                                          QStringLiteral("theme.json"),
                                                          tr("Theme files (*.json)"));
        if (!path.isEmpty())
            Manager::instance().exportTo(path);
    });

    connect(importBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import theme"), QString(),
                                                          tr("Theme files (*.json)"));
        if (path.isEmpty())
            return;
        if (Manager::instance().importFrom(path)) {
            Manager::instance().apply();
            Manager::instance().applyFonts();
            Manager::instance().save();
            extraCssEdit->setPlainText(Manager::instance().extraCss());
            refreshFontControls();
        }
    });

    outer->addStretch(1);

    // Keep every control in sync with the Manager, no matter which widget
    // triggered the change (picker, hex field, preset, generate, import).
    connect(&Manager::instance(), &Manager::themeChanged, this, [this]() {
        refreshTokenControls();
        refreshAccentSeed();
        if (themePreview)
            themePreview->update();
    });
    connect(&Manager::instance(), &Manager::metricsChanged, this, [this]() {
        if (themePreview)
            themePreview->update();
    });
}

void AppearancePage::refreshTokenControls()
{
    for (auto it = tokenSwatches.constBegin(); it != tokenSwatches.constEnd(); ++it) {
        const Token token = it.key();
        const QColor color = Manager::instance().color(token);

        if (QPushButton *swatch = it.value())
            swatch->setStyleSheet(swatchStyle(color));

        if (QLineEdit *edit = tokenHexEdits.value(token)) {
            QSignalBlocker block(edit);
            edit->setText(cssColor(color));
        }
    }
}

void AppearancePage::refreshFontControls()
{
    for (auto it = familyCombos.constBegin(); it != familyCombos.constEnd(); ++it) {
        const FontRole role = it.key();
        const QFont f = Manager::instance().font(role);

        QSignalBlocker familyBlock(it.value());
        it.value()->setCurrentFont(f);

        if (QSpinBox *size = sizeSpins.value(role)) {
            QSignalBlocker sizeBlock(size);
            size->setValue(QFontInfo(f).pointSize());
        }
    }
}

void AppearancePage::refreshAccentSeed()
{
    const QColor accent = Manager::instance().color(Token::Highlight);
    seedColor = accent;
    if (seedSwatch)
        seedSwatch->setStyleSheet(swatchStyle(seedColor));
}

void AppearancePage::generateInto(const QColor &seed, int schemeIndex, bool dark)
{
    const auto scheme = static_cast<Core::Theme::Scheme>(
            std::clamp(schemeIndex, 0, Core::Theme::schemeCount - 1));
    Manager::instance().setOverrides(Core::Theme::generate(seed, scheme, dark));
    Manager::instance().apply();
    Manager::instance().save();
}

void AppearancePage::pickTokenColor(Token token)
{
    const TokenDescriptor &d = Core::Theme::descriptor(token);
    QColorDialog::ColorDialogOptions opts;
    if (d.supportsAlpha)
        opts |= QColorDialog::ShowAlphaChannel;

    QColor seed = Manager::instance().color(token);
    if (!seed.isValid())
        seed = Qt::white;

    const QColor picked = QColorDialog::getColor(seed, this, QString::fromUtf8(d.label), opts);
    if (!picked.isValid())
        return;

    Manager::instance().setOverride(token, picked);
    Manager::instance().apply();
    Manager::instance().save();
}

void AppearancePage::applyTokenHex(Token token, const QString &hex)
{
    const QString normalized = normalizeHex(hex);
    if (normalized.isEmpty()) {
        // Revert the field to the current resolved color when unparseable.
        QSignalBlocker block(tokenHexEdits.value(token));
        tokenHexEdits.value(token)->setText(cssColor(Manager::instance().color(token)));
        return;
    }

    const QColor color(normalized);
    if (color == Manager::instance().color(token))
        return;

    Manager::instance().setOverride(token, color);
    Manager::instance().apply();
    Manager::instance().save();
}

} // namespace UI
} // namespace Acheron
