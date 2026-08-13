#include "ForumPostDelegate.hpp"

#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTextDocument>

#include "Core/TimeUtils.hpp"
#include "ForumPostModel.hpp"
#include "UI/Chat/ChatLayout.hpp"

namespace Acheron {
namespace UI {

static constexpr int kPadX = 12;
static constexpr int kPadTop = 8;
static constexpr int kPadBottom = 8;
static constexpr int kGap = 4;

static qreal drawReplyGlyph(QPainter *p, qreal x, qreal centerY, const QColor &color)
{
    const qreal w = 13, h = 10;
    const qreal top = centerY - h / 2;
    QPen pen(color, 1.3);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p->save();
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(QRectF(x, top, w - 2, h - 3), 2, 2);
    p->drawLine(QLineF(x + 3, top + h - 3, x + 2.5, top + h));
    p->drawLine(QLineF(x + 2.5, top + h, x + 5.5, top + h - 3));
    p->restore();
    return w;
}

struct CardLayout
{
    int titleY = 0;
    int titleH = 0;
    int tagsY = 0;
    int tagsH = 0; // 0 = no tag row
    int previewY = 0;
    int metaY = 0;
    int totalH = 0;
};

static CardLayout cardLayout(const QFont &font, bool hasTags)
{
    QFont titleFont = font;
    titleFont.setBold(true);
    const int lineH = QFontMetrics(font).height();

    CardLayout l;
    l.titleY = kPadTop;
    l.titleH = QFontMetrics(titleFont).height();
    int y = l.titleY + l.titleH + kGap;
    if (hasTags) {
        l.tagsY = y;
        l.tagsH = lineH + 4;
        y += l.tagsH + kGap;
    }
    l.previewY = y;
    y += lineH + kGap;
    l.metaY = y;
    l.totalH = y + lineH + kPadBottom;
    return l;
}

ForumPostDelegate::ForumPostDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void ForumPostDelegate::paint(QPainter *p, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    const QRect r = option.rect;
    const bool unread = index.data(ForumPostModel::UnreadRole).toBool();
    const bool isNew = index.data(ForumPostModel::IsNewRole).toBool();
    const QColor textColor = option.palette.color(QPalette::Text);
    QColor primary = textColor;
    QColor content = textColor;
    QColor muted = textColor;
    if (unread) {
        muted.setAlpha(165);
    } else {
        primary.setAlpha(120);
        content.setAlpha(110);
        muted.setAlpha(85);
    }

    if (option.state & QStyle::State_Selected) {
        QColor sel = option.palette.color(QPalette::Highlight);
        sel.setAlpha(60);
        p->fillRect(r, sel);
    } else if (option.state & QStyle::State_MouseOver) {
        QColor hover = textColor;
        hover.setAlpha(14);
        p->fillRect(r, hover);
    }

    const int x = r.left() + kPadX;

    const bool hasMedia = index.data(ForumPostModel::HasMediaRole).toBool();
    QRect thumbRect;
    int right = r.right() - kPadX;
    if (hasMedia) {
        const int side = qBound(48, r.height() - kPadTop - kPadBottom, 88);
        thumbRect = QRect(r.right() - kPadX - side, r.top() + (r.height() - side) / 2, side, side);
        right = thumbRect.left() - 12;
    }

    const QFontMetrics fm(option.font);
    const int lineH = fm.height();

    const auto tags = index.data(ForumPostModel::TagsRole).value<QList<ForumTagChip>>();
    const CardLayout cl = cardLayout(option.font, !tags.isEmpty());

    QFont titleFont = option.font;
    titleFont.setBold(true);
    const QFontMetrics tmf(titleFont);
    p->setFont(titleFont);
    const QString title = index.data(ForumPostModel::TitleRole).toString();

    int y = r.top() + cl.titleY;

    static const QString kNew = QStringLiteral("NEW");
    const int badgeGap = 8;
    const int badgeW = isNew ? (tmf.horizontalAdvance(kNew) + 12) : 0;
    const int titleAvail = right - x - (isNew ? badgeW + badgeGap : 0);
    const QString elidedTitle = tmf.elidedText(title, Qt::ElideRight, titleAvail);
    p->setPen(primary);
    p->drawText(QRect(x, y, titleAvail, tmf.height()), Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);
    if (isNew) {
        const int bx = x + tmf.horizontalAdvance(elidedTitle) + badgeGap;
        const int badgeH = tmf.height();
        const QRect badge(bx, y, badgeW, badgeH);
        QColor badgeBg = option.palette.color(QPalette::Highlight);
        p->setPen(Qt::NoPen);
        p->setBrush(badgeBg);
        p->drawRoundedRect(badge, 4, 4);
        QFont badgeFont = option.font;
        badgeFont.setBold(true);
        p->setFont(badgeFont);
        p->setPen(option.palette.color(QPalette::HighlightedText));
        p->drawText(badge, Qt::AlignCenter, kNew);
    }

    if (!tags.isEmpty()) {
        p->setFont(option.font);
        y = r.top() + cl.tagsY;
        const int chipH = cl.tagsH;
        const int chipPad = 7;
        const int emojiGap = 5;
        const int imagePx = qMin(lineH, 16);
        int tx = x;
        for (const ForumTagChip &tag : tags) {
            int emojiW = 0;
            if (!tag.emoji.isEmpty())
                emojiW = fm.horizontalAdvance(tag.emoji);
            else if (tag.hasImage)
                emojiW = imagePx;
            const int gap = emojiW > 0 ? emojiGap : 0;
            const int nameW = fm.horizontalAdvance(tag.name);
            const int w = chipPad + emojiW + gap + nameW + chipPad;
            if (tx + w > right)
                break;

            QColor chipBg = textColor;
            chipBg.setAlpha(42);
            p->setPen(Qt::NoPen);
            p->setBrush(chipBg);
            p->drawRoundedRect(QRect(tx, y, w, chipH), 4, 4);

            int cx = tx + chipPad;
            if (!tag.emoji.isEmpty()) {
                p->setPen(textColor);
                p->drawText(QRect(cx, y, emojiW, chipH), Qt::AlignCenter, tag.emoji);
            } else if (tag.hasImage && !tag.image.isNull()) {
                p->drawPixmap(QRect(cx, y + (chipH - imagePx) / 2, imagePx, imagePx), tag.image);
            }
            cx += emojiW + gap;

            p->setPen(textColor);
            p->drawText(QRect(cx, y, nameW, chipH), Qt::AlignLeft | Qt::AlignVCenter, tag.name);
            tx += w + 6;
        }
    }

    y = r.top() + cl.previewY;
    const int previewBaseline = y + fm.ascent();
    int px = x;
    p->setFont(option.font);

    const QString author = index.data(ForumPostModel::AuthorRole).toString();
    if (!author.isEmpty()) {
        QColor authorColor = index.data(ForumPostModel::AuthorColorRole).value<QColor>();
        if (!authorColor.isValid())
            authorColor = textColor;
        QString authorText = author + QStringLiteral(": ");
        if (fm.horizontalAdvance(authorText) > right - x)
            authorText = fm.elidedText(authorText, Qt::ElideRight, right - x);
        p->setPen(authorColor);
        p->drawText(QPointF(px, previewBaseline), authorText);
        px += fm.horizontalAdvance(authorText);
    }

    const auto *model = qobject_cast<const ForumPostModel *>(index.model());
    const Snowflake threadId = index.data(ForumPostModel::ThreadIdRole).toULongLong();
    QTextDocument *doc = model ? model->previewDocument(threadId, option.font) : nullptr;
    if (doc && px < right) {
        p->save();
        p->translate(px, y);
        p->setClipRect(0, 0, right - px, lineH);
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette.setColor(QPalette::Text, content);
        doc->documentLayout()->draw(p, ctx);
        p->restore();
    }

    y = r.top() + cl.metaY;
    p->setFont(option.font);
    const int baseline = y + fm.ascent();
    const qreal centerY = y + lineH / 2.0;
    qreal mx = x;

    const QString reactionEmoji = index.data(ForumPostModel::ReactionEmojiRole).toString();
    if (!reactionEmoji.isEmpty()) {
        const int rc = index.data(ForumPostModel::ReactionCountRole).toInt();
        const QString rtext = reactionEmoji + QStringLiteral(" ") + QString::number(rc);
        p->setPen(muted);
        p->drawText(QPointF(mx, baseline), rtext);
        mx += fm.horizontalAdvance(rtext) + 14;
    }

    mx += drawReplyGlyph(p, mx, centerY, muted) + 5;
    const QString replyText =
            QString::number(index.data(ForumPostModel::MessageCountRole).toInt());
    p->setPen(muted);
    p->drawText(QPointF(mx, baseline), replyText);
    mx += fm.horizontalAdvance(replyText) + 14;

    const QString unreadText = index.data(ForumPostModel::UnreadTextRole).toString();
    if (!unreadText.isEmpty()) {
        const QString newText = ForumPostDelegate::tr("(%1 New)").arg(unreadText);
        p->setPen(option.palette.color(QPalette::Highlight));
        p->drawText(QPointF(mx, baseline), newText);
        mx += fm.horizontalAdvance(newText) + 14;
        p->setPen(muted);
    }

    const QDateTime ts = index.data(ForumPostModel::TimestampRole).toDateTime();
    p->drawText(QPointF(mx, baseline), Core::TimeUtils::relativeTime(ts));

    if (hasMedia && thumbRect.isValid()) {
        const QPixmap thumb = index.data(ForumPostModel::ThumbnailRole).value<QPixmap>();
        QPainterPath clip;
        clip.addRoundedRect(thumbRect, 6, 6);
        p->save();
        p->setClipPath(clip);
        if (!thumb.isNull()) {
            ChatLayout::drawCroppedPixmap(p, thumbRect, thumb);
        } else {
            QColor ph = textColor;
            ph.setAlpha(20);
            p->fillRect(thumbRect, ph);
        }
        p->restore();
    }

    p->restore();
}

QSize ForumPostDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const bool hasTags = index.data(ForumPostModel::HasTagsRole).toBool();
    return QSize(0, cardLayout(option.font, hasTags).totalH);
}

} // namespace UI
} // namespace Acheron
