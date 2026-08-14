#include "ChatDelegate.hpp"

#include "ChatModel.hpp"
#include "ChatLayout.hpp"
#include "ChatView.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/AnimationUtils.hpp"

#include <algorithm>

namespace Acheron {
namespace UI {

static const QRegularExpression &emojiImgRegex()
{
    static const QRegularExpression re(
            R"lol(<img src="(https://cdn\.discordapp\.com/emojis/\d+\.(?:webp|png|gif)\?size=\d+)"[^>]*width="(\d+)")lol");
    return re;
}

static const QString emojiCdnPrefix = QStringLiteral("https://cdn.discordapp.com/emojis/");

static void registerEmojiResources(QTextDocument &doc, const QString &html,
                                   Core::ImageManager *imageManager)
{
    if (!imageManager || !html.contains(emojiCdnPrefix))
        return;

    auto it = emojiImgRegex().globalMatch(html);
    while (it.hasNext()) {
        auto match = it.next();
        QUrl url(match.captured(1));
        int size = match.captured(2).toInt();
        QPixmap px = imageManager->get(url, QSize(size, size));
        doc.addResource(QTextDocument::ImageResource, url, px);
    }
}

static void drawImageErrorBox(QPainter *painter, const QRect &rect,
                              const QStyleOptionViewItem &option, const QString &text)
{
    painter->fillRect(rect, QColor(60, 60, 60));
    painter->setPen(QPen(option.palette.mid().color(), 1));
    painter->drawRect(rect);
    painter->setPen(option.palette.placeholderText().color());
    QFont errorFont = option.font;
    errorFont.setPointSize(errorFont.pointSize() - 1);
    painter->setFont(errorFont);
    painter->drawText(rect.adjusted(8, 0, -8, 0), Qt::AlignCenter, text);
}

static void drawUploadProgress(QPainter *painter, const QRect &barRect, qint64 sent, qint64 total, const QPalette &palette)
{
    qreal fraction = total > 0 ? qBound(0.0, qreal(sent) / qreal(total), 1.0) : 0.0;

    if (barRect.width() < 20)
        return;

    int radius = barRect.height() / 2;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 160));
    painter->drawRoundedRect(barRect, radius, radius);
    if (fraction > 0.0) {
        QRect fillRect = barRect;
        fillRect.setWidth(qMax(barRect.height(), qRound(fraction * barRect.width())));
        painter->setBrush(palette.highlight());
        painter->drawRoundedRect(fillRect, radius, radius);
    }
    painter->restore();
}

static void drawQuickReactionBar(QPainter *painter, const QStyleOptionViewItem &option,
                                 const ChatLayout::QuickReactionLayout &bar)
{
    if (bar.barRect.isNull())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Floating bar background with a subtle border so it reads over any content.
    painter->setPen(QPen(option.palette.mid().color(), 1));
    painter->setBrush(option.palette.base().color());
    painter->drawRoundedRect(bar.barRect, 8, 8);

    QFont emojiFont = option.font;
    emojiFont.setPixelSize(ChatLayout::quickReactionButtonSize() - 8);
    painter->setFont(emojiFont);
    painter->setPen(option.palette.text().color());

    const QStringList &emojis = ChatLayout::quickReactionEmojis();
    for (int i = 0; i < bar.buttonRects.size() && i < emojis.size(); ++i)
        painter->drawText(bar.buttonRects[i], Qt::AlignCenter, emojis[i]);

    // "More" button: a bold plus glyph inviting the full emoji picker.
    if (!bar.moreButtonRect.isNull()) {
        QFont moreFont = option.font;
        moreFont.setPixelSize(ChatLayout::quickReactionButtonSize() - 10);
        moreFont.setBold(true);
        painter->setFont(moreFont);
        painter->setPen(option.palette.placeholderText().color());
        painter->drawText(bar.moreButtonRect, Qt::AlignCenter, QStringLiteral("+"));
    }

    painter->restore();
}

static ChatLayout::LayoutContext buildLayoutContext(const QStyleOptionViewItem &option,
                                                    const QModelIndex &index)
{
    ChatLayout::LayoutContext ctx;
    ctx.font = option.font;
    ctx.rowWidth = option.rect.width();
    ctx.rowTop = option.rect.top();
    ctx.showHeader = index.data(ChatModel::ShowHeaderRole).toBool();
    ctx.hasSeparator = index.data(ChatModel::DateSeparatorRole).toBool();
    ctx.htmlContent = index.data(ChatModel::HtmlRole).toString();
    ctx.attachments = index.data(ChatModel::AttachmentsRole).value<QList<AttachmentData>>();
    ctx.embeds = index.data(ChatModel::EmbedsRole).value<QList<EmbedData>>();
    ctx.stickers = index.data(ChatModel::StickersRole).value<QList<StickerData>>();
    ctx.reactions = index.data(ChatModel::ReactionsRole).value<QList<ReactionData>>();
    ctx.replyData = index.data(ChatModel::ReplyDataRole).value<ReplyData>();
    ctx.isSystemMessage = index.data(ChatModel::IsSystemMessageRole).toBool();
    ctx.compactMode = option.widget && option.widget->property("compactMode").toBool();
    ctx.model = qobject_cast<const ChatModel *>(index.model());
    ctx.messageId = index.data(ChatModel::MessageIdRole).toULongLong();

    QDateTime editedTime = index.data(ChatModel::EditedTimestampRole).toDateTime();
    if (editedTime.isValid()) {
        QColor editedColor = option.palette.text().color().darker(200);
        ctx.htmlContent += QString(R"(<span style="color: %1"> (edited)</span>)").arg(editedColor.name());
    }

    return ctx;
}

void ChatDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    painter->save();

    if (auto *chatModel = qobject_cast<const ChatModel *>(index.model()))
        chatModel->suppressImageFetch = false;

    ChatLayout::LayoutContext ctx = buildLayoutContext(option, index);
    ChatLayout::MessageLayout layout = ChatLayout::calculateMessageLayout(ctx);

    const QString username = index.data(ChatModel::UsernameRole).toString();
    const QPixmap avatar = qvariant_cast<QPixmap>(index.data(ChatModel::AvatarRole));
    const QDateTime timestamp = index.data(ChatModel::TimestampRole).toDateTime().toLocalTime();
    const ChatView *view = qobject_cast<const ChatView *>(option.widget);
    const bool isHoveredRow = view && view->hoveredRowAtPaint() == index.row();
    const bool showTimestamps = view && view->showTimestamps();

    if (view && view->isSearchMatchRow(index.row())) {
        QColor highlight = option.palette.highlight().color();
        highlight.setAlpha(view->isActiveSearchMatchRow(index.row()) ? 70 : 35);
        painter->fillRect(option.rect, highlight);
    }

    // Subtle hover highlight
    if (isHoveredRow && !view->isSearchMatchRow(index.row())) {
        QColor hoverTint = option.palette.highlight().color();
        hoverTint.setAlpha(12);
        painter->fillRect(option.rect, hoverTint);
    }

    if (layout.hasSeparator) {
        painter->setPen(QPen(option.palette.alternateBase().color(), 1));
        int midY = layout.separatorRect.center().y();
        painter->drawLine(layout.separatorRect.left() + 10, midY, layout.separatorRect.right() - 10,
                          midY);

        QString dateText = timestamp.toString("MMMM d, yyyy");

        painter->setFont(option.font);
        QFontMetrics separatorFm(option.font);
        int textWidth = separatorFm.horizontalAdvance(dateText) + 20;
        QRect textBgRect(layout.separatorRect.center().x() - textWidth / 2,
                         layout.separatorRect.top(), textWidth, layout.separatorRect.height());

        painter->fillRect(textBgRect, option.palette.base());
        painter->setPen(option.palette.text().color());
        painter->drawText(layout.separatorRect, Qt::AlignCenter, dateText);
    }

    if (layout.hasReply && !layout.replyRect.isNull()) {
        ReplyData replyData = ctx.replyData;

        // Compute reply text font/metrics first so the connector aligns with the text
        QFont replyFont = option.font;
        replyFont.setPointSizeF(replyFont.pointSizeF() * 0.85);
        QFontMetrics replyFm(replyFont);

        int textX = layout.replyRect.left() + 4;
        int textY = layout.replyRect.top();
        int availWidth = layout.replyRect.width() - 4;

        // The vertical center of the reply text line
        int textMidY = textY + replyFm.height() / 2;

        // Draw the reply connector line (L-shaped)
        QColor lineColor = option.palette.text().color();
        lineColor.setAlpha(80);
        QPen replyPen(lineColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(replyPen);

        int lineX = layout.avatarRect.center().x();
        int lineBottom = layout.avatarRect.top();
        int cornerRadius = 5;

        QPainterPath replyPath;
        replyPath.moveTo(lineX, lineBottom);
        replyPath.lineTo(lineX, textMidY + cornerRadius);
        replyPath.quadTo(lineX, textMidY, lineX + cornerRadius, textMidY);
        replyPath.lineTo(layout.replyRect.left(), textMidY);
        painter->drawPath(replyPath);

        // Draw reply text
        painter->setFont(replyFont);

        QColor replyTextColor = option.palette.text().color();
        replyTextColor.setAlpha(180);

        if (replyData.state == ReplyData::State::Present) {
            // Author name in bold, with role color if available
            QFont authorFont = replyFont;
            authorFont.setBold(true);
            painter->setFont(authorFont);
            QFontMetrics authorFm(authorFont);
            QColor authorColor = replyData.authorColor.isValid() ? replyData.authorColor : replyTextColor;
            painter->setPen(authorColor);
            QString authorName = replyData.authorName;
            int authorWidth = authorFm.horizontalAdvance(authorName);
            painter->drawText(textX, textY + authorFm.ascent(), authorName);

            // Content snippet
            painter->setFont(replyFont);
            painter->setPen(replyTextColor);
            int snippetX = textX + authorWidth + 6;
            int snippetWidth = availWidth - authorWidth - 6;
            // Don't draw snippet if there's less than 20px — it would be invisible
            if (snippetWidth >= 20) {
                QString snippet = replyData.contentSnippet;
                snippet.replace('\n', ' ');
                QString elidedSnippet = replyFm.elidedText(snippet, Qt::ElideRight, snippetWidth);
                painter->drawText(snippetX, textY + replyFm.ascent(), elidedSnippet);
            }
        } else if (replyData.state == ReplyData::State::Deleted) {
            painter->setPen(replyTextColor);
            painter->drawText(textX, textY + replyFm.ascent(),
                              tr("Original message was deleted"));
        } else {
            painter->setPen(replyTextColor);
            painter->drawText(textX, textY + replyFm.ascent(),
                              tr("Unknown message"));
        }
    }

    if (layout.showHeader) {
        if (!avatar.isNull())
            painter->drawPixmap(layout.avatarRect, avatar);

        QFont headerFont = option.font;
        headerFont.setBold(true);
        painter->setFont(headerFont);
        QFontMetrics headerFm(headerFont);

        QColor headerColor;
        if (option.state & QStyle::State_Selected) {
            headerColor = option.palette.highlightedText().color();
        } else {
            QColor roleColor = index.data(ChatModel::UsernameColorRole).value<QColor>();
            headerColor = roleColor.isValid() ? roleColor : option.palette.text().color();
        }

        QString timestampText = timestamp.toString("hh:mm");
        QFont timestampFont = option.font;
        timestampFont.setWeight(QFont::Light);
        QFontMetrics timestampFm(timestampFont);
        const int timestampWidth = timestampFm.horizontalAdvance(timestampText);
        const int spacing = 10;
        const int availableWidth = layout.headerRect.width();

        QRect nameRect = layout.headerRect;
        QRect timestampRect = layout.headerRect;
        bool drawTimestamp = showTimestamps || isHoveredRow;

        if (drawTimestamp) {
            const int maxNameWidth = availableWidth - timestampWidth - spacing;
            if (maxNameWidth > 0) {
                nameRect.setWidth(maxNameWidth);
                timestampRect.setLeft(nameRect.right() + spacing);
            } else {
                drawTimestamp = false;
            }
        }

        painter->setPen(headerColor);
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignTop,
                          headerFm.elidedText(username, Qt::ElideRight, nameRect.width()));

        if (drawTimestamp) {
            painter->setFont(timestampFont);
            painter->setPen(option.palette.text().color().darker(150));
            painter->drawText(timestampRect, Qt::AlignRight | Qt::AlignTop, timestampText);
        }
    }

    const auto *chatModel = qobject_cast<const ChatModel *>(index.model());
    Snowflake msgId = index.data(ChatModel::MessageIdRole).toULongLong();

    // New-message highlight: brief background flash for recently inserted messages
    if (chatModel && chatModel->newMessageIds.contains(msgId)) {
        QColor newMsgGlow = option.palette.highlight().color();
        newMsgGlow.setAlpha(18);
        painter->fillRect(option.rect, newMsgGlow);
    }

    if (!chatModel)
        return;

    QFont bodyFont = option.font;
    if (ctx.isSystemMessage)
        bodyFont.setItalic(true);

    DocCacheKey bodyKey = bodyDocKey(msgId);
    QTextDocument *doc = chatModel->getCachedDocument(bodyKey);
    if (!doc) {
        doc = new QTextDocument;
        ChatLayout::setupDocument(*doc, ctx.htmlContent, bodyFont, layout.textRect.width());
        registerEmojiResources(*doc, ctx.htmlContent, imageManager);
        chatModel->cacheDocument(bodyKey, doc);
    } else if (int(doc->textWidth()) != layout.textRect.width()) {
        doc->setTextWidth(layout.textRect.width());
    }

    painter->translate(layout.textRect.topLeft());

    QAbstractTextDocumentLayout::PaintContext paintCtx;

    bool isPending = index.data(ChatModel::IsPendingRole).toBool();
    bool isErrored = index.data(ChatModel::IsErroredRole).toBool();

    QColor textColor;
    if (isErrored) {
        textColor = Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError);
    } else if (isPending) {
        textColor = option.palette.text().color().lighter(50);
    } else if (ctx.isSystemMessage) {
        textColor = option.palette.text().color();
        textColor.setAlpha(140);
    } else {
        textColor = (option.state & QStyle::State_Selected)
                            ? option.palette.highlightedText().color()
                            : option.palette.text().color();
    }
    paintCtx.palette.setColor(QPalette::Text, textColor);

    if (view && view->hasTextSelection()) {
        auto start = view->selectionStart();
        auto end = view->selectionEnd();
        int r = index.row();

        if (r >= start.row && r <= end.row) {
            int startChar = 0;
            int endChar = -1;

            if (r == start.row)
                startChar = start.index;
            if (r == end.row)
                endChar = end.index;

            QTextCursor cursor(doc);
            cursor.setPosition(startChar);

            if (endChar == -1)
                cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            else
                cursor.setPosition(endChar, QTextCursor::KeepAnchor);

            QAbstractTextDocumentLayout::Selection sel;
            sel.cursor = cursor;
            sel.format.setBackground(option.palette.highlight());
            sel.format.setForeground(option.palette.highlightedText());
            paintCtx.selections.append(sel);
        }
    }

    doc->documentLayout()->draw(painter, paintCtx);

    painter->restore();
    painter->save();

    QList<AttachmentData> attachments = ctx.attachments;

    for (const auto &imgLayout : layout.imageLayouts) {
        if (imgLayout.index >= attachments.size())
            continue;

        const auto &att = attachments[imgLayout.index];
        bool isSingleImage =
                (layout.imageLayouts.size() == 1 &&
                 std::count_if(attachments.begin(), attachments.end(),
                               [](const AttachmentData &a) { return a.isImage; }) == 1);

        bool showBlurred = att.isSpoiler;
        if (showBlurred && chatModel->isSpoilerRevealed(att.id))
            showBlurred = false;

        // Handle animated GIF attachments
        Core::GifAnimation *gifAnim = nullptr;
        if (att.isGif && !att.proxyUrl.isEmpty() && !showBlurred &&
            !att.proxyUrl.isLocalFile()) {
            gifAnim = chatModel->ensureGifAnimation(att.proxyUrl, index.row());
        }

        if (att.isGif && gifAnim && gifAnim->isReady()) {
            QPixmap frame = gifAnim->currentFrame();
            if (!frame.isNull()) {
                QPixmap displayPixmap = frame;

                if (isSingleImage)
                    painter->drawPixmap(imgLayout.rect, displayPixmap);
                else
                    ChatLayout::drawCroppedPixmap(painter, imgLayout.rect, displayPixmap);

                // Draw "GIF" label badge
                QFont gifBadgeFont = option.font;
                gifBadgeFont.setPointSize(gifBadgeFont.pointSize() - 2);
                gifBadgeFont.setBold(true);
                QFontMetrics gifFm(gifBadgeFont);
                QString gifLabel = QStringLiteral("GIF");
                int badgeWidth = gifFm.horizontalAdvance(gifLabel) + 8;
                QRect badgeRect(imgLayout.rect.left() + 4, imgLayout.rect.top() + 4,
                                badgeWidth, gifFm.height() + 4);
                painter->fillRect(badgeRect, QColor(0, 0, 0, 160));
                painter->setPen(Qt::white);
                painter->setFont(gifBadgeFont);
                painter->drawText(badgeRect, Qt::AlignCenter, gifLabel);
                continue;
            }
        }

        // Show loading progress for GIFs still loading from network
        if (att.isGif && gifAnim && gifAnim->isLoading()) {
            painter->fillRect(imgLayout.rect, QColor(60, 60, 60));

            int progress = gifAnim->loadProgress();
            if (progress >= 0) {
                // Draw progress bar
                QRect barRect(imgLayout.rect.left() + 8, imgLayout.rect.bottom() - 13,
                              imgLayout.rect.width() - 16, 6);
                drawUploadProgress(painter, barRect, progress, 100, option.palette);
            }

            painter->setPen(option.palette.placeholderText().color());
            QFont progressFont = option.font;
            progressFont.setPointSize(progressFont.pointSize() - 1);
            painter->setFont(progressFont);
            painter->drawText(imgLayout.rect, Qt::AlignCenter, tr("Loading GIF..."));
            continue;
        }

        if (!att.pixmap.isNull()) {
            if (!att.proxyUrl.isLocalFile() &&
                chatModel->hasImageFailed(att.proxyUrl, att.displaySize)) {
                drawImageErrorBox(painter, imgLayout.rect, option,
                                  tr("Image failed to load"));
                continue;
            }
            QPixmap displayPixmap = att.pixmap;

            if (showBlurred)
                displayPixmap = ChatLayout::createBlurredPixmap(att.pixmap, 60);

            if (isSingleImage)
                painter->drawPixmap(imgLayout.rect, displayPixmap);
            else
                ChatLayout::drawCroppedPixmap(painter, imgLayout.rect, displayPixmap);

            if (showBlurred) {
                painter->fillRect(imgLayout.rect, QColor(0, 0, 0, 100));

                QFont spoilerFont = option.font;
                spoilerFont.setBold(true);
                spoilerFont.setPointSize(spoilerFont.pointSize() + 2);
                painter->setFont(spoilerFont);
                painter->setPen(Qt::white);
                painter->drawText(imgLayout.rect, Qt::AlignCenter, tr("SPOILER"));
            }

            // Show "GIF" badge for static preview of GIF attachments (before animation starts)
            if (att.isGif && !showBlurred && !gifAnim) {
                QFont gifBadgeFont = option.font;
                gifBadgeFont.setPointSize(gifBadgeFont.pointSize() - 2);
                gifBadgeFont.setBold(true);
                QFontMetrics gifFm(gifBadgeFont);
                QString gifLabel = QStringLiteral("GIF");
                int badgeWidth = gifFm.horizontalAdvance(gifLabel) + 8;
                QRect badgeRect(imgLayout.rect.left() + 4, imgLayout.rect.top() + 4,
                                badgeWidth, gifFm.height() + 4);
                painter->fillRect(badgeRect, QColor(0, 0, 0, 160));
                painter->setPen(Qt::white);
                painter->setFont(gifBadgeFont);
                painter->drawText(badgeRect, Qt::AlignCenter, gifLabel);
            }
        } else {
            painter->fillRect(imgLayout.rect, QColor(60, 60, 60));
            painter->setPen(option.palette.text().color());
            painter->drawText(imgLayout.rect, Qt::AlignCenter, tr("Loading..."));
        }

        if (isPending && att.uploadSent >= 0) {
            QRect barRect(imgLayout.rect.left() + 8, imgLayout.rect.bottom() - 13,
                          imgLayout.rect.width() - 16, 6);
            drawUploadProgress(painter, barRect, att.uploadSent, att.uploadTotal, option.palette);
        }
    }

    // --- Voice Messages ---
    for (int i = 0; i < attachments.size(); ++i) {
        const auto &att = attachments[i];
        if (!att.isVoice)
            continue;

        // Find the layout rect for this voice attachment by searching fileLayouts
        // (voice messages are not images, so they always go into fileLayouts)
        QRect voiceRect;
        for (const auto &fl : layout.fileLayouts) {
            if (fl.index == i) {
                voiceRect = fl.rect;
                break;
            }
        }
        if (voiceRect.isNull())
            continue;

        // Sanity: if the rect is too narrow to render anything useful, skip
        if (voiceRect.width() < 100)
            continue;

        // Background
        painter->fillRect(voiceRect, option.palette.alternateBase().color().darker(110));
        painter->setPen(QPen(option.palette.mid().color(), 1));
        painter->drawRect(voiceRect);

        // Play button
        int playX = voiceRect.left() + 10;
        int playY = voiceRect.top() + (voiceRect.height() - 32) / 2;
        QRect playRect(playX, playY, 32, 32);
        painter->fillRect(playRect, QColor(0, 0, 0, 100));
        painter->setPen(Qt::white);
        QPolygon playTriangle;
        playTriangle << QPoint(playRect.left() + 10, playRect.top() + 7)
                      << QPoint(playRect.left() + 10, playRect.bottom() - 7)
                      << QPoint(playRect.right() - 7, playRect.center().y());
        painter->drawPolygon(playTriangle);

        // Waveform visualization
        int waveLeft = playRect.right() + 12;
        int waveRight = voiceRect.right() - 12;
        int waveTop = playY + 4;
        int waveHeight = 24;
        int waveMidY = waveTop + waveHeight / 2;

        if (!att.waveform.isEmpty() && waveRight > waveLeft) {
            const auto &wf = att.waveform;
            int availableWidth = waveRight - waveLeft;
            int barCount = qMin(wf.size(), availableWidth / 3);
            // Guard: at least 1 bar and no division by zero
            if (barCount > 0) {
                int barWidth = qMax(1, availableWidth / barCount - 1);

                painter->setPen(Qt::NoPen);
                for (int b = 0; b < barCount; ++b) {
                    // Map byte 0-255 to bar height 0-waveHeight
                    int sampleIdx = b * wf.size() / barCount;
                    uint8_t val = static_cast<uint8_t>(wf[sampleIdx]);
                    int barHeight = qMax(2, static_cast<int>(val * waveHeight / 255.0));
                    int barX = waveLeft + b * (barWidth + 1);
                    int barY = waveMidY - barHeight / 2;
                    painter->fillRect(barX, barY, barWidth, barHeight,
                                      option.palette.highlight().color());
                }
            }
        }

        // Duration label
        int mins = static_cast<int>(att.durationSecs) / 60;
        int secs = static_cast<int>(att.durationSecs) % 60;
        QString durText = att.durationSecs > 0
                ? QString("%1:%2").arg(mins).arg(secs, 2, 10, QLatin1Char('0'))
                : QStringLiteral("--:--");

        QFont durFont = option.font;
        durFont.setPointSize(durFont.pointSize() - 1);
        painter->setFont(durFont);
        painter->setPen(option.palette.placeholderText().color());
        QFontMetrics durFm(durFont);
        QRect durRect(voiceRect.right() - 60, playY, 55, 32);
        painter->drawText(durRect, Qt::AlignRight | Qt::AlignVCenter, durText);
    }

    // --- Video Attachments ---
    for (const auto &videoLayout : layout.videoLayouts) {
        if (videoLayout.index >= attachments.size())
            continue;

        const auto &att = attachments[videoLayout.index];
        QRect videoRect = videoLayout.rect;

        // Dark video tile background
        painter->fillRect(videoRect, QColor(0, 0, 0, 200));
        painter->setPen(QPen(option.palette.mid().color(), 1));
        painter->drawRect(videoRect);

        // Centered play-button overlay
        int playSize = 48;
        QRect playRect(videoRect.center().x() - playSize / 2,
                       videoRect.center().y() - playSize / 2, playSize, playSize);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(QColor(255, 255, 255, 48));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(playRect);
        painter->setBrush(Qt::white);
        QPolygon playTriangle;
        int triOffset = playSize / 4;
        playTriangle << QPoint(playRect.left() + triOffset + 3, playRect.top() + triOffset)
                     << QPoint(playRect.left() + triOffset + 3, playRect.bottom() - triOffset)
                     << QPoint(playRect.right() - triOffset, playRect.center().y());
        painter->drawPolygon(playTriangle);
        painter->setRenderHint(QPainter::Antialiasing, false);

        // Filename + size at the bottom of the tile
        QFont nameFont = option.font;
        nameFont.setPointSize(nameFont.pointSize() - 1);
        painter->setFont(nameFont);
        painter->setPen(option.palette.text().color());
        QFontMetrics nameFm(nameFont);
        QRect nameRect(videoRect.left() + 10, videoRect.bottom() - 36,
                       videoRect.width() - 20, nameFm.height());
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                          nameFm.elidedText(att.filename, Qt::ElideMiddle, nameRect.width()));

        QFont sizeFont = option.font;
        sizeFont.setPointSize(sizeFont.pointSize() - 2);
        painter->setFont(sizeFont);
        painter->setPen(option.palette.placeholderText().color());
        QFontMetrics sizeFm(sizeFont);
        QRect sizeRect(nameRect.left(), nameRect.bottom(), nameRect.width(), sizeFm.height());
        painter->drawText(sizeRect, Qt::AlignLeft | Qt::AlignVCenter,
                          ChatLayout::formatFileSize(att.fileSizeBytes));
    }

    constexpr int fileAttachmentPadding = 8;

    for (const auto &fileLayout : layout.fileLayouts) {
        if (fileLayout.index >= attachments.size())
            continue;

        const auto &att = attachments[fileLayout.index];

        // Voice messages are rendered separately above; skip here
        if (att.isVoice)
            continue;
        QRect fileRect = fileLayout.rect;

        QColor bgColor = option.palette.alternateBase().color();
        painter->fillRect(fileRect, bgColor);

        painter->setPen(QPen(option.palette.mid().color(), 1));
        painter->drawRect(fileRect);

        QRect iconRect(fileRect.left() + fileAttachmentPadding,
                       fileRect.top() + fileAttachmentPadding, 32, 32);
        painter->fillRect(iconRect, option.palette.mid());
        const qreal fileIconDpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap fileIcon = Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::FileText, 20, option.palette.text().color(), fileIconDpr);
        QRect fileGlyphRect(0, 0, 20, 20);
        fileGlyphRect.moveCenter(iconRect.center());
        painter->drawPixmap(fileGlyphRect, fileIcon);

        int textLeft = iconRect.right() + fileAttachmentPadding;
        QRect textAreaRect(textLeft, fileRect.top() + fileAttachmentPadding,
                           fileRect.width() - (textLeft - fileRect.left()) - fileAttachmentPadding,
                           fileRect.height() - fileAttachmentPadding * 2);

        QFont filenameFont = option.font;
        painter->setFont(filenameFont);
        painter->setPen(option.palette.text().color());
        QFontMetrics filenameFm(filenameFont);
        QString elidedFilename =
                filenameFm.elidedText(att.filename, Qt::ElideMiddle, textAreaRect.width());
        painter->drawText(textAreaRect.left(), textAreaRect.top() + filenameFm.ascent(),
                          elidedFilename);

        bool uploading = isPending && att.uploadSent >= 0;

        QFont sizeFont = option.font;
        sizeFont.setPointSize(sizeFont.pointSize() - 1);
        painter->setFont(sizeFont);
        painter->setPen(option.palette.placeholderText().color());
        QString sizeText = ChatLayout::formatFileSize(att.fileSizeBytes);
        if (uploading && att.uploadTotal > 0)
            sizeText = ChatLayout::formatFileSize(att.uploadSent) + " / " +
                       ChatLayout::formatFileSize(att.uploadTotal);
        QFontMetrics sizeFm(sizeFont);
        painter->drawText(textAreaRect.left(),
                          textAreaRect.top() + filenameFm.height() + sizeFm.ascent(), sizeText);

        if (uploading) {
            QRect barRect(fileRect.left() + 2, fileRect.bottom() - 5, fileRect.width() - 4, 4);
            drawUploadProgress(painter, barRect, att.uploadSent, att.uploadTotal, option.palette);
        }
    }

    QList<EmbedData> embeds = ctx.embeds;

    for (int embedIdx = 0; embedIdx < layout.embedLayouts.size() && embedIdx < embeds.size();
         ++embedIdx) {
        const auto &embedLayout = layout.embedLayouts[embedIdx];
        const auto &embed = embeds[embedIdx];

        if (embed.type == EmbedType::Gifv) {
            const QRect imgRect = embedLayout.imagesRect;

            // Try to play the GIF itself (the gifv thumbnail URL points at the
            // animated source via the media proxy) instead of a static frame.
            Core::GifAnimation *gifAnim = nullptr;
            if (!embed.thumbnailUrl.isEmpty())
                gifAnim = chatModel->ensureGifAnimation(embed.thumbnailUrl, index.row());

            const bool thumbnailFailed =
                    !embed.thumbnailUrl.isEmpty() &&
                    chatModel->hasImageFailed(embed.thumbnailUrl, embed.thumbnailSize);

            if (gifAnim && gifAnim->isReady() && !imgRect.isNull()) {
                QPixmap frame = gifAnim->currentFrame();
                if (!frame.isNull()) {
                    QPixmap scaledFrame = frame.scaled(imgRect.size(), Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation);
                    painter->drawPixmap(imgRect.topLeft(), scaledFrame);
                }
            } else if (gifAnim && gifAnim->isLoading() && !imgRect.isNull() &&
                       embed.thumbnail.isNull()) {
                painter->fillRect(imgRect, QColor(60, 60, 60));
                painter->setPen(option.palette.placeholderText().color());
                painter->drawText(imgRect, Qt::AlignCenter, tr("Loading GIF..."));
            } else if (thumbnailFailed && !imgRect.isNull()) {
                drawImageErrorBox(painter, imgRect, option, tr("GIF failed to load"));
            } else if (!imgRect.isNull() && !embed.thumbnail.isNull()) {
                // Static first frame while the animation loads (or as fallback
                // when the animation itself failed but the still image worked).
                QPixmap scaledThumb = embed.thumbnail.scaled(
                        embed.thumbnailSize * embed.thumbnail.devicePixelRatio(),
                        Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter->drawPixmap(imgRect.topLeft(), scaledThumb);
            }

            QFont gifFont = option.font;
            gifFont.setPointSize(gifFont.pointSize() - 2);
            painter->setFont(gifFont);
            painter->setPen(option.palette.placeholderText().color());
            QFontMetrics gifFm(gifFont);
            int gifLabelTop = embedLayout.imagesRect.isNull() ? embedLayout.embedRect.top()
                                                              : embedLayout.imagesRect.bottom();
            painter->drawText(embedLayout.embedRect.left(), gifLabelTop + gifFm.ascent() + 2,
                              "GIF");
            continue;
        }

        if (embed.type == EmbedType::Image) {
            if (!embedLayout.imagesRect.isNull() &&
                chatModel->hasImageFailed(embed.thumbnailUrl, embed.thumbnailSize)) {
                drawImageErrorBox(painter, embedLayout.imagesRect, option,
                                  tr("Image failed to load"));
                continue;
            }
            if (!embedLayout.imagesRect.isNull() && !embed.thumbnail.isNull()) {
                QPixmap scaledThumb = embed.thumbnail.scaled(
                        embed.thumbnailSize * embed.thumbnail.devicePixelRatio(),
                        Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter->drawPixmap(embedLayout.imagesRect.topLeft(), scaledThumb);
            }
            continue;
        }

        painter->fillRect(embedLayout.embedRect, option.palette.base().color().darker(110));

        QRect borderRect(embedLayout.embedRect.left(), embedLayout.embedRect.top(),
                         ChatLayout::embedBorderWidth(), embedLayout.embedRect.height());
        painter->fillRect(borderRect, embed.color);

        if (embedLayout.hasThumbnail && !embedLayout.thumbnailRect.isNull()) {
            QPixmap thumb = !embed.thumbnail.isNull() ? embed.thumbnail : embed.videoThumbnail;
            if (!thumb.isNull()) {
                QPixmap scaledThumb = thumb.scaled(embedLayout.thumbnailRect.size(),
                                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter->drawPixmap(embedLayout.thumbnailRect.topLeft(), scaledThumb);
            }
        }

        if (!embed.providerName.isEmpty() && !embedLayout.providerRect.isNull()) {
            QFont providerFont = option.font;
            providerFont.setPointSize(providerFont.pointSize() - 2);
            painter->setFont(providerFont);
            painter->setPen(option.palette.placeholderText().color());
            QFontMetrics providerFm(providerFont);
            painter->drawText(embedLayout.providerRect.left(),
                              embedLayout.providerRect.top() + providerFm.ascent(),
                              embed.providerName);
        }

        if (!embed.authorName.isEmpty() && !embedLayout.authorRect.isNull()) {
            int authorX = embedLayout.authorRect.left();
            int authorY = embedLayout.authorRect.top();
            if (!embed.authorIcon.isNull()) {
                QRect iconRect(authorX, authorY, ChatLayout::authorIconSize(),
                               ChatLayout::authorIconSize());
                painter->drawPixmap(iconRect, embed.authorIcon);
                authorX += ChatLayout::authorIconSize() + 6;
            }
            QFont authorFont = option.font;
            authorFont.setPointSize(authorFont.pointSize() - 1);
            authorFont.setBold(true);
            painter->setFont(authorFont);
            painter->setPen(option.palette.text().color());
            QFontMetrics authorFm(authorFont);
            int textY = authorY + (ChatLayout::authorIconSize() - authorFm.height()) / 2 +
                        authorFm.ascent();
            painter->drawText(authorX, textY, embed.authorName);
        }

        if (!embed.title.isEmpty() && !embedLayout.titleRect.isNull()) {
            QFont titleFont = option.font;
            titleFont.setBold(true);
            painter->setFont(titleFont);
            QColor titleColor = !embed.url.isEmpty() ? option.palette.link().color()
                                                     : option.palette.text().color();
            painter->setPen(titleColor);

            QString titleHtml = !embed.titleParsed.isEmpty() ? embed.titleParsed : embed.title;
            DocCacheKey titleKey = embedTitleDocKey(msgId, embedIdx);
            QTextDocument *titleDoc = chatModel->getCachedDocument(titleKey);
            if (!titleDoc) {
                titleDoc = new QTextDocument;
                titleDoc->setDefaultFont(titleFont);
                titleDoc->setTextWidth(embedLayout.titleRect.width());
                registerEmojiResources(*titleDoc, titleHtml, imageManager);
                titleDoc->setHtml(titleHtml);
                chatModel->cacheDocument(titleKey, titleDoc);
            } else if (int(titleDoc->textWidth()) != embedLayout.titleRect.width()) {
                titleDoc->setTextWidth(embedLayout.titleRect.width());
            }

            painter->save();
            painter->translate(embedLayout.titleRect.topLeft());
            QAbstractTextDocumentLayout::PaintContext titleCtx;
            titleCtx.palette.setColor(QPalette::Text, titleColor);
            titleDoc->documentLayout()->draw(painter, titleCtx);
            painter->restore();
        }

        if (!embed.description.isEmpty() && !embedLayout.descriptionRect.isNull()) {
            QFont descFont = option.font;
            painter->setFont(descFont);
            painter->setPen(option.palette.text().color());

            QString descHtml = !embed.descriptionParsed.isEmpty() ? embed.descriptionParsed
                                                                  : embed.description;
            DocCacheKey descKey = embedDescDocKey(msgId, embedIdx);
            QTextDocument *descDoc = chatModel->getCachedDocument(descKey);
            if (!descDoc) {
                descDoc = new QTextDocument;
                descDoc->setDefaultFont(descFont);
                descDoc->setTextWidth(embedLayout.descriptionRect.width());
                registerEmojiResources(*descDoc, descHtml, imageManager);
                descDoc->setHtml(descHtml);
                chatModel->cacheDocument(descKey, descDoc);
            } else if (int(descDoc->textWidth()) != embedLayout.descriptionRect.width()) {
                descDoc->setTextWidth(embedLayout.descriptionRect.width());
            }

            painter->save();
            painter->translate(embedLayout.descriptionRect.topLeft());
            QAbstractTextDocumentLayout::PaintContext descCtx;
            descCtx.palette.setColor(QPalette::Text, option.palette.text().color());
            descDoc->documentLayout()->draw(painter, descCtx);
            painter->restore();
        }

        QFont fieldNameFont = option.font;
        fieldNameFont.setBold(true);
        QFontMetrics fieldNameFm(fieldNameFont);

        for (const auto &fieldLayout : embedLayout.fieldLayouts) {
            if (fieldLayout.fieldIndex >= embed.fields.size())
                continue;

            const auto &field = embed.fields[fieldLayout.fieldIndex];
            int fi = fieldLayout.fieldIndex;

            QString nameHtml = !field.nameParsed.isEmpty() ? field.nameParsed : field.name;
            DocCacheKey nameKey = embedFieldNameDocKey(msgId, embedIdx, fi);
            QTextDocument *nameDoc = chatModel->getCachedDocument(nameKey);
            if (!nameDoc) {
                nameDoc = new QTextDocument;
                nameDoc->setDefaultFont(fieldNameFont);
                nameDoc->setTextWidth(fieldLayout.nameRect.width());
                registerEmojiResources(*nameDoc, nameHtml, imageManager);
                nameDoc->setHtml(nameHtml);
                chatModel->cacheDocument(nameKey, nameDoc);
            } else if (int(nameDoc->textWidth()) != fieldLayout.nameRect.width()) {
                nameDoc->setTextWidth(fieldLayout.nameRect.width());
            }

            painter->save();
            painter->translate(fieldLayout.nameRect.topLeft());
            QAbstractTextDocumentLayout::PaintContext nameCtx;
            nameCtx.palette.setColor(QPalette::Text, option.palette.text().color());
            nameDoc->documentLayout()->draw(painter, nameCtx);
            painter->restore();

            QString valueHtml = !field.valueParsed.isEmpty() ? field.valueParsed : field.value;
            DocCacheKey valueKey = embedFieldValueDocKey(msgId, embedIdx, fi);
            QTextDocument *valueDoc = chatModel->getCachedDocument(valueKey);
            if (!valueDoc) {
                valueDoc = new QTextDocument;
                valueDoc->setDefaultFont(option.font);
                valueDoc->setTextWidth(fieldLayout.valueRect.width());
                registerEmojiResources(*valueDoc, valueHtml, imageManager);
                valueDoc->setHtml(valueHtml);
                chatModel->cacheDocument(valueKey, valueDoc);
            } else if (int(valueDoc->textWidth()) != fieldLayout.valueRect.width()) {
                valueDoc->setTextWidth(fieldLayout.valueRect.width());
            }

            painter->save();
            painter->translate(fieldLayout.valueRect.topLeft());
            QAbstractTextDocumentLayout::PaintContext valueCtx;
            valueCtx.palette.setColor(QPalette::Text, option.palette.text().color().darker(110));
            valueDoc->documentLayout()->draw(painter, valueCtx);
            painter->restore();
        }

        if (!embed.images.isEmpty()) {
            for (const auto &imgLayout : embedLayout.imageLayouts) {
                if (imgLayout.imageIndex >= embed.images.size())
                    continue;

                const auto &img = embed.images[imgLayout.imageIndex];
                bool isSingleImage = (embed.images.size() == 1);

                if (!img.pixmap.isNull()) {
                    if (isSingleImage) {
                        QPixmap scaledImage =
                                img.pixmap.scaled(img.displaySize * img.pixmap.devicePixelRatio(),
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        painter->drawPixmap(imgLayout.rect.topLeft(), scaledImage);
                    } else {
                        ChatLayout::drawCroppedPixmap(painter, imgLayout.rect, img.pixmap);
                    }
                } else {
                    painter->fillRect(imgLayout.rect, QColor(60, 60, 60));
                    painter->setPen(option.palette.text().color());
                    painter->drawText(imgLayout.rect, Qt::AlignCenter, "Loading...");
                }
            }
        } else if (!embed.videoThumbnail.isNull() && embed.thumbnail.isNull() &&
                   !embedLayout.imagesRect.isNull()) {
            QPixmap scaledVideo = embed.videoThumbnail.scaled(
                    embedLayout.imagesRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter->drawPixmap(embedLayout.imagesRect.topLeft(), scaledVideo);

            int playSize = std::min(
                    48,
                    std::min(embedLayout.imagesRect.width(), embedLayout.imagesRect.height()) / 2);
            QRect playRect(embedLayout.imagesRect.center().x() - playSize / 2,
                           embedLayout.imagesRect.center().y() - playSize / 2, playSize, playSize);
            painter->setBrush(QColor(0, 0, 0, 180));
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(playRect);
            painter->setPen(Qt::white);
            QPolygon triangle;
            int triOffset = playSize / 4;
            triangle << QPoint(playRect.left() + triOffset + 4, playRect.top() + triOffset)
                     << QPoint(playRect.left() + triOffset + 4, playRect.bottom() - triOffset)
                     << QPoint(playRect.right() - triOffset, playRect.center().y());
            painter->setBrush(Qt::white);
            painter->drawPolygon(triangle);
        }

        if (!embed.footerText.isEmpty() && !embedLayout.footerRect.isNull()) {
            int footerX = embedLayout.footerRect.left();
            int footerY = embedLayout.footerRect.top();
            if (!embed.footerIcon.isNull()) {
                QRect iconRect(footerX, footerY, ChatLayout::footerIconSize(),
                               ChatLayout::footerIconSize());
                painter->drawPixmap(iconRect, embed.footerIcon);
                footerX += ChatLayout::footerIconSize() + 6;
            }
            QFont footerFont = option.font;
            footerFont.setPointSize(footerFont.pointSize() - 2);
            painter->setFont(footerFont);
            painter->setPen(option.palette.placeholderText().color());
            QFontMetrics footerFm(footerFont);

            QString footerText = embed.footerText;
            if (embed.timestamp.isValid())
                footerText += " • " + embed.timestamp.toLocalTime().toString("MMM d, yyyy h:mm AP");

            int textY = footerY + (ChatLayout::footerIconSize() - footerFm.height()) / 2 +
                        footerFm.ascent();
            painter->drawText(footerX, textY, footerText);
        }
    }

    // --- Stickers ---
    QList<StickerData> stickers = ctx.stickers;

    for (int i = 0; i < layout.stickerRects.size(); ++i) {
        if (i >= stickers.size())
            break;
        const auto &sticker = stickers[i];
        QRect sr = layout.stickerRects[i];

        // Use animated current frame if available, fall back to static pixmap
        QPixmap stickerPixmap = sticker.isAnimated && !sticker.currentFrame.isNull()
                ? sticker.currentFrame
                : sticker.pixmap;

        if (!stickerPixmap.isNull()) {
            QPixmap scaled = stickerPixmap.scaled(sr.size(), Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation);
            int x = sr.x() + (sr.width() - scaled.width()) / 2;
            int y = sr.y() + (sr.height() - scaled.height()) / 2;
            painter->drawPixmap(x, y, scaled);
        } else if (sticker.isLoading) {
            painter->setPen(option.palette.placeholderText().color());
            painter->drawText(sr, Qt::AlignCenter, QStringLiteral("..."));
        } else {
            painter->setPen(option.palette.placeholderText().color());
            painter->drawText(sr, Qt::AlignCenter, QStringLiteral(":%1:").arg(sticker.name));
        }
    }

    QList<ReactionData> reactions = ctx.reactions;

    for (const auto &reactionLayout : layout.reactionLayouts) {
        if (reactionLayout.reactionIndex >= reactions.size())
            continue;

        const auto &reaction = reactions[reactionLayout.reactionIndex];

        QColor pillBg;
        if (reaction.isBurst && reaction.burstTintColor.isValid()) {
            pillBg = reaction.burstTintColor;
            pillBg.setAlpha(40);
        } else {
            pillBg = option.palette.alternateBase().color();
        }

        QColor borderColor;
        int borderWidth;
        if (reaction.me) {
            borderColor = option.palette.highlight().color();
            borderWidth = 1;
        } else {
            borderColor = option.palette.mid().color();
            borderWidth = 1;
        }

        painter->setPen(QPen(borderColor, borderWidth));
        painter->setBrush(pillBg);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->drawRoundedRect(reactionLayout.pillRect, 6, 6);
        painter->setRenderHint(QPainter::Antialiasing, false);

        if (reaction.emojiId.isValid()) {
            if (!reaction.emojiPixmap.isNull())
                painter->drawPixmap(reactionLayout.emojiRect, reaction.emojiPixmap);
        } else {
            // render smaller than the rect to fit within pill
            QFont emojiFont = option.font;
            emojiFont.setPixelSize(ChatLayout::reactionEmojiSize() - 4);
            painter->setFont(emojiFont);
            painter->setPen(option.palette.text().color());
            painter->drawText(reactionLayout.emojiRect, Qt::AlignCenter, reaction.emojiName);
        }

        QFont countFont = option.font;
        countFont.setPointSizeF(countFont.pointSizeF() * 0.85);
        painter->setFont(countFont);

        QColor countColor;
        if (reaction.me)
            countColor = option.palette.highlight().color();
        else
            countColor = option.palette.text().color();

        painter->setPen(countColor);
        painter->drawText(reactionLayout.countRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QString::number(reaction.count));
    }

    // Quick-reaction hover bar — a floating overlay shown only on the hovered row.
    if (isHoveredRow && !layout.hasSeparator && !ctx.isSystemMessage)
        drawQuickReactionBar(painter, option, layout.quickReaction);

    painter->restore();
}

QSize ChatDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    int viewportWidth = 400;
    const ChatView *chatView = nullptr;
    if (option.widget) {
        if (auto view = qobject_cast<const QAbstractItemView *>(option.widget)) {
            viewportWidth = view->viewport()->width();
            chatView = qobject_cast<const ChatView *>(view);
        } else {
            viewportWidth = option.widget->width();
        }
    }

    bool isEditing = chatView && chatView->editingRow() == index.row();

    QSize cached = index.data(ChatModel::CachedSizeRole).toSize();
    if (cached.isValid() && cached.width() == viewportWidth && !isEditing)
        return cached;

    const auto *chatModel = qobject_cast<const ChatModel *>(index.model());
    if (chatModel)
        chatModel->suppressImageFetch = true;

    ChatLayout::LayoutContext ctx = buildLayoutContext(option, index);

    if (chatModel)
        chatModel->suppressImageFetch = false;

    ctx.rowWidth = viewportWidth;
    ctx.rowTop = 0;

    ChatLayout::MessageLayout layout = ChatLayout::calculateMessageLayout(ctx);

    int height = layout.totalHeight;
    if (isEditing)
        height = qMax(ChatView::InlineEditMinHeight, height);

    QSize size(viewportWidth, height);

    if (!isEditing && chatModel) {
        // Write the size cache without emitting dataChanged, which would
        // re-enter layout/paint from within the view's layout pass.
        const auto prevSize = index.data(ChatModel::CachedSizeRole).toSize();
        if (size != prevSize)
            chatModel->setCachedSize(index, size);
    }

    return size;
}

} // namespace UI
} // namespace Acheron
