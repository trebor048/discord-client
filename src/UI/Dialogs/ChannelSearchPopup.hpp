#pragma once

#include "Core/Result.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QSet>
#include <QUrl>

#include <functional>

class QLineEdit;
class QListWidget;
class QLabel;
class QTimer;
class QWidget;
class QMenu;

namespace Acheron {
namespace Core {
class ImageManager;
}
namespace Discord {
class Client;
}
namespace UI {

class ChatModel;

/// Search popup for the currently viewed channel (Ctrl+F). Matches are found
/// in the messages already loaded in the chat model, then older history is
/// fetched page by page to extend the search backwards. Results render like
/// chat rows (avatar, author, timestamp, snippet). Left-click / Enter jumps
/// the main chat to the message; right-click offers open-in-tab / -window /
/// -tiled actions, all scrolled to the message.
class ChannelSearchPopup : public QDialog
{
    Q_OBJECT
public:
    explicit ChannelSearchPopup(ChatModel *chatModel, Discord::Client *client,
                                Core::ImageManager *imageManager, QWidget *parent = nullptr);

    /// Configure which channel is searched. Call before exec().
    void setChannel(Core::Snowflake channelId, const QString &channelName);

    /// Swap the REST client (e.g. after switching accounts).
    void setClient(Discord::Client *c) { client = c; }

signals:
    /// Scroll the main chat window to the message.
    void jumpRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    /// Open the message in a new tab, scrolled to it.
    void openInNewTabRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    /// Open the message in a new window (untiled), scrolled to it.
    void openInNewWindowRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    /// Open the message in a tiled side window, scrolled to it.
    void openInTiledViewRequested(Core::Snowflake channelId, Core::Snowflake messageId);

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct ResultEntry
    {
        Core::Snowflake messageId;
        Core::Snowflake authorId;
        QString authorAvatarHash;
        QString authorName;
        QString content;
        qint64 timestampSecs = 0;
        bool fromHistory = false; // found by paging history, not loaded
    };

    void onQueryChanged(const QString &text);
    void runSearch(const QString &query);
    void onHistoryPage(const Core::Result<QList<Discord::Message>> &result);
    void rebuildList();
    void activateCurrent();
    void showContextMenu(const QPoint &globalPos);
    void moveSelection(int delta);

    /// Emits the given action for the current (or first) result and closes.
    void performAction(std::function<void(Core::Snowflake, Core::Snowflake)> emitFn);

    ChatModel *chatModel;
    Discord::Client *client;
    Core::ImageManager *imageManager;

    Core::Snowflake channelId;
    QString channelName;

    QWidget *panel = nullptr;
    QLineEdit *searchEdit = nullptr;
    QListWidget *resultsList = nullptr;
    QLabel *statusLabel = nullptr;
    QTimer *debounceTimer = nullptr;
    QMenu *contextMenu = nullptr;

    QString currentQuery;
    QList<ResultEntry> results; // merged, ordered oldest -> newest
    QSet<Core::Snowflake> hitIds;
    quint64 searchGeneration = 0; // bumped on every query; stale callbacks ignored
    bool historySearchActive = false;
    int historyPagesLeft = 0;
    Core::Snowflake nextHistoryBeforeId;

    // Avatar fetches: url -> rows of the results list awaiting that avatar.
    QHash<QUrl, QVector<int>> avatarPendingRows;
    bool avatarFetchConnected = false;

    static constexpr int kMaxHistoryPages = 4;
};

} // namespace UI
} // namespace Acheron
