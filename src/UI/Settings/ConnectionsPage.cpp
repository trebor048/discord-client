#include "ConnectionsPage.hpp"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/Result.hpp"
#include "Discord/Client.hpp"
#include "Discord/CdnUrls.hpp"

namespace Acheron {
namespace UI {

ConnectionsPage::ConnectionsPage(QWidget *parent)
    : QWidget(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    auto *layout = new QVBoxLayout(this);

    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(tr("Connected Accounts"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 14px;"));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    refreshButton = new QPushButton(tr("Refresh"), this);
    headerLayout->addWidget(refreshButton);
    layout->addLayout(headerLayout);

    connectionsList = new QListWidget(this);
    connectionsList->setAlternatingRowColors(true);
    layout->addWidget(connectionsList);

    statusLabel = new QLabel(this);
    statusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(statusLabel);

    layout->addStretch();

    connect(refreshButton, &QPushButton::clicked, this, &ConnectionsPage::refreshConnections);
}

void ConnectionsPage::setClient(Discord::Client *c)
{
    client = c;
    refreshConnections();
}

void ConnectionsPage::refreshConnections()
{
    if (!client) {
        statusLabel->setText(tr("Not connected."));
        return;
    }

    statusLabel->setText(tr("Loading..."));
    connectionsList->clear();

    QPointer<ConnectionsPage> guard(this);
    client->fetchConnections([guard](const Core::Result<QJsonObject> &result) {
        if (!guard)
            return;
        if (!result.success() || !result.value) {
            guard->statusLabel->setText(tr("Failed to load connections."));
            return;
        }

        QJsonObject obj = result.value.value();
        QJsonArray connections = obj.value("connections").toArray();
        if (connections.isEmpty()) {
            guard->statusLabel->setText(tr("No connected accounts."));
            return;
        }

        guard->statusLabel->clear();
        for (const QJsonValue &val : connections) {
            guard->addConnectionEntry(val.toObject());
        }
    });
}

void ConnectionsPage::addConnectionEntry(const QJsonObject &conn)
{
    QString type = conn.value("type").toString();
    QString name = conn.value("name").toString();
    QString id = conn.value("id").toString();
    bool verified = conn.value("verified").toBool();

    auto *item = new QListWidgetItem(connectionsList);
    item->setData(Qt::UserRole, type);
    item->setData(Qt::UserRole + 1, id);

    auto *widget = new QWidget(this);
    auto *rowLayout = new QHBoxLayout(widget);
    rowLayout->setContentsMargins(4, 4, 4, 4);

    // Connection icon
    auto *iconLabel = new QLabel(widget);
    QUrl iconUrl = Discord::Cdn::connectionIcon(type);
    iconLabel->setFixedSize(24, 24);
    iconLabel->setPixmap({});
    rowLayout->addWidget(iconLabel);

    // Name and type
    auto *infoLabel = new QLabel(widget);
    QString displayText = QStringLiteral("<b>%1</b> (%2)").arg(name, type);
    if (verified)
        displayText += QStringLiteral(" [verified]");
    infoLabel->setText(displayText);
    rowLayout->addWidget(infoLabel, 1);

    // Disconnect button
    auto *disconnectBtn = new QPushButton(tr("Disconnect"), widget);
    disconnectBtn->setProperty("conn_type", type);
    disconnectBtn->setProperty("conn_id", id);
    rowLayout->addWidget(disconnectBtn);

    connect(disconnectBtn, &QPushButton::clicked, this, [this, type, id]() {
        onRemoveConnection(type, id);
    });

    item->setSizeHint(widget->sizeHint());
    connectionsList->setItemWidget(item, widget);

    // Load icon asynchronously
    if (iconUrl.isValid()) {
        QNetworkRequest req(iconUrl);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_nam->get(req);
        QPointer<QLabel> iconGuard(iconLabel);
        connect(reply, &QNetworkReply::finished, this, [iconGuard, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError)
                return;
            if (!iconGuard)
                return;
            QPixmap pix;
            pix.loadFromData(reply->readAll());
            if (!pix.isNull())
                iconGuard->setPixmap(pix.scaled(24, 24, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation));
        });
    }
}

void ConnectionsPage::onRemoveConnection(const QString &type, const QString &id)
{
    if (!client)
        return;

    client->removeConnection(type, id, [this, type, id, guard = QPointer<ConnectionsPage>(this)](const Core::Result<QJsonObject> &result) {
        if (!guard)
            return;
        if (result.success()) {
            // Remove the item from the list
            for (int i = 0; i < connectionsList->count(); ++i) {
                QListWidgetItem *item = connectionsList->item(i);
                if (item->data(Qt::UserRole).toString() == type &&
                    item->data(Qt::UserRole + 1).toString() == id) {
                    delete connectionsList->takeItem(i);
                    break;
                }
            }
            emit connectionsChanged();
        } else {
            statusLabel->setText(tr("Failed to disconnect %1.").arg(type));
        }
    });
}

} // namespace UI
} // namespace Acheron
