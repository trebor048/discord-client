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
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace Acheron {
namespace UI {

using Core::Theme::FontRole;
using Core::Theme::Manager;
using Core::Theme::Token;
using Core::Theme::TokenDescriptor;

namespace {
QString swatchStyle(const QColor &c)
{
    return QStringLiteral(
                   "QPushButton { background-color: %1; border: 1px solid #888888; "
                   "border-radius: 3px; }")
            .arg(c.name(QColor::HexRgb));
}
} // namespace

AppearancePage::AppearancePage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(14);

    seedColor = Manager::instance().color(Token::Highlight);

    auto *genGroup = new QGroupBox(tr("Generate from a color"), this);
    auto *genLayout = new QFormLayout(genGroup);
    genLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    genLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    genLayout->setVerticalSpacing(10);

    // Row: base color + scheme + mode
    auto *baseRow = new QHBoxLayout();
    baseRow->setSpacing(10);
    seedSwatch = new QPushButton(genGroup);
    seedSwatch->setFixedSize(52, 26);
    seedSwatch->setCursor(Qt::PointingHandCursor);
    seedSwatch->setStyleSheet(swatchStyle(seedColor));
    seedSwatch->setToolTip(tr("Pick a base color"));
    baseRow->addWidget(seedSwatch);

    auto *schemeCombo = new QComboBox(genGroup);
    for (int i = 0; i < Core::Theme::schemeCount; ++i)
        schemeCombo->addItem(Core::Theme::schemeName(static_cast<Core::Theme::Scheme>(i)));
    baseRow->addWidget(schemeCombo, 1);

    auto *modeCombo = new QComboBox(genGroup);
    modeCombo->addItem(tr("Dark"));
    modeCombo->addItem(tr("Light"));
    baseRow->addWidget(modeCombo);
    genLayout->addRow(tr("Base:"), baseRow);

    // Row: actions
    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(10);
    auto *genBtn = new QPushButton(tr("Generate"), genGroup);
    auto *randBtn = new QPushButton(tr("Randomize"), genGroup);
    actionRow->addWidget(genBtn);
    actionRow->addWidget(randBtn);
    actionRow->addStretch(1);
    genLayout->addRow(QString(), actionRow);

    outer->addWidget(genGroup);

    auto *layoutGroup = new QGroupBox(tr("Channel list"), this);
    auto *layoutGroupLayout = new QHBoxLayout(layoutGroup);
    layoutGroupLayout->addWidget(new QLabel(tr("Style:"), layoutGroup));
    auto *channelListCombo = new QComboBox(layoutGroup);
    channelListCombo->addItem(tr("Tree")); // 0
    channelListCombo->addItem(tr("Classic")); // 1
    channelListCombo->setCurrentIndex(QSettings().value("ui/channelListMode").toString() == "classic" ? 1 : 0);
    layoutGroupLayout->addWidget(channelListCombo, 1);
    outer->addWidget(layoutGroup);

    connect(channelListCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                bool classic = index == 1;
                QSettings().setValue("ui/channelListMode", classic ? "classic" : "tree");
                emit channelListModeChanged(classic);
            });

    auto *messageGroup = new QGroupBox(tr("Messages"), this);
    auto *messageLayout = new QHBoxLayout(messageGroup);
    auto *compactToggle = new QCheckBox(tr("Compact mode"), messageGroup);
    compactToggle->setChecked(QSettings().value("ui/compactMessages", false).toBool());
    messageLayout->addWidget(compactToggle);
    auto *timestampsToggle = new QCheckBox(tr("Always show timestamps"), messageGroup);
    timestampsToggle->setChecked(QSettings().value("ui/showTimestamps", false).toBool());
    messageLayout->addWidget(timestampsToggle);
    auto *compactInputToggle = new QCheckBox(tr("Compact input bar"), messageGroup);
    compactInputToggle->setChecked(QSettings().value("ui/compactInput", false).toBool());
    messageLayout->addWidget(compactInputToggle);
    auto *numberedUnreadToggle = new QCheckBox(tr("Numbered unread badges"), messageGroup);
    numberedUnreadToggle->setChecked(Core::Appearance::AppearanceConfig::instance().numberedUnread());
    messageLayout->addWidget(numberedUnreadToggle);
    messageLayout->addStretch(1);
    outer->addWidget(messageGroup);

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

    // --- Motion (global animation preferences) ---
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
    speedSlider->setValue(currentSpeed <= 0.6f ? 0 : currentSpeed <= 1.4f ? 1 : currentSpeed <= 2.4f ? 2 : 3);
    motionLayout->addRow(tr("Animation speed:"), speedSlider);

    auto *speedHint = new QLabel(tr("Slow  ·  Normal  ·  Fast  ·  Turbo"), motionGroup);
    speedHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    motionLayout->addRow(QString(), speedHint);

    auto *reduceMotionCheck = new QCheckBox(tr("Reduce motion (disable transitions)"), motionGroup);
    reduceMotionCheck->setChecked(Core::AnimationConfig::instance().reduceMotion());
    motionLayout->addRow(reduceMotionCheck);

    outer->addWidget(motionGroup);

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

    auto *scalingGroup = new QGroupBox(tr("Scaling"), this);
    auto *scalingLayout = new QFormLayout(scalingGroup);
    scalingLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    scalingLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    scalingLayout->setVerticalSpacing(10);

    auto addScaleRow = [this, scalingGroup, scalingLayout](const QString &label, float initial,
                                                           std::function<void(float)> setter) {
        auto *stepper = new ScaleStepper(scalingGroup);
        stepper->setValue(initial);
        scalingLayout->addRow(label, stepper);
        connect(stepper, &ScaleStepper::valueChanged, this, [setter](float v) { setter(v); });
        return stepper;
    };

    addScaleRow(tr("Member cards:"),
                Core::Appearance::AppearanceConfig::instance().memberCardScale(),
                [](float v) { Core::Appearance::AppearanceConfig::instance().setMemberCardScale(v); });
    addScaleRow(tr("Guild icons:"),
                Core::Appearance::AppearanceConfig::instance().guildIconScale(),
                [](float v) { Core::Appearance::AppearanceConfig::instance().setGuildIconScale(v); });
    auto *channelStepper = addScaleRow(tr("Channel list:"),
                                       Core::Appearance::AppearanceConfig::instance().channelScale(),
                                       [](float v) { Core::Appearance::AppearanceConfig::instance().setChannelScale(v); });
    // The channel list scales over a much wider range than member/guild icons.
    channelStepper->setRange(Core::Appearance::AppearanceConfig::kChannelMinScale,
                             Core::Appearance::AppearanceConfig::kChannelMaxScale);

    outer->addWidget(scalingGroup);

    connect(speedSlider, &QSlider::valueChanged, this, [](int index) {
        const float speed = index == 0 ? 0.5f : index == 1 ? 1.0f : index == 2 ? 1.75f : 2.5f;
        Core::AnimationConfig::instance().setSpeed(speed);
    });
    connect(reduceMotionCheck, &QCheckBox::toggled, this, [](bool on) {
        Core::AnimationConfig::instance().setReduceMotion(on);
    });

    connect(compactToggle, &QCheckBox::toggled, this, [this](bool compact) {
        QSettings().setValue("ui/compactMessages", compact);
        emit compactModeChanged(compact);
    });
    connect(timestampsToggle, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue("ui/showTimestamps", enabled);
        emit showTimestampsChanged(enabled);
    });
    connect(compactInputToggle, &QCheckBox::toggled, this, [this](bool compact) {
        QSettings().setValue("ui/compactInput", compact);
        emit compactInputChanged(compact);
    });
    connect(numberedUnreadToggle, &QCheckBox::toggled, this, [](bool on) {
        Core::Appearance::AppearanceConfig::instance().setNumberedUnread(on);
    });

    connect(seedSwatch, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(seedColor, this, tr("Base color"));
        if (!picked.isValid())
            return;
        seedColor = picked;
        seedSwatch->setStyleSheet(swatchStyle(seedColor));
    });

    connect(genBtn, &QPushButton::clicked, this, [this, schemeCombo, modeCombo]() {
        generateInto(seedColor, schemeCombo->currentIndex(), modeCombo->currentIndex() == 0);
    });

    connect(randBtn, &QPushButton::clicked, this, [this, schemeCombo, modeCombo]() {
        auto *rng = QRandomGenerator::global();
        const qreal h = rng->generateDouble() * 360.0;
        const qreal s = 0.55 + rng->generateDouble() * 0.40; // 0.55..0.95
        const qreal l = 0.45 + rng->generateDouble() * 0.20; // 0.45..0.65
        seedColor = QColor::fromHslF(static_cast<float>(h / 360.0), static_cast<float>(s), static_cast<float>(l));
        seedSwatch->setStyleSheet(swatchStyle(seedColor));
        const int scheme = static_cast<int>(rng->generateDouble() * Core::Theme::schemeCount);
        schemeCombo->setCurrentIndex(scheme);
        generateInto(seedColor, scheme, modeCombo->currentIndex() == 0);
    });

    // Fonts & colors live directly in the page layout — the SettingsWindow
    // wrapper scroll area handles overflow. A nested QScrollArea here would
    // fight the wrapper (crushing this section to near-zero height).
    auto *fontsHeader = new QLabel(tr("Fonts"), this);
    {
        QFont hf = fontsHeader->font();
        hf.setBold(true);
        fontsHeader->setFont(hf);
    }
    outer->addWidget(fontsHeader);

    for (const Core::Theme::FontDescriptor &fd : Core::Theme::fontRegistry()) {
        const FontRole role = fd.role;

        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        row->addWidget(new QLabel(QString::fromUtf8(fd.label), this));
        row->addStretch(1);

        auto *family = new QFontComboBox(this);
        family->setCurrentFont(Manager::instance().font(role));
        row->addWidget(family);

        auto *size = new QSpinBox(this);
        size->setRange(6, 40);
        size->setSuffix(" pt");
        size->setValue(QFontInfo(Manager::instance().font(role)).pointSize());
        row->addWidget(size);

        auto *reset = new QToolButton(this);
        reset->setText(tr("Reset"));
        row->addWidget(reset);

        outer->addLayout(row);

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

    QString currentGroup;
    for (const TokenDescriptor &d : Core::Theme::registry()) {
        const QString group = QString::fromUtf8(d.group);
        if (group != currentGroup) {
            currentGroup = group;
            if (outer->count() > 1)
                outer->addSpacing(8);
            auto *header = new QLabel(group, this);
            QFont f = header->font();
            f.setBold(true);
            header->setFont(f);
            outer->addWidget(header);
        }

        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        row->addWidget(new QLabel(QString::fromUtf8(d.label), this));
        row->addStretch(1);

        auto *swatch = new QPushButton(this);
        swatch->setFixedSize(48, 22);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setStyleSheet(swatchStyle(Manager::instance().color(d.token)));
        row->addWidget(swatch);

        auto *reset = new QToolButton(this);
        reset->setText(tr("Reset"));
        row->addWidget(reset);

        outer->addLayout(row);

        const Token token = d.token;
        const bool supportsAlpha = d.supportsAlpha;
        const QString title = QString::fromUtf8(d.label);
        swatches.insert(token, swatch);

        connect(swatch, &QPushButton::clicked, this,
                [this, token, supportsAlpha, title, swatch]() {
                    QColorDialog::ColorDialogOptions opts;
                    if (supportsAlpha)
                        opts |= QColorDialog::ShowAlphaChannel;
                    const QColor picked = QColorDialog::getColor(Manager::instance().color(token), this, title, opts);
                    if (!picked.isValid())
                        return;
                    Manager::instance().setOverride(token, picked);
                    Manager::instance().apply();
                    Manager::instance().save();
                    swatch->setStyleSheet(swatchStyle(picked));
                });

        connect(reset, &QToolButton::clicked, this, [this, token, swatch]() {
            Manager::instance().clearOverride(token);
            Manager::instance().apply();
            Manager::instance().save();
            swatch->setStyleSheet(swatchStyle(Manager::instance().color(token)));
        });
    }

    outer->addStretch(1);

    auto *actions = new QHBoxLayout();
    auto *resetAll = new QPushButton(tr("Reset all"), this);
    auto *exportBtn = new QPushButton(tr("Export"), this);
    auto *importBtn = new QPushButton(tr("Import"), this);
    actions->addWidget(resetAll);
    actions->addStretch(1);
    actions->addWidget(exportBtn);
    actions->addWidget(importBtn);
    outer->addLayout(actions);

    connect(resetAll, &QPushButton::clicked, this, [this]() {
        Manager::instance().resetAll();
        Manager::instance().apply();
        Manager::instance().applyFonts();
        Manager::instance().save();
        rebuildSwatches();
        refreshFontControls();
    });

    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
                this, tr("Export theme"), "theme.json",
                tr("Theme files (*.json)"));
        if (!path.isEmpty())
            Manager::instance().exportTo(path);
    });

    connect(importBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import theme"), QString(), tr("Theme files (*.json)"));
        if (path.isEmpty())
            return;
        if (Manager::instance().importFrom(path)) {
            Manager::instance().apply();
            Manager::instance().applyFonts();
            Manager::instance().save();
            rebuildSwatches();
            refreshFontControls();
        }
    });
}

void AppearancePage::rebuildSwatches()
{
    for (auto it = swatches.begin(); it != swatches.end(); ++it) {
        const Token token = it.key();
        it.value()->setStyleSheet(swatchStyle(Manager::instance().color(token)));
    }
}

void AppearancePage::refreshFontControls()
{
    for (auto it = familyCombos.begin(); it != familyCombos.end(); ++it) {
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

void AppearancePage::generateInto(const QColor &seed, int schemeIndex, bool dark)
{
    const auto scheme = static_cast<Core::Theme::Scheme>(std::clamp(schemeIndex, 0, Core::Theme::schemeCount - 1));
    Manager::instance().setOverrides(Core::Theme::generate(seed, scheme, dark));
    Manager::instance().apply();
    Manager::instance().save();
    rebuildSwatches();
}

} // namespace UI
} // namespace Acheron
