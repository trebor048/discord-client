#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QDateTime>
#include <optional>

#include "Snowflake.hpp"

namespace Acheron {
namespace Core {

class UserManager;

struct TyperInfo
{
    Snowflake userId;
    std::optional<Snowflake> guildId;
    QString name;
};

class TypingTracker : public QObject
{
    Q_OBJECT
public:
    explicit TypingTracker(QObject *parent = nullptr);

    void setUserManager(UserManager *manager);
    void setActiveChannel(Snowflake channelId);
    void setCurrentUserId(Snowflake userId);

    void addTyper(Snowflake channelId, Snowflake userId, std::optional<Snowflake> guildId);
    void removeTyper(Snowflake channelId, Snowflake userId);
    void clear();

    QList<TyperInfo> getActiveTypers() const;

signals:
    void typersChanged();

private slots:
    void cleanupExpired();

private:
    struct TypingEntry
    {
        Snowflake userId;
        std::optional<Snowflake> guildId;
        QDateTime expiresAt;
    };

    QHash<Snowflake, QList<TypingEntry>> channelTypers;
    Snowflake activeChannel;
    Snowflake currentUserId;
    UserManager *userManager = nullptr;
    QTimer cleanupTimer;
};

} // namespace Core
} // namespace Acheron
