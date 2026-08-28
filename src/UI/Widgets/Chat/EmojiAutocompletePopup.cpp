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

// One-time folded-name index over EmojiCatalog::items(), bucketed by the
// distinct characters of each folded name. computeMatches only ever needs the
// bucket for the needle's first character (the existing pre-filter is
// "name contains firstNeedleChar"), so per-keystroke work drops from a full
// catalog scan + per-item toCaseFolded to one bucket walk.
struct EmojiIndexEntry
{
    Core::EmojiCatalogItem item;
    QString foldedName;
};

struct EmojiSearchIndex
{
    QVector<EmojiIndexEntry> entries;
    // folded char -> entry indices whose folded name contains it (distinct
    // chars per name, mirroring the contains() pre-filter semantics).
    QHash<QChar, QVector<int>> byChar;
    int catalogSize = -1;
};

const EmojiSearchIndex &emojiSearchIndex()
{
    static EmojiSearchIndex index;
    const auto &items = Core::EmojiCatalog::items();
    // EmojiCatalog::items() is a lazily rebuilt combined list (built-in +
    // runtime-registered custom emoji); a size change means a rebuild here.
    if (index.catalogSize == items.size() && !index.entries.isEmpty())
        return index;

    index.entries.clear();
    index.byChar.clear();
    index.entries.reserve(items.size());
    for (const auto &item : items) {
        if (item.name.isEmpty())
            continue;
        EmojiIndexEntry entry;
        entry.item = item;
        entry.foldedName = item.name.toCaseFolded();
        index.entries.append(std::move(entry));
    }
    QSet<QChar> seen;
    for (int i = 0; i < index.entries.size(); ++i) {
        seen.clear();
        const QString &folded = index.entries[i].foldedName;
        for (int c = 0; c < folded.size(); ++c) {
            const QChar ch = folded.at(c);
            if (seen.contains(ch))
                continue;
            seen.insert(ch);
            index.byChar[ch].append(i);
        }
    }
    index.catalogSize = items.size();
    return index;
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

    // Debounce burst typing: every setQuery() only records the latest prefix;
    // the single-shot timer coalesces a burst into one compute+populate pass.
    debounceTimer_ = new QTimer(this);
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(100);
    connect(debounceTimer_, &QTimer::timeout, this, [this]() { applyPendingQuery(); });

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

void EmojiAutocompletePopup::hideEvent(QHideEvent *event)
{
    // Any hide — including Qt::Popup auto-hide on outside clicks — must drop a
    // debounced-but-unapplied query, otherwise the pending timer could fire
    // later and resurrect the popup without a valid emoji context.
    cancelPendingQuery();
    QFrame::hideEvent(event);
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
    results.reserve(kMaxResults);

    const QChar firstNeedleChar = needle.isEmpty() ? QChar() : needle.front();

    // A subsequence (fuzzy) match requires the needle's first character to
    // appear in the name, so names lacking it can never match. The index
    // buckets names by their distinct chars, turning the full-catalog
    // contains() pre-filter into an O(bucket) walk.
    const auto &index = emojiSearchIndex();
    if (needle.isEmpty()) {
        for (const auto &entry : index.entries) {
            const int score = fuzzyScore(entry.foldedName, needle);
            if (score >= 0)
                results.append({ entry.item, entry.foldedName, score });
        }
    } else {
        const auto bucketIt = index.byChar.constFind(firstNeedleChar);
        if (bucketIt == index.byChar.constEnd())
            return results;
        for (int idx : bucketIt.value()) {
            const EmojiIndexEntry &entry = index.entries[idx];
            const int score = fuzzyScore(entry.foldedName, needle);
            if (score >= 0)
                results.append({ entry.item, entry.foldedName, score });
        }
    }

    std::sort(results.begin(), results.end(), [](const MatchResult &a, const MatchResult &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.foldedName < b.foldedName;
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

    // Deduplicate by URL: repeatedly typed shortcodes reuse the in-memory
    // pixmap cache, and concurrent duplicates share a single in-flight reply.
    const QString url = item.cdnUrl(32);
    const auto cached = iconCache_.constFind(url);
    if (cached != iconCache_.constEnd()) {
        row->setIcon(QIcon(cached.value()));
        return;
    }
    if (iconFetching_.contains(url))
        return; // in flight; the shared finished() handler refreshes live rows

    iconFetching_.insert(url);
    QNetworkReply *reply = nam_->get(QNetworkRequest(QUrl(url)));
    pendingIconReplies_.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        pendingIconReplies_.remove(reply);
        iconFetching_.remove(url);
        reply->deleteLater();
        if (!list_)
            return;
        QPixmap pix;
        const bool ok = reply->error() == QNetworkReply::NoError && pix.loadFromData(reply->readAll());
        if (ok) {
            if (iconCache_.size() >= 512)
                iconCache_.clear();
            iconCache_.insert(url, pix);
        }
        // Refresh any live row for this emoji (rows created while the fetch
        // was in flight were skipped above). Failure leaves rows iconless,
        // like the pre-cache behavior; the next query retries.
        for (int i = 0; i < list_->count(); ++i) {
            QListWidgetItem *liveRow = list_->item(i);
            if (liveRow->data(Qt::UserRole + 1).toString() == url) {
                if (ok)
                    liveRow->setIcon(QIcon(pix));
            }
        }
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
    // Stable URL for the icon-finish refresh path (see loadIconForItem).
    if (item.isCustom())
        row->setData(Qt::UserRole + 1, item.cdnUrl(32));
    row->setToolTip(QStringLiteral(":%1:").arg(item.name));
}

void EmojiAutocompletePopup::populateList(const QList<MatchResult> &matches)
{
    // In-flight icon replies are NOT aborted here: their shared finished()
    // handler fills the URL cache and refreshes rows that are still live, so
    // rapid retyping never re-downloads the same icon.
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
        // Not a valid emoji context: drop any pending query immediately so a
        // stale timer can't resurrect the popup.
        pendingPrefix_.clear();
        debounceTimer_->stop();
        populateList({});
        hide();
        emit queryApplied();
        return;
    }

    // Debounce: each keystroke only records the latest prefix; the timer
    // coalesces a burst into one compute+populate pass.
    pendingPrefix_ = prefix;
    debounceTimer_->start();
}

void EmojiAutocompletePopup::cancelPendingQuery()
{
    pendingPrefix_.clear();
    debounceTimer_->stop();
}

void EmojiAutocompletePopup::applyPendingQuery()
{
    const QString prefix = pendingPrefix_;
    pendingPrefix_.clear();
    if (prefix.length() < kMinPrefixLength) {
        hide();
        emit queryApplied();
        return;
    }

    const auto matches = computeMatches(prefix);
    if (matches.isEmpty()) {
        populateList({});
        hide();
        emit queryApplied();
        return;
    }

    populateList(matches);
    selectFirst();
    adjustSize();

    if (!isVisible())
        show();
    emit queryApplied();
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
