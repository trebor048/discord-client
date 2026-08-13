#include "EmojiAutocompletePopup.hpp"

#include <QAbstractItemView>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace Acheron {
namespace UI {

namespace {

constexpr int kMaxResults = 12;
constexpr int kMinPrefixLength = 1;

QString shortName(const Core::EmojiCatalogItem &item)
{
    return item.name;
}

QIcon renderUnicodeIcon(const QString &emojiText, int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPixelSize(size - 4);
    p.setFont(f);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, emojiText);
    p.end();
    return QIcon(pix);
}

} // namespace

EmojiAutocompletePopup::EmojiAutocompletePopup(QWidget *parent)
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
    headerLabel_->setText(QStringLiteral("Emoji matching"));
    layout->addWidget(headerLabel_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("EmojiAutocompleteList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(true);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setIconSize(QSize(24, 24));
    list_->setMaximumHeight(280);
    list_->setStyleSheet(QStringLiteral(
            "QListWidget { background: palette(base); color: palette(text); border: none; }"
            "QListWidget::item { padding: 4px 8px; }"
            "QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }"));
    layout->addWidget(list_);

    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *) {
        acceptCurrent();
    });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *) {
        acceptCurrent();
    });

    list_->installEventFilter(this);

    hide();
}

EmojiAutocompletePopup::~EmojiAutocompletePopup()
{
    // Abort all pending network replies so the finished() lambdas don't
    // fire with a dangling this pointer.  Iterate over a copy because
    // abort() fires finished() synchronously, which removes from the set.
    const auto pending = pendingIconReplies_;
    for (auto *reply : pending) {
        reply->abort();
        reply->deleteLater();
    }
    pendingIconReplies_.clear();
}

void EmojiAutocompletePopup::showEvent(QShowEvent *event)
{
    m_accepted = false;
    QFrame::showEvent(event);
}

void EmojiAutocompletePopup::setNetworkAccessManager(QNetworkAccessManager *nam)
{
    nam_ = nam;
}

int EmojiAutocompletePopup::fuzzyScore(const QString &lowercaseName, const QString &lowercaseNeedle)
{
    if (lowercaseNeedle.isEmpty())
        return 100;

    const QString &n = lowercaseName;
    const QString &q = lowercaseNeedle;

    if (n == q)
        return 1000;

    if (n.startsWith(q))
        return 500 + (q.length() == n.length() ? 100 : 0);

    // Subsequence match scoring (fuzzy)
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
                    score += 5; // bonus for matching at same position
                lastMatchPos = ni;
                ++ni;
                found = true;
                break;
            }
        }
        if (!found)
            return -1;
    }

    // If the full needle is a substring of the name, give bonus
    if (n.contains(q))
        score += 50;

    return score;
}

QList<EmojiAutocompletePopup::MatchResult>
EmojiAutocompletePopup::computeMatches(const QString &prefix) const
{
    const QString needle = prefix.trimmed().toCaseFolded();
    QList<MatchResult> results;
    results.reserve(32);

    const auto &allItems = Core::EmojiCatalog::items();

    // A subsequence (fuzzy) match requires the needle's first character to
    // appear in the name, so names lacking it can never match.  Cheap pre-filter
    // that skips the expensive subsequence scan for the common non-match case.
    const QChar firstNeedleChar = needle.isEmpty() ? QChar() : needle.front();

    for (const auto &item : allItems) {
        if (item.name.isEmpty())
            continue;

        const QString name = item.name.toCaseFolded();
        if (!firstNeedleChar.isNull() && !name.contains(firstNeedleChar))
            continue;

        const int score = fuzzyScore(name, needle);
        if (score >= 0)
            results.append({item, score});
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

void EmojiAutocompletePopup::loadIconForItem(QListWidgetItem *row, const Core::EmojiCatalogItem &item)
{
    if (!item.isCustom()) {
        row->setIcon(renderUnicodeIcon(item.unicodeEmoji, 24));
        return;
    }

    if (!nam_ || item.customId.isEmpty())
        return;

    // Store the selection value so the lambda can look up the item by stable identifier
    const QString selValue = item.selectionValue();
    QNetworkReply *reply = nam_->get(QNetworkRequest(QUrl(item.cdnUrl(32))));
    pendingIconReplies_.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, selValue]() {
        pendingIconReplies_.remove(reply);
        reply->deleteLater();
        if (!list_)
            return;
        // Look up by stable identifier instead of raw pointer (which may have been deleted)
        QListWidgetItem *found = nullptr;
        for (int i = 0; i < list_->count(); ++i) {
            if (list_->item(i)->data(Qt::UserRole).toString() == selValue) {
                found = list_->item(i);
                break;
            }
        }
        if (!found)
            return;
        if (reply->error() != QNetworkReply::NoError)
            return;
        QPixmap pix;
        if (pix.loadFromData(reply->readAll()))
            found->setIcon(QIcon(pix));
    });
}

void EmojiAutocompletePopup::presentItem(QListWidgetItem *row, const Core::EmojiCatalogItem &item)
{
    QString text;
    if (item.isCustom()) {
        text = QStringLiteral(":%1:").arg(item.name);
        if (!item.guildName.isEmpty())
            text += QStringLiteral("  [%1]").arg(item.guildName);
    } else {
        text = QStringLiteral("%1  :%2:").arg(item.unicodeEmoji, item.name);
    }
    row->setText(text);
    row->setData(Qt::UserRole, item.selectionValue());
    row->setToolTip(QStringLiteral(":%1:").arg(item.name));
}

void EmojiAutocompletePopup::populateList(const QList<MatchResult> &matches)
{
    // Abort all pending icon replies before clearing — iterate over a COPY
    // of the set because abort() fires finished() synchronously, which calls
    // pendingIconReplies_.remove() and would invalidate the range-for iterator.
    const auto pending = pendingIconReplies_;
    for (auto *reply : pending) {
        reply->abort();
        reply->deleteLater();
    }
    pendingIconReplies_.clear();

    m_accepted = false;
    currentItems_.clear();
    list_->clear();

    for (const auto &match : matches) {
        currentItems_.append(match.item);
        auto *row = new QListWidgetItem(list_);
        presentItem(row, match.item);
        loadIconForItem(row, match.item);
    }

    headerLabel_->setText(matches.isEmpty()
                                  ? QStringLiteral("No matching emoji")
                                  : QStringLiteral("%1 matching emoji")
                                            .arg(matches.size()));
}

void EmojiAutocompletePopup::setQuery(const QString &prefix)
{
    if (prefix.length() < kMinPrefixLength) {
        populateList({});
        hide();
        return;
    }

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

void EmojiAutocompletePopup::selectFirst()
{
    if (list_->count() > 0)
        list_->setCurrentRow(0);
}

void EmojiAutocompletePopup::moveSelection(int delta)
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

void EmojiAutocompletePopup::acceptCurrent()
{
    if (m_accepted)
        return;
    m_accepted = true;

    const int row = list_->currentRow();
    if (row < 0 || row >= currentItems_.size())
        return;

    emit emojiSelected(currentItems_.at(row));
}

bool EmojiAutocompletePopup::hasResults() const
{
    return list_->count() > 0;
}

int EmojiAutocompletePopup::resultCount() const
{
    return list_->count();
}

bool EmojiAutocompletePopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == list_ && event->type() == QEvent::KeyPress) {
        // KeyPress events are handled by the parent (MessageInput) which calls
        // moveSelection / acceptCurrent directly.
        return false;
    }
    return QFrame::eventFilter(watched, event);
}

} // namespace UI
} // namespace Acheron
