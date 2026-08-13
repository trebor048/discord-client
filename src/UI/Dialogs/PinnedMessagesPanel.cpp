#include "PinnedMessagesPanel.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Markdown/Parser.hpp"
#include "Core/PermissionManager.hpp"
#include "Core/TimeUtils.hpp"
#include "Core/UserManager.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Enums.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {

QLabel *makeAuthorLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}

QLabel *makeMutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color: palette(placeholder-text); font-size: 11px;"));
    return label;
}

QLabel *makeRichTextLabel(const QString &html, QWidget *parent)
{
    auto *label = new QLabel(html, parent);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    return label;
}

} // namespace

PinnedMessagesPanel::PinnedMessagesPanel(Core::ImageManager *images, QWidget *parent)
    : BasePopup(parent), imageManager(images), parser(std::make_unique<Core::Markdown::Parser>())
{
    QFrame *c = getContainer();
    c->setMinimumWidth(520);
    c->setMaximumWidth(760);

    auto *layout = new QVBoxLayout(c);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    titleLabel = new QLabel(tr("Pinned Messages"), c);
    {
        QFont f = titleLabel->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 2);
        titleLabel->setFont(f);
    }
    layout->addWidget(titleLabel);

    scrollArea = new QScrollArea(c);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *itemsContainer = new QWidget(scrollArea);
    itemsLayout = new QVBoxLayout(itemsContainer);
    itemsLayout->setContentsMargins(0, 0, 0, 0);
    itemsLayout->setSpacing(8);
    itemsLayout->setAlignment(Qt::AlignTop);
    scrollArea->setWidget(itemsContainer);

    layout->addWidget(scrollArea, 1);
    scrollArea->setMinimumHeight(360);
}

PinnedMessagesPanel::~PinnedMessagesPanel() = default;

void PinnedMessagesPanel::configure(Core::ClientInstance *inst, Core::Snowflake chId,
                                    Core::Snowflake gId, bool dm)
{
    disconnect(pinsConnection);
    instance = inst;
    channelId = chId;
    guildId = gId;
    isDm = dm;

    if (instance && instance->discord()) {
        pinsConnection = connect(instance->discord(), &Discord::Client::channelPinsUpdated, this,
                                 &PinnedMessagesPanel::onChannelPinsUpdated);
    }

    parser->setUserResolver([this](const QString &userId) {
        const Core::Snowflake id(userId.toULongLong());
        if (!instance)
            return QString::number(quint64(id));
        const QString name = instance->users()->getDisplayName(id, guildId);
        if (name.isEmpty() || name == QStringLiteral("Unknown User"))
            return QString::number(quint64(id));
        return name;
    });

    parser->setChannelResolver([this](const QString &chanId) {
        const Core::Snowflake id(chanId.toULongLong());
        if (!instance)
            return QString::number(quint64(id));
        if (auto ch = instance->getChannel(id); ch && ch->name.hasValue() && !ch->name->isEmpty())
            return QStringLiteral("#") + ch->name.get();
        return QString::number(quint64(id));
    });

    QString name;
    if (instance) {
        if (auto ch = instance->getChannel(channelId); ch && ch->name.hasValue())
            name = ch->name.get();
    }
    titleLabel->setText(name.isEmpty() ? tr("Pinned Messages")
                                       : tr("Pinned Messages — #%1").arg(name));

    refresh();
}

void PinnedMessagesPanel::refresh()
{
    ++loadGeneration;
    clearItems();

    if (!instance || !channelId.isValid()) {
        addNoticeItem(tr("No channel selected"));
        return;
    }

    addNoticeItem(tr("Loading…"));

    QPointer<PinnedMessagesPanel> self(this);
    const int gen = loadGeneration;
    instance->discord()->getPinnedMessages(
            channelId,
            [this, self, gen](const Core::Result<QList<Discord::Message>> &res) {
                if (!self || gen != loadGeneration)
                    return;

                clearItems();
                if (!res.success()) {
                    addNoticeItem(tr("Failed to load pinned messages: %1").arg(res.error));
                    return;
                }

                const QList<Discord::Message> &messages = *res.value;
                if (messages.isEmpty()) {
                    addNoticeItem(tr("No pinned messages"));
                    return;
                }

                for (const auto &msg : messages)
                    addMessageItem(msg);
            });
}

void PinnedMessagesPanel::clearItems()
{
    while (QLayoutItem *item = itemsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

void PinnedMessagesPanel::addNoticeItem(const QString &text)
{
    itemsLayout->addWidget(makeMutedLabel(text, scrollArea->widget()));
}

void PinnedMessagesPanel::addMessageItem(const Discord::Message &msg)
{
    auto *frame = new QFrame(scrollArea->widget());
    frame->setObjectName(QStringLiteral("pinnedMessageItem"));
    frame->setStyleSheet(QStringLiteral(
            "#pinnedMessageItem { background: rgba(128, 128, 128, 0.06); border: 1px solid "
            "rgba(128, 128, 128, 0.18); border-radius: 6px; }"));

    auto *root = new QVBoxLayout(frame);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(6);

    // Header: avatar, author, timestamp
    auto *header = new QHBoxLayout;
    header->setSpacing(10);

    auto *avatar = new QLabel(frame);
    avatar->setFixedSize(40, 40);
    const QUrl avatarUrl = resolveAvatarUrl(msg.author.get());
    if (!avatarUrl.isEmpty())
        imageManager->assign(avatar, avatarUrl, QSize(40, 40));
    else
        avatar->setPixmap(imageManager->placeholder(QSize(40, 40)));
    header->addWidget(avatar, 0, Qt::AlignTop);

    auto *nameCol = new QVBoxLayout;
    nameCol->setSpacing(1);
    nameCol->addWidget(makeAuthorLabel(resolveAuthorName(msg.author.get()), frame));
    if (msg.timestamp.get().isValid())
        nameCol->addWidget(makeMutedLabel(Core::TimeUtils::absoluteTime(msg.timestamp.get()), frame));
    header->addLayout(nameCol, 1);
    root->addLayout(header);

    // Body (markdown-rendered)
    if (msg.content.hasValue() && !msg.content->isEmpty()) {
        const QString bodyHtml = renderBodyHtml(msg);
        if (!bodyHtml.isEmpty())
            root->addWidget(makeRichTextLabel(bodyHtml, frame));
    }

    // Embeds
    if (msg.embeds.hasValue()) {
        for (const auto &embed : *msg.embeds) {
            const QString embedHtml = renderEmbedHtml(embed);
            if (embedHtml.isEmpty())
                continue;

            auto *embedFrame = new QFrame(frame);
            embedFrame->setObjectName(QStringLiteral("pinnedEmbed"));
            QString borderColor = QStringLiteral("palette(mid)");
            if (embed.color.hasValue() && embed.color.get() != 0) {
                const QColor color(embed.color.get());
                if (color.isValid())
                    borderColor = color.name();
            }
            embedFrame->setStyleSheet(QStringLiteral(
                    "#pinnedEmbed { background: rgba(128, 128, 128, 0.05); border-left: 4px "
                    "solid %1; border-radius: 4px; }")
                                              .arg(borderColor));

            auto *embedLayout = new QVBoxLayout(embedFrame);
            embedLayout->setContentsMargins(10, 6, 10, 6);
            embedLayout->setSpacing(2);
            embedLayout->addWidget(makeRichTextLabel(embedHtml, embedFrame));

            root->addWidget(embedFrame);
        }
    }

    // Unpin affordance
    if (canUnpin()) {
        auto *footer = new QHBoxLayout;
        footer->addStretch(1);
        auto *unpinButton = new QPushButton(tr("Unpin"), frame);
        unpinButton->setCursor(Qt::PointingHandCursor);
        const Core::Snowflake messageId = msg.id.get();
        connect(unpinButton, &QPushButton::clicked, this,
                [this, messageId]() { onUnpinClicked(messageId); });
        footer->addWidget(unpinButton, 0);
        root->addLayout(footer);
    }

    itemsLayout->addWidget(frame);
}

bool PinnedMessagesPanel::canUnpin() const
{
    if (!instance)
        return false;
    if (isDm)
        return true;

    const Core::Snowflake userId = instance->accountId();
    return instance->permissions()->hasChannelPermission(userId, channelId,
                                                         Discord::Permission::MANAGE_MESSAGES);
}

QString PinnedMessagesPanel::renderBodyHtml(const Discord::Message &msg)
{
    Core::Markdown::ParseState state;
    state.isInline = true;
    const auto ast = parser->parse(msg.content.get(), state);
    return parser->toHtml(ast, Core::Markdown::Parser::isEmojiOnly(ast));
}

QString PinnedMessagesPanel::renderEmbedHtml(const Discord::Embed &embed)
{
    QStringList parts;

    if (embed.title.hasValue() && !embed.title->isEmpty()) {
        const QString title = embed.title.get().toHtmlEscaped();
        if (embed.url.hasValue() && !embed.url->isEmpty())
            parts.append(QStringLiteral("<a href=\"%1\"><b>%2</b></a>")
                                 .arg(embed.url.get().toHtmlEscaped(), title));
        else
            parts.append(QStringLiteral("<b>%1</b>").arg(title));
    }

    if (embed.description.hasValue() && !embed.description->isEmpty()) {
        Core::Markdown::ParseState state;
        state.isInline = true;
        const auto ast = parser->parse(embed.description.get(), state);
        parts.append(parser->toHtml(ast));
    }

    if (embed.fields.hasValue()) {
        for (const auto &field : *embed.fields) {
            QString fieldHtml;
            if (field.name.hasValue() && !field.name->isEmpty())
                fieldHtml += QStringLiteral("<b>%1</b>").arg(field.name.get().toHtmlEscaped());
            if (field.value.hasValue() && !field.value->isEmpty()) {
                if (!fieldHtml.isEmpty())
                    fieldHtml += QStringLiteral("<br>");
                fieldHtml += field.value.get().toHtmlEscaped();
            }
            if (!fieldHtml.isEmpty())
                parts.append(fieldHtml);
        }
    }

    if (embed.footer.hasValue() && !embed.footer->text.get().isEmpty())
        parts.append(QStringLiteral(
                "<span style=\"color: palette(placeholder-text); font-size: 11px;\">%1</span>")
                             .arg(embed.footer->text.get().toHtmlEscaped()));

    return parts.join(QStringLiteral("<br>"));
}

QUrl PinnedMessagesPanel::resolveAvatarUrl(const Discord::User &author) const
{
    if (!instance)
        return {};

    const Core::Snowflake userId = author.id.get();
    if (guildId.isValid()) {
        if (auto member = instance->users()->getMember(guildId, userId)) {
            if (member->avatar.hasValue() && !member->avatar->isEmpty())
                return Discord::Cdn::guildMemberAvatar(guildId, userId, member->avatar.get(), 64);
        }
    }

    if (author.avatar.hasValue() && !author.avatar->isEmpty())
        return Discord::Cdn::userAvatar(userId, author.avatar.get(), 64);
    return {};
}

QString PinnedMessagesPanel::resolveAuthorName(const Discord::User &author) const
{
    QString name = author.getDisplayName();
    if (instance && guildId.isValid()) {
        if (auto member = instance->users()->getMember(guildId, author.id.get())) {
            if (member->nick.hasValue() && !member->nick->isEmpty())
                name = member->nick.get();
        }
    }
    return name;
}

void PinnedMessagesPanel::onUnpinClicked(Core::Snowflake messageId)
{
    if (!instance || !channelId.isValid())
        return;

    instance->discord()->unpinMessage(channelId, messageId);
    refresh();
}

void PinnedMessagesPanel::onChannelPinsUpdated(const Discord::ChannelPinsUpdate &event)
{
    if (!channelId.isValid() || event.channelId.get() != channelId)
        return;
    refresh();
}

} // namespace UI
} // namespace Acheron
