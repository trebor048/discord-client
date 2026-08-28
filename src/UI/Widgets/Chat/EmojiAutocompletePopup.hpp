#pragma once

#include <QFrame>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QSet>
#include <QTimer>

#include <functional>

#include "Core/EmojiCatalog.hpp"

class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class QLabel;

namespace Acheron {
namespace UI {

/*
 * A lightweight popup that displays fuzzy-matched emoji results for the
 * chatbar autocomplete.  It is driven by MessageInput which detects the
 * `:shortcode:` pattern and calls setQuery().  Keyboard navigation (Up/Down,
 * Tab, Enter) is forwarded back to the caller via signals.
 */
class EmojiAutocompletePopup : public QFrame
{
    Q_OBJECT
public:
    explicit EmojiAutocompletePopup(QWidget *parent = nullptr);
    ~EmojiAutocompletePopup() override;

    void setQuery(const QString &prefix);
    /// Drops any debounced-but-not-yet-applied query (e.g. when the popup is
    /// hidden); the pending timer is stopped so it can't resurrect the popup.
    void cancelPendingQuery();
    void selectFirst();
    void moveSelection(int delta);
    void acceptCurrent();

    [[nodiscard]] bool hasResults() const;
    [[nodiscard]] int resultCount() const;

    void setNetworkAccessManager(QNetworkAccessManager *nam);

signals:
    void emojiSelected(const Core::EmojiCatalogItem &item);
    /// Emitted after a debounced query was actually applied (results shown or
    /// cleared), so the owner can show/hide the popup without reacting to
    /// every keystroke.
    void queryApplied();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct MatchResult {
        Core::EmojiCatalogItem item;
        QString foldedName; // pre-folded at index build time, used for sorting
        int score = 0;
    };

    // Both arguments must already be lowercased (toCaseFolded) by the caller.
    static int fuzzyScore(const QString &lowercaseName, const QString &lowercaseNeedle);
    QList<MatchResult> computeMatches(const QString &prefix) const;

    void populateList(const QList<MatchResult> &matches);
    void loadIconForItem(QListWidgetItem *row, const Core::EmojiCatalogItem &item);
    void presentItem(QListWidgetItem *row, const Core::EmojiCatalogItem &item);
    void applyPendingQuery();

    QListWidget *list_ = nullptr;
    QLabel *headerLabel_ = nullptr;
    QNetworkAccessManager *nam_ = nullptr;
    QSet<QNetworkReply *> pendingIconReplies_;
    QList<Core::EmojiCatalogItem> currentItems_;
    bool m_accepted = false;

    // Debounced query state: keystrokes only record the latest prefix; the
    // single-shot timer coalesces a burst into one compute+populate pass.
    QTimer *debounceTimer_ = nullptr;
    QString pendingPrefix_;
    // Custom-emoji icon URL cache + in-flight dedup so repeatedly typed
    // shortcodes don't re-download icons or duplicate requests.
    QHash<QString, QPixmap> iconCache_;
    QSet<QString> iconFetching_;
};

} // namespace UI
} // namespace Acheron
