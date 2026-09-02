#include "GeneralPage.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "UI/Widgets/CustomStatusEdit.hpp"
#include "UI/Dialogs/EmojiPickerDialog.hpp"
#include "UI/Chat/ChatLayout.hpp"
#include "Core/EmojiCatalog.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Settings.hpp"

namespace Acheron {
namespace UI {

namespace {

// Keep the hover bar narrow enough for typical message rows.
constexpr int MaxQuickReactions = 10;

QStringList currentQuickReactions()
{
    const QStringList stored =
            QSettings().value(QStringLiteral("chat/quick_reactions")).toStringList();
    if (stored.isEmpty())
        return ChatLayout::defaultQuickReactionEmojis();
    return stored.mid(0, MaxQuickReactions);
}

void saveQuickReactions(const QStringList &emojis)
{
    QSettings().setValue(QStringLiteral("chat/quick_reactions"), emojis);
}

} // namespace

GeneralPage::GeneralPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // Edit Profile button at top
    auto *profileRow = new QHBoxLayout();
    editProfileBtn = new QPushButton(tr("Edit Profile"), this);
    profileRow->addWidget(editProfileBtn);
    profileRow->addStretch(1);
    layout->addLayout(profileRow);
    connect(editProfileBtn, &QPushButton::clicked, this, &GeneralPage::editProfileRequested);

    auto *generalGroup = new QGroupBox(tr("General"), this);
    auto *generalLayout = new QVBoxLayout(generalGroup);
    generalLayout->setSpacing(12);

    inMemoryCacheCheckbox = new QCheckBox(tr("In-memory cache database (requires restart)"), generalGroup);
    inMemoryCacheCheckbox->setChecked(QSettings().value("general/in_memory_cache", false).toBool());
    generalLayout->addWidget(inMemoryCacheCheckbox);

    notificationSoundsCheckbox = new QCheckBox(tr("Play a sound for new messages outside the current channel"), generalGroup);
    notificationSoundsCheckbox->setChecked(QSettings().value("notifications/sounds", true).toBool());
    generalLayout->addWidget(notificationSoundsCheckbox);

    developerModeCheckbox = new QCheckBox(tr("Developer Mode"), generalGroup);
    developerModeCheckbox->setChecked(Core::Settings::instance().developerMode());
    generalLayout->addWidget(developerModeCheckbox);

    silentTypingCheckbox = new QCheckBox(tr("Silent typing (don't send typing indicators)"), generalGroup);
    silentTypingCheckbox->setChecked(QSettings().value("chat/silentTyping", false).toBool());
    silentTypingCheckbox->setToolTip(tr("When enabled, other users won't see a \"typing…\" indicator "
                                        "while you are composing a message."));
    generalLayout->addWidget(silentTypingCheckbox);

    layout->addWidget(generalGroup);

    auto *tabsGroup = new QGroupBox(tr("Tabs & Status"), this);
    auto *tabsGroupLayout = new QVBoxLayout(tabsGroup);
    tabsGroupLayout->setSpacing(12);

    auto *tabsLayout = new QFormLayout();
    tabsLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    tabsLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    newTabBehaviorCombo = new QComboBox(tabsGroup);
    newTabBehaviorCombo->addItem(tr("Open channel picker"), "picker");
    newTabBehaviorCombo->addItem(tr("Duplicate current channel"), "duplicate");
    const QString newTabBehavior = QSettings().value("ui/newTabBehavior", "picker").toString();
    int newTabIdx = newTabBehaviorCombo->findData(newTabBehavior);
    if (newTabIdx < 0)
        newTabIdx = 0;
    newTabBehaviorCombo->setCurrentIndex(newTabIdx);
    tabsLayout->addRow(tr("When opening a new tab:"), newTabBehaviorCombo);
    tabsGroupLayout->addLayout(tabsLayout);

    auto *statusLayout = new QFormLayout();
    statusLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    statusLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    customStatusWidget = new CustomStatusEdit(tabsGroup);
    statusLayout->addRow(tr("Custom status"), customStatusWidget);
    tabsGroupLayout->addLayout(statusLayout);

    layout->addWidget(tabsGroup);

    auto *mediaGroup = new QGroupBox(tr("Media"), this);
    auto *mediaLayout = new QVBoxLayout(mediaGroup);
    mediaLayout->setSpacing(12);

    autoplayGifsCheckbox = new QCheckBox(tr("Autoplay GIFs"), mediaGroup);
    autoplayGifsCheckbox->setChecked(QSettings().value("ui/gifAutoplay", true).toBool());
    mediaLayout->addWidget(autoplayGifsCheckbox);

    autoplayVideosCheckbox = new QCheckBox(tr("Autoplay Videos"), mediaGroup);
    autoplayVideosCheckbox->setChecked(QSettings().value("ui/videoAutoplay", true).toBool());
    mediaLayout->addWidget(autoplayVideosCheckbox);

    layout->addWidget(mediaGroup);

    // Quick reactions: the emoji row shown on the message hover bar.
    auto *chatGroup = new QGroupBox(tr("Chat"), this);
    auto *chatGroupLayout = new QVBoxLayout(chatGroup);
    chatGroupLayout->setSpacing(10);

    auto *quickReactionHint = new QLabel(tr("Emoji shown on the quick-reaction bar when you hover a "
                                            "message. Click an emoji to change it, right-click to "
                                            "remove it."), chatGroup);
    quickReactionHint->setWordWrap(true);
    quickReactionHint->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;")
                                             .arg(palette().placeholderText().color().name()));
    chatGroupLayout->addWidget(quickReactionHint);

    quickReactionRowLayout = new QHBoxLayout();
    quickReactionRowLayout->setSpacing(6);

    auto *addButton = new QPushButton(QStringLiteral("+"), chatGroup);
    addButton->setFixedSize(32, 32);
    addButton->setToolTip(tr("Add a quick reaction"));
    addButton->setCursor(Qt::PointingHandCursor);
    QColor addHover = palette().highlight().color();
    addHover.setAlpha(45);
    addButton->setStyleSheet(QStringLiteral(
            "QPushButton { border: 1px dashed %1; border-radius: 8px;"
            "  background: transparent; color: %2; font-size: 16px; font-weight: 600; }"
            "QPushButton:hover { background-color: %3; }")
            .arg(palette().mid().color().name(), palette().text().color().name(),
                 addHover.name(QColor::HexArgb)));
    connect(addButton, &QPushButton::clicked, this, &GeneralPage::addQuickReaction);
    quickReactionRowLayout->addWidget(addButton);

    auto *resetButton = new QPushButton(tr("Reset"), chatGroup);
    resetButton->setCursor(Qt::PointingHandCursor);
    resetButton->setToolTip(tr("Restore the default quick reactions"));
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        QSettings().remove(QStringLiteral("chat/quick_reactions"));
        rebuildQuickReactionRow();
    });
    quickReactionRowLayout->addWidget(resetButton);
    quickReactionRowLayout->addStretch(1);

    chatGroupLayout->addLayout(quickReactionRowLayout);
    layout->addWidget(chatGroup);

    rebuildQuickReactionRow();

    layout->addStretch();

    connect(inMemoryCacheCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue("general/in_memory_cache", checked);
    });
    connect(notificationSoundsCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue("notifications/sounds", checked);
        emit notificationSoundsChanged(checked);
    });
    connect(developerModeCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        Core::Settings::instance().setDeveloperMode(checked);
    });
    connect(silentTypingCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue("chat/silentTyping", checked);
    });
    connect(autoplayGifsCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings().setValue("ui/gifAutoplay", checked);
        // The running ImageManager caches this flag; invalidate so the toggle
        // applies immediately (new loads + resume-from-pause pick it up).
        Core::ImageManager::invalidateGifAutoplayCache();
    });
    connect(autoplayVideosCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings().setValue("ui/videoAutoplay", checked);
    });
    connect(newTabBehaviorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        QSettings().setValue("ui/newTabBehavior", newTabBehaviorCombo->currentData().toString());
        emit newTabBehaviorChanged();
    });
    connect(customStatusWidget, &CustomStatusEdit::statusChanged, this, [this](const QString &text, const QString &, qint64) {
        emit customStatusChanged(text);
    });
    connect(customStatusWidget, &CustomStatusEdit::statusCleared, this, [this]() {
        emit customStatusChanged(QString());
    });
}

void GeneralPage::rebuildQuickReactionRow()
{
    for (QPushButton *button : std::as_const(quickEmojiButtons)) {
        quickReactionRowLayout->removeWidget(button);
        button->deleteLater();
    }
    quickEmojiButtons.clear();

    const QStringList emojis = currentQuickReactions();
    for (int i = 0; i < emojis.size(); ++i) {
        auto *button = new QPushButton(QString(), this);
        button->setFixedSize(32, 32);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tr("Click to change · Right-click to remove"));
        QColor hoverFill = palette().highlight().color();
        hoverFill.setAlpha(45);
        button->setStyleSheet(QStringLiteral(
                "QPushButton { border: 1px solid %1; border-radius: 8px;"
                "  background: transparent; font-size: 17px; }"
                "QPushButton:hover { background-color: %2; }")
                .arg(palette().mid().color().name(), hoverFill.name(QColor::HexArgb)));
        connect(button, &QPushButton::clicked, this, [this, i]() { changeQuickReaction(i); });
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QPushButton::customContextMenuRequested, this,
                [this, i]() { removeQuickReaction(i); });

        // Custom emoji tokens (`<:name:id>` / `<a:name:id>`) render as the
        // actual image instead of the raw token text; Unicode emoji render as
        // their glyph.
        const QString url = Core::EmojiCatalog::cdnUrlForSelection(emojis[i], 32);
        if (url.isEmpty()) {
            button->setText(emojis[i]);
        } else {
            const QSize iconSize(24, 24);
            button->setIconSize(iconSize);
            if (imageManager) {
                QPixmap pix = imageManager->getIfCached(QUrl(url), iconSize);
                if (!pix.isNull()) {
                    button->setIcon(QIcon(pix));
                } else {
                    // Icon not cached yet: start the fetch and remember the
                    // url so imageFetched can fill the button in.
                    imageManager->get(QUrl(url), iconSize);
                    button->setProperty("quickReactionUrl", url);
                }
            }
        }

        // Emoji slots go before the "+" / Reset controls at the end of the row.
        quickReactionRowLayout->insertWidget(quickEmojiButtons.size(), button);
        quickEmojiButtons.append(button);
    }
}

void GeneralPage::setImageManager(Core::ImageManager *mgr)
{
    imageManager = mgr;
    if (imageManager) {
        connect(imageManager, &Core::ImageManager::imageFetched, this,
                [this](const QUrl &url, const QSize &, const QPixmap &pixmap) {
                    // Fill any quick-reaction button waiting on this emoji.
                    const QString urlStr = url.toString();
                    for (QPushButton *button : std::as_const(quickEmojiButtons)) {
                        if (button->property("quickReactionUrl").toString() == urlStr) {
                            button->setIcon(QIcon(pixmap));
                            button->setProperty("quickReactionUrl", QString());
                        }
                    }
                });
    }
    rebuildQuickReactionRow();
}

void GeneralPage::changeQuickReaction(int index)
{
    QStringList emojis = currentQuickReactions();
    if (index < 0 || index >= emojis.size())
        return;
    const QString chosen = pickEmoji(this, tr("Change Quick Reaction"), tr("Search emoji"));
    if (chosen.isEmpty())
        return;
    emojis[index] = chosen;
    saveQuickReactions(emojis);
    rebuildQuickReactionRow();
}

void GeneralPage::removeQuickReaction(int index)
{
    QStringList emojis = currentQuickReactions();
    if (index < 0 || index >= emojis.size())
        return;
    emojis.removeAt(index);
    if (emojis.isEmpty()) {
        QSettings().remove(QStringLiteral("chat/quick_reactions"));
    } else {
        saveQuickReactions(emojis);
    }
    rebuildQuickReactionRow();
}

void GeneralPage::addQuickReaction()
{
    QStringList emojis = currentQuickReactions();
    if (emojis.size() >= MaxQuickReactions)
        return;
    const QString chosen = pickEmoji(this, tr("Add Quick Reaction"), tr("Search emoji"));
    if (chosen.isEmpty())
        return;
    emojis.append(chosen);
    saveQuickReactions(emojis);
    rebuildQuickReactionRow();
}

void GeneralPage::setClient(Discord::Client *c)
{
    customStatusWidget->setClient(c);
}

} // namespace UI
} // namespace Acheron
