#include "MentionAutocompletePopup.hpp"

#include <QAbstractItemView>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace Acheron {
namespace UI {

namespace {
constexpr int kMaxResults = 12;
} // namespace

MentionAutocompletePopup::MentionAutocompletePopup(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::Box);
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_QuitOnClose, false);
    setFixedWidth(320);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    headerLabel_ = new QLabel(this);
    headerLabel_->setStyleSheet(QStringLiteral(
            "QLabel { padding: 4px 8px; background: palette(mid); color: palette(window-text); "
            "font-size: 11px; }"));
    headerLabel_->setText(QStringLiteral("Mentions"));
    layout->addWidget(headerLabel_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("MentionAutocompleteList"));
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

void MentionAutocompletePopup::showEvent(QShowEvent *event)
{
    m_accepted = false;
    QFrame::showEvent(event);
}

void MentionAutocompletePopup::setItems(const QList<MentionItem> &items)
{
    items_ = items;
}

QString MentionAutocompletePopup::kindPrefix(MentionItem::Kind kind)
{
    switch (kind) {
    case MentionItem::Kind::Role: return QStringLiteral("@");
    case MentionItem::Kind::Channel: return QStringLiteral("#");
    case MentionItem::Kind::User:
    default: return QStringLiteral("@");
    }
}

int MentionAutocompletePopup::fuzzyScore(const QString &lowercaseName, const QString &lowercaseNeedle)
{
    if (lowercaseNeedle.isEmpty())
        return 100;

    const QString &n = lowercaseName;
    const QString &q = lowercaseNeedle;

    if (n == q)
        return 1000;
    if (n.startsWith(q))
        return 500 + (q.length() == n.length() ? 100 : 0);

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

QList<MentionAutocompletePopup::MatchResult>
MentionAutocompletePopup::computeMatches(const QString &prefix) const
{
    const QString needle = prefix.trimmed().toCaseFolded();
    QList<MatchResult> results;
    results.reserve(32);

    const QChar firstNeedleChar = needle.isEmpty() ? QChar() : needle.front();

    for (const auto &item : items_) {
        if (item.name.isEmpty())
            continue;
        const QString name = item.name.toCaseFolded();
        if (!firstNeedleChar.isNull() && !name.contains(firstNeedleChar))
            continue;
        const int score = fuzzyScore(name, needle);
        if (score >= 0)
            results.append({ item, score });
    }

    std::sort(results.begin(), results.end(), [](const MatchResult &a, const MatchResult &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.item.name.toCaseFolded() < b.item.name.toCaseFolded();
    });

    if (results.size() > kMaxResults)
        results = results.mid(0, kMaxResults);

    return results;
}

void MentionAutocompletePopup::populateList(const QList<MatchResult> &matches)
{
    m_accepted = false;
    currentItems_.clear();
    list_->clear();

    for (const auto &match : matches) {
        currentItems_.append(match.item);
        auto *row = new QListWidgetItem(list_);
        row->setText(QStringLiteral("%1%2").arg(kindPrefix(match.item.kind), match.item.name));
        row->setData(Qt::UserRole, match.item.name);
    }

    headerLabel_->setText(matches.isEmpty()
                                  ? QStringLiteral("No matches")
                                  : QStringLiteral("%1 match%2")
                                            .arg(matches.size())
                                            .arg(matches.size() == 1 ? QStringLiteral("") : QStringLiteral("es")));
}

void MentionAutocompletePopup::setQuery(const QString &prefix)
{
    // Allow an empty prefix (a bare '@'/'#') so the full mentionable list shows.
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

void MentionAutocompletePopup::selectFirst()
{
    if (list_->count() > 0)
        list_->setCurrentRow(0);
}

void MentionAutocompletePopup::moveSelection(int delta)
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

void MentionAutocompletePopup::acceptCurrent()
{
    if (m_accepted)
        return;
    m_accepted = true;

    const int row = list_->currentRow();
    if (row < 0 || row >= currentItems_.size()) {
        // Don't leave the "already accepted" latch stuck on an invalid row,
        // otherwise every subsequent Enter/click becomes a silent no-op.
        m_accepted = false;
        return;
    }

    emit mentionSelected(currentItems_.at(row));
}

bool MentionAutocompletePopup::hasResults() const
{
    return list_->count() > 0;
}

int MentionAutocompletePopup::resultCount() const
{
    return list_->count();
}

} // namespace UI
} // namespace Acheron
