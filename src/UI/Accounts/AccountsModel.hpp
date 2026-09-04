#pragma once

#include <QAbstractListModel>
#include <QVector>
#include "Core/AccountInfo.hpp"
#include "UI/AvatarRequestTracker.hpp"

namespace Acheron {

namespace Core {
class Session;
}

namespace UI {

class AccountsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit AccountsModel(Core::Session *session, QObject *parent = nullptr);

    enum Roles {
        AccountObjectRole = Qt::UserRole + 1,
        ConnectionStateRole,
        AutoConnectRole,
    };

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    const Core::AccountInfo *getAccountById(Core::Snowflake id) const;

    /// Adds an account (DB + keychain + model). Returns false — leaving neither
    /// an account row nor a stored credential behind — when the token could not
    /// be stored in the keychain, or when the DB insert then fails (the just-
    /// stored token is deleted again). A failed save can't leave an account that
    /// exists on next startup with no stored token, nor a token with no row.
    bool addAccount(const Core::AccountInfo &account);
    void removeAccount(int row);

    void setConnectionState(int row, Core::ConnectionState state);
    void setAutoConnect(int row, bool enabled);

private:
    Core::Session *session;

    QVector<Core::AccountInfo> accounts;
    mutable AvatarRequestTracker<QPersistentModelIndex> avatarTracker;
};

} // namespace UI
} // namespace Acheron