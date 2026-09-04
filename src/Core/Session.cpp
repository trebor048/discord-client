#include "Session.hpp"

#include <QDebug>

#include "Logging.hpp"
#include "TokenStore.hpp"
#include "Storage/DatabaseManager.hpp"

namespace Acheron {
namespace Core {

Session::Session(QObject *parent) : QObject(parent)
{
    imageManager = new ImageManager(this);
}

Session::~Session()
{
    shutdown();
}

void Session::start()
{
    qCDebug(LogCore) << "Session started";
}

void Session::shutdown()
{
    qCDebug(LogCore) << "Session shutdown";
    const auto snapshot = clients.values();
    clients.clear();

    for (auto *instance : snapshot) {
        if (!instance)
            continue;
        disconnect(instance, nullptr, this, nullptr);
        instance->stop();
        Storage::DatabaseManager::instance().closeCacheDatabase(instance->accountId());
        // Synchronous delete: shutdown() runs after app.exec() returns, when no
        // event loop remains to process DeferredDelete — deleteLater() would
        // leave the ClientInstance (and its repos/connections) alive while
        // DatabaseManager::shutdown() tears the cache connections out from
        // under them.
        delete instance;
    }
}

void Session::connectAccount(Snowflake accountId)
{
    if (clients.contains(accountId)) {
        ClientInstance *existing = clients.value(accountId);

        if (existing->state() != ConnectionState::Disconnected) {
            qCWarning(LogCore) << "Account already connected or connecting:" << accountId;
            return;
        }

        // were dead
        ClientInstance *dead = clients.take(accountId);
        Storage::DatabaseManager::instance().closeCacheDatabase(accountId);
        dead->deleteLater();
    }

    AccountInfo acc = repo.getAccount(accountId);
    // AccountRepository::getAccount returns a default-constructed AccountInfo
    // when no row exists, whose id is Snowflake::Invalid (-1ULL) — never 0.
    // The previous `== 0` test could not fire, so a DB row missing while a
    // keychain token survived would have built a ClientInstance with an
    // invalid account id.
    if (!acc.id.isValid()) {
        qCWarning(LogCore) << "Account not found:" << accountId;
        return;
    }

    acc.token = TokenStore::loadToken(accountId);
    if (acc.token.isEmpty()) {
        qCWarning(LogCore) << "No token found in keychain for account:" << accountId;
        return;
    }

    auto *instance = new ClientInstance(acc, captchaResolver, this);

    connect(instance, &ClientInstance::stateChanged, this,
            [this, accountId, instance](ConnectionState state) {
                emit connectionStateChanged(accountId, state);

                if (state == ConnectionState::Disconnected) {
                    if (clients.value(accountId) == instance) {
                        ClientInstance *dead = clients.take(accountId);
                        Storage::DatabaseManager::instance().closeCacheDatabase(accountId);
                        dead->deleteLater();
                    }
                }
            });

    connect(instance, &ClientInstance::detailsUpdated, this, [this](const AccountInfo &info) {
        repo.saveAccount(info);
        emit accountDetailsUpdated(info);
    });

    connect(instance, &ClientInstance::ready, this,
            [this](const Discord::Ready &ready) { emit this->ready(ready); });

    clients.insert(accountId, instance);

    instance->start();
}

void Session::autoConnectAccounts()
{
    for (const auto &acc : repo.getAllAccounts()) {
        if (acc.autoConnect)
            connectAccount(acc.id);
    }
}

void Session::disconnectAccount(Snowflake accountId)
{
    if (!clients.contains(accountId))
        return;

    ClientInstance *instance = clients.take(accountId);

    instance->stop();

    // Invariant: no live ClientInstance may hold repository QSqlDatabase handles
    // when the cache connection is removed. closeCacheDatabase() closes and
    // removeDatabase()s the per-account connection (and its per-thread clones);
    // running it while this instance is still alive would leave a queued event
    // able to touch a deregistered connection and trip Qt's "connection still in
    // use" warning. Defer the close until the deferred delete below has actually
    // destroyed the instance (which also destroys its managers/repos and cancels
    // any queued deliveries).
    connect(instance, &QObject::destroyed, this, [this, accountId]() {
        Storage::DatabaseManager::instance().closeCacheDatabase(accountId);
    });

    // Keep deleteLater rather than deleting synchronously: stop() lets the
    // client finish its close handshake and report Disconnected to the UI, and
    // the destroyed() handler above runs only once the instance is really gone.
    instance->deleteLater();
}

ClientInstance *Session::client(Snowflake accountId) const
{
    return clients.value(accountId, nullptr);
}

AccountInfo Session::getAccountInfo(Snowflake accountId)
{
    if (clients.contains(accountId)) {
        return clients[accountId]->accountInfo();
    }
    return repo.getAccount(accountId);
}

bool Session::hasActiveConnection() const
{
    for (auto *c : clients)
        if (c->state() == ConnectionState::Connected || c->state() == ConnectionState::Connecting)
            return true;
    return false;
}

} // namespace Core
} // namespace Acheron
