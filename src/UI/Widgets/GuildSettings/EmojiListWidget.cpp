#include "EmojiListWidget.hpp"

#include "Discord/Client.hpp"
#include "Discord/HttpClient.hpp"
#include "Discord/CdnUrls.hpp"

#include <QBuffer>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMovie>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace Acheron {
namespace UI {

namespace {
constexpr qint64 kMaxEmojiSize = 256 * 1024; // 256 KB
constexpr int kEmojiGridColumns = 8;
constexpr int kEmojiCellSize = 48;
constexpr int kEmojiIconSize = 40;
} // namespace

EmojiListWidget::EmojiListWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(12, 12, 12, 12);
    outerLayout->setSpacing(8);

    titleLabel_ = new QLabel(tr("Guild Emojis"), this);
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
    uploadButton_ = new QPushButton(tr("Upload Emoji"), this);
    uploadButton_->setEnabled(false);
    connect(uploadButton_, &QPushButton::clicked, this, &EmojiListWidget::onUploadClicked);
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

EmojiListWidget::~EmojiListWidget()
{
    clearEmojiWidgets();
}

void EmojiListWidget::setGuildId(Core::Snowflake guildId)
{
    guildId_ = guildId;
}

void EmojiListWidget::setDiscordClient(Discord::Client *client)
{
    client_ = client;
}

void EmojiListWidget::setHttpClient(Discord::HttpClient *http)
{
    http_ = http;
}

void EmojiListWidget::setEnabled(bool enabled)
{
    // EmojiPage::load() runs on every navigation to the Emoji tab and always
    // calls setEnabled() with the same value. Rebuilding the whole grid (and
    // re-fetching every emoji image) on a no-op toggle would hammer the CDN.
    const bool changed = enabled != enabled_;
    enabled_ = enabled;
    uploadButton_->setEnabled(enabled);
    permissionLabel_->setText(enabled
            ? tr("You can manage emojis for this server.")
            : tr("You need the Manage Expressions permission to manage emojis."));
    if (changed)
        rebuildEmojiList();
}

void EmojiListWidget::setEmojis(const QList<Discord::Emoji> &emojis)
{
    QList<Discord::Emoji> sorted = emojis;
    std::sort(sorted.begin(), sorted.end(), [](const Discord::Emoji &a, const Discord::Emoji &b) {
        return a.name.get().toCaseFolded() < b.name.get().toCaseFolded();
    });

    // Skip the rebuild when the set is unchanged: the caller (EmojiPage) fires
    // this on every page visit and on every gateway customEmojisChanged, and a
    // full reset would re-create every cell and re-download every preview.
    // Compare every field a cell renders: id/name/animated drive the preview
    // and label, and `managed` drives whether Rename/Delete are enabled — a
    // gateway update flipping only that would otherwise leave the buttons stale.
    if (sorted.size() == emojis_.size()) {
        bool same = true;
        for (int i = 0; i < sorted.size(); ++i) {
            if (sorted[i].id.get() != emojis_[i].id.get()
                || sorted[i].name.get() != emojis_[i].name.get()
                || sorted[i].animated.getOr(false) != emojis_[i].animated.getOr(false)
                || sorted[i].managed.getOr(false) != emojis_[i].managed.getOr(false)) {
                same = false;
                break;
            }
        }
        if (same)
            return;
    }

    emojis_ = sorted;
    rebuildEmojiList();
}

void EmojiListWidget::clearEmojiWidgets()
{
    for (auto &entry : emojiWidgets_) {
        // Clean up movie/buffer stored as button properties
        if (entry.previewButton) {
            auto *movie = entry.previewButton->property("emojiMovie").value<QMovie *>();
            auto *buffer = entry.previewButton->property("emojiBuffer").value<QBuffer *>();
            if (movie) {
                movie->stop();
                delete movie;
            }
            if (buffer)
                delete buffer;
        }
    }
    emojiWidgets_.clear();

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

void EmojiListWidget::loadEmojiPreview(EmojiWidget &entry, const Discord::Emoji &emoji)
{
    if (!emoji.id.hasValue())
        return;

    const QString emojiIdStr = emoji.id->toString();
    const bool animated = emoji.animated.getOr(false);
    const QUrl url = Discord::Cdn::emojiImage(emojiIdStr, animated, 64);

    // Capture QPointer to the preview button so the lambda can safely detect
    // widget destruction if the emoji list is rebuilt before the reply arrives.
    QPointer<QToolButton> btn = entry.previewButton;

    if (animated) {
        QNetworkReply *reply = nam_->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, btn]() {
            reply->deleteLater();
            if (!btn)
                return;
            if (reply->error() != QNetworkReply::NoError)
                return;

            QByteArray data = reply->readAll();
            auto *buffer = new QBuffer(this);
            buffer->setData(data);
            buffer->open(QIODevice::ReadOnly);

            auto *movie = new QMovie(this);
            movie->setDevice(buffer);
            movie->setFormat(QByteArrayLiteral("gif"));
            movie->setScaledSize(QSize(kEmojiIconSize, kEmojiIconSize));

            if (movie->isValid()) {
                btn->setIcon(QIcon(QPixmap::fromImage(movie->currentImage())));
                movie->start();

                // Store the movie and buffer on the button so they are cleaned up
                // when the button is destroyed.
                btn->setProperty("emojiMovie", QVariant::fromValue(movie));
                btn->setProperty("emojiBuffer", QVariant::fromValue(buffer));

                connect(movie, &QMovie::frameChanged, this, [btn, movie](int) {
                    if (btn)
                        btn->setIcon(QIcon(QPixmap::fromImage(movie->currentImage())));
                });
            } else {
                delete movie;
                delete buffer;
            }
        });
    } else {
        QNetworkReply *reply = nam_->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, btn]() {
            reply->deleteLater();
            if (!btn)
                return;
            if (reply->error() != QNetworkReply::NoError)
                return;
            QPixmap pix;
            if (pix.loadFromData(reply->readAll())) {
                pix = pix.scaled(kEmojiIconSize, kEmojiIconSize,
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
                btn->setIcon(QIcon(pix));
            }
        });
    }
}

void EmojiListWidget::rebuildEmojiList()
{
    clearEmojiWidgets();

    if (emojis_.isEmpty()) {
        auto *label = new QLabel(tr("No custom emojis in this server."), container_);
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

    for (const auto &emoji : emojis_) {
        if (!emoji.id.hasValue() || !emoji.name.hasValue())
            continue;

        EmojiWidget entry;
        entry.emojiId = emoji.id.get();
        entry.name = emoji.name.get();
        entry.animated = emoji.animated.getOr(false);

        // Emoji preview cell
        auto *cellWidget = new QWidget(gridWidget);
        auto *cellLayout = new QVBoxLayout(cellWidget);
        cellLayout->setContentsMargins(4, 4, 4, 4);
        cellLayout->setSpacing(4);

        entry.previewButton = new QToolButton(cellWidget);
        entry.previewButton->setFixedSize(kEmojiCellSize, kEmojiCellSize);
        entry.previewButton->setIconSize(QSize(kEmojiIconSize, kEmojiIconSize));
        entry.previewButton->setStyleSheet(QStringLiteral(
                "QToolButton { border: 1px solid palette(mid); border-radius: 6px; }"
                "QToolButton:hover { border-color: palette(highlight); }"));
        entry.previewButton->setToolTip(QStringLiteral(":%1:").arg(entry.name));

        entry.nameLabel = new QLabel(entry.name, cellWidget);
        entry.nameLabel->setAlignment(Qt::AlignCenter);
        entry.nameLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));

        entry.renameButton = new QPushButton(tr("Rename"), cellWidget);
        entry.renameButton->setFixedSize(64, 24);
        entry.renameButton->setEnabled(enabled_ && !emoji.managed.getOr(false));
        entry.renameButton->setStyleSheet(QStringLiteral("font-size: 11px;"));

        entry.deleteButton = new QPushButton(tr("Delete"), cellWidget);
        entry.deleteButton->setFixedSize(64, 24);
        entry.deleteButton->setEnabled(enabled_ && !emoji.managed.getOr(false));
        entry.deleteButton->setStyleSheet(QStringLiteral(
                "font-size: 11px;"
                "QPushButton:hover { color: palette(bright-text); }"));

        cellLayout->addWidget(entry.previewButton, 0, Qt::AlignCenter);
        cellLayout->addWidget(entry.nameLabel, 0, Qt::AlignCenter);

        auto *buttonRow = new QHBoxLayout();
        buttonRow->setSpacing(4);
        buttonRow->addStretch();
        buttonRow->addWidget(entry.renameButton);
        buttonRow->addWidget(entry.deleteButton);
        buttonRow->addStretch();
        cellLayout->addLayout(buttonRow);

        // Wire up buttons
        const Core::Snowflake eid = entry.emojiId;
        const QString ename = entry.name;
        connect(entry.renameButton, &QPushButton::clicked, this,
                [this, eid, ename]() { onRenameClicked(eid, ename); });
        connect(entry.deleteButton, &QPushButton::clicked, this,
                [this, eid, ename]() { onDeleteClicked(eid, ename); });

        loadEmojiPreview(entry, emoji);
        emojiWidgets_.append(entry);

        grid->addWidget(cellWidget, row, col);
        ++col;
        if (col >= kEmojiGridColumns) {
            col = 0;
            ++row;
        }
    }

    containerLayout_->insertWidget(containerLayout_->count() - 1, gridWidget);
}

void EmojiListWidget::onUploadClicked()
{
    if (!enabled_ || !guildId_.isValid()) {
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
            this, tr("Select Emoji Image"), QString(),
            tr("Images (*.png *.gif);;PNG (*.png);;GIF (*.gif)"));

    if (filePath.isEmpty())
        return;

    QFileInfo fi(filePath);
    if (fi.size() > kMaxEmojiSize) {
        QMessageBox::warning(this, tr("File Too Large"),
                             tr("The selected file is %1 KB. Maximum emoji size is 256 KB.")
                                     .arg(fi.size() / 1024));
        return;
    }

    // Derive emoji name from filename (without extension)
    QString emojiName = fi.completeBaseName();
    emojiName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    if (emojiName.isEmpty())
        emojiName = QStringLiteral("emoji");

    if (emojiName.length() > 32)
        emojiName = emojiName.left(32);

    // Confirm the name
    bool ok = false;
    const QString confirmedName = QInputDialog::getText(
            this, tr("Upload Emoji"), tr("Emoji name:"), QLineEdit::Normal,
            emojiName, &ok);
    if (!ok || confirmedName.isEmpty())
        return;

    uploadEmojiFile(filePath, confirmedName);
}

void EmojiListWidget::uploadEmojiFile(const QString &filePath, const QString &name)
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
    const bool isGif = fi.suffix().toLower() == QStringLiteral("gif");

    Discord::FileUpload upload;
    upload.filename = fi.fileName();
    upload.data = fileData;
    upload.mimeType = isGif ? QStringLiteral("image/gif") : QStringLiteral("image/png");

    QJsonObject payload;
    payload[QStringLiteral("name")] = name;
    payload[QStringLiteral("image")] = QStringLiteral("data:%1;base64,%2")
                                               .arg(upload.mimeType,
                                                    QString::fromLatin1(fileData.toBase64()));

    const QString endpoint = QStringLiteral("/guilds/%1/emojis")
                                     .arg(QString::number(quint64(guildId_)));

    QPointer<EmojiListWidget> guard(this);
    http_->post(endpoint, payload, [guard, name](const Discord::HttpResponse &response) {
        if (!guard)
            return;
        if (!response.success) {
            QMessageBox::warning(guard, tr("Upload Failed"),
                                 tr("Failed to upload emoji '%1': %2")
                                         .arg(name, response.error));
            return;
        }

        // The API response contains the new emoji object.
        // The gateway GUILD_EMOJIS_UPDATE event will fire and
        // setEmojis() will be called to refresh the list.
        emit guard->emojiModified();
    });
}

void EmojiListWidget::onRenameClicked(Core::Snowflake emojiId, const QString &oldName)
{
    if (!enabled_ || !http_ || !guildId_.isValid())
        return;

    bool ok = false;
    const QString newName = QInputDialog::getText(
            this, tr("Rename Emoji"), tr("New name:"), QLineEdit::Normal,
            oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName)
        return;

    if (newName.length() > 32) {
        QMessageBox::warning(this, tr("Name Too Long"),
                             tr("Emoji names must be 32 characters or fewer."));
        return;
    }

    QJsonObject payload;
    payload[QStringLiteral("name")] = newName;

    const QString endpoint = QStringLiteral("/guilds/%1/emojis/%2")
                                     .arg(QString::number(quint64(guildId_)),
                                          QString::number(quint64(emojiId)));

    QPointer<EmojiListWidget> guard(this);
    http_->patch(endpoint, payload, [guard, newName, oldName](const Discord::HttpResponse &response) {
        if (!guard)
            return;
        if (!response.success) {
            QMessageBox::warning(guard, tr("Rename Failed"),
                                 tr("Failed to rename emoji '%1' to '%2': %3")
                                         .arg(oldName, newName, response.error));
            return;
        }
        emit guard->emojiModified();
    });
}

void EmojiListWidget::onDeleteClicked(Core::Snowflake emojiId, const QString &name)
{
    if (!enabled_ || !http_ || !guildId_.isValid())
        return;

    const auto result = QMessageBox::question(
            this, tr("Delete Emoji"),
            tr("Are you sure you want to delete ':%1:'?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result != QMessageBox::Yes)
        return;

    const QString endpoint = QStringLiteral("/guilds/%1/emojis/%2")
                                     .arg(QString::number(quint64(guildId_)),
                                          QString::number(quint64(emojiId)));

    QPointer<EmojiListWidget> guard(this);
    http_->delete_(endpoint, [guard, name](const Discord::HttpResponse &response) {
        if (!guard)
            return;
        if (!response.success) {
            QMessageBox::warning(guard, tr("Delete Failed"),
                                 tr("Failed to delete emoji '%1': %2")
                                         .arg(name, response.error));
            return;
        }
        emit guard->emojiModified();
    });
}

} // namespace UI
} // namespace Acheron
