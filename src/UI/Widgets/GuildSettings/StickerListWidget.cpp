#include "StickerListWidget.hpp"

#include "Discord/Client.hpp"
#include "Discord/HttpClient.hpp"
#include "Discord/CdnUrls.hpp"

#include <QBuffer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMovie>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace Acheron {
namespace UI {

namespace {
constexpr qint64 kMaxStickerSize = 512 * 1024; // 512 KB for guild stickers
constexpr int kStickerGridColumns = 6;
constexpr int kStickerCellSize = 80;
constexpr int kStickerIconSize = 64;
} // namespace

StickerListWidget::StickerListWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(12, 12, 12, 12);
    outerLayout->setSpacing(8);

    titleLabel_ = new QLabel(tr("Guild Stickers"), this);
    QFont titleFont = titleLabel_->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel_->setFont(titleFont);
    outerLayout->addWidget(titleLabel_);

    permissionLabel_ = new QLabel(this);
    permissionLabel_->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    permissionLabel_->setWordWrap(true);
    outerLayout->addWidget(permissionLabel_);

    auto *buttonRow = new QHBoxLayout();
    uploadButton_ = new QPushButton(tr("Upload Sticker"), this);
    uploadButton_->setEnabled(false);
    connect(uploadButton_, &QPushButton::clicked, this, &StickerListWidget::onUploadClicked);
    buttonRow->addWidget(uploadButton_);
    buttonRow->addStretch();
    outerLayout->addLayout(buttonRow);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    container_ = new QWidget(scrollArea_);
    containerLayout_ = new QVBoxLayout(container_);
    containerLayout_->setContentsMargins(0, 0, 0, 0);
    containerLayout_->setSpacing(8);
    containerLayout_->addStretch();
    scrollArea_->setWidget(container_);
    outerLayout->addWidget(scrollArea_, 1);

    nam_ = new QNetworkAccessManager(this);
}

StickerListWidget::~StickerListWidget()
{
    clearStickerWidgets();
}

void StickerListWidget::setGuildId(Core::Snowflake guildId)
{
    guildId_ = guildId;
}

void StickerListWidget::setDiscordClient(Discord::Client *client)
{
    client_ = client;
}

void StickerListWidget::setHttpClient(Discord::HttpClient *http)
{
    http_ = http;
}

void StickerListWidget::setEnabled(bool enabled)
{
    // Also propagate to the widget itself: without this, the scroll area and
    // its children (selection, previews) stay fully interactive even when a
    // caller considers the widget "disabled".
    QWidget::setEnabled(enabled);
    enabled_ = enabled;
    uploadButton_->setEnabled(enabled);
    permissionLabel_->setText(enabled
            ? tr("You can manage stickers for this server.")
            : tr("You need the Manage Expressions permission to manage stickers."));
    rebuildStickerList();
}

void StickerListWidget::setStickers(const QList<Discord::Sticker> &stickers)
{
    stickers_ = stickers;
    std::sort(stickers_.begin(), stickers_.end(),
              [](const Discord::Sticker &a, const Discord::Sticker &b) {
                  return a.name.get().toCaseFolded() < b.name.get().toCaseFolded();
              });
    rebuildStickerList();
}

void StickerListWidget::clearStickerWidgets()
{
    for (auto &entry : stickerWidgets_) {
        if (entry.movie) {
            entry.movie->stop();
            delete entry.movie;
        }
        if (entry.buffer)
            delete entry.buffer;
    }
    stickerWidgets_.clear();

    while (containerLayout_->count() > 0) {
        QLayoutItem *item = containerLayout_->takeAt(0);
        if (!item)
            continue;
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        if (QLayout *layout = item->layout()) {
            while (layout->count() > 0) {
                QLayoutItem *child = layout->takeAt(0);
                if (child) {
                    if (QWidget *childWidget = child->widget())
                        childWidget->deleteLater();
                    delete child;
                }
            }
            delete layout;
        }
        delete item;
    }
    containerLayout_->addStretch();
}

QString StickerListWidget::formatTypeString(Discord::StickerFormatType type)
{
    switch (type) {
    case Discord::StickerFormatType::PNG:
        return QStringLiteral("PNG");
    case Discord::StickerFormatType::APNG:
        return QStringLiteral("APNG");
    case Discord::StickerFormatType::Lottie:
        return QStringLiteral("Lottie");
    case Discord::StickerFormatType::GIF:
        return QStringLiteral("GIF");
    default:
        return QStringLiteral("Unknown");
    }
}

void StickerListWidget::loadStickerPreview(StickerWidget &entry,
                                           const Discord::Sticker &sticker)
{
    if (sticker.formatType.get() == Discord::StickerFormatType::Lottie) {
        // Lottie is not supported by Qt natively; show placeholder
        entry.previewButton->setText(QStringLiteral("Lottie"));
        entry.previewButton->setEnabled(false);
        entry.previewButton->setToolTip(tr("Preview unavailable for Lottie stickers"));
        return;
    }

    const QUrl url = Discord::Cdn::stickerImage(sticker.id, sticker.formatType.get(), 64);
    const bool isAnimated = (sticker.formatType.get() == Discord::StickerFormatType::APNG ||
                             sticker.formatType.get() == Discord::StickerFormatType::GIF);

    // Capture a QPointer to the button so we can detect if the sticker list
    // was rebuilt (and the button destroyed) before the reply arrives.
    QPointer<QToolButton> btn = entry.previewButton;

    if (isAnimated) {
        QNetworkReply *reply = nam_->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, btn]() {
            reply->deleteLater();
            if (!btn)
                return; // sticker list was rebuilt, button no longer exists
            if (reply->error() != QNetworkReply::NoError)
                return;

            QByteArray data = reply->readAll();
            // Parent both to the BUTTON (not `this`): rebuildStickerList()
            // destroys the button grid on every refresh, so the movie and its
            // buffer die with the button instead of leaking per rebuild and
            // decoding frames forever against dead widgets.
            auto *buffer = new QBuffer(btn);
            buffer->setData(data);
            buffer->open(QIODevice::ReadOnly);

            auto *movie = new QMovie(btn);
            movie->setDevice(buffer);
            movie->setScaledSize(QSize(kStickerIconSize, kStickerIconSize));

            if (movie->isValid() && movie->frameCount() > 1) {
                btn->setIcon(
                        QIcon(QPixmap::fromImage(movie->currentImage())));
                movie->start();
                connect(movie, &QMovie::frameChanged, this, [btn, movie](int) {
                    if (!btn || !movie)
                        return;
                    btn->setIcon(
                            QIcon(QPixmap::fromImage(movie->currentImage())));
                });
            } else {
                // Single-frame or invalid: fall back to static display
                delete movie;
                delete buffer;

                QPixmap pix;
                pix.loadFromData(data);
                pix = pix.scaled(kStickerIconSize, kStickerIconSize, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
                btn->setIcon(QIcon(pix));
            }
        });
    } else {
        QNetworkReply *reply = nam_->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, btn]() {
            reply->deleteLater();
            if (!btn)
                return; // sticker list was rebuilt
            if (reply->error() != QNetworkReply::NoError)
                return;
            QPixmap pix;
            if (pix.loadFromData(reply->readAll())) {
                pix = pix.scaled(kStickerIconSize, kStickerIconSize, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
                btn->setIcon(QIcon(pix));
            }
        });
    }
}

void StickerListWidget::rebuildStickerList()
{
    clearStickerWidgets();

    if (stickers_.isEmpty()) {
        auto *label = new QLabel(tr("No custom stickers in this server."), container_);
        label->setStyleSheet(QStringLiteral("color: palette(mid); font-style: italic;"));
        containerLayout_->insertWidget(0, label);
        return;
    }

    // Build grid sections
    auto *gridWidget = new QWidget(container_);
    auto *grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    int row = 0;
    int col = 0;

    for (const auto &sticker : stickers_) {
        if (!sticker.id.hasValue() || !sticker.name.hasValue())
            continue;

        StickerWidget entry;
        entry.stickerId = sticker.id.get();
        entry.name = sticker.name.get();
        entry.formatType = sticker.formatType.get();

        // Sticker preview cell
        auto *cellWidget = new QWidget(gridWidget);
        auto *cellLayout = new QVBoxLayout(cellWidget);
        cellLayout->setContentsMargins(4, 4, 4, 4);
        cellLayout->setSpacing(4);

        entry.previewButton = new QToolButton(cellWidget);
        entry.previewButton->setFixedSize(kStickerCellSize, kStickerCellSize);
        entry.previewButton->setIconSize(QSize(kStickerIconSize, kStickerIconSize));
        entry.previewButton->setStyleSheet(QStringLiteral(
                "QToolButton { border: 1px solid palette(mid); border-radius: 6px; }"
                "QToolButton:hover { border-color: palette(highlight); }"));

        // Tooltip with sticker info
        QString tooltip = entry.name;
        if (sticker.description.hasValue() && !sticker.description->isEmpty())
            tooltip += QStringLiteral("\n") + sticker.description.get();
        if (sticker.tags.hasValue() && !sticker.tags->isEmpty())
            tooltip += QStringLiteral("\nTags: ") + sticker.tags.get();
        entry.previewButton->setToolTip(tooltip);

        entry.nameLabel = new QLabel(entry.name, cellWidget);
        entry.nameLabel->setAlignment(Qt::AlignCenter);
        entry.nameLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));

        entry.formatLabel = new QLabel(formatTypeString(entry.formatType), cellWidget);
        entry.formatLabel->setAlignment(Qt::AlignCenter);
        entry.formatLabel->setStyleSheet(QStringLiteral("font-size: 10px; color: palette(mid);"));

        entry.renameButton = new QPushButton(tr("Rename"), cellWidget);
        entry.renameButton->setFixedSize(64, 24);
        entry.renameButton->setEnabled(enabled_);
        entry.renameButton->setStyleSheet(QStringLiteral("font-size: 11px;"));

        entry.deleteButton = new QPushButton(tr("Delete"), cellWidget);
        entry.deleteButton->setFixedSize(64, 24);
        entry.deleteButton->setEnabled(enabled_);
        entry.deleteButton->setStyleSheet(QStringLiteral(
                "font-size: 11px;"
                "QPushButton:hover { color: palette(bright-text); }"));

        cellLayout->addWidget(entry.previewButton, 0, Qt::AlignCenter);
        cellLayout->addWidget(entry.nameLabel, 0, Qt::AlignCenter);
        cellLayout->addWidget(entry.formatLabel, 0, Qt::AlignCenter);

        auto *buttonRow = new QHBoxLayout();
        buttonRow->setSpacing(4);
        buttonRow->addStretch();
        buttonRow->addWidget(entry.renameButton);
        buttonRow->addWidget(entry.deleteButton);
        buttonRow->addStretch();
        cellLayout->addLayout(buttonRow);

        // Wire up buttons
        const Core::Snowflake sid = entry.stickerId;
        const QString sname = entry.name;
        connect(entry.renameButton, &QPushButton::clicked, this,
                [this, sid, sname]() { onRenameClicked(sid, sname); });
        connect(entry.deleteButton, &QPushButton::clicked, this,
                [this, sid, sname]() { onDeleteClicked(sid, sname); });

        loadStickerPreview(entry, sticker);
        stickerWidgets_.append(entry);

        grid->addWidget(cellWidget, row, col);
        ++col;
        if (col >= kStickerGridColumns) {
            col = 0;
            ++row;
        }
    }

    containerLayout_->insertWidget(containerLayout_->count() - 1, gridWidget);
}

void StickerListWidget::onUploadClicked()
{
    if (!enabled_ || !guildId_.isValid())
        return;

    const QString filePath = QFileDialog::getOpenFileName(
            this, tr("Select Sticker Image"), QString(),
            tr("Images (*.png *.apng *.gif);;PNG (*.png);;APNG (*.apng);;GIF (*.gif)"));

    if (filePath.isEmpty())
        return;

    QFileInfo fi(filePath);
    if (fi.size() > kMaxStickerSize) {
        QMessageBox::warning(this, tr("File Too Large"),
                             tr("The selected file is %1 KB. Maximum sticker size is 512 KB.")
                                     .arg(fi.size() / 1024));
        return;
    }

    // Derive sticker name from filename
    QString stickerName = fi.completeBaseName();
    stickerName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")),
                        QStringLiteral("_"));
    if (stickerName.isEmpty())
        stickerName = QStringLiteral("sticker");
    if (stickerName.length() > 30)
        stickerName = stickerName.left(30);

    // Confirm name
    bool ok = false;
    const QString confirmedName = QInputDialog::getText(
            this, tr("Upload Sticker"), tr("Sticker name:"), QLineEdit::Normal,
            stickerName, &ok);
    if (!ok || confirmedName.isEmpty())
        return;

    // Optional description
    bool descOk = false;
    const QString description = QInputDialog::getText(
            this, tr("Upload Sticker"), tr("Description (optional):"),
            QLineEdit::Normal, QString(), &descOk);
    // Cancelling any modal step aborts the whole upload (consistent with the
    // name dialog above); previously only the name prompt aborted, so a
    // cancelled description still started an upload with an empty value.
    if (!descOk)
        return;

    // Optional tags (used for search/autocomplete)
    bool tagsOk = false;
    const QString tags = QInputDialog::getText(
            this, tr("Upload Sticker"), tr("Related emoji tags (optional):"),
            QLineEdit::Normal, QString(), &tagsOk);
    if (!tagsOk)
        return;

    uploadStickerFile(filePath, confirmedName, description, tags);
}

void StickerListWidget::uploadStickerFile(const QString &filePath, const QString &name,
                                           const QString &description, const QString &tags)
{
    if (!http_ || !guildId_.isValid())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file: %1").arg(filePath));
        return;
    }

    const QByteArray fileData = file.readAll();
    file.close();

    const QFileInfo fi(filePath);
    const QString suffix = fi.suffix().toLower();
    QString mimeType;
    if (suffix == QStringLiteral("gif"))
        mimeType = QStringLiteral("image/gif");
    else if (suffix == QStringLiteral("apng"))
        mimeType = QStringLiteral("image/apng");
    else
        mimeType = QStringLiteral("image/png");

    QJsonObject payload;
    payload[QStringLiteral("name")] = name;
    if (!description.isEmpty())
        payload[QStringLiteral("description")] = description;
    if (!tags.isEmpty())
        payload[QStringLiteral("tags")] = tags;
    payload[QStringLiteral("image")] = QStringLiteral("data:%1;base64,%2")
                                              .arg(mimeType,
                                                   QString::fromLatin1(fileData.toBase64()));

    const QString endpoint = QStringLiteral("/guilds/%1/stickers")
                                     .arg(QString::number(quint64(guildId_)));

    QPointer<StickerListWidget> guard(this);
    http_->post(endpoint, payload, [guard, name](const Discord::HttpResponse &response) {
        if (!guard)
            return;
        if (!response.success) {
            QMessageBox::warning(guard, tr("Upload Failed"),
                                 tr("Failed to upload sticker '%1': %2")
                                         .arg(name, response.error));
            return;
        }

        // The gateway GUILD_STICKERS_UPDATE event will fire and
        // setStickers() will be called to refresh the list.
        emit guard->stickerModified();
    });
}

void StickerListWidget::onRenameClicked(Core::Snowflake stickerId, const QString &oldName)
{
    if (!enabled_ || !http_ || !guildId_.isValid())
        return;

    bool ok = false;
    const QString newName = QInputDialog::getText(
            this, tr("Rename Sticker"), tr("New name:"), QLineEdit::Normal,
            oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName)
        return;

    if (newName.length() > 30) {
        QMessageBox::warning(this, tr("Name Too Long"),
                             tr("Sticker names must be 30 characters or fewer."));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("name")] = newName;

    const QString endpoint = QStringLiteral("/guilds/%1/stickers/%2")
                                     .arg(QString::number(quint64(guildId_)),
                                          QString::number(quint64(stickerId)));

    QPointer<StickerListWidget> guard(this);
    http_->patch(endpoint, payload,
                 [guard, newName, oldName](const Discord::HttpResponse &response) {
                     if (!guard)
                         return;
                     if (!response.success) {
                         QMessageBox::warning(guard, tr("Rename Failed"),
                                              tr("Failed to rename sticker '%1' to '%2': %3")
                                                      .arg(oldName, newName, response.error));
                         return;
                     }
                     emit guard->stickerModified();
                 });
}

void StickerListWidget::onDeleteClicked(Core::Snowflake stickerId, const QString &name)
{
    if (!enabled_ || !http_ || !guildId_.isValid())
        return;

    const auto result = QMessageBox::question(
            this, tr("Delete Sticker"),
            tr("Are you sure you want to delete the sticker '%1'?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result != QMessageBox::Yes)
        return;

    const QString endpoint = QStringLiteral("/guilds/%1/stickers/%2")
                                     .arg(QString::number(quint64(guildId_)),
                                          QString::number(quint64(stickerId)));

    QPointer<StickerListWidget> guard(this);
    http_->delete_(endpoint, [guard, name](const Discord::HttpResponse &response) {
        if (!guard)
            return;
        if (!response.success) {
            QMessageBox::warning(guard, tr("Delete Failed"),
                                 tr("Failed to delete sticker '%1': %2")
                                         .arg(name, response.error));
            return;
        }
        emit guard->stickerModified();
    });
}

} // namespace UI
} // namespace Acheron
