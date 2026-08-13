#include "AttachmentPreviewPanel.hpp"

#include <QBuffer>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "UI/Chat/ChatLayout.hpp"
#include "UI/Dialogs/BasePopup.hpp"

namespace Acheron {
namespace UI {

namespace {

class AttachmentEditPopup : public BasePopup
{
public:
    AttachmentEditPopup(Core::PendingAttachment &attachment, QWidget *parent)
        : BasePopup(parent)
    {
        auto *layout = new QVBoxLayout(getContainer());
        layout->setSpacing(12);
        layout->setContentsMargins(24, 24, 24, 24);

        auto *titleLabel = new QLabel(tr("Edit Attachment"), getContainer());
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 2);
        titleLabel->setFont(titleFont);
        layout->addWidget(titleLabel);

        layout->addWidget(new QLabel(tr("File Name"), getContainer()));
        auto *filenameEdit = new QLineEdit(attachment.filename, getContainer());
        filenameEdit->setMaxLength(1024);
        filenameEdit->setMinimumWidth(320);
        layout->addWidget(filenameEdit);

        layout->addWidget(new QLabel(tr("Description (Alt Text)"), getContainer()));
        auto *descriptionEdit = new QLineEdit(attachment.description, getContainer());
        descriptionEdit->setMaxLength(1024);
        descriptionEdit->setPlaceholderText(tr("Add a description"));
        layout->addWidget(descriptionEdit);

        auto *spoilerCheck = new QCheckBox(tr("Mark as spoiler"), getContainer());
        spoilerCheck->setChecked(attachment.isSpoiler);
        layout->addWidget(spoilerCheck);

        auto *btnBox = new QDialogButtonBox(getContainer());
        QPushButton *saveButton = btnBox->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
        btnBox->addButton(QDialogButtonBox::Cancel);
        saveButton->setDefault(true);
        layout->addWidget(btnBox);

        connect(btnBox, &QDialogButtonBox::accepted, this,
                [this, &attachment, filenameEdit, descriptionEdit, spoilerCheck]() {
                    QString filename = filenameEdit->text().trimmed();
                    if (!filename.isEmpty())
                        attachment.filename = filename;
                    attachment.description = descriptionEdit->text();
                    attachment.isSpoiler = spoilerCheck->isChecked();
                    accept();
                });
        connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
};

} // namespace

AttachmentPreviewPanel::AttachmentPreviewPanel(QWidget *parent) : QWidget(parent)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 4, 0, 2);
    outerLayout->setSpacing(2);

    errorLabel = new QLabel(this);
    errorLabel->setVisible(false);
    QColor errorColor = Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError);
    errorLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(errorColor.name()));
    outerLayout->addWidget(errorLabel);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFixedHeight(132);

    cardsContainer = new QWidget(scrollArea);
    cardsLayout = new QHBoxLayout(cardsContainer);
    cardsLayout->setContentsMargins(4, 4, 4, 4);
    cardsLayout->setSpacing(8);
    cardsLayout->addStretch();
    scrollArea->setWidget(cardsContainer);
    outerLayout->addWidget(scrollArea);

    errorTimer = new QTimer(this);
    errorTimer->setSingleShot(true);
    errorTimer->setInterval(4000);
    connect(errorTimer, &QTimer::timeout, this, [this]() {
        errorLabel->setVisible(false);
        updateVisibility();
        emit attachmentsChanged();
    });

    setVisible(false);
}

void AttachmentPreviewPanel::setMaxFileSize(qint64 bytes)
{
    maxFileSize = bytes;
}

void AttachmentPreviewPanel::addFiles(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;

        QFileInfo info(url.toLocalFile());
        if (!info.isFile() || !info.isReadable()) {
            showError(tr("Can't read %1").arg(info.fileName()));
            continue;
        }

        Core::PendingAttachment attachment;
        attachment.filePath = info.absoluteFilePath();
        attachment.filename = info.fileName();
        attachment.size = info.size();
        attachment.mimeType = QMimeDatabase().mimeTypeForFile(info.absoluteFilePath()).name();
        if (!appendAttachment(attachment))
            break; // no more
    }
}

void AttachmentPreviewPanel::addImage(const QImage &image)
{
    if (image.isNull())
        return;

    Core::PendingAttachment attachment;
    QBuffer buffer(&attachment.data);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        showError(tr("Couldn't process the pasted image"));
        return;
    }
    attachment.image = image;
    attachment.filename = QStringLiteral("image.png");
    attachment.mimeType = QStringLiteral("image/png");
    attachment.size = attachment.data.size();
    appendAttachment(attachment);
}

void AttachmentPreviewPanel::addGifData(const QByteArray &data, const QImage &preview,
                                           const QString &filename)
{
    if (data.isEmpty())
        return;

    Core::PendingAttachment attachment;
    attachment.data = data;
    attachment.image = preview;
    attachment.filename = filename.isEmpty() ? QStringLiteral("gif.gif") : filename;
    attachment.mimeType = QStringLiteral("image/gif");
    attachment.size = data.size();
    appendAttachment(attachment);
}

void AttachmentPreviewPanel::showTransientError(const QString &message)
{
    showError(message);
}

bool AttachmentPreviewPanel::appendAttachment(Core::PendingAttachment attachment)
{
    if (items.size() >= MaxFiles) {
        showError(tr("You can attach up to %1 files per message").arg(MaxFiles));
        return false;
    }
    if (attachment.size > maxFileSize) {
        showError(tr("%1 exceeds the %2 upload limit")
                          .arg(attachment.filename, ChatLayout::formatFileSize(maxFileSize)));
        return false;
    }

    items.append(std::move(attachment));
    rebuildCards();
    emit attachmentsChanged();
    return true;
}

void AttachmentPreviewPanel::clearAttachments()
{
    if (items.isEmpty() && !errorLabel->isVisible())
        return;

    items.clear();
    errorLabel->setVisible(false);
    rebuildCards();
    emit attachmentsChanged();
}

void AttachmentPreviewPanel::rebuildCards()
{
    while (cardsLayout->count() > 1) { // keep the stretch at the end
        QLayoutItem *item = cardsLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (int i = 0; i < items.size(); i++)
        cardsLayout->insertWidget(i, createCard(i));

    updateVisibility();
}

QWidget *AttachmentPreviewPanel::createCard(int index)
{
    const auto &item = items[index];
    auto &theme = Core::Theme::Manager::instance();

    auto *card = new QFrame(cardsContainer);
    card->setFixedSize(124, 120);
    card->setStyleSheet(QStringLiteral("QFrame { background-color: %1; border-radius: 4px; }")
                                .arg(theme.color(Core::Theme::Token::AlternateBaseBg).name()));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 2, 6, 4);
    layout->setSpacing(2);

    auto *thumbLabel = new QLabel(card);
    thumbLabel->setFixedHeight(64);
    thumbLabel->setAlignment(Qt::AlignCenter);
    thumbLabel->setPixmap(thumbnailFor(item));

    auto makeButton = [card](const QString &iconName, const QString &tooltip) {
        auto *button = new QToolButton(card);
        button->setIcon(Core::Theme::Icons::icon(iconName, Core::Theme::Token::PlaceholderText));
        button->setIconSize(QSize(15, 15));
        button->setToolTip(tooltip);
        button->setFixedSize(18, 18);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet("QToolButton { border: none; }");
        return button;
    };

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(2);
    buttonRow->addStretch();

    auto *spoilerButton = makeButton(Core::Theme::Icons::Name::Eye, tr("Spoiler"));
    spoilerButton->setCheckable(true);
    spoilerButton->setChecked(item.isSpoiler);
    connect(spoilerButton, &QToolButton::toggled, this, [this, index, thumbLabel](bool checked) {
        if (index >= items.size())
            return;
        items[index].isSpoiler = checked;
        thumbLabel->setPixmap(thumbnailFor(items[index]));
    });
    buttonRow->addWidget(spoilerButton);

    auto *editButton = makeButton(Core::Theme::Icons::Name::Pencil, tr("Edit"));
    connect(editButton, &QToolButton::clicked, this, [this, index]() {
        editAttachment(index);
    });
    buttonRow->addWidget(editButton);

    auto *removeButton = makeButton(Core::Theme::Icons::Name::X, tr("Remove"));
    connect(removeButton, &QToolButton::clicked, this, [this, index]() {
        if (index >= items.size())
            return;
        items.removeAt(index);
        rebuildCards();
        emit attachmentsChanged();
    });
    buttonRow->addWidget(removeButton);

    layout->addLayout(buttonRow);
    layout->addWidget(thumbLabel);

    QFont nameFont = font();
    nameFont.setPointSize(nameFont.pointSize() - 1);
    QFontMetrics nameFm(nameFont);

    auto *nameLabel = new QLabel(nameFm.elidedText(item.filename, Qt::ElideMiddle, 108), card);
    nameLabel->setFont(nameFont);
    nameLabel->setToolTip(item.filename);
    layout->addWidget(nameLabel);

    auto *sizeLabel = new QLabel(item.size > 0 ? ChatLayout::formatFileSize(item.size)
                                               : tr("Clipboard image"),
                                 card);
    sizeLabel->setFont(nameFont);
    sizeLabel->setStyleSheet(
            QStringLiteral("color: %1;")
                    .arg(theme.color(Core::Theme::Token::PlaceholderText).name()));
    layout->addWidget(sizeLabel);

    return card;
}

QPixmap AttachmentPreviewPanel::thumbnailFor(const Core::PendingAttachment &item) const
{
    constexpr QSize thumbSize(108, 64);

    QPixmap thumb;
    bool isImageThumb = false;
    if (!item.image.isNull()) {
        thumb = QPixmap::fromImage(
                item.image.scaled(thumbSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        isImageThumb = !thumb.isNull();
    } else if (item.mimeType.startsWith("image/")) {
        QImageReader reader(item.filePath);
        reader.setAutoTransform(true);
        QSize original = reader.size();
        if (original.isValid())
            reader.setScaledSize(original.scaled(thumbSize, Qt::KeepAspectRatio));
        thumb = QPixmap::fromImage(reader.read());
        isImageThumb = !thumb.isNull();
    }

    if (thumb.isNull()) {
        QFileIconProvider iconProvider;
        thumb = iconProvider.icon(QFileInfo(item.filePath)).pixmap(48, 48);
    }

    if (item.isSpoiler && isImageThumb)
        thumb = ChatLayout::createBlurredPixmap(thumb, 60);

    qreal dpr = devicePixelRatioF();
    QPixmap canvas(thumbSize * dpr);
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    QSizeF logicalSize = QSizeF(thumb.size()) / thumb.devicePixelRatio();
    painter.drawPixmap(QPointF((thumbSize.width() - logicalSize.width()) / 2.0,
                               (thumbSize.height() - logicalSize.height()) / 2.0),
                       thumb);

    if (item.isSpoiler) {
        painter.fillRect(QRect(QPoint(0, 0), thumbSize), QColor(0, 0, 0, 100));

        QFont spoilerFont = font();
        spoilerFont.setBold(true);
        painter.setFont(spoilerFont);
        painter.setPen(Qt::white);
        painter.drawText(QRect(QPoint(0, 0), thumbSize), Qt::AlignCenter, tr("SPOILER"));
    }

    return canvas;
}

void AttachmentPreviewPanel::editAttachment(int index)
{
    if (index < 0 || index >= items.size())
        return;

    AttachmentEditPopup popup(items[index], this);
    if (popup.exec() == QDialog::Accepted) {
        rebuildCards();
        emit attachmentsChanged();
    }
}

void AttachmentPreviewPanel::showError(const QString &message)
{
    errorLabel->setText(message);
    errorLabel->setVisible(true);
    errorTimer->start();
    updateVisibility();
    emit attachmentsChanged();
}

void AttachmentPreviewPanel::updateVisibility()
{
    setVisible(!items.isEmpty() || errorLabel->isVisible());
}

} // namespace UI
} // namespace Acheron
