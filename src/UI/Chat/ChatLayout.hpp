#pragma once

#include <QRect>
#include <QPoint>
#include <QPalette>
#include <QTextDocument>
#include <QAbstractItemView>
#include <QModelIndex>

#include "ChatModel.hpp"

namespace Acheron {
namespace UI {
namespace ChatLayout {

constexpr int padding() noexcept
{
    return 8;
}
constexpr int blockTopPadding() noexcept
{
    return 14;
}
constexpr int avatarSize() noexcept
{
    return 32;
}
constexpr int separatorHeight() noexcept
{
    return 24;
}
constexpr int embedMaxWidth() noexcept
{
    return 400;
}
constexpr int embedBorderWidth() noexcept
{
    return 4;
}
constexpr int embedPadding() noexcept
{
    return 12;
}
constexpr int thumbnailSize() noexcept
{
    return 80;
}
constexpr int authorIconSize() noexcept
{
    return 24;
}
constexpr int footerIconSize() noexcept
{
    return 16;
}
constexpr int fieldSpacing() noexcept
{
    return 8;
}
constexpr int fileAttachmentHeight() noexcept
{
    return 48;
}
constexpr int videoAttachmentHeight() noexcept
{
    return 180;
}
constexpr int maxAttachmentWidth() noexcept
{
    return 400;
}
constexpr int replyBarHeight() noexcept
{
    return 18;
}
constexpr int replyBarSpacing() noexcept
{
    return 4;
}
constexpr int reactionPillHeight() noexcept
{
    return 22;
}
constexpr int reactionPillPadding() noexcept
{
    return 6;
}
constexpr int reactionEmojiSize() noexcept
{
    return 16;
}
constexpr int reactionSpacing() noexcept
{
    return 4;
}
constexpr int reactionRowSpacing() noexcept
{
    return 4;
}
constexpr int reactionTopMargin() noexcept
{
    return 4;
}
constexpr int quickReactionButtonSize() noexcept
{
    return 28;
}
constexpr int quickReactionBarHeight() noexcept
{
    return 32;
}
constexpr int quickReactionBarPadding() noexcept
{
    return 2;
}
constexpr int quickReactionButtonSpacing() noexcept
{
    return 2;
}
constexpr int quickReactionBarMargin() noexcept
{
    return 8;
}
constexpr int stickerSize() noexcept
{
    return 120;
}
constexpr int stickerSpacing() noexcept
{
    return 8;
}

struct AttachmentGridCell
{
    int attachmentIndex;
    QRect rect;
};

struct AttachmentGridLayout
{
    QList<AttachmentGridCell> cells;
    int totalHeight;
};

AttachmentGridLayout calculateAttachmentGrid(int count, int maxWidth);

struct EmbedFieldLayout
{
    int fieldIndex;
    QRect nameRect;
    QRect valueRect;
};

struct EmbedImageLayout
{
    int imageIndex;
    QRect rect;
};

struct EmbedLayout
{
    QRect embedRect;
    QRect contentRect;
    int contentWidth;
    bool hasThumbnail;

    QRect thumbnailRect;
    QRect providerRect;
    QRect authorRect;
    QRect titleRect;
    QRect descriptionRect;
    QRect imagesRect;
    QRect footerRect;

    QList<EmbedFieldLayout> fieldLayouts;
    QList<EmbedImageLayout> imageLayouts;

    int totalHeight;
};

struct ReactionLayout
{
    int reactionIndex;
    QRect pillRect;
    QRect emojiRect;
    QRect countRect;
};

// Floating quick-reaction bar shown on the hovered message row. Geometry is an
// overlay (does not affect row height); it is only painted/hit-tested when the
// row is hovered.
struct QuickReactionLayout
{
    QRect barRect;
    QList<QRect> buttonRects; // one per quickReactionEmojis() entry, same order
    QRect moreButtonRect;
};

struct AttachmentLayout
{
    QRect rect;
    bool isImage;
    int index;
};

struct HitRegion
{
    enum class Kind {
        Avatar,
        UsernameHeader,
        ReplyBar,
        AttachmentImage,
        AttachmentFile,
        AttachmentVideo,
        EmbedThumbnail,
        EmbedAuthor,
        EmbedTitle,
        EmbedImage,
        EmbedVideoThumbnail,
        EmbedDescription,
        EmbedFieldName,
        EmbedFieldValue,
        Reaction,

        TextLink,
        TextCursor,
        EmbedLink,
    };

    Kind kind;
    QRect rect;
    int index = -1;
    int subIndex = -1;
    QString url;
};

struct MessageLayout
{
    QRect rowRect;
    QRect separatorRect;
    QRect replyRect;
    QRect avatarRect;
    QRect headerRect;
    QRect textRect;

    bool showHeader;
    bool hasSeparator;
    bool hasReply = false;

    int textHeight;

    int attachmentsTop;
    QList<AttachmentLayout> imageLayouts;
    QList<AttachmentLayout> videoLayouts;
    QList<AttachmentLayout> fileLayouts;
    AttachmentGridLayout imageGrid;
    int attachmentsTotalHeight;

    int embedsTop;
    QList<EmbedLayout> embedLayouts;
    int embedsTotalHeight;

    int reactionsTop;
    QList<ReactionLayout> reactionLayouts;
    int reactionsTotalHeight;

    QuickReactionLayout quickReaction;

    int stickersTop;
    QList<QRect> stickerRects;
    int stickersTotalHeight;

    int totalHeight;

    QList<HitRegion> hitRegions;
};

struct LayoutContext
{
    QFont font;
    int rowWidth;
    int rowTop = 0;

    // Data from model
    bool showHeader;
    bool hasSeparator;
    QString htmlContent;
    QList<AttachmentData> attachments;
    QList<EmbedData> embeds;
    QList<StickerData> stickers;
    QList<ReactionData> reactions;
    ReplyData replyData;

    bool isSystemMessage = false;
    bool compactMode = false;
    const ChatModel *model = nullptr;
    Core::Snowflake messageId;
};

MessageLayout calculateMessageLayout(const LayoutContext &ctx);

struct ResolvedLayout
{
    MessageLayout layout;
    LayoutContext ctx;
};

LayoutContext buildContext(const QAbstractItemView *view, const QModelIndex &index,
                           const QFont &font, const QRect &rowRect, const QPalette &palette);
ResolvedLayout resolveLayout(const QAbstractItemView *view, const QModelIndex &index);
EmbedLayout calculateEmbedLayout(const EmbedData &embed, const QFont &font, int maxWidth, int left,
                                 int top, const ChatModel *model = nullptr,
                                 Core::Snowflake messageId = Core::Snowflake::Invalid,
                                 int embedIndex = -1);
int calculateAttachmentsHeight(const QList<AttachmentData> &attachments, int textWidth);
int calculateEmbedsHeight(const QList<EmbedData> &embeds, const QFont &font, int textWidth);

QRect dateSeparatorRectForRow(const QRect &rowRect);

QString richTextStyleSheet();
void setupDocument(QTextDocument &doc, const QString &htmlContent, const QFont &font,
                   int textWidth);
QRectF charRectInDocument(const QTextDocument &doc, int charIndex);

std::optional<HitRegion> hitTest(const ResolvedLayout &resolved, const QPoint &mousePos);

int hitTestCharIndex(const ResolvedLayout &resolved, const QPoint &viewportPos);
QString getLinkAt(const ResolvedLayout &resolved, const QPoint &mousePos);
std::optional<QRect> editedMarkerRectAt(const ResolvedLayout &resolved, const QPoint &mousePos);
int hitTestCharIndex(const QAbstractItemView *view, const QModelIndex &index, const QPoint &viewportPos);

QStringList extractUrls(const QString &text);

QString formatFileSize(qint64 bytes);

// Default quick-reaction emoji, shown on the hover bar. Hardcoded for now but
// kept in one place so it can be made user-configurable later.
const QStringList &quickReactionEmojis();

void drawCroppedPixmap(QPainter *painter, const QRect &targetRect, const QPixmap &pixmap);
QPixmap createBlurredPixmap(const QPixmap &source, int blurRadius = 30);

} // namespace ChatLayout
} // namespace UI
} // namespace Acheron
