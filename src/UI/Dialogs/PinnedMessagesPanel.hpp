#pragma once

#include "UI/Dialogs/BasePopup.hpp"

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

#include <QMetaObject>
#include <QPointer>

#include <memory>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace Acheron {
namespace Core {
class ClientInstance;
class ImageManager;
namespace Markdown {
class Parser;
}
} // namespace Core

namespace UI {

class PinnedMessagesPanel : public BasePopup
{
    Q_OBJECT
public:
    explicit PinnedMessagesPanel(Core::ImageManager *imageManager, QWidget *parent = nullptr);
    ~PinnedMessagesPanel() override;

    void configure(Core::ClientInstance *instance, Core::Snowflake channelId,
                   Core::Snowflake guildId, bool isDm);

public slots:
    void refresh();

private:
    void reload();
    void clearItems();
    void addMessageItem(const Discord::Message &msg);
    void addNoticeItem(const QString &text);
    void onUnpinClicked(Core::Snowflake messageId);
    void onChannelPinsUpdated(const Discord::ChannelPinsUpdate &event);

    [[nodiscard]] bool canUnpin() const;
    [[nodiscard]] QString renderBodyHtml(const Discord::Message &msg);
    [[nodiscard]] QString renderEmbedHtml(const Discord::Embed &embed);
    [[nodiscard]] QUrl resolveAvatarUrl(const Discord::User &author) const;
    [[nodiscard]] QString resolveAuthorName(const Discord::User &author) const;

    QPointer<Core::ClientInstance> instance;
    Core::ImageManager *imageManager = nullptr;
    Core::Snowflake channelId = Core::Snowflake::Invalid;
    Core::Snowflake guildId = Core::Snowflake::Invalid;
    bool isDm = false;
    int loadGeneration = 0;
    QMetaObject::Connection pinsConnection;

    std::unique_ptr<Core::Markdown::Parser> parser;

    QLabel *titleLabel = nullptr;
    QScrollArea *scrollArea = nullptr;
    QVBoxLayout *itemsLayout = nullptr;
};

} // namespace UI
} // namespace Acheron
