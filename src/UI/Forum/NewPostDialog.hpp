#pragma once

#include <QDialog>
#include <QList>

#include "Core/PendingAttachment.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QPushButton;
class QDialogButtonBox;

namespace Acheron {
namespace UI {

class AttachmentPreviewPanel;

class NewPostDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewPostDialog(const QList<Discord::Channel::ForumTag> &availableTags, bool tagRequired, QWidget *parent = nullptr);

    void setMaxUploadSize(qint64 bytes);
    void setBusy(bool isBusy);
    void showError(const QString &error);

    [[nodiscard]] QString title() const;
    [[nodiscard]] QString content() const;
    [[nodiscard]] QList<Core::Snowflake> selectedTagIds() const;
    [[nodiscard]] QList<Core::PendingAttachment> attachments() const;

signals:
    void submitted();

public slots:
    void reject() override;

private slots:
    void addFiles();
    void updateOkState();

private:
    static constexpr int kMaxAppliedTags = 5;
    void updateTagAvailability();

    QLineEdit *titleEdit;
    QPlainTextEdit *bodyEdit;
    QListWidget *tagList = nullptr;
    QPushButton *addFilesButton;
    AttachmentPreviewPanel *attachmentPanel;
    QLabel *errorLabel;
    QDialogButtonBox *buttons;
    bool busy = false;
    bool tagRequired = false;
};

} // namespace UI
} // namespace Acheron
