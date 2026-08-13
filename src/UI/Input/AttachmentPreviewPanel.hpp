#pragma once

#include <QWidget>

#include "Core/PendingAttachment.hpp"

class QHBoxLayout;
class QLabel;
class QScrollArea;
class QTimer;

namespace Acheron {
namespace UI {

class AttachmentPreviewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AttachmentPreviewPanel(QWidget *parent = nullptr);

    void setMaxFileSize(qint64 bytes);
    void addFiles(const QList<QUrl> &urls);
    void addImage(const QImage &image);
    void addGifData(const QByteArray &data, const QImage &preview, const QString &filename);
    void showTransientError(const QString &message);
    [[nodiscard]] bool hasAttachments() const { return !items.isEmpty(); }
    [[nodiscard]] QList<Core::PendingAttachment> attachments() const { return items; }
    void clearAttachments();

signals:
    void attachmentsChanged();

private:
    static constexpr int MaxFiles = 10;
    bool appendAttachment(Core::PendingAttachment attachment);
    void rebuildCards();
    QWidget *createCard(int index);
    QPixmap thumbnailFor(const Core::PendingAttachment &item) const;
    void editAttachment(int index);
    void showError(const QString &message);
    void updateVisibility();

    QList<Core::PendingAttachment> items;
    QScrollArea *scrollArea;
    QWidget *cardsContainer;
    QHBoxLayout *cardsLayout;
    QLabel *errorLabel;
    QTimer *errorTimer;
    qint64 maxFileSize = 10ll * 1024 * 1024;
};

} // namespace UI
} // namespace Acheron
