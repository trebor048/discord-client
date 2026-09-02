#include "ChannelSearchPopup.hpp"

#include "UI/Chat/ChatModel.hpp"

#include "Core/AnimationUtils.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Result.hpp"
#include "Core/Theme/Manager.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Client.hpp"
#include "Discord/Entities.hpp"

#include <QAbstractItemView>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace Acheron {
namespace UI {

namespace {
QString formatTimestamp(qint64 secs)
{
    if (secs <= 0)
        return QString();
    return QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("MMM d, HH:mm"));
}

/// Build the one-line snippet for a result, keeping the match in the middle.
QString makeSnippet(const QString &content, const QString &needle)
{
    QString snippet = content;
    if (!needle.isEmpty()) {
        const int pos = snippet.toCaseFolded().indexOf(needle);
        if (pos >= 0) {
            const int start = qMax(0, pos - 24);
            if (start > 0)
                snippet = QStringLiteral("…") + snippet.mid(start);
            else
                snippet = snippet.mid(0, qMin(snippet.size(), 160));
        }
    }
    snippet.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (snippet.size() > 160)
        snippet = snippet.left(157) + QStringLiteral("…");
    return snippet;
}

/// Round avatar label sized to the chat-style 32px.
QLabel *makeAvatarLabel(QWidget *parent)
{
    auto *avatar = new QLabel(parent);
    avatar->setFixedSize(32, 32);
    avatar->setScaledContents(true);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(QStringLiteral(
            "QLabel { background: palette(mid); border-radius: 16px; }"));
    return avatar;
}
} // namespace

ChannelSearchPopup::ChannelSearchPopup(ChatModel *chatModel, Discord::Client *client,
                                       Core::ImageManager *imageManager, QWidget *parent)
    : QDialog(parent), chatModel(chatModel), client(client), imageManager(imageManager)
{
    setWindowTitle(tr("Search Channel"));
    setModal(true);
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumSize(560, 480);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);

    panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("channelSearchPanel"));
    panel->setStyleSheet(QStringLiteral(
            "#channelSearchPanel { background: palette(window); border: 1px solid palette(mid); "
            "border-radius: %1px; }")
                                 .arg(Core::Theme::Manager::instance().roundness()));

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("Search"), panel);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    title->setFont(titleFont);
    layout->addWidget(title);

    searchEdit = new QLineEdit(panel);
    searchEdit->setPlaceholderText(tr("Search messages in this channel"));
    searchEdit->setClearButtonEnabled(true);
    layout->addWidget(searchEdit);

    resultsList = new QListWidget(panel);
    resultsList->setObjectName(QStringLiteral("channelSearchResults"));
    resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    resultsList->setUniformItemSizes(false);
    resultsList->setContextMenuPolicy(Qt::CustomContextMenu);
    resultsList->setStyleSheet(QStringLiteral(
            "QListWidget#channelSearchResults { background: transparent; border: none; }"
            "QListWidget#channelSearchResults::item { padding: 4px 8px; border-radius: 6px; }"
            "QListWidget#channelSearchResults::item:hover { background: palette(mid); }"
            "QListWidget#channelSearchResults::item:selected { background: palette(highlight); "
            "color: palette(highlighted-text); }"));
    layout->addWidget(resultsList, 1);

    statusLabel = new QLabel(panel);
    statusLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text); font-size: 11px;"));
    statusLabel->setText(tr("Type to search. Enter jumps to a result, Esc closes."));
    layout->addWidget(statusLabel);

    outer->addWidget(panel, 1);

    debounceTimer = new QTimer(this);
    debounceTimer->setSingleShot(true);
    debounceTimer->setInterval(220);
    connect(debounceTimer, &QTimer::timeout, this, [this]() { runSearch(searchEdit->text()); });

    connect(searchEdit, &QLineEdit::textChanged, this, &ChannelSearchPopup::onQueryChanged);
    connect(searchEdit, &QLineEdit::returnPressed, this, &ChannelSearchPopup::activateCurrent);
    connect(resultsList, &QListWidget::itemActivated, this, [this](QListWidgetItem *) {
        activateCurrent();
    });
    connect(resultsList, &QListWidget::itemClicked, this, [this](QListWidgetItem *) {
        activateCurrent();
    });
    connect(resultsList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                showContextMenu(resultsList->viewport()->mapToGlobal(pos));
            });

    // Context menu with the four navigation actions.
    contextMenu = new QMenu(resultsList);
    contextMenu->addAction(tr("Jump to message"), this, [this]() {
        performAction([this](Core::Snowflake ch, Core::Snowflake msg) {
            emit jumpRequested(ch, msg);
        });
    });
    contextMenu->addAction(tr("Open in new tab"), this, [this]() {
        performAction([this](Core::Snowflake ch, Core::Snowflake msg) {
            emit openInNewTabRequested(ch, msg);
        });
    });
    contextMenu->addAction(tr("Open in new window"), this, [this]() {
        performAction([this](Core::Snowflake ch, Core::Snowflake msg) {
            emit openInNewWindowRequested(ch, msg);
        });
    });
    contextMenu->addAction(tr("Open in tiled view"), this, [this]() {
        performAction([this](Core::Snowflake ch, Core::Snowflake msg) {
            emit openInTiledViewRequested(ch, msg);
        });
    });

    // Fetch avatars as they arrive and refresh the matching rows.
    if (imageManager) {
        connect(imageManager, &Core::ImageManager::imageFetched, this,
                [this](const QUrl &url, const QSize &, const QPixmap &pixmap) {
                    auto it = avatarPendingRows.find(url);
                    if (it == avatarPendingRows.end())
                        return;
                    for (int row : it.value()) {
                        if (QListWidgetItem *item = resultsList->item(row)) {
                            if (auto *widget = resultsList->itemWidget(item)) {
                                if (auto *avatar = widget->findChild<QLabel *>(
                                            QStringLiteral("searchAvatar"))) {
                                    avatar->setPixmap(pixmap);
                                }
                            }
                        }
                    }
                    avatarPendingRows.erase(it);
                });
    }
}

void ChannelSearchPopup::setChannel(Core::Snowflake chId, const QString &name)
{
    channelId = chId;
    channelName = name;
    if (!channelName.isEmpty())
        setWindowTitle(tr("Search — %1").arg(channelName));
}

void ChannelSearchPopup::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    searchEdit->clear();
    resultsList->clear();
    avatarPendingRows.clear();
    statusLabel->setText(tr("Type to search. Enter jumps to a result, Esc closes."));
    searchEdit->setFocus();
    Acheron::Core::AnimationUtils::fadeIn(panel, 140);
}

void ChannelSearchPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ||
        event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
        const bool page = event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown;
        const int step = page ? 10 : 1;
        moveSelection(event->key() == Qt::Key_Up || event->key() == Qt::Key_PageUp ? -step : step);
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        activateCurrent();
        return;
    }
    QDialog::keyPressEvent(event);
}

void ChannelSearchPopup::onQueryChanged(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        // Cancel any in-flight history paging for the old query.
        ++searchGeneration;
        historySearchActive = false;
        results.clear();
        hitIds.clear();
        avatarPendingRows.clear();
        rebuildList();
        statusLabel->setText(tr("Type to search. Enter jumps to a result, Esc closes."));
        return;
    }
    debounceTimer->start();
}

void ChannelSearchPopup::runSearch(const QString &text)
{
    const QString query = text.trimmed();
    if (query.isEmpty())
        return;

    const quint64 gen = ++searchGeneration;
    historySearchActive = false;
    currentQuery = query;

    results.clear();
    hitIds.clear();
    avatarPendingRows.clear();

    // 1) Search the messages already loaded for the channel (instant).
    if (chatModel) {
        const auto hits = chatModel->searchLoadedMessages(query, 300);
        for (const auto &hit : hits) {
            ResultEntry entry;
            entry.messageId = hit.messageId;
            entry.authorId = hit.authorId;
            entry.authorAvatarHash = hit.authorAvatarHash;
            entry.authorName = hit.authorName;
            entry.content = hit.content;
            entry.timestampSecs = hit.timestampSecs;
            entry.fromHistory = false;
            results.append(entry);
            hitIds.insert(hit.messageId);
        }
    }

    // 2) Page backwards through history to find older matches, anchored at the
    //    oldest loaded message. When nothing is loaded yet, skip history.
    if (client && channelId.isValid() && chatModel) {
        const Core::Snowflake anchor = chatModel->getOldestMessageId();
        if (anchor.isValid()) {
            historySearchActive = true;
            historyPagesLeft = kMaxHistoryPages;
            nextHistoryBeforeId = anchor;
            const quint64 pageGen = gen;
            QPointer<ChannelSearchPopup> guard(this);
            client->fetchHistory(channelId, nextHistoryBeforeId, 100,
                                 [this, guard, pageGen](const Core::Result<QList<Discord::Message>> &result) {
                                     if (!guard)
                                         return;
                                     if (pageGen != searchGeneration)
                                         return;
                                     onHistoryPage(result);
                                 });
            statusLabel->setText(tr("Searching loaded messages and history…"));
        }
    }

    if (!historySearchActive)
        statusLabel->setText(tr("Searching loaded messages…"));

    rebuildList();
}

void ChannelSearchPopup::onHistoryPage(const Core::Result<QList<Discord::Message>> &result)
{
    if (!historySearchActive || historyPagesLeft <= 0)
        return;

    --historyPagesLeft;

    QList<Discord::Message> page;
    if (result.success())
        page = result.value.value();

    const QString needle = currentQuery.toCaseFolded();
    Core::Snowflake oldestOnPage;
    for (const auto &msg : page) {
        if (!msg.id.hasValue())
            continue;
        const Core::Snowflake id = msg.id.get();
        if (!oldestOnPage.isValid() || id < oldestOnPage)
            oldestOnPage = id;

        if (hitIds.contains(id))
            continue;
        if (!msg.content.hasValue())
            continue;
        if (!msg.content.get().toCaseFolded().contains(needle))
            continue;

        ResultEntry entry;
        entry.messageId = id;
        const Discord::User author = msg.author.getOr(Discord::User{});
        entry.authorId = author.id.getOr(Core::Snowflake::Invalid);
        entry.authorAvatarHash = author.avatar.getOr(QString());
        entry.authorName = author.getDisplayName();
        entry.content = msg.content.get();
        if (msg.timestamp.hasValue())
            entry.timestampSecs = msg.timestamp.get().toSecsSinceEpoch();
        entry.fromHistory = true;
        results.append(entry);
        hitIds.insert(id);
    }

    const bool haveMore = historyPagesLeft > 0 && !page.isEmpty() && oldestOnPage.isValid();
    if (haveMore) {
        nextHistoryBeforeId = oldestOnPage;
        QPointer<ChannelSearchPopup> guard(this);
        const quint64 pageGen = searchGeneration;
        client->fetchHistory(channelId, nextHistoryBeforeId, 100,
                             [this, guard, pageGen](const Core::Result<QList<Discord::Message>> &next) {
                                 if (!guard)
                                     return;
                                 if (pageGen != searchGeneration)
                                     return;
                                 onHistoryPage(next);
                             });
    } else {
        historySearchActive = false;
    }

    rebuildList();
}

void ChannelSearchPopup::rebuildList()
{
    // Preserve the user's selection across history-page rebuilds: each page
    // that streams in calls rebuildList(), which clears and re-creates every
    // row. Without this, the selection snaps back to the oldest result on
    // every page and keyboard navigation fights the streaming results.
    QListWidgetItem *previousCurrent = resultsList->currentItem();
    const qulonglong previousId =
            previousCurrent ? previousCurrent->data(Qt::UserRole).toULongLong() : 0;

    resultsList->clear();
    avatarPendingRows.clear();

    const QString needle = currentQuery.toCaseFolded();
    std::sort(results.begin(), results.end(), [](const ResultEntry &a, const ResultEntry &b) {
        if (a.timestampSecs != b.timestampSecs)
            return a.timestampSecs < b.timestampSecs;
        return a.messageId < b.messageId;
    });

    QListWidgetItem *first = nullptr;
    for (const auto &entry : results) {
        const QString snippet = makeSnippet(entry.content, needle);
        const QString time = formatTimestamp(entry.timestampSecs);

        // Chat-style row: avatar | name + time / snippet.
        auto *rowWidget = new QWidget(resultsList);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(4, 4, 4, 4);
        rowLayout->setSpacing(10);

        auto *avatar = makeAvatarLabel(rowWidget);
        avatar->setObjectName(QStringLiteral("searchAvatar"));
        rowLayout->addWidget(avatar, 0, Qt::AlignTop);

        auto *textCol = new QVBoxLayout();
        textCol->setSpacing(2);

        auto *header = new QLabel(rowWidget);
        header->setObjectName(QStringLiteral("searchHeader"));
        QString headerText;
        if (!entry.authorName.isEmpty()) {
            headerText = QStringLiteral("<b>%1</b>").arg(entry.authorName.toHtmlEscaped());
        }
        if (!time.isEmpty()) {
            if (!headerText.isEmpty())
                headerText += QStringLiteral("  ");
            headerText += QStringLiteral("<span style='color: palette(mid);'>%1</span>")
                                  .arg(time.toHtmlEscaped());
        }
        header->setTextFormat(Qt::RichText);
        header->setText(headerText);
        textCol->addWidget(header);

        auto *snippetLabel = new QLabel(snippet, rowWidget);
        snippetLabel->setObjectName(QStringLiteral("searchSnippet"));
        snippetLabel->setWordWrap(true);
        snippetLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        textCol->addWidget(snippetLabel);

        rowLayout->addLayout(textCol, 1);

        auto *item = new QListWidgetItem(resultsList);
        item->setData(Qt::UserRole, static_cast<qulonglong>(entry.messageId));
        item->setToolTip(entry.content);
        // Deterministic row height: widget->sizeHint() under-estimates the
        // wrapped snippet before the list is laid out. Compute from metrics
        // instead so multi-line snippets never clip.
        {
            const int wrapWidth = qMax(200, resultsList->viewport()->width() - 32 - 10 - 24);
            const QFontMetrics fm(snippetLabel->font());
            const int headerH = qMax(fm.height(), 20) + 2;
            const int snippetLines = qMax(1, int(std::ceil(
                    qreal(fm.horizontalAdvance(snippet)) / qMax(1, wrapWidth))));
            item->setSizeHint(QSize(0, headerH + snippetLines * fm.height() + 12));
        }
        resultsList->setItemWidget(item, rowWidget);
        if (!first)
            first = item;

        // Request the avatar.
        const QUrl url = Discord::Cdn::userAvatar(entry.authorId, entry.authorAvatarHash, 64);
        if (imageManager && !url.isEmpty()) {
            const QPixmap pm = imageManager->get(url, QSize(32, 32));
            if (!pm.isNull()) {
                avatar->setPixmap(pm);
            } else {
                avatarPendingRows[url].append(resultsList->row(item));
            }
        }
    }

    if (results.isEmpty()) {
        statusLabel->setText(historySearchActive
                                     ? tr("No matches yet — still searching older messages…")
                                     : tr("No results for \"%1\"").arg(currentQuery));
    } else {
        statusLabel->setText(historySearchActive
                                     ? tr("%1 result(s) — searching older messages…").arg(results.size())
                                     : tr("%1 result(s)").arg(results.size()));
    }

    // Restore the previous selection (matched by message id) so streaming
    // history pages don't yank the user's cursor back to the oldest result;
    // fall back to the first row only when the old selection is gone.
    if (previousId != 0) {
        for (int i = 0; i < resultsList->count(); ++i) {
            QListWidgetItem *it = resultsList->item(i);
            if (it && it->data(Qt::UserRole).toULongLong() == previousId) {
                resultsList->setCurrentItem(it);
                break;
            }
        }
    }
    if (!resultsList->currentItem() && first)
        resultsList->setCurrentItem(first);
}

void ChannelSearchPopup::activateCurrent()
{
    QListWidgetItem *item = resultsList->currentItem();
    if (!item)
        item = resultsList->item(0);
    if (!item)
        return;

    const Core::Snowflake messageId(item->data(Qt::UserRole).toULongLong());
    if (!messageId.isValid())
        return;

    emit jumpRequested(channelId, messageId);
    accept();
}

void ChannelSearchPopup::showContextMenu(const QPoint &globalPos)
{
    QListWidgetItem *item = resultsList->itemAt(resultsList->viewport()->mapFromGlobal(globalPos));
    if (!item)
        return;

    resultsList->setCurrentItem(item);
    contextMenu->popup(globalPos);
}

void ChannelSearchPopup::performAction(std::function<void(Core::Snowflake, Core::Snowflake)> emitFn)
{
    QListWidgetItem *item = resultsList->currentItem();
    if (!item)
        item = resultsList->item(0);
    if (!item)
        return;

    const Core::Snowflake messageId(item->data(Qt::UserRole).toULongLong());
    if (!messageId.isValid())
        return;

    emitFn(channelId, messageId);
    accept();
}

void ChannelSearchPopup::moveSelection(int delta)
{
    const int count = resultsList->count();
    if (count == 0)
        return;
    int row = resultsList->currentRow();
    if (row < 0)
        row = delta > 0 ? 0 : count - 1;
    else
        row = qBound(0, row + delta, count - 1);
    resultsList->setCurrentRow(row);
    resultsList->scrollToItem(resultsList->currentItem(), QAbstractItemView::PositionAtCenter);
}

} // namespace UI
} // namespace Acheron
