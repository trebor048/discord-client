#pragma once

#include <QtWidgets>
#include <QClipboard>
#include <QGuiApplication>
#include <QPointer>
#include <QPropertyAnimation>
#include <QVariantAnimation>

#include <functional>
#include <QHash>

#include "ChatLayout.hpp"
#include "ChatModel.hpp"
#include "Core/Snowflake.hpp"

namespace Acheron {
namespace Core {
class MessageManager;
}
namespace UI {

class ImageViewer;
struct ChatCursor
{
    int row = -1;
    int index = -1;

    bool isValid() const { return row >= 0 && index >= 0; }

    bool operator==(const ChatCursor &other) const
    {
        return row == other.row && index == other.index;
    }
    bool operator!=(const ChatCursor &other) const { return !(*this == other); }
    bool operator<(const ChatCursor &other) const
    {
        if (row != other.row)
            return row < other.row;
        return index < other.index;
    }
};

class ChatView : public QListView
{
    Q_OBJECT
public:
    ChatView(QWidget *parent = nullptr);

    int hoveredRowAtPaint() const { return hoveredRow; }
    int hoveredCharIndexAtPaint() const { return hoveredChar; }
    int editingRow() const { return currentEditingIndex.isValid() ? currentEditingIndex.row() : -1; }
    bool isSearchMatchRow(int row) const { return searchMatches.contains(row); }
    bool isActiveSearchMatchRow(int row) const;
    /// Opacity (0..1) for a row that is still fading in as a newly inserted
    /// message; 1.0 for everything else. Consulted by ChatDelegate::paint.
    qreal rowAppearOpacity(int row) const;

    static constexpr int InlineEditMinHeight = 60;

    bool hasTextSelection() const;

    ChatCursor selectionStart() const;
    ChatCursor selectionEnd() const;

    void setModel(QAbstractItemModel *model) override;
    void setCurrentUserId(Core::Snowflake userId);
    void setCanPinMessages(bool canPin);
    void setCanManageMessages(bool canManage);
    void setCompactMode(bool compact);
    bool compactMode() const { return isCompactMode; }
    void setShowTimestamps(bool enabled);
    bool showTimestamps() const { return isShowTimestamps; }
    void setGuildOrder(const QStringList &orderedGuildIds);
    void setCurrentGuildId(const Core::Snowflake &guildId);
    void setMessageManager(Core::MessageManager *mgr);

    /// Empty/loading placeholder shown over an empty chat viewport.
    void showLoadingPlaceholder(const QString &text = QString());
    void showEmptyPlaceholder(const QString &text = QString());
    void hidePlaceholder();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void clearSelection();
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

signals:
    void historyRequested();
    void historyRequestFailed();
    void messageJumpLoadStarted();
    void messageJumpLoadFinished(bool success);
    void editMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId, const QString &currentContent);
    void deleteMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void pinMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void pinnedMessagesRequested(Core::Snowflake channelId);
    void replyToMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void forwardMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void addReactionRequested(Core::Snowflake channelId, Core::Snowflake messageId,
                              const QString &emoji);
    void cancelUploadRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void filesDropped(const QList<QUrl> &urls);
    void toggleReactionClicked(Core::Snowflake channelId, Core::Snowflake messageId,
                               const QString &emoji, bool currentlyReacted, bool isBurst);
    void channelMentionClicked(Core::Snowflake channelId);
    void userContextMenuRequested(Core::Snowflake userId, QPoint globalPos);

public slots:
    void onHistoryRequestFinished();
    void onHistoryRequestFailed();

    /// Snapshots the current viewport and crossfades it out over ~100ms to
    /// reveal newly switched channel content. Called by MainWindow right before
    /// the model's active channel changes.
    void beginChannelCrossfade();

    /// Scrolls a loaded message into view, centered. Returns false when the
    /// message isn't present in the current channel view.
    bool scrollToMessage(Core::Snowflake messageId);

private slots:
    void onScrollBarValueChanged(int value);
    void onRowsAboutToBeInserted(const QModelIndex &parent, int start, int end);
    void onRowsInserted(const QModelIndex &parent, int start, int end);
    void onMessageJumpReady(Core::Snowflake channelId, Core::Snowflake messageId);
    void onMessageJumpFailed(Core::Snowflake channelId, Core::Snowflake messageId);
    void onAppearTick(qreal progress);

private:
    /// Smoothly glides the view to the bottom (new messages, channel switches,
    /// jump-to-bottom button). Falls back to an instant jump when reduce-motion
    /// is on (the animation duration collapses to zero).
    void animateScrollToBottom();
    void copySelectedText();
    void copyMessageContent(const QModelIndex &index);
    void cancelPendingJump();
    void startInlineEdit(const QModelIndex &index);
    void commitInlineEdit();
    void cancelInlineEdit();
    void showSearchBar();
    void hideSearchBar();
    void updateSearchMatches();
    void moveToSearchMatch(int delta);
    void positionSearchPanel();
    void updatePlaceholderPosition();
    void openReactionPicker(Core::Snowflake channelId, Core::Snowflake messageId);
    bool handleQuickReactionClick(const QModelIndex &index,
                                  const ChatLayout::ResolvedLayout &resolved,
                                  const QPoint &pos);
    void ensureVoicePlayerPanel();
    void positionVoicePlayerPanel();

    QTextEdit *inlineEditWidget = nullptr;
    QWidget *searchPanel = nullptr;
    QLineEdit *searchEdit = nullptr;
    QLabel *searchCountLabel = nullptr;
    QTimer *searchDebounceTimer = nullptr;
    QVector<int> searchMatches;
    int activeSearchMatch = -1;

    // Hover-state layout cache: resolveLayout deep-copies role payloads, so
    // mouseMoveEvent reuses it until the hovered row or scroll offset changes.
    ChatLayout::ResolvedLayout hoverLayoutCache;
    int hoverLayoutRow = -1;
    int hoverLayoutScroll = -1;

    Core::Snowflake currentEditingMessageId = Core::Snowflake::Invalid;
    QPersistentModelIndex currentEditingIndex;

    int hoveredRow;
    int hoveredChar;

    ChatCursor selectionAnchor;
    ChatCursor selectionHead;

    bool isFetchingTop = false;

    QPersistentModelIndex anchorIndex;
    int anchorDistanceFromBottom = 0;

    bool atBottom = false;

    Core::Snowflake currentUserId = Core::Snowflake::Invalid;
    bool canPinMessages = false;
    bool pendingScroll_ = false;
    bool canManageMessages = false;
    bool isCompactMode = false;
    bool isShowTimestamps = false;
    QStringList cachedGuildOrder;
    Core::Snowflake cachedGuildId;

    Core::MessageManager *messageManager = nullptr;
    QMetaObject::Connection messageJumpReadyConnection;
    QMetaObject::Connection messageJumpFailedConnection;
    Core::Snowflake pendingJumpMessageId = Core::Snowflake::Invalid;

    QLabel *placeholderLabel = nullptr;
    QPushButton *jumpToBottomButton = nullptr;
    QPointer<QWidget> voicePlayerPanel;
    QPropertyAnimation *jumpToBottomAnimation = nullptr;
    QPropertyAnimation *scrollAnimation = nullptr;
    QVariantAnimation *channelFadeAnimation = nullptr;
    QPointer<QWidget> channelFadeOverlay;
    QVector<QMetaObject::Connection> modelConnections;

    // Newly inserted rows fade in over ~300ms (drives ChatDelegate opacity).
    QVariantAnimation *appearAnimation = nullptr;
    QHash<int, qreal> appearRows;   // row -> current opacity (0..1)
};
} // namespace UI
} // namespace Acheron