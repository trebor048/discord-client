#pragma once

#include <QFrame>
#include <QHash>
#include <QList>
#include <QSet>

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
    void selectFirst();
    void moveSelection(int delta);
    void acceptCurrent();

    [[nodiscard]] bool hasResults() const;
    [[nodiscard]] int resultCount() const;

    void setNetworkAccessManager(QNetworkAccessManager *nam);

signals:
    void emojiSelected(const Core::EmojiCatalogItem &item);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct MatchResult {
        Core::EmojiCatalogItem item;
        int score = 0;
    };

    // Both arguments must already be lowercased (toCaseFolded) by the caller.
    static int fuzzyScore(const QString &lowercaseName, const QString &lowercaseNeedle);
    QList<MatchResult> computeMatches(const QString &prefix) const;

    void populateList(const QList<MatchResult> &matches);
    void loadIconForItem(QListWidgetItem *row, const Core::EmojiCatalogItem &item);
    void presentItem(QListWidgetItem *row, const Core::EmojiCatalogItem &item);

    QListWidget *list_ = nullptr;
    QLabel *headerLabel_ = nullptr;
    QNetworkAccessManager *nam_ = nullptr;
    QSet<QNetworkReply *> pendingIconReplies_;
    QList<Core::EmojiCatalogItem> currentItems_;
    bool m_accepted = false;
};

} // namespace UI
} // namespace Acheron
