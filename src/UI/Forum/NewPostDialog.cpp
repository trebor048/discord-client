#include "NewPostDialog.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "Core/Theme/Manager.hpp"
#include "UI/Input/AttachmentPreviewPanel.hpp"

namespace Acheron {
namespace UI {

NewPostDialog::NewPostDialog(const QList<Discord::Channel::ForumTag> &availableTags, bool tagRequired, QWidget *parent)
    : QDialog(parent),
      tagRequired(tagRequired && !availableTags.isEmpty())
{
    setWindowTitle(tr("New Post"));
    setModal(true);
    resize(480, 460);

    auto *layout = new QVBoxLayout(this);

    titleEdit = new QLineEdit(this);
    titleEdit->setMaxLength(100);
    titleEdit->setPlaceholderText(tr("Post title"));
    layout->addWidget(titleEdit);

    if (!availableTags.isEmpty()) {
        const QString tagsLabel = this->tagRequired
                                          ? tr("Tags (at least one, up to %1)").arg(kMaxAppliedTags)
                                          : tr("Tags (up to %1)").arg(kMaxAppliedTags);
        layout->addWidget(new QLabel(tagsLabel, this));
        tagList = new QListWidget(this);
        tagList->setMaximumHeight(110);
        for (const auto &tag : availableTags) {
            if (!tag.id.hasValue() || !tag.name.hasValue())
                continue;
            auto *item = new QListWidgetItem(tag.name.get(), tagList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            item->setData(Qt::UserRole, QVariant::fromValue<quint64>(tag.id.get()));
        }
        layout->addWidget(tagList);
    }

    bodyEdit = new QPlainTextEdit(this);
    bodyEdit->setPlaceholderText(tr("Write your post…"));
    layout->addWidget(bodyEdit, 1);

    auto *fileRow = new QHBoxLayout();
    addFilesButton = new QPushButton(tr("Add files…"), this);
    fileRow->addWidget(addFilesButton);
    fileRow->addStretch(1);
    layout->addLayout(fileRow);

    attachmentPanel = new AttachmentPreviewPanel(this);
    layout->addWidget(attachmentPanel);

    errorLabel = new QLabel(this);
    errorLabel->setWordWrap(true);
    errorLabel->setVisible(false);
    QColor errorColor = Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError);
    errorLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(errorColor.name()));
    layout->addWidget(errorLabel);

    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Post"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &NewPostDialog::submitted);
    connect(buttons, &QDialogButtonBox::rejected, this, &NewPostDialog::reject);
    connect(addFilesButton, &QPushButton::clicked, this, &NewPostDialog::addFiles);
    connect(titleEdit, &QLineEdit::textChanged, this, &NewPostDialog::updateOkState);
    connect(bodyEdit, &QPlainTextEdit::textChanged, this, &NewPostDialog::updateOkState);
    connect(attachmentPanel, &AttachmentPreviewPanel::attachmentsChanged, this,
            &NewPostDialog::updateOkState);
    if (tagList)
        connect(tagList, &QListWidget::itemChanged, this, [this]() {
            updateTagAvailability();
            updateOkState();
        });

    updateOkState();
}

void NewPostDialog::addFiles()
{
    QList<QUrl> urls;
    for (const QString &path : QFileDialog::getOpenFileNames(this, tr("Add files")))
        urls.append(QUrl::fromLocalFile(path));
    attachmentPanel->addFiles(urls);
}

void NewPostDialog::setMaxUploadSize(qint64 bytes)
{
    attachmentPanel->setMaxFileSize(bytes);
}

void NewPostDialog::updateOkState()
{
    errorLabel->setVisible(false);

    bool hasTitle = !titleEdit->text().trimmed().isEmpty();
    bool hasBody =
            !bodyEdit->toPlainText().trimmed().isEmpty() || attachmentPanel->hasAttachments();
    bool hasTags = !tagRequired || !selectedTagIds().isEmpty();
    buttons->button(QDialogButtonBox::Ok)->setEnabled(hasTitle && hasBody && hasTags);
}

void NewPostDialog::updateTagAvailability()
{
    if (!tagList)
        return;

    const bool atCap = selectedTagIds().size() >= kMaxAppliedTags;
    QSignalBlocker blocker(tagList);
    for (int i = 0; i < tagList->count(); i++) {
        QListWidgetItem *item = tagList->item(i);
        const bool checked = item->checkState() == Qt::Checked;
        item->setFlags(atCap && !checked ? item->flags() & ~Qt::ItemIsEnabled
                                         : item->flags() | Qt::ItemIsEnabled);
    }
}

void NewPostDialog::setBusy(bool isBusy)
{
    busy = isBusy;
    titleEdit->setEnabled(!busy);
    bodyEdit->setEnabled(!busy);
    addFilesButton->setEnabled(!busy);
    attachmentPanel->setEnabled(!busy);
    if (tagList)
        tagList->setEnabled(!busy);
    buttons->button(QDialogButtonBox::Cancel)->setEnabled(!busy);

    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(busy ? tr("Posting…") : tr("Post"));
    if (busy)
        ok->setEnabled(false);
    else
        updateOkState();
}

void NewPostDialog::reject()
{
    if (busy)
        return;
    QDialog::reject();
}

void NewPostDialog::showError(const QString &error)
{
    setBusy(false);
    errorLabel->setText(error);
    errorLabel->setVisible(true);
}

QList<Core::PendingAttachment> NewPostDialog::attachments() const
{
    return attachmentPanel->attachments();
}

QString NewPostDialog::title() const
{
    return titleEdit->text().trimmed();
}

QString NewPostDialog::content() const
{
    return bodyEdit->toPlainText();
}

QList<Core::Snowflake> NewPostDialog::selectedTagIds() const
{
    QList<Core::Snowflake> ids;
    if (!tagList)
        return ids;
    for (int i = 0; i < tagList->count(); i++) {
        QListWidgetItem *item = tagList->item(i);
        if (item->checkState() == Qt::Checked)
            ids.append(Core::Snowflake(item->data(Qt::UserRole).toULongLong()));
    }
    return ids;
}

} // namespace UI
} // namespace Acheron
