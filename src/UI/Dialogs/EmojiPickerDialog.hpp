#pragma once

#include <QDialog>
#include <QSet>
#include <QStringList>

#include <QBuffer>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QMovie>
#include <QSemaphore>
#include <QTimer>

#include <exception>

#include "Core/EmojiCatalog.hpp"
#include "Core/Snowflake.hpp"

class QDialogButtonBox;
class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QToolButton;
class QWidget;

namespace Acheron {
namespace UI {

class VirtualEmojiGrid;

class EmojiPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EmojiPickerDialog(QWidget *parent = nullptr);
    ~EmojiPickerDialog() override;

    QString selectedEmoji() const { return currentSelection.raw; }
    const Core::EmojiSelectionValue &selectedValue() const { return currentSelection; }

    void setSearchPlaceholder(const QString &text);
    void setOrderedGuildIds(const QStringList &guildIds);
    void setCurrentGuildId(const QString &guildId);

signals:
    void emojiSelected(const QString &emoji);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void rebuildResults();
    void onSearchDebounced();
    void acceptCurrent();
    void updateFavoriteState();
    void toggleFavorite();

private:
    // Allocation-heavy part of rebuildResults(), called under a guard so an
    // out-of-memory failure degrades to an empty picker instead of escaping
    // into Qt's event dispatch and aborting the process.
    void rebuildResultsUnchecked();
    enum class Section {
        All = 0,
        Recents = 1,
        Favorites = 2,
        Server = 3,
    };

    const QList<Core::EmojiCatalogItem> &itemsForSection(Section section) const;
    QList<Core::EmojiCatalogItem> filterItems(const QList<Core::EmojiCatalogItem> &items,
                                              const QString &query) const;
    Core::EmojiCatalogItem itemForValue(const QString &value) const;
    void selectFirstItem();
    void loadCustomEmojiIcon(QListWidgetItem *row, const Core::EmojiCatalogItem &item);
    void applyIconToButton(QToolButton *button, const Core::EmojiCatalogItem &item);
    void requestCustomIconFetch(const Core::EmojiCatalogItem &item, const QString &value);
    void startCustomIconFetch(const Core::EmojiCatalogItem &item, const QString &value);
    void drainPendingEmojiFetches();
    void refreshVisibleCustomEmoji(const QString &value);
    void rebuildServerGrid(const QList<Core::EmojiCatalogItem> &items);
    void rebuildCategoryGrid(const QList<Core::EmojiCatalogItem> &items);
    void clearServerGrid();
    void clearCategoryGrid();
    void setSelectedEmojiValue(const QString &emojiValue);
    QString currentSelectedEmojiValue() const;
    void restoreSelectionToEmoji(const QString &emojiValue);
    bool isServerSectionActive() const;
    void rebuildServerSectionIfStale();
    void scrollToCategory(const QString &categoryName);
    void updateCategoryStickyHeader();

    // Skin tone variation support
    void showSkinTonePicker(QToolButton *button, const Core::EmojiCatalogItem &item);
    [[nodiscard]] QString applySkinTone(const QString &emoji, int toneIndex) const;
    void setSkinTone(int toneIndex);
    [[nodiscard]] int currentSkinTone() const;

    QLineEdit *searchEdit = nullptr;
    QTabWidget *sectionTabs = nullptr;
    QListWidget *resultsList = nullptr;
    QScrollArea *serverScrollArea = nullptr;
    VirtualEmojiGrid *serverGrid = nullptr;
    QScrollArea *categoryScrollArea = nullptr;
    VirtualEmojiGrid *categoryGrid = nullptr;
    QLabel *categoryStickyHeader = nullptr;
    QDialogButtonBox *buttonBox = nullptr;
    QPushButton *useButton = nullptr;
    QPushButton *favoriteButton = nullptr;
    QToolButton *skinToneButton = nullptr;
    QNetworkAccessManager *nam = nullptr;
    QTimer searchDebounceTimer;
    QString pendingSearchText;
    QSet<QNetworkReply *> pendingIconRequests;
    QStringList orderedGuildIds;
    QString selectedServerEmoji;
    Core::EmojiSelectionValue currentSelection;
    QHash<QString, QByteArray> gifCache;
    QHash<QToolButton *, QMovie *> hoveredMovies;
    QSet<QToolButton *> animatedButtons;
    bool accepting = false;

    // Bounded-concurrency fetch limiter: at most 8 emoji downloads in flight.
    QSemaphore iconFetchSemaphore{8};
    QHash<QString, QIcon> staticEmojiIconCache;
    QSet<QString> emojiFetchPending;
    struct PendingEmojiFetch {
        Core::EmojiCatalogItem item;
        QString value;
    };
    QList<PendingEmojiFetch> pendingEmojiFetches;

    QString currentGuildId;
    void fadeInWidget(QWidget *w, int durationMs = 150);

    // Set the first time the dialog becomes visible; the destructor only
    // persists geometry when the picker was actually shown (an unshown dialog
    // has a default geometry that would otherwise clobber the saved one).
    bool wasShown = false;
};

// Modal helper: run the emoji picker and return the chosen emoji, or an empty
// string when the user cancels. Shared by the message input and settings pages.
inline QString pickEmoji(QWidget *parent, const QString &title, const QString &prompt,
                         const QStringList &orderedGuildIds = {},
                         const Core::Snowflake &currentGuildId = {})
{
    // The picker allocates large widget/emoji pools while it opens; an
    // out-of-memory failure must not escape into Qt's event dispatch and
    // terminate the whole app — return "cancel" instead.
    try {
        EmojiPickerDialog dialog(parent);
        dialog.setWindowTitle(title);
        dialog.setSearchPlaceholder(prompt);
        if (!orderedGuildIds.isEmpty())
            dialog.setOrderedGuildIds(orderedGuildIds);
        if (currentGuildId.isValid())
            dialog.setCurrentGuildId(currentGuildId.toString());
        if (dialog.exec() != QDialog::Accepted)
            return {};
        return dialog.selectedEmoji();
    } catch (const std::exception &) {
        return {};
    }
}

} // namespace UI
} // namespace Acheron
