#include "AttachmentGallery.hpp"

#include <QCache>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>

#include <functional>

namespace Acheron {
namespace UI {
namespace {

// Bounded session cache of decoded full-resolution images, keyed by the fetch
// URL (proxy URL + format/quality query) plus the device pixel ratio the
// pixmap was decoded at. Navigating back and forth between attachments no
// longer re-downloads / re-decodes an image already shown this session.
constexpr qsizetype kImageCacheMaxBytes = 160 * 1024 * 1024; // ~32 full-res images

QCache<QString, QPixmap> &fullImageCache()
{
    static QCache<QString, QPixmap> cache(static_cast<int>(kImageCacheMaxBytes / 1024));
    return cache;
}

void cacheFullImage(const QString &key, const QPixmap &pixmap)
{
    const qsizetype cost = qMax<qsizetype>(1, pixmap.sizeInBytes() / 1024);
    if (cost <= kImageCacheMaxBytes / 1024)
        fullImageCache().insert(key, new QPixmap(pixmap), static_cast<int>(cost));
}

/// QGraphicsView that routes navigation/zoom keys and the mouse wheel to the
/// gallery so keyboard/wheel interaction works even when the view has focus.
class GalleryView : public QGraphicsView
{
public:
    using QGraphicsView::QGraphicsView;

    std::function<void(int)> keyHandler;
    std::function<void(qreal)> zoomHandler;

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        if (zoomHandler && event->angleDelta().y() != 0) {
            zoomHandler(event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15);
            event->accept();
            return;
        }
        QGraphicsView::wheelEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (keyHandler) {
            switch (event->key()) {
            case Qt::Key_Escape:
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Plus:
            case Qt::Key_Equal:
            case Qt::Key_Minus:
            case Qt::Key_0:
            case Qt::Key_S:
            case Qt::Key_C:
                keyHandler(event->key());
                event->accept();
                return;
            default:
                break;
            }
        }
        QGraphicsView::keyPressEvent(event);
    }
};

} // namespace

AttachmentGallery::AttachmentGallery(const QList<AttachmentData> &atts, int startIndex,
                                     QWidget *parent)
    : QDialog(parent), attachments(atts)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Attachment Gallery"));
    resize(960, 680);

    scene = new QGraphicsScene(this);
    auto *galleryView = new GalleryView(scene, this);
    view = galleryView;
    view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    view->setDragMode(QGraphicsView::ScrollHandDrag);
    view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    view->setBackgroundBrush(QColor(18, 18, 18));
    view->setFrameShape(QFrame::NoFrame);
    view->setFocusPolicy(Qt::StrongFocus);

    imageItem = new QGraphicsPixmapItem();
    imageItem->setTransformationMode(Qt::SmoothTransformation);
    scene->addItem(imageItem);

    prevButton = new QPushButton(tr("Prev"), this);
    nextButton = new QPushButton(tr("Next"), this);
    counterLabel = new QLabel(this);
    counterLabel->setAlignment(Qt::AlignCenter);
    counterLabel->setMinimumWidth(64);
    zoomOutButton = new QPushButton(tr("Zoom Out"), this);
    zoomInButton = new QPushButton(tr("Zoom In"), this);
    fitButton = new QPushButton(tr("Fit"), this);
    saveButton = new QPushButton(tr("Save As"), this);
    saveAllButton = new QPushButton(tr("Save All"), this);
    copyLinkButton = new QPushButton(tr("Copy Link"), this);
    auto *closeButton = new QPushButton(tr("Close"), this);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(prevButton);
    toolbar->addWidget(nextButton);
    toolbar->addWidget(counterLabel);
    toolbar->addStretch();
    toolbar->addWidget(zoomOutButton);
    toolbar->addWidget(zoomInButton);
    toolbar->addWidget(fitButton);
    toolbar->addStretch();
    toolbar->addWidget(saveButton);
    toolbar->addWidget(saveAllButton);
    toolbar->addWidget(copyLinkButton);
    toolbar->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(toolbar);
    layout->addWidget(view, 1);

    networkManager = new QNetworkAccessManager(this);

    galleryView->keyHandler = [this](int key) { handleKey(key); };
    galleryView->zoomHandler = [this](qreal factor) { zoomBy(factor); };

    connect(prevButton, &QPushButton::clicked, this, [this]() { showIndex(currentIndex - 1); });
    connect(nextButton, &QPushButton::clicked, this, [this]() { showIndex(currentIndex + 1); });
    connect(zoomInButton, &QPushButton::clicked, this, [this]() { zoomBy(1.25); });
    connect(zoomOutButton, &QPushButton::clicked, this, [this]() { zoomBy(1.0 / 1.25); });
    connect(fitButton, &QPushButton::clicked, this, [this]() { fitToWindow(); });
    connect(saveButton, &QPushButton::clicked, this, [this]() { saveCurrent(); });
    connect(saveAllButton, &QPushButton::clicked, this, [this]() { saveAll(); });
    connect(copyLinkButton, &QPushButton::clicked, this, [this]() { copyLink(); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    if (attachments.isEmpty()) {
        updateNavigationState();
        updateCounter();
        return;
    }

    showIndex(qBound(0, startIndex, attachments.size() - 1));
    view->setFocus();
}

void AttachmentGallery::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    fitToWindow();
}

void AttachmentGallery::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Plus:
    case Qt::Key_Equal:
    case Qt::Key_Minus:
    case Qt::Key_0:
    case Qt::Key_S:
    case Qt::Key_C:
        handleKey(event->key());
        event->accept();
        return;
    default:
        break;
    }
    QDialog::keyPressEvent(event);
}

void AttachmentGallery::handleKey(int key)
{
    switch (key) {
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_Left:
        showIndex(currentIndex - 1);
        break;
    case Qt::Key_Right:
        showIndex(currentIndex + 1);
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomBy(1.25);
        break;
    case Qt::Key_Minus:
        zoomBy(1.0 / 1.25);
        break;
    case Qt::Key_0:
        fitToWindow();
        break;
    case Qt::Key_S:
        saveCurrent();
        break;
    case Qt::Key_C:
        copyLink();
        break;
    default:
        break;
    }
}

void AttachmentGallery::showIndex(int index)
{
    if (attachments.isEmpty())
        return;

    index = qBound(0, index, attachments.size() - 1);
    currentIndex = index;

    const AttachmentData &att = attachments[currentIndex];
    imageItem->setPixmap(att.pixmap.isNull() ? QPixmap() : att.pixmap);
    fetchFullImage(att.proxyUrl);
    fitToWindow();
    updateNavigationState();
    updateCounter();
}

void AttachmentGallery::zoomBy(qreal factor)
{
    const qreal current = view->transform().m11();
    const qreal next = qBound(0.05, current * factor, 20.0);
    view->scale(next / current, next / current);
}

void AttachmentGallery::fitToWindow()
{
    view->resetTransform();
    if (imageItem && !imageItem->pixmap().isNull())
        view->fitInView(imageItem, Qt::KeepAspectRatio);
}

void AttachmentGallery::fetchFullImage(const QUrl &proxyUrl)
{
    if (activeReply) {
        disconnect(activeReply, nullptr, this, nullptr);
        activeReply->abort();
        activeReply->deleteLater();
        activeReply = nullptr;
    }

    if (proxyUrl.isEmpty() || proxyUrl.isLocalFile())
        return;

    QUrl fetchUrl = proxyUrl;
    QUrlQuery query(fetchUrl);
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("webp"));
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("lossless"));
    fetchUrl.setQuery(query);

    const qreal dpr = devicePixelRatioF();
    const QString cacheKey = fetchUrl.toString() + QLatin1Char('@') + QString::number(dpr);

    if (const QPixmap *cached = fullImageCache().object(cacheKey)) {
        imageItem->setPixmap(*cached);
        fitToWindow();
        return;
    }

    QNetworkReply *reply = networkManager->get(QNetworkRequest(fetchUrl));
    activeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, cacheKey]() {
        if (activeReply == reply)
            activeReply = nullptr;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
            return;

        QByteArray data = reply->readAll();
        QPixmap pixmap;
        if (!pixmap.loadFromData(data))
            return;

        pixmap.setDevicePixelRatio(devicePixelRatioF());
        cacheFullImage(cacheKey, pixmap);
        imageItem->setPixmap(pixmap);
        fitToWindow();
    });
}

void AttachmentGallery::saveCurrent()
{
    if (currentIndex < 0 || currentIndex >= attachments.size())
        return;
    saveAttachment(attachments[currentIndex], this);
}

void AttachmentGallery::copyLink()
{
    if (currentIndex < 0 || currentIndex >= attachments.size())
        return;

    const AttachmentData &att = attachments[currentIndex];
    // The CDN URL (originalUrl) is the canonical link to share; fall back to the
    // proxy URL for locally pasted images that never reached the CDN.
    const QUrl link = !att.originalUrl.isEmpty() ? att.originalUrl : att.proxyUrl;
    QGuiApplication::clipboard()->setText(link.toString());
}

void AttachmentGallery::updateNavigationState()
{
    prevButton->setEnabled(currentIndex > 0);
    nextButton->setEnabled(currentIndex < attachments.size() - 1);
}

void AttachmentGallery::updateCounter()
{
    counterLabel->setText(tr("%1 / %2").arg(currentIndex + 1).arg(attachments.size()));
}

void AttachmentGallery::saveAttachment(const AttachmentData &att, QWidget *parent)
{
    QString suggestedName = att.filename.isEmpty()
                                    ? QFileInfo(att.originalUrl.path()).fileName()
                                    : att.filename;
    if (suggestedName.isEmpty())
        suggestedName = tr("attachment");

    QString startDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (startDir.isEmpty())
        startDir = QDir::homePath();

    QString path = QFileDialog::getSaveFileName(parent, tr("Save Attachment"),
                                                startDir + "/" + suggestedName);
    if (path.isEmpty())
        return;

    saveAttachmentTo(att, path, parent);
}

void AttachmentGallery::saveAttachmentTo(const AttachmentData &att, const QString &path,
                                         QWidget *parent)
{
    // Prefer the full-resolution source URL. `att.pixmap` is the downscaled chat
    // thumbnail (capped ~400x300), so it must only be a last resort for images
    // that never had a CDN URL (e.g. pasted previews).
    const QUrl source = !att.originalUrl.isEmpty() ? att.originalUrl : att.proxyUrl;

    if (!source.isEmpty()) {
        if (source.isLocalFile()) {
            QFile::remove(path);
            QFile::copy(source.toLocalFile(), path);
            return;
        }

        auto *network = new QNetworkAccessManager(parent);
        QNetworkReply *reply = network->get(QNetworkRequest(source));
        QObject::connect(reply, &QNetworkReply::finished, network, [reply, network, path]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                network->deleteLater();
                return;
            }
            QByteArray data = reply->readAll();
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(data);
                file.close();
            }
            network->deleteLater();
        });
        return;
    }

    if (att.isImage && !att.pixmap.isNull())
        att.pixmap.save(path, "PNG");
}

void AttachmentGallery::saveAll()
{
    QString startDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (startDir.isEmpty())
        startDir = QDir::homePath();

    const QString dir = QFileDialog::getExistingDirectory(this, tr("Save All Attachments"), startDir);
    if (dir.isEmpty())
        return;

    // Seed with names already present on disk so existing files aren't silently
    // overwritten by the dedup.
    QSet<QString> usedNames;
    const QDir targetDir(dir);
    const QStringList existing = targetDir.entryList(QDir::Files);
    for (const QString &name : existing)
        usedNames.insert(name.toCaseFolded());

    for (const AttachmentData &att : attachments) {
        QString name = att.filename.isEmpty() ? QFileInfo(att.originalUrl.path()).fileName()
                                              : att.filename;
        if (name.isEmpty())
            name = tr("attachment");

        QString base = QFileInfo(name).completeBaseName();
        QString suffix = QFileInfo(name).suffix();
        QString candidate = name;
        int counter = 1;
        while (usedNames.contains(candidate.toCaseFolded())) {
            candidate = base + QStringLiteral(" (%1)").arg(counter++)
                          + (suffix.isEmpty() ? QString() : "." + suffix);
        }
        usedNames.insert(candidate.toCaseFolded());

        saveAttachmentTo(att, targetDir.filePath(candidate), this);
    }
}

} // namespace UI
} // namespace Acheron
