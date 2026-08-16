#pragma once

#include <QFrame>
#include <QList>

#include "Core/Snowflake.hpp"

class QListWidget;
class QLabel;

namespace Acheron {
namespace UI {

// A mentionable target for `@` / `#` autocomplete.
struct MentionItem
{
    enum class Kind { User, Role, Channel };

    Core::Snowflake id;
    QString name;
    Kind kind = Kind::User;
};

// Popup listing user/role/channel suggestions while the user types `@` or `#`.
// Mirrors EmojiAutocompletePopup / SlashCommandPopup. MessageInput drives it via
// setQuery()/moveSelection()/acceptCurrent().
class MentionAutocompletePopup : public QFrame
{
    Q_OBJECT
public:
    explicit MentionAutocompletePopup(QWidget *parent = nullptr);

    void setItems(const QList<MentionItem> &items);
    void setQuery(const QString &prefix);
    void selectFirst();
    void moveSelection(int delta);
    void acceptCurrent();

    [[nodiscard]] bool hasResults() const;
    [[nodiscard]] int resultCount() const;

signals:
    void mentionSelected(const MentionItem &item);

protected:
    void showEvent(QShowEvent *event) override;

private:
    struct MatchResult
    {
        MentionItem item;
        int score = 0;
    };

    static int fuzzyScore(const QString &lowercaseName, const QString &lowercaseNeedle);
    QList<MatchResult> computeMatches(const QString &prefix) const;
    void populateList(const QList<MatchResult> &matches);
    static QString kindPrefix(MentionItem::Kind kind);

    QListWidget *list_ = nullptr;
    QLabel *headerLabel_ = nullptr;
    QList<MentionItem> items_;
    QList<MentionItem> currentItems_;
    bool m_accepted = false;
};

} // namespace UI
} // namespace Acheron
