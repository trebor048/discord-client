#include "AuthorizedAppsPage.hpp"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/Result.hpp"
#include "Core/Snowflake.hpp"
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
    refreshApps();
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
    // OAuth2 token structure: { access_token, client_id, scopes: [...], ... }
    QString clientId = app.value("client_id").toString();
    QList<QVariant> scopesVariant = app.value("scopes").toVariant().toList();
    QStringList scopes;
    for (const auto &s : scopesVariant)
        scopes << s.toString();

    auto *item = new QListWidgetItem(appsList);
    item->setData(Qt::UserRole, clientId);

    auto *widget = new QWidget(this);
    auto *rowLayout = new QHBoxLayout(widget);
    rowLayout->setContentsMargins(4, 4, 4, 4);

    auto *infoLabel = new QLabel(widget);
    QString displayText = QStringLiteral("<b>%1</b><br><span style=\"color: gray;\">Scopes: %2</span>")
                             .arg(clientId, scopes.join(", "));
    infoLabel->setText(displayText);
    infoLabel->setTextFormat(Qt::RichText);
    rowLayout->addWidget(infoLabel, 1);

    auto *revokeBtn = new QPushButton(tr("Revoke Access"), widget);
    revokeBtn->setProperty("client_id", clientId);
    rowLayout->addWidget(revokeBtn);

    connect(revokeBtn, &QPushButton::clicked, this, [this, clientId]() {
        // client_id may be a snowflake
        bool ok = false;
        quint64 appId = clientId.toULongLong(&ok);
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
            QString idStr = QString::number(appId);
            for (int i = 0; i < appsList->count(); ++i) {
                QListWidgetItem *item = appsList->item(i);
                if (item->data(Qt::UserRole).toString() == idStr) {
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
