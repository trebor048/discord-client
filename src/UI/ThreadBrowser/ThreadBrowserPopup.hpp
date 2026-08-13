#pragma once

#include "UI/Dialogs/BasePopup.hpp"

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

#include <QPointer>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Acheron {
namespace Core {
class ClientInstance;
}
namespace UI {

class ThreadBrowserPopup : public BasePopup
{
    Q_OBJECT
public:
    explicit ThreadBrowserPopup(QWidget *parent = nullptr);

    void configure(Core::ClientInstance *instance, Core::Snowflake channelId, const QString &channelName);

signals:
    void threadActivated(Core::Snowflake threadId);

private:
    void reload();
    void loadActive();
    void loadArchived();
    void addSectionHeader(const QString &text);
    void addNoticeItem(const QString &text);
    void addThreadItem(const Discord::Channel &thread);
    void onItemClicked(QListWidgetItem *item);

    QPointer<Core::ClientInstance> instance;
    Core::Snowflake channelId;

    QLabel *titleLabel = nullptr;
    QListWidget *list = nullptr;
    QPushButton *loadMoreButton = nullptr;

    int archivedOffset = 0;
    bool archivedLoaded = false;
    int loadGeneration = 0;
};

} // namespace UI
} // namespace Acheron
