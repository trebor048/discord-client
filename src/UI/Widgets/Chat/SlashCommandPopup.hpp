#pragma once

#include <QFrame>
#include <QList>

#include "Discord/Entities.hpp"

class QListWidget;
class QLabel;

namespace Acheron {
namespace UI {

/*
 * Popup listing slash-command suggestions while the user types `/` in the
 * chat input. Mirrors EmojiAutocompletePopup's structure (Qt::Popup frame +
 * QListWidget + fuzzy match) but for ApplicationCommand objects.
 * MessageInput drives it via setQuery()/moveSelection()/acceptCurrent().
 */
class SlashCommandPopup : public QFrame
{
    Q_OBJECT
public:
    explicit SlashCommandPopup(QWidget *parent = nullptr);

    void setCommands(const QList<Discord::ApplicationCommand> &commands);
    void setQuery(const QString &prefix);
    // Argument suggestions (sub-commands / option choices). `names` are the
    // display names, `descriptions` optional second-line text, `insertTexts`
    // what gets inserted on selection.
    void setSuggestions(const QStringList &names, const QStringList &descriptions,
                        const QStringList &insertTexts, const QString &prefix);
    void selectFirst();
    void moveSelection(int delta);
    void acceptCurrent();

    [[nodiscard]] bool hasResults() const;
    [[nodiscard]] int resultCount() const;

signals:
    void commandSelected(const Discord::ApplicationCommand &command);
    void suggestionSelected(const QString &insertText);

protected:
    void showEvent(QShowEvent *event) override;

private:
    struct MatchResult
    {
        Discord::ApplicationCommand command;
        int score = 0;
    };

    // Mirrors EmojiAutocompletePopup::fuzzyScore. Both arguments must already
    // be lowercased (toCaseFolded) by the caller.
    static int fuzzyScore(const QString &lowercaseName, const QString &lowercaseNeedle);
    QList<MatchResult> computeMatches(const QString &prefix) const;
    void populateList(const QList<MatchResult> &matches);

    QListWidget *list_ = nullptr;
    QLabel *headerLabel_ = nullptr;
    QList<Discord::ApplicationCommand> commands_;
    QList<Discord::ApplicationCommand> currentItems_;
    QStringList suggestionInsertTexts_;
    bool m_suggestionMode = false;
    bool m_accepted = false;
};

} // namespace UI
} // namespace Acheron
