#include "EmojiPickerDialog.hpp"
#include "VirtualEmojiGrid.hpp"

#include "Core/AnimationUtils.hpp"
#include "Core/Theme/Manager.hpp"
#include "EmojiPreferences.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDialogButtonBox>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <limits>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

namespace Acheron {
namespace UI {

namespace {
QString guildHeaderText(const QList<Core::EmojiCatalogItem> &items)
{
    if (items.isEmpty())
        return QStringLiteral("Unknown Server");
    const QString name = items.first().guildName.trimmed();
    return name.isEmpty() ? QStringLiteral("Unknown Server") : name;
}

constexpr int kDialogMinWidth = 720;
// Raw GIF payloads are tens of KB to several MB each, so bound the cache by
// total bytes rather than entry count to keep memory usage predictable.
constexpr qsizetype kGifCacheMaxBytes = 32 * 1024 * 1024;
constexpr int kStaticIconCacheMaxEntries = 1000;
constexpr int kMaxDisplayedSearchResults = 200;

// Evict entries from gifCache until the total byte budget (minus the entry
// that was just inserted) fits within kGifCacheMaxBytes.
void pruneGifCache(QHash<QString, QByteArray> &cache, const QString &newKey)
{
    qsizetype totalBytes = 0;
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it)
        totalBytes += it.value().size();
    while (totalBytes > kGifCacheMaxBytes && cache.size() > 1) {
        auto it = cache.begin();
        if (it.key() == newKey)
            ++it;
        totalBytes -= it.value().size();
        cache.erase(it);
    }
}

// Glyph rendering is deterministic, so rendered Unicode emoji pixmaps are
// cached (keyed by emoji text + size + device pixel ratio) and reused across
// rebuilds/scroll recycling instead of re-drawing ~200-264 pixmaps per
// search-rebuild. Custom emoji keep their own fetch caches below.
constexpr int kUnicodeGlyphCacheMaxEntries = 512;

QHash<QString, QIcon> &unicodeGlyphCache()
{
    static QHash<QString, QIcon> cache;
    return cache;
}

QIcon renderUnicodeEmojiIcon(const QString &emojiText, int size)
{
    const qreal dpr = qApp->devicePixelRatio();
    const QString key = QStringLiteral("%1|%2|%3").arg(emojiText).arg(size).arg(dpr);

    QHash<QString, QIcon> &cache = unicodeGlyphCache();
    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPixelSize(size - 4);
    p.setFont(f);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, emojiText);
    p.end();
    QIcon icon(pix);

    if (cache.size() >= kUnicodeGlyphCacheMaxEntries && !cache.contains(key))
        cache.erase(cache.begin());
    cache.insert(key, icon);
    return icon;
}

} // namespace

EmojiPickerDialog::~EmojiPickerDialog()
{
    // Persist the picker's last position and size so the next open returns to
    // the same spot (mirrors the MainWindow layout/geometry pattern). Runs for
    // every construction site — stack dialogs in ChatView/pickEmoji and the
    // WA_DeleteOnClose dialog in CustomStatusEdit — because they all funnel
    // through this destructor. Only save when the dialog was actually shown:
    // an unshown dialog still has a default geometry that would overwrite the
    // user's saved spot.
    if (wasShown) {
        QSettings settings;
        settings.setValue(QStringLiteral("emojiPicker/geometry"), saveGeometry());
    }

    // Clean up hover state before member destruction to prevent eventFilter
    // from accessing destroyed hashes during widget teardown.
    for (auto it = hoveredMovies.begin(); it != hoveredMovies.end(); ++it) {
        disconnect(it.key(), nullptr, this, nullptr);
        it.value()->stop();
        it.value()->deleteLater();
    }
    hoveredMovies.clear();
    animatedButtons.clear();

    for (auto *reply : pendingIconRequests) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    pendingIconRequests.clear();
}

void EmojiPickerDialog::showEvent(QShowEvent *event)
{
    wasShown = true;
    QDialog::showEvent(event);
}

void EmojiPickerDialog::fadeInWidget(QWidget *w, int durationMs)
{
    Acheron::Core::AnimationUtils::fadeIn(w, durationMs);
}

EmojiPickerDialog::EmojiPickerDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Emoji Picker"));
    setModal(true);
    setMinimumSize(kDialogMinWidth, 520);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);

    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(QStringLiteral("emojiSearchEdit"));
    searchEdit->setPlaceholderText(tr("Search emoji"));
    searchEdit->installEventFilter(this);
    outer->addWidget(searchEdit);

    sectionTabs = new QTabWidget(this);
    sectionTabs->setObjectName(QStringLiteral("emojiCategoryTabs"));
    sectionTabs->addTab(new QWidget(sectionTabs), tr("All"));
    sectionTabs->addTab(new QWidget(sectionTabs), tr("Recents"));
    sectionTabs->addTab(new QWidget(sectionTabs), tr("Favorites"));
    sectionTabs->addTab(new QWidget(sectionTabs), tr("Server"));
    // The Server tab is the default view; the initial rebuildResults() at the
    // end of the constructor picks this up.
    sectionTabs->setCurrentIndex(static_cast<int>(Section::Server));
    outer->addWidget(sectionTabs);

    // Category quick-jump bar
    auto *categoryBar = new QWidget(this);
    auto *categoryBarLayout = new QHBoxLayout(categoryBar);
    categoryBarLayout->setContentsMargins(0, 0, 0, 0);
    categoryBarLayout->setSpacing(2);
    for (const auto &category : Core::EmojiCatalog::categoryNames()) {
        auto *btn = new QToolButton(categoryBar);
        btn->setText(category);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setStyleSheet(QStringLiteral(
                "QToolButton { border: 1px solid palette(mid); border-radius: 4px; "
                "padding: 2px 6px; font-size: 11px; }"
                "QToolButton:hover { background: palette(base); }"));
        const QString cat = category;
        connect(btn, &QToolButton::clicked, this, [this, cat]() {
            scrollToCategory(cat);
        });
        categoryBarLayout->addWidget(btn);
    }
    categoryBarLayout->addStretch();
    outer->addWidget(categoryBar);

    resultsList = new QListWidget(this);
    resultsList->setObjectName(QStringLiteral("emojiResultsList"));
    resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsList->setUniformItemSizes(true);
    resultsList->setAlternatingRowColors(true);
    resultsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    resultsList->setIconSize(QSize(24, 24));
    outer->addWidget(resultsList, 1);

    // Category grid (virtualized via a fixed widget pool + scroll recycling)
    categoryScrollArea = new QScrollArea(this);
    categoryScrollArea->setWidgetResizable(false);
    categoryScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categoryGrid = new VirtualEmojiGrid(categoryScrollArea);
    categoryGrid->attachScrollArea(categoryScrollArea);
    categoryGrid->setIconApplicator([this](QToolButton *button, const Core::EmojiCatalogItem &item) {
        applyIconToButton(button, item);
    });
    connect(categoryGrid, &VirtualEmojiGrid::itemClicked, this,
            [this](const QString &value) { setSelectedEmojiValue(value); });
    connect(categoryGrid, &VirtualEmojiGrid::itemContextMenuRequested, this,
            [this](QToolButton *button, const Core::EmojiCatalogItem &item) {
                showSkinTonePicker(button, item);
            });
    categoryScrollArea->setWidget(categoryGrid);
    categoryScrollArea->hide();

    // Sticky category header overlay (LOW #4)
    categoryStickyHeader = new QLabel(categoryScrollArea->viewport());
    categoryStickyHeader->setVisible(false);
    categoryStickyHeader->setGeometry(0, 0, categoryScrollArea->viewport()->width(), 28);
    QFont stickyFont = categoryStickyHeader->font();
    stickyFont.setBold(true);
    stickyFont.setPointSize(stickyFont.pointSize() + 1);
    categoryStickyHeader->setFont(stickyFont);
    categoryStickyHeader->setStyleSheet(
            "QLabel { background: palette(window); border-bottom: 1px solid palette(mid); "
            "padding: 6px 8px; }");
    categoryStickyHeader->setFixedHeight(28);
    categoryStickyHeader->raise();
    connect(categoryScrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &EmojiPickerDialog::updateCategoryStickyHeader);
    // Resize tracking: update header width when viewport resizes
    categoryScrollArea->viewport()->installEventFilter(this);

    outer->addWidget(categoryScrollArea, 1);

    serverScrollArea = new QScrollArea(this);
    serverScrollArea->setWidgetResizable(false);
    serverScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    serverGrid = new VirtualEmojiGrid(serverScrollArea);
    serverGrid->attachScrollArea(serverScrollArea);
    serverGrid->setIconApplicator([this](QToolButton *button, const Core::EmojiCatalogItem &item) {
        applyIconToButton(button, item);
    });
    connect(serverGrid, &VirtualEmojiGrid::itemClicked, this,
            [this](const QString &value) { setSelectedEmojiValue(value); });
    // Server grid is custom-emoji only: no skin-tone context menu.
    serverScrollArea->setWidget(serverGrid);
    serverScrollArea->hide();
    outer->addWidget(serverScrollArea, 1);

    auto *footer = new QWidget(this);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(6);

    favoriteButton = new QPushButton(tr("Favorite"), footer);
    favoriteButton->setObjectName(QStringLiteral("emojiFavoriteButton"));
    favoriteButton->setCheckable(true);
    favoriteButton->setEnabled(false);
    footerLayout->addWidget(favoriteButton);

    // Skin tone picker button
    skinToneButton = new QToolButton(footer);
    skinToneButton->setText(QStringLiteral("\U0001F3FB"));
    skinToneButton->setToolTip(tr("Skin tone"));
    skinToneButton->setFixedSize(28, 28);
    skinToneButton->setPopupMode(QToolButton::InstantPopup);
    skinToneButton->setStyleSheet(QStringLiteral(
            "QToolButton { border: 1px solid palette(mid); border-radius: 4px; font-size: 14px; }"
            "QToolButton:hover { background: palette(base); }"));
    auto *skinToneMenu = new QMenu(skinToneButton);
    const QStringList skinToneLabels = {
            tr("Default"), QStringLiteral("\U0001F3FB"), QStringLiteral("\U0001F3FC"),
            QStringLiteral("\U0001F3FD"), QStringLiteral("\U0001F3FE"), QStringLiteral("\U0001F3FF")};
    const QStringList skinToneCodes = {
            QStringLiteral(""), QStringLiteral("\U0001F3FB"), QStringLiteral("\U0001F3FC"),
            QStringLiteral("\U0001F3FD"), QStringLiteral("\U0001F3FE"), QStringLiteral("\U0001F3FF")};
    for (int i = 0; i < skinToneLabels.size(); ++i) {
        QAction *act = skinToneMenu->addAction(skinToneLabels[i]);
        const int tone = i;
        connect(act, &QAction::triggered, this, [this, tone, skinToneLabels]() {
            setSkinTone(tone);
            skinToneButton->setText(skinToneLabels[tone]);
        });
    }
    skinToneButton->setMenu(skinToneMenu);
    footerLayout->addWidget(skinToneButton);

    footerLayout->addStretch(1);

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, footer);
    useButton = buttonBox->addButton(tr("Use"), QDialogButtonBox::AcceptRole);
    useButton->setEnabled(false);
    footerLayout->addWidget(buttonBox);
    outer->addWidget(footer);

    // Search debounce timer
    searchDebounceTimer.setSingleShot(true);
    searchDebounceTimer.setInterval(200);
    connect(&searchDebounceTimer, &QTimer::timeout, this, &EmojiPickerDialog::onSearchDebounced);

    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        pendingSearchText = text;
        searchDebounceTimer.start();
    });
    connect(sectionTabs, &QTabWidget::currentChanged, this, &EmojiPickerDialog::rebuildResults);
    connect(resultsList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) {
                if (!isServerSectionActive()) {
                    useButton->setEnabled(resultsList->currentItem() != nullptr);
                    updateFavoriteState();
                }
            });
    connect(resultsList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { acceptCurrent(); });
    connect(resultsList, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *) { acceptCurrent(); });
    connect(useButton, &QAbstractButton::clicked, this, &EmojiPickerDialog::acceptCurrent);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(favoriteButton, &QAbstractButton::clicked, this, &EmojiPickerDialog::toggleFavorite);

    // Reopen where the user last left the picker: restore the saved position
    // and size (saved in the destructor). Must happen before the dialog is
    // shown, so restoring here in the constructor covers every construction
    // site.
    QSettings settings;
    const QByteArray savedGeometry =
            settings.value(QStringLiteral("emojiPicker/geometry")).toByteArray();
    if (!savedGeometry.isEmpty())
        restoreGeometry(savedGeometry);

    nam = new QNetworkAccessManager(this);
    rebuildResults();
    searchEdit->setFocus();
}

void EmojiPickerDialog::setSearchPlaceholder(const QString &text)
{
    searchEdit->setPlaceholderText(text);
}

void EmojiPickerDialog::setOrderedGuildIds(const QStringList &guildIds)
{
    if (orderedGuildIds == guildIds)
        return;
    orderedGuildIds = guildIds;
    rebuildServerSectionIfStale();
}

void EmojiPickerDialog::setCurrentGuildId(const QString &guildId)
{
    if (currentGuildId == guildId)
        return;
    currentGuildId = guildId;
    rebuildServerSectionIfStale();
}

// The constructor runs rebuildResults() before callers can push the guild
// ordering, so the Server grid is initially built with empty ordering state.
// When ordering arrives later while the Server tab is showing, rebuild it.
void EmojiPickerDialog::rebuildServerSectionIfStale()
{
    if (!isServerSectionActive())
        return;
    rebuildResults();
}

void EmojiPickerDialog::onSearchDebounced()
{
    // Replace the pending search text immediately into rebuildResults
    rebuildResults();
}

void EmojiPickerDialog::setSkinTone(int toneIndex)
{
    QSettings settings;
    settings.setValue(QStringLiteral("emoji/skinTone"), toneIndex);
}

int EmojiPickerDialog::currentSkinTone() const
{
    QSettings settings;
    return settings.value(QStringLiteral("emoji/skinTone"), 0).toInt();
}

QString EmojiPickerDialog::applySkinTone(const QString &emoji, int toneIndex) const
{
    if (toneIndex <= 0 || emoji.isEmpty())
        return emoji;

    // Skin tone modifiers: U+1F3FB through U+1F3FF
    static const QStringList kSkinTones = {
            QString(), // default
            QStringLiteral("\U0001F3FB"),
            QStringLiteral("\U0001F3FC"),
            QStringLiteral("\U0001F3FD"),
            QStringLiteral("\U0001F3FE"),
            QStringLiteral("\U0001F3FF")};

    if (toneIndex < 1 || toneIndex >= kSkinTones.size())
        return emoji;

    const QString modifier = kSkinTones[toneIndex];
    if (modifier.isEmpty())
        return emoji;

    // For multi-codepoint emoji (e.g. ZWJ sequences), insert the modifier
    // after the first codepoint if it is an emoji modifier base.
    // For simple single-emoji, just append the modifier.
    if (emoji.length() <= 2) {
        return emoji + modifier;
    }

    // For complex emoji, try inserting after first character
    QString result;
    result.reserve(emoji.size() + modifier.size());
    if (emoji[0].isHighSurrogate() && emoji.size() > 1 && emoji[1].isLowSurrogate()) {
        result.append(emoji[0]);
        result.append(emoji[1]);
        result.append(modifier);
        result.append(emoji.mid(2));
    } else {
        result.append(emoji[0]);
        result.append(modifier);
        result.append(emoji.mid(1));
    }
    return result;
}

void EmojiPickerDialog::scrollToCategory(const QString &categoryName)
{
    if (isServerSectionActive())
        return; // Not applicable for server section

    if (categoryGrid)
        categoryGrid->scrollToSection(categoryName.toCaseFolded());
}

void EmojiPickerDialog::updateCategoryStickyHeader()
{
    if (!categoryStickyHeader || !categoryScrollArea->isVisible() || !categoryGrid)
        return;

    const int scrollPos = categoryScrollArea->verticalScrollBar()->value();
    const QString currentSection = categoryGrid->sectionKeyAtTop(scrollPos);
    if (currentSection.isEmpty()) {
        categoryStickyHeader->setVisible(false);
        return;
    }

    // Convert internal key to display name
    QString displayName = currentSection;
    if (displayName == QStringLiteral("recents"))
        displayName = tr("Frequently Used");
    else if (displayName == QStringLiteral("guild"))
        displayName = tr("Guild Emoji");
    else if (!displayName.isEmpty())
        displayName[0] = displayName[0].toUpper();

    categoryStickyHeader->setText(displayName);
    categoryStickyHeader->setFixedWidth(categoryScrollArea->viewport()->width());
    categoryStickyHeader->setVisible(true);
    categoryStickyHeader->raise();
}

void EmojiPickerDialog::clearCategoryGrid()
{
    if (categoryGrid)
        categoryGrid->clear();

    // Drop any grid selection so it doesn't leak into the results list
    selectedServerEmoji.clear();
    useButton->setEnabled(false);

    // Clean up hover state for grid buttons before they are recycled
    for (auto it = hoveredMovies.begin(); it != hoveredMovies.end(); ++it) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    hoveredMovies.clear();
    animatedButtons.clear();
}

void EmojiPickerDialog::rebuildCategoryGrid(const QList<Core::EmojiCatalogItem> &items)
{
    clearCategoryGrid();

    // Group by category. Custom guild emoji are pulled into their own section
    // (below) so they are exposed from the shared picker surface, not just the
    // Server tab, instead of being lumped into the generic "other" bucket.
    QHash<QString, QList<Core::EmojiCatalogItem>> grouped;
    QList<Core::EmojiCatalogItem> guildEmoji;
    for (const auto &item : items) {
        if (item.isCustom()) {
            guildEmoji.append(item);
        } else {
            const QString cat = item.category.isEmpty() ? QStringLiteral("other") : item.category.toCaseFolded();
            grouped[cat].append(item);
        }
    }
    QStringList categories = grouped.keys();
    categories.sort();

    // Shared stylesheet for all emoji grid buttons
    const int gridR = Core::Theme::Manager::instance().roundness();
    const QString kGridButtonStyle =
            QStringLiteral(
                    "QToolButton[emojiGridButton=\"true\"] { border: 1px solid transparent; border-radius: %1px; padding: 2px; }"
                    "QToolButton[emojiGridButton=\"true\"]:hover { border-color: palette(mid); background: palette(base); }"
                    "QToolButton[emojiGridButton=\"true\"]:checked { border-color: palette(highlight); background: palette(highlight); }")
                    .arg(gridR);
    categoryGrid->setStyleSheet(kGridButtonStyle);

    QList<EmojiGridSection> sections;

    // Show recently used at top
    const auto recents = EmojiPreferences::recents();
    if (!recents.isEmpty()) {
        EmojiGridSection recentsSection;
        recentsSection.key = QStringLiteral("recents");
        recentsSection.header = tr("Recently Used");
        for (const auto &value : recents) {
            const auto item = itemForValue(value);
            if (item.name.isEmpty())
                continue;
            recentsSection.items.append(item);
        }
        if (!recentsSection.items.isEmpty())
            sections.append(recentsSection);
    }

    // Render each category section
    for (const auto &category : categories) {
        const auto catKey = category.toCaseFolded();
        const auto catItems = grouped.value(catKey);
        if (catItems.isEmpty())
            continue;

        EmojiGridSection section;
        section.key = catKey;
        section.header = category;
        section.items = catItems;
        sections.append(section);
    }

    // Guild emoji: all custom emoji the session has registered. Reuses the
    // grid's shared click/select path (itemClicked -> setSelectedEmojiValue),
    // so insertion of `:name:` / `<a:name:id>` works exactly like the Server
    // tab.
    if (!guildEmoji.isEmpty()) {
        std::sort(guildEmoji.begin(), guildEmoji.end(), [](const auto &a, const auto &b) {
            return a.name.toCaseFolded() < b.name.toCaseFolded();
        });
        EmojiGridSection guildSection;
        guildSection.key = QStringLiteral("guild");
        guildSection.header = tr("Guild Emoji");
        guildSection.items = guildEmoji;
        sections.append(guildSection);
    }

    if (categoryGrid)
        categoryGrid->setSections(sections);

    // Fade in the category grid after layout
    fadeInWidget(categoryGrid, 200);
}

void EmojiPickerDialog::showSkinTonePicker(QToolButton *button, const Core::EmojiCatalogItem &item)
{
    if (item.isCustom())
        return;

    // Only show skin tone picker for emoji that support skin tones
    // (emoji modifier bases — we check the first codepoint)
    if (item.unicodeEmoji.isEmpty())
        return;

    const QString &text = item.unicodeEmoji;
    const QChar first = text.at(0);
    const char32_t firstCp = (first.isHighSurrogate() && text.size() > 1 && text.at(1).isLowSurrogate())
            ? QChar::surrogateToUcs4(first, text.at(1))
            : first.unicode();

    // Check if it's an emoji modifier base (simplified check)
    bool isModifierBase = (firstCp == 0x261D || firstCp == 0x26F9 ||
                           (firstCp >= 0x270A && firstCp <= 0x270D) || firstCp == 0x1F385 ||
                           (firstCp >= 0x1F3C2 && firstCp <= 0x1F3C4) || firstCp == 0x1F3C7 ||
                           (firstCp >= 0x1F3CA && firstCp <= 0x1F3CC) ||
                           (firstCp >= 0x1F442 && firstCp <= 0x1F443) ||
                           (firstCp >= 0x1F446 && firstCp <= 0x1F450) ||
                           (firstCp >= 0x1F466 && firstCp <= 0x1F478) || firstCp == 0x1F47C ||
                           (firstCp >= 0x1F481 && firstCp <= 0x1F483) ||
                           (firstCp >= 0x1F485 && firstCp <= 0x1F487) || firstCp == 0x1F48F ||
                           firstCp == 0x1F491 || firstCp == 0x1F4AA ||
                           (firstCp >= 0x1F574 && firstCp <= 0x1F575) || firstCp == 0x1F57A ||
                           firstCp == 0x1F590 || (firstCp >= 0x1F595 && firstCp <= 0x1F596) ||
                           (firstCp >= 0x1F645 && firstCp <= 0x1F647) ||
                           (firstCp >= 0x1F64B && firstCp <= 0x1F64F) || firstCp == 0x1F6A3 ||
                           (firstCp >= 0x1F6B4 && firstCp <= 0x1F6B6) || firstCp == 0x1F6C0 ||
                           firstCp == 0x1F6CC || firstCp == 0x1F90C || firstCp == 0x1F90F ||
                           (firstCp >= 0x1F918 && firstCp <= 0x1F91F) || firstCp == 0x1F926 ||
                           (firstCp >= 0x1F930 && firstCp <= 0x1F939) ||
                           (firstCp >= 0x1F93C && firstCp <= 0x1F93E));

    if (!isModifierBase)
        return;

    auto *menu = new QMenu(button);
    const QStringList skinToneLabels = {
            tr("Default"), QStringLiteral("\U0001F3FB \U0001F3FB"),
            QStringLiteral("\U0001F3FC \U0001F3FC"), QStringLiteral("\U0001F3FD \U0001F3FD"),
            QStringLiteral("\U0001F3FE \U0001F3FE"), QStringLiteral("\U0001F3FF \U0001F3FF")};

    for (int i = 0; i < skinToneLabels.size(); ++i) {
        QAction *act = menu->addAction(skinToneLabels[i]);
        const int tone = i;
        connect(act, &QAction::triggered, this, [this, item, tone]() {
            const QString modified = applySkinTone(item.unicodeEmoji, tone);
            Core::EmojiSelectionValue sel;
            sel.raw = modified;
            sel.isCustom = false;
            sel.name = item.name;
            currentSelection = sel;
            EmojiPreferences::addRecent(modified);
            emit emojiSelected(modified);
            accept();
        });
    }

    menu->exec(QCursor::pos());
    menu->deleteLater();
}

void EmojiPickerDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool EmojiPickerDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == searchEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            acceptCurrent();
            return true;
        }
    }

    // Update sticky header width when category viewport is resized (LOW #4)
    if (watched == categoryScrollArea->viewport() && event->type() == QEvent::Resize) {
        if (categoryStickyHeader && categoryStickyHeader->isVisible()) {
            categoryStickyHeader->setFixedWidth(
                    static_cast<QResizeEvent *>(event)->size().width());
        }
        return false;
    }

    auto *btn = qobject_cast<QToolButton *>(watched);
    if (btn && animatedButtons.contains(btn)) {
        if (event->type() == QEvent::Enter) {
            const QString emojiValue = btn->property("emojiValue").toString();
            if (emojiValue.isEmpty() || !gifCache.contains(emojiValue))
                return false;

            // Stop and dispose any previous movie for this button before
            // creating a new one; repeated Enter events would otherwise leak.
            QMovie *oldMovie = hoveredMovies.take(btn);
            if (oldMovie) {
                oldMovie->stop();
                oldMovie->deleteLater();
            }

            auto *movie = new QMovie(this);
            auto *buf = new QBuffer(movie);
            buf->setData(gifCache.value(emojiValue));
            buf->open(QIODevice::ReadOnly);
            movie->setDevice(buf);
            movie->setFormat(QByteArrayLiteral("gif"));
            movie->setScaledSize(QSize(24, 24));

            if (!movie->isValid()) {
                delete movie;
                return false;
            }

            hoveredMovies.insert(btn, movie);
            movie->start();

            connect(movie, &QMovie::frameChanged, this, [this, btn](int) {
                QMovie *m = hoveredMovies.value(btn);
                if (!m)
                    return;
                btn->setIcon(QIcon(QPixmap::fromImage(m->currentImage())));
            });
            return false;
        }

        if (event->type() == QEvent::Leave) {
            QMovie *movie = hoveredMovies.take(btn);
            if (movie) {
                movie->stop();
                movie->deleteLater();
            }
            const QString emojiValue = btn->property("emojiValue").toString();
            if (gifCache.contains(emojiValue)) {
                QPixmap first;
                if (first.loadFromData(gifCache.value(emojiValue))) {
                    first = first.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    btn->setIcon(QIcon(first));
                }
            }
            return false;
        }
    }

    return QDialog::eventFilter(watched, event);
}

const QList<Core::EmojiCatalogItem> &EmojiPickerDialog::itemsForSection(Section section) const
{
    // The All section uses the catalog's cached list directly — no copy.
    if (section == Section::All)
        return Core::EmojiCatalog::items();

    static thread_local QList<Core::EmojiCatalogItem> items;
    items.clear();
    switch (section) {
    case Section::All:
        Q_UNREACHABLE();
        break;
    case Section::Recents: {
        for (const auto &value : EmojiPreferences::recents())
            items.append(itemForValue(value));
        break;
    }
    case Section::Favorites: {
        for (const auto &value : EmojiPreferences::favorites())
            items.append(itemForValue(value));
        break;
    }
    case Section::Server:
        items = Core::EmojiCatalog::customEmojis();
        break;
    }

    items.erase(std::remove_if(items.begin(), items.end(),
                               [](const Core::EmojiCatalogItem &item) { return item.name.isEmpty(); }),
                items.end());
    return items;
}

void EmojiPickerDialog::loadCustomEmojiIcon(QListWidgetItem *row, const Core::EmojiCatalogItem &item)
{
    if (!row || !item.isCustom() || item.customId.isEmpty())
        return;

    const QString value = item.selectionValue();

    // Fast path: already cached.
    if (item.animated && gifCache.contains(value)) {
        QPixmap first;
        if (first.loadFromData(gifCache.value(value)))
            row->setIcon(QIcon(first));
        return;
    }
    if (!item.animated && staticEmojiIconCache.contains(value)) {
        row->setIcon(staticEmojiIconCache.value(value));
        return;
    }

    requestCustomIconFetch(item, value);
}

void EmojiPickerDialog::applyIconToButton(QToolButton *button, const Core::EmojiCatalogItem &item)
{
    if (!button)
        return;

    // Reset any hover/animated state left over from a previous (recycled) cell.
    QMovie *movie = hoveredMovies.take(button);
    if (movie) {
        movie->stop();
        movie->deleteLater();
    }
    animatedButtons.remove(button);
    // Unconditionally drop any stale event filter so recycled buttons never
    // accumulate duplicate animated-emoji filters across reuses.
    button->removeEventFilter(this);

    button->setText(QString());
    button->setIcon(QIcon());

    if (!item.isCustom()) {
        // Icon size is already reset to the button's native default by the
        // grid before this applicator runs, matching the original rendering.
        button->setIcon(renderUnicodeEmojiIcon(item.unicodeEmoji, EmojiGridMetrics::kIconSize));
        return;
    }

    const QString value = item.selectionValue();
    if (item.animated) {
        if (gifCache.contains(value)) {
            QPixmap first;
            if (first.loadFromData(gifCache.value(value))) {
                first = first.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                button->setIcon(QIcon(first));
                button->setIconSize(QSize(24, 24));
                animatedButtons.insert(button);
                button->installEventFilter(this);
                return;
            }
        }
    } else if (staticEmojiIconCache.contains(value)) {
        button->setIcon(staticEmojiIconCache.value(value));
        button->setIconSize(QSize(24, 24));
        return;
    }

    // Not yet fetched: show a subtle loading placeholder.
    button->setText(QStringLiteral("\u2022"));
    requestCustomIconFetch(item, value);
}

void EmojiPickerDialog::requestCustomIconFetch(const Core::EmojiCatalogItem &item, const QString &value)
{
    if (emojiFetchPending.contains(value))
        return; // already queued or in flight
    emojiFetchPending.insert(value);

    if (!iconFetchSemaphore.tryAcquire()) {
        PendingEmojiFetch job;
        job.item = item;
        job.value = value;
        pendingEmojiFetches.append(job);
        return;
    }

    startCustomIconFetch(item, value);
}

void EmojiPickerDialog::startCustomIconFetch(const Core::EmojiCatalogItem &item, const QString &value)
{
    const bool animated = item.animated;
    QNetworkReply *reply = nam->get(QNetworkRequest(QUrl(item.cdnUrl(animated ? 64 : 32))));
    pendingIconRequests.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, value, animated]() {
        if (!pendingIconRequests.remove(reply))
            return;
        iconFetchSemaphore.release();
        drainPendingEmojiFetches();

        const bool ok = (reply->error() == QNetworkReply::NoError);
        const QByteArray data = ok ? reply->readAll() : QByteArray();
        reply->deleteLater();

        if (!ok) {
            // A failed fetch must not park the value in emojiFetchPending
            // forever (which leaves the "•" placeholder up indefinitely).
            // Drop it so the next render re-attempts a bounded fetch instead
            // of stalling permanently; no automatic retry is scheduled here.
            emojiFetchPending.remove(value);
            return;
        }

        if (animated) {
            pruneGifCache(gifCache, value);
            gifCache[value] = data;
        } else {
            QPixmap pix;
            if (!pix.loadFromData(data)) {
                // Decode failure must also drop the value from the pending
                // set (like the network-error path above), or the cell stays
                // stuck on the placeholder until the next rebuild.
                emojiFetchPending.remove(value);
                return;
            }
            // The static icon cache previously grew without bound; cap it so
            // long sessions in emoji-heavy guilds don't accumulate icons.
            if (staticEmojiIconCache.size() >= kStaticIconCacheMaxEntries
                && !staticEmojiIconCache.contains(value))
                staticEmojiIconCache.erase(staticEmojiIconCache.begin());
            staticEmojiIconCache.insert(value, QIcon(pix));
        }

        refreshVisibleCustomEmoji(value);
    });
}

void EmojiPickerDialog::drainPendingEmojiFetches()
{
    while (!pendingEmojiFetches.isEmpty() && iconFetchSemaphore.tryAcquire()) {
        const PendingEmojiFetch job = pendingEmojiFetches.takeFirst();
        startCustomIconFetch(job.item, job.value);
    }
}

void EmojiPickerDialog::refreshVisibleCustomEmoji(const QString &value)
{
    if (serverGrid)
        serverGrid->refreshValue(value);
    if (categoryGrid)
        categoryGrid->refreshValue(value);

    if (resultsList) {
        for (int i = 0; i < resultsList->count(); ++i) {
            QListWidgetItem *row = resultsList->item(i);
            if (!row || row->data(Qt::UserRole).toString() != value)
                continue;
            if (staticEmojiIconCache.contains(value)) {
                row->setIcon(staticEmojiIconCache.value(value));
            } else if (gifCache.contains(value)) {
                QPixmap first;
                if (first.loadFromData(gifCache.value(value)))
                    row->setIcon(QIcon(first));
            }
        }
    }
}

QList<Core::EmojiCatalogItem> EmojiPickerDialog::filterItems(
        const QList<Core::EmojiCatalogItem> &items, const QString &query) const
{
    const QString needle = query.trimmed().toCaseFolded();
    if (needle.isEmpty())
        return items;

    struct ScoredItem {
        Core::EmojiCatalogItem item;
        QString foldedName;
        int score; // 0 = exact, 1 = prefix, 2 = other
    };

    QList<ScoredItem> scored;
    scored.reserve(items.size());
    QSet<QString> seen; // deduplicate by selectionValue
    for (const auto &item : items) {
        const QString val = item.selectionValue();
        if (seen.contains(val))
            continue;
        const QString foldedName = item.name.toCaseFolded();
        if (!foldedName.contains(needle))
            continue;
        seen.insert(val);
        int score = 2;
        if (foldedName == needle)
            score = 0;
        else if (foldedName.startsWith(needle))
            score = 1;
        scored.append({ item, foldedName, score });
    }

    std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
        if (a.score != b.score)
            return a.score < b.score;
        return a.foldedName < b.foldedName;
    });

    QList<Core::EmojiCatalogItem> filtered;
    filtered.reserve(scored.size());
    for (const auto &s : scored)
        filtered.append(s.item);
    return filtered;
}

Core::EmojiCatalogItem EmojiPickerDialog::itemForValue(const QString &value) const
{
    if (const auto selection = Core::EmojiCatalog::selectionForRaw(value)) {
        if (selection->isCustom) {
            if (const auto item = Core::EmojiCatalog::itemForCustomId(selection->customId))
                return *item;
            Core::EmojiCatalogItem item;
            item.name = selection->name;
            item.customId = selection->customId;
            item.animated = selection->animated;
            return item;
        }
    }

    if (const auto *item = Core::EmojiCatalog::itemForUnicode(value))
        return *item;
    return {};
}

bool EmojiPickerDialog::isServerSectionActive() const
{
    return static_cast<Section>(sectionTabs->currentIndex()) == Section::Server;
}

void EmojiPickerDialog::clearServerGrid()
{
    qDeleteAll(hoveredMovies);
    hoveredMovies.clear();
    animatedButtons.clear();
    if (serverGrid)
        serverGrid->clear();
    selectedServerEmoji.clear();

    // Do NOT clear gifCache — animated emoji data is reusable across tab switches.
    // Cache eviction is handled on insertion via pruneGifCache().
}

void EmojiPickerDialog::setSelectedEmojiValue(const QString &emojiValue)
{
    selectedServerEmoji = emojiValue;

    if (isServerSectionActive()) {
        if (serverGrid)
            serverGrid->selectValue(emojiValue);
    } else if (categoryGrid) {
        categoryGrid->selectValue(emojiValue);
    }

    if (useButton)
        useButton->setEnabled(!selectedServerEmoji.isEmpty());
    updateFavoriteState();
}

QString EmojiPickerDialog::currentSelectedEmojiValue() const
{
    // A grid selection (server tab or the All-tab category grid) always takes
    // precedence - both are handled through setSelectedEmojiValue().
    if (!selectedServerEmoji.isEmpty())
        return selectedServerEmoji;

    QListWidgetItem *item = resultsList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void EmojiPickerDialog::rebuildServerGrid(const QList<Core::EmojiCatalogItem> &items)
{
    clearServerGrid();

    QHash<QString, QList<Core::EmojiCatalogItem>> grouped;
    for (const auto &item : items)
        grouped[item.guildId].append(item);

    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        auto &groupItems = it.value();
        std::sort(groupItems.begin(), groupItems.end(), [](const auto &a, const auto &b) {
            return a.name.toCaseFolded() < b.name.toCaseFolded();
        });
    }

    QList<QString> orderedGroups;
    QSet<QString> seen;

    // Current guild always goes first
    if (!currentGuildId.isEmpty() && grouped.contains(currentGuildId)) {
        orderedGroups.append(currentGuildId);
        seen.insert(currentGuildId);
    }

    // Then the guild sidebar order
    for (const auto &guildId : orderedGuildIds) {
        if (grouped.contains(guildId) && !seen.contains(guildId)) {
            orderedGroups.append(guildId);
            seen.insert(guildId);
        }
    }

    QList<QString> leftovers;
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        if (!seen.contains(it.key()))
            leftovers.append(it.key());
    }
    std::sort(leftovers.begin(), leftovers.end(), [&](const QString &left, const QString &right) {
        const QString leftName = guildHeaderText(grouped.value(left)).toCaseFolded();
        const QString rightName = guildHeaderText(grouped.value(right)).toCaseFolded();
        if (leftName == rightName)
            return left < right;
        return leftName < rightName;
    });
    orderedGroups.append(leftovers);

    // Shared stylesheet for server emoji grid buttons
    const int serverGridR = Core::Theme::Manager::instance().roundness();
    const QString kServerButtonStyle =
            QStringLiteral(
                    "QToolButton[emojiGridButton=\"true\"] { border: 1px solid transparent; border-radius: %1px; padding: 4px; }"
                    "QToolButton[emojiGridButton=\"true\"]:hover { border-color: palette(mid); background: palette(base); }"
                    "QToolButton[emojiGridButton=\"true\"]:checked { border-color: palette(highlight); background: palette(highlight); }")
                    .arg(serverGridR);
    serverGrid->setStyleSheet(kServerButtonStyle);

    QList<EmojiGridSection> sections;
    for (const auto &guildId : orderedGroups) {
        const auto guildItems = grouped.value(guildId);
        if (guildItems.isEmpty())
            continue;

        EmojiGridSection section;
        section.key = guildId;
        section.header = guildHeaderText(guildItems);
        section.items = guildItems;
        sections.append(section);
    }
    if (serverGrid)
        serverGrid->setSections(sections);

    // Select the first emoji so "Use" is immediately available, matching the
    // previous eager-build behavior.
    if (!sections.isEmpty() && !sections.first().items.isEmpty())
        setSelectedEmojiValue(sections.first().items.first().selectionValue());
    else
        setSelectedEmojiValue(QString());

    // Fade in the server grid after layout
    fadeInWidget(serverGrid, 200);
}

void EmojiPickerDialog::rebuildResults()
{
    // Building the section lists, grids and result rows allocates heavily
    // (registry copies, section hashes, button pools). Any failure here —
    // typically std::bad_alloc under memory pressure — must not escape into
    // Qt's event dispatch, where an unhandled exception terminates the whole
    // process. This function runs during the constructor (before the dialog
    // is even shown), so a failure degrades to an empty picker instead of
    // aborting before the picker appears.
    try {
        rebuildResultsUnchecked();
    } catch (const std::exception &) {
        // Degrade to an empty picker: drop the per-build fetch bookkeeping and
        // leave the currently-visible grid/list empty. The dialog stays open
        // and usable (the user can still search or close it).
        emojiFetchPending.clear();
        pendingEmojiFetches.clear();
        clearServerGrid();
        clearCategoryGrid();
        if (resultsList)
            resultsList->clear();
    }
}

void EmojiPickerDialog::rebuildResultsUnchecked()
{
    const auto section = static_cast<Section>(sectionTabs->currentIndex());
    // Use the debounced text if available, otherwise use the current text
    const QString query = pendingSearchText.isNull() ? searchEdit->text() : pendingSearchText;
    pendingSearchText = QString();
    const auto items = filterItems(itemsForSection(section), query);

    // Reset the per-build fetch bookkeeping so cached/attempted marks do not
    // leak across tab switches. In-flight replies are left to complete and
    // populate the shared (value-keyed) icon caches — safe across rebuilds.
    emojiFetchPending.clear();
    pendingEmojiFetches.clear();

    if (section == Section::Server) {
        if (resultsList)
            resultsList->hide();
        if (categoryScrollArea)
            categoryScrollArea->hide();
        if (serverScrollArea)
            serverScrollArea->show();
        rebuildServerGrid(items);
        updateFavoriteState();
        return;
    }

    // Hide sticky category header when leaving the category grid
    if (categoryStickyHeader)
        categoryStickyHeader->setVisible(false);

    if (serverScrollArea)
        serverScrollArea->hide();
    selectedServerEmoji.clear();
    clearServerGrid();

    // If we are in "All" section with no search query, show the category grid
    // with recently-used at top and scroll-jump sections.
    if (section == Section::All && query.trimmed().isEmpty()) {
        if (resultsList)
            resultsList->hide();
        if (categoryScrollArea)
            categoryScrollArea->show();
        rebuildCategoryGrid(items);
        updateFavoriteState();
        // The sticky header was explicitly hidden above when leaving the grid;
        // re-evaluate it now that the grid is visible again. The scroll
        // position is preserved across tab switches, so no scroll event fires
        // to re-show it otherwise — it would stay hidden until a manual scroll.
        updateCategoryStickyHeader();
        return;
    }

    if (categoryScrollArea)
        categoryScrollArea->hide();
    clearCategoryGrid();
    if (resultsList)
        resultsList->show();

    if (!resultsList)
        return;

    resultsList->clear();
    const int totalMatches = items.size();
    int displayed = 0;
    for (const auto &item : items) {
        // Cap displayed rows: every custom emoji row triggers an icon fetch,
        // so an unbounded list can fire thousands of network requests.
        if (displayed >= kMaxDisplayedSearchResults)
            break;
        ++displayed;
        auto *row = new QListWidgetItem(resultsList);
        row->setData(Qt::UserRole, item.selectionValue());
        row->setToolTip(QStringLiteral(":%1:").arg(item.name));
        const int iconSize = 24;
        if (item.isCustom()) {
            row->setText(QStringLiteral(" :%1:").arg(item.name));
            loadCustomEmojiIcon(row, item);
        } else {
            row->setText(QStringLiteral(" :%1:").arg(item.name));
            row->setIcon(renderUnicodeEmojiIcon(item.unicodeEmoji, iconSize));
        }
    }
    if (totalMatches > displayed) {
        auto *hint = new QListWidgetItem(
                tr("Showing first %1 of %2 matches — refine your search").arg(displayed).arg(totalMatches),
                resultsList);
        hint->setFlags(Qt::NoItemFlags);
    }

    selectFirstItem();
    updateFavoriteState();
}

void EmojiPickerDialog::selectFirstItem()
{
    if (isServerSectionActive())
        return;

    if (resultsList && resultsList->count() > 0) {
        resultsList->setCurrentRow(0);
        resultsList->scrollToItem(resultsList->currentItem());
    }
}

void EmojiPickerDialog::acceptCurrent()
{
    if (accepting || result() == QDialog::Accepted)
        return;

    const QString emoji = currentSelectedEmojiValue();
    if (emoji.isEmpty())
        return;

    const auto selection = Core::EmojiCatalog::selectionForRaw(emoji);
    if (!selection)
        return;

    accepting = true;
    currentSelection = *selection;
    EmojiPreferences::addRecent(emoji);
    emit emojiSelected(emoji);
    accept();
}

void EmojiPickerDialog::updateFavoriteState()
{
    const QString emoji = currentSelectedEmojiValue();
    const bool favorite = !emoji.isEmpty() && EmojiPreferences::isFavorite(emoji);
    if (favoriteButton)
        favoriteButton->setEnabled(!emoji.isEmpty());
    if (favoriteButton)
        favoriteButton->setChecked(favorite);
}

void EmojiPickerDialog::toggleFavorite()
{
    const QString emoji = currentSelectedEmojiValue();
    if (emoji.isEmpty())
        return;

    EmojiPreferences::setFavorite(emoji, !EmojiPreferences::isFavorite(emoji));
    rebuildResults();
    updateFavoriteState();
    // Rebuild resets the selection to the first emoji; restore it so a
    // subsequent "Use" inserts the emoji the user actually favorited.
    restoreSelectionToEmoji(emoji);
    // Reflect the restored row in the favorite checkbox.
    updateFavoriteState();
}

void EmojiPickerDialog::restoreSelectionToEmoji(const QString &emojiValue)
{
    if (emojiValue.isEmpty())
        return;

    if (isServerSectionActive()) {
        if (serverGrid && serverGrid->selectValue(emojiValue))
            setSelectedEmojiValue(emojiValue);
        return;
    }

    if (categoryGrid && categoryScrollArea->isVisible()) {
        if (categoryGrid->selectValue(emojiValue))
            setSelectedEmojiValue(emojiValue);
        return;
    }

    if (resultsList) {
        for (int i = 0; i < resultsList->count(); ++i) {
            auto *item = resultsList->item(i);
            if (item && item->data(Qt::UserRole).toString() == emojiValue) {
                resultsList->setCurrentRow(i);
                return;
            }
        }
    }
}

} // namespace UI
} // namespace Acheron
