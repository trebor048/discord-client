#include "AuthorizedAppsPage.hpp"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/ImageManager.hpp"
#include "Core/Result.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {

AuthorizedAppsPage::AuthorizedAppsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Authorized Apps"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 14px;"));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    refreshButton = new QPushButton(tr("Refresh"), this);
    headerLayout->addWidget(refreshButton);
    layout->addLayout(headerLayout);

    appsList = new QListWidget(this);
    appsList->setAlternatingRowColors(true);
    layout->addWidget(appsList);

    statusLabel = new QLabel(this);
    statusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(statusLabel);

    layout->addStretch();

    connect(refreshButton, &QPushButton::clicked, this, &AuthorizedAppsPage::refreshApps);
}

void AuthorizedAppsPage::setClient(Discord::Client *c)
{
    client = c;
    if (imageManager)
        refreshApps();
    else
        pendingRefresh = true;
}

void AuthorizedAppsPage::setImageManager(Core::ImageManager *manager)
{
    imageManager = manager;
    if (client && pendingRefresh) {
        pendingRefresh = false;
        refreshApps();
    }
}

void AuthorizedAppsPage::refreshApps()
{
    if (!client) {
        statusLabel->setText(tr("Not connected."));
        return;
    }

    statusLabel->setText(tr("Loading..."));
    appsList->clear();

    QPointer<AuthorizedAppsPage> guard(this);
    client->fetchAuthorizedApps([guard](const Core::Result<QJsonObject> &result) {
        if (!guard)
            return;
        if (!result.success() || !result.value) {
            guard->statusLabel->setText(tr("Failed to load authorized apps."));
            return;
        }

        QJsonObject obj = result.value.value();
        QJsonArray tokens = obj.value("tokens").toArray();
        if (tokens.isEmpty()) {
            guard->statusLabel->setText(tr("No authorized applications."));
            return;
        }

        guard->statusLabel->clear();
        for (const QJsonValue &val : tokens) {
            guard->addAppEntry(val.toObject());
        }
    });
}

void AuthorizedAppsPage::addAppEntry(const QJsonObject &app)
{
    // OAuth2 token structure: { id, application: { id, name, icon, description, ... }, scopes: [...], ... }
    QJsonObject application = app.value("application").toObject();
    QString clientId = application.value("id").toString();
    if (clientId.isEmpty())
        clientId = app.value("client_id").toString();

    QString name = application.value("name").toString();
    if (name.isEmpty())
        name = clientId;
    QString description = application.value("description").toString();
    QString iconHash = application.value("icon").toString();

    bool ok = false;
    quint64 appId = clientId.toULongLong(&ok);

    QList<QVariant> scopesVariant = app.value("scopes").toVariant().toList();
    QStringList scopes;
    for (const auto &s : scopesVariant)
        scopes << s.toString();

    auto *item = new QListWidgetItem(appsList);
    item->setData(Qt::UserRole, appId);

    auto *widget = new QWidget(this);
    auto *rowLayout = new QHBoxLayout(widget);
    rowLayout->setContentsMargins(4, 4, 4, 4);

    auto *iconLabel = new QLabel(widget);
    iconLabel->setFixedSize(40, 40);
    iconLabel->setAlignment(Qt::AlignCenter);
    rowLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    auto *infoLabel = new QLabel(widget);
    QString displayText = QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped());
    if (!description.isEmpty())
        displayText += QStringLiteral("<br>%1").arg(description.toHtmlEscaped());
    displayText += QStringLiteral("<br><span style=\"color: gray;\">Scopes: %1</span>")
                           .arg(scopes.join(", ").toHtmlEscaped());
    infoLabel->setText(displayText);
    infoLabel->setTextFormat(Qt::RichText);
    infoLabel->setWordWrap(true);
    rowLayout->addWidget(infoLabel, 1);

    // Load the app icon via the shared image cache
    QUrl iconUrl = ok ? Discord::Cdn::applicationIcon(Core::Snowflake(appId), iconHash, 80)
                      : QUrl();
    if (imageManager && iconUrl.isValid())
        imageManager->assign(iconLabel, iconUrl, QSize(40, 40));

    auto *revokeBtn = new QPushButton(tr("Revoke Access"), widget);
    revokeBtn->setProperty("client_id", clientId);
    rowLayout->addWidget(revokeBtn);

    connect(revokeBtn, &QPushButton::clicked, this, [this, clientId, appId, ok]() {
        if (ok)
            onRevokeApp(appId);
        else
            statusLabel->setText(tr("Unable to revoke app access for non-numeric client ID %1.").arg(clientId));
    });

    item->setSizeHint(widget->sizeHint());
    appsList->setItemWidget(item, widget);
}

void AuthorizedAppsPage::onRevokeApp(quint64 appId)
{
    if (!client)
        return;

    client->revokeAuthorizedApp(Core::Snowflake(appId), [this, appId, guard = QPointer<AuthorizedAppsPage>(this)](const Core::Result<QJsonObject> &result) {
        if (!guard)
            return;
        if (result.success()) {
            for (int i = 0; i < appsList->count(); ++i) {
                QListWidgetItem *item = appsList->item(i);
                if (item->data(Qt::UserRole).toULongLong() == appId) {
                    delete appsList->takeItem(i);
                    break;
                }
            }
        } else {
            statusLabel->setText(tr("Failed to revoke app access."));
        }
    });
}

} // namespace UI
} // namespace Acheron
