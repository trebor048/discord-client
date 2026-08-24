#include "SlashCommandPopup.hpp"

#include <QAbstractItemView>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPair>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace Acheron {
namespace UI {

namespace {
constexpr int kMaxResults = 12;
} // namespace

SlashCommandPopup::SlashCommandPopup(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::Box);
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_QuitOnClose, false);
    setFixedWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    headerLabel_ = new QLabel(this);
    headerLabel_->setStyleSheet(QStringLiteral(
            "QLabel { padding: 4px 8px; background: palette(mid); color: palette(window-text); "
            "font-size: 11px; }"));
    headerLabel_->setText(QStringLiteral("Slash commands"));
    layout->addWidget(headerLabel_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("SlashCommandList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(true);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setMaximumHeight(280);
    list_->setStyleSheet(QStringLiteral(
            "QListWidget { background: palette(base); color: palette(text); border: none; }"
            "QListWidget::item { padding: 4px 8px; }"
            "QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }"));
    layout->addWidget(list_);

    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { acceptCurrent(); });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { acceptCurrent(); });

    hide();
}

void SlashCommandPopup::showEvent(QShowEvent *event)
{
    m_accepted = false;
    QFrame::showEvent(event);
}

void SlashCommandPopup::setCommands(const QList<Discord::ApplicationCommand> &commands)
{
    commands_ = commands;
}

int SlashCommandPopup::fuzzyScore(const QString &lowercaseName, const QString &lowercaseNeedle)
{
    if (lowercaseNeedle.isEmpty())
        return 100;

    const QString &n = lowercaseName;
    const QString &q = lowercaseNeedle;

    if (n == q)
        return 1000;
    if (n.startsWith(q))
        return 500 + (q.length() == n.length() ? 100 : 0);

    // Subsequence match scoring (fuzzy), same algorithm as EmojiAutocompletePopup.
    int score = 0;
    int ni = 0;
    int consecutive = 0;
    int lastMatchPos = -1;

    for (int qi = 0; qi < q.length() && ni < n.length(); ++qi) {
        const QChar qc = q[qi];
        bool found = false;
        for (; ni < n.length(); ++ni) {
            if (n[ni] == qc) {
                score += 10;
                if (ni == lastMatchPos + 1) {
                    consecutive += 10;
                    score += consecutive;
                } else {
                    consecutive = 0;
                }
                if (ni == qi)
                    score += 5;
                lastMatchPos = ni;
                ++ni;
                found = true;
                break;
            }
        }
        if (!found)
            return -1;
    }

    if (n.contains(q))
        score += 50;

    return score;
}

QList<SlashCommandPopup::MatchResult> SlashCommandPopup::computeMatches(const QString &prefix) const
{
    const QString needle = prefix.trimmed().toCaseFolded();
    QList<MatchResult> results;
    results.reserve(32);

    const QChar firstNeedleChar = needle.isEmpty() ? QChar() : needle.front();

    for (const auto &cmd : commands_) {
        if (cmd.name.get().isEmpty())
            continue;
        const QString name = cmd.name.get().toCaseFolded();
        if (!firstNeedleChar.isNull() && !name.contains(firstNeedleChar))
            continue;
        const int score = fuzzyScore(name, needle);
        if (score >= 0)
            results.append({ cmd, score });
    }

    std::sort(results.begin(), results.end(), [](const MatchResult &a, const MatchResult &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.command.name.get().toCaseFolded() < b.command.name.get().toCaseFolded();
    });

    if (results.size() > kMaxResults)
        results = results.mid(0, kMaxResults);

    return results;
}

void SlashCommandPopup::populateList(const QList<MatchResult> &matches)
{
    m_accepted = false;
    currentItems_.clear();
    list_->clear();

    for (const auto &match : matches) {
        currentItems_.append(match.command);
        auto *row = new QListWidgetItem(list_);

        QString text = QStringLiteral("/%1").arg(match.command.name.get());
        if (match.command.description.hasValue() && !match.command.description->isEmpty())
            text += QStringLiteral("  —  %1").arg(match.command.description.get());
        row->setText(text);
        row->setData(Qt::UserRole, match.command.name.get());
    }

    headerLabel_->setText(matches.isEmpty()
                                  ? QStringLiteral("No matching commands")
                                  : QStringLiteral("%1 command%2")
                                            .arg(matches.size())
                                            .arg(matches.size() == 1 ? QStringLiteral("") : QStringLiteral("s")));
}

void SlashCommandPopup::setQuery(const QString &prefix)
{
    m_suggestionMode = false;

    // Allow an empty prefix (a bare '/') so the full command list shows.
    const auto matches = computeMatches(prefix);
    if (matches.isEmpty()) {
        populateList({});
        hide();
        return;
    }

    populateList(matches);
    selectFirst();
    adjustSize();

    if (!isVisible())
        show();
}

void SlashCommandPopup::setSuggestions(const QStringList &names, const QStringList &descriptions,
                                       const QStringList &insertTexts, const QString &prefix)
{
    m_accepted = false;
    m_suggestionMode = true;
    currentItems_.clear();
    suggestionInsertTexts_.clear();
    list_->clear();

    const QString needle = prefix.trimmed().toCaseFolded();

    QList<QPair<int, int>> scored;
    scored.reserve(names.size());
    for (int i = 0; i < names.size(); ++i) {
        const int score = fuzzyScore(names.at(i).toCaseFolded(), needle);
        if (score >= 0)
            scored.append({ score, i });
    }
    std::sort(scored.begin(), scored.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
        return a.first > b.first;
    });
    if (scored.size() > kMaxResults)
        scored = scored.mid(0, kMaxResults);

    for (const auto &s : scored) {
        const int idx = s.second;
        // Lists are produced in lockstep, but guard like descriptions below:
        // a mismatched insertTexts list must never index out of bounds.
        suggestionInsertTexts_.append(idx < insertTexts.size() ? insertTexts.at(idx)
                                                               : names.at(idx));
        auto *row = new QListWidgetItem(list_);
        QString text = names.at(idx);
        if (idx < descriptions.size() && !descriptions.at(idx).isEmpty())
            text += QStringLiteral("  —  %1").arg(descriptions.at(idx));
        row->setText(text);
    }

    headerLabel_->setText(list_->count() == 0 ? QStringLiteral("No matching options")
                                              : QStringLiteral("Options"));

    selectFirst();
    adjustSize();
}

void SlashCommandPopup::selectFirst()
{
    if (list_->count() > 0)
        list_->setCurrentRow(0);
}

void SlashCommandPopup::moveSelection(int delta)
{
    if (list_->count() == 0)
        return;
    int row = list_->currentRow();
    if (row < 0)
        row = 0;
    else
        row = (row + delta + list_->count()) % list_->count();
    list_->setCurrentRow(row);
}

void SlashCommandPopup::acceptCurrent()
{
    if (m_accepted)
        return;
    m_accepted = true;

    const int row = list_->currentRow();
    if (row < 0)
        return;

    if (m_suggestionMode) {
        if (row < suggestionInsertTexts_.size())
            emit suggestionSelected(suggestionInsertTexts_.at(row));
        return;
    }

    if (row >= currentItems_.size())
        return;

    emit commandSelected(currentItems_.at(row));
}

bool SlashCommandPopup::hasResults() const
{
    return list_->count() > 0;
}

int SlashCommandPopup::resultCount() const
{
    return list_->count();
}

} // namespace UI
} // namespace Acheron
