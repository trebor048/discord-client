#pragma once

#include <QWidget>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace Core {
class ClientInstance;
}

namespace UI {

class GuildSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit GuildSettingsPage(Core::ClientInstance *instance, Core::Snowflake guildId,
                               QWidget *parent = nullptr)
        : QWidget(parent)
        , m_instance(instance)
        , m_guildId(guildId)
    {}

    virtual void load() {}
    virtual void refresh() { load(); }

    [[nodiscard]] Core::Snowflake guildId() const { return m_guildId; }
    [[nodiscard]] Core::ClientInstance *instance() const { return m_instance; }

signals:
    void statusMessage(const QString &message);

protected:
    Core::ClientInstance *m_instance;
    Core::Snowflake m_guildId;
};

} // namespace UI
} // namespace Acheron
