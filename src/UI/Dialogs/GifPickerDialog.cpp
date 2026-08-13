#include "GifPickerDialog.hpp"
#include "Core/AnimationUtils.hpp"
#include "Core/Logging.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QBuffer>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QComboBox>
#include <QSignalBlocker>
#include <QLabel>
#include <QLineEdit>
#include <QMovie>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {
constexpr int kDialogMinWidth = 720;
constexpr int kDialogMinHeight = 520;

/// Detect image format from URL extension or data header.
QByteArray detectImageFormat(const QUrl &url, const QByteArray &data)
{
    QString path = url.path().toLower();
    if (path.endsWith(".webp"))
        return QByteArrayLiteral("webp");
    if (path.endsWith(".png"))
        return QByteArrayLiteral("png");
    if (path.endsWith(".jpg") || path.endsWith(".jpeg"))
        return QByteArrayLiteral("jpeg");
    if (path.endsWith(".mp4"))
        return QByteArray(); // not supported by QMovie
    // GIF is the default — also try to detect from data header
    if (data.size() >= 6 && (data.mid(0, 6) == "GIF87a" || data.mid(0, 6) == "GIF89a"))
        return QByteArrayLiteral("gif");
    return QByteArrayLiteral("gif"); // fallback
}
} // namespace

GifPickerDialog::GifPickerDialog(QWidget *parent)
    : QDialog(parent)
    , m_provider(new Discord::GifProvider(this))
    , m_thumbNam(new QNetworkAccessManager(this))
    , m_searchDebounce(new QTimer(this))
{
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(300);
    m_currentProvider = m_provider->provider();

    setWindowTitle(tr("GIF Picker"));
    setModal(true);
    setMinimumSize(kDialogMinWidth, kDialogMinHeight);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);

    // Search bar
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("gifSearchEdit"));
    m_searchEdit->setClearButtonEnabled(true);
    headerRow->addWidget(m_searchEdit, 1);

    // Provider selector
    m_providerCombo = new QComboBox(this);
    m_providerCombo->setMinimumWidth(140);
    m_providerCombo->setMaximumWidth(180);
    m_providerCombo->addItem(Discord::GifProvider::providerDisplayName(Discord::GifProvider::Provider::Tenor),
                             static_cast<int>(Discord::GifProvider::Provider::Tenor));
    m_providerCombo->addItem(Discord::GifProvider::providerDisplayName(Discord::GifProvider::Provider::Giphy),
                             static_cast<int>(Discord::GifProvider::Provider::Giphy));
    m_providerCombo->addItem(Discord::GifProvider::providerDisplayName(Discord::GifProvider::Provider::Klipy),
                             static_cast<int>(Discord::GifProvider::Provider::Klipy));
    const int providerIndex = m_providerCombo->findData(static_cast<int>(m_currentProvider));
    if (providerIndex >= 0)
        m_providerCombo->setCurrentIndex(providerIndex);
    headerRow->addWidget(m_providerCombo, 0);

    outer->addLayout(headerRow);

    // Category bar (horizontal scroll of category buttons)
    m_categoryScroll = new QScrollArea(this);
    m_categoryScroll->setWidgetResizable(true);
    m_categoryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_categoryScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categoryScroll->setFixedHeight(40);
    m_categoryBar = new QWidget(m_categoryScroll);
    auto *catLayout = new QHBoxLayout(m_categoryBar);
    catLayout->setContentsMargins(0, 0, 0, 0);
    catLayout->setSpacing(4);
    catLayout->addStretch();
    m_categoryScroll->setWidget(m_categoryBar);
    outer->addWidget(m_categoryScroll);

    // API key warning label — shown when no key is configured
    m_apiKeyWarning = new QLabel(this);
    m_apiKeyWarning->setVisible(false);
    m_apiKeyWarning->setStyleSheet(
            QStringLiteral("QLabel { background: #3d1f1f; color: #f04747; padding: 8px; "
                           "border-radius: 4px; font-size: 11px; }"));
    m_apiKeyWarning->setWordWrap(true);
    m_apiKeyWarning->setText(
            tr("GIPHY API key not configured. "
               "Set <code>giphy/apiKey</code> in settings or get a free key at "
               "<a style='color:#00a8fc' href='https://developers.giphy.com/'>"
               "developers.giphy.com</a>"));
    m_apiKeyWarning->setTextFormat(Qt::RichText);
    m_apiKeyWarning->setOpenExternalLinks(true);
    outer->addWidget(m_apiKeyWarning);

    // Scroll area for GIF grid
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_gridContainer = new QWidget(m_scrollArea);
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setContentsMargins(0, 0, 0, 0);
    m_gridLayout->setSpacing(6);
    m_gridLayout->setAlignment(Qt::AlignTop);
    m_scrollArea->setWidget(m_gridContainer);
    outer->addWidget(m_scrollArea, 1);

    // Status / loading label
    m_loadingLabel = new QLabel(tr("Loading..."), this);
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    m_loadingLabel->setStyleSheet(QStringLiteral("color: #b5bac1; padding: 20px;"));
    m_loadingLabel->setVisible(false);
    outer->addWidget(m_loadingLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(
            QStringLiteral("color: #80848e; padding: 4px 8px; font-size: 11px;"));
    outer->addWidget(m_statusLabel);

    // Connect search — debounce to avoid firing a network request on every keystroke
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.trimmed().isEmpty()) {
            m_searchDebounce->stop();
            m_mode = Mode::Trending;
            m_currentQuery.clear();
            clearGrid();
            loadTrending();
        } else {
            m_mode = Mode::Search;
            m_currentQuery = text.trimmed();
            m_offset = 0;
            clearGrid();
            m_searchDebounce->start(); // restart the 300ms timer
        }
    });

    // Actual search fires after the debounce window elapses
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() {
        if (m_mode == Mode::Search && !m_currentQuery.isEmpty()) {
            loadSearch(m_currentQuery, 0);
        }
    });

    // Connect scrollbar for infinite scroll
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &GifPickerDialog::onScrollChanged);

    m_searchEdit->setFocus();

    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { handleProviderChanged(); });

    updateProviderUi();
    if (!Discord::GifProvider::providerNeedsKey(m_currentProvider)
        || !Discord::GifProvider::apiKey(m_currentProvider).isEmpty()) {
        loadCategories();
        loadTrending();
    }
}

void GifPickerDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool GifPickerDialog::eventFilter(QObject *watched, QEvent *event)
{
    auto *btn = qobject_cast<QToolButton *>(watched);
    if (btn && m_hoverUrls.contains(btn)) {
        if (event->type() == QEvent::Enter) {
            const QUrl &url = m_hoverUrls.value(btn);
            if (url.isValid()) {
                startHoverMovie(btn, url);
            }
        } else if (event->type() == QEvent::Leave) {
            stopHoverMovie(btn);
        }
    }
    return QDialog::eventFilter(watched, event);
}

void GifPickerDialog::loadTrending()
{
    if (m_loading)
        return;

    if (Discord::GifProvider::providerNeedsKey(m_currentProvider)
        && Discord::GifProvider::apiKey(m_currentProvider).isEmpty()) {
        updateProviderUi();
        return;
    }

    m_loading = true;
    m_loadingLabel->setVisible(m_nextRow == 0);

    const int generation = ++m_requestGeneration;
    m_provider->setProvider(m_currentProvider);
    m_provider->trending(m_offset,
                          [this, generation](const QList<Discord::GifItem> &items, bool hasMore) {
                              if (generation != m_requestGeneration)
                                  return; // stale response from a cleared grid
                              m_loading = false;
                              m_loadingLabel->setVisible(false);
                              appendGifs(items, hasMore);

                              if (m_nextRow == 0 && items.isEmpty()) {
                                  m_statusLabel->setText(tr("No GIFs found."));
                              }
                          });
}

void GifPickerDialog::loadCategories()
{
    if (Discord::GifProvider::providerNeedsKey(m_currentProvider)
        && Discord::GifProvider::apiKey(m_currentProvider).isEmpty()) {
        m_apiKeyWarning->setVisible(true);
        return;
    }

    m_provider->setProvider(m_currentProvider);
    const int generation = ++m_categoriesGeneration;
    m_provider->categories([this, generation](const QList<Discord::GifCategory> &cats) {
        if (generation != m_categoriesGeneration)
            return; // stale response from a provider switch
        if (cats.isEmpty()) {
            // Categories may not be available; this is non-fatal
            return;
        }
        buildCategoryBar(cats);
    });
}

void GifPickerDialog::loadSearch(const QString &query, int offset)
{
    if (m_loading)
        return;

    if (Discord::GifProvider::providerNeedsKey(m_currentProvider)
        && Discord::GifProvider::apiKey(m_currentProvider).isEmpty()) {
        updateProviderUi();
        return;
    }

    m_loading = true;
    m_loadingLabel->setVisible(m_nextRow == 0);

    const int generation = ++m_requestGeneration;
    m_provider->setProvider(m_currentProvider);
    m_provider->search(query, offset,
                        [this, generation](const QList<Discord::GifItem> &items, bool hasMore) {
                            if (generation != m_requestGeneration)
                                return; // stale response from a cleared grid
                            m_loading = false;
                            m_loadingLabel->setVisible(false);
                            appendGifs(items, hasMore);

                            if (m_nextRow == 0 && items.isEmpty()) {
                                m_statusLabel->setText(tr("No GIFs found."));
                            }
                        });
}

void GifPickerDialog::loadMore()
{
    if (m_loading || !m_hasMore)
        return;

    if (Discord::GifProvider::providerNeedsKey(m_currentProvider)
        && Discord::GifProvider::apiKey(m_currentProvider).isEmpty())
        return;

    m_provider->setProvider(m_currentProvider);
    if (m_mode == Mode::Search)
        loadSearch(m_currentQuery, m_offset);
    else
        loadTrending();
}

void GifPickerDialog::appendGifs(const QList<Discord::GifItem> &items, bool hasMore)
{
    m_hasMore = hasMore;

    for (const auto &item : items) {
        auto *widget = createGifWidget(item);
        if (!widget)
            continue;

        m_gridLayout->addWidget(widget, m_nextRow, m_nextCol);
        Core::AnimationUtils::fadeIn(widget, 180);
        m_nextCol++;
        if (m_nextCol >= kGridColumns) {
            m_nextCol = 0;
            m_nextRow++;
        }
    }

    // Advance the pagination offset so loadMore() fetches the next page
    m_offset = m_provider->nextOffset();

    if (m_statusLabel->text().isEmpty() && !items.isEmpty()) {
        m_statusLabel->clear();
    }
}

void GifPickerDialog::clearGrid()
{
    // Invalidate any in-flight search/trending responses; aborted requests
    // still fire their callbacks, so a generation token discards them.
    ++m_requestGeneration;

    // Cancel all in-flight search/trending requests so stale results
    // don't populate the grid after we've cleared it.
    m_provider->cancelAll();

    // Stop all hover movies
    for (auto *movie : m_hoverMovies) {
        movie->stop();
        movie->deleteLater();
    }
    m_hoverMovies.clear();
    m_hoverUrls.clear();

    // Cancel pending thumbnail requests
    for (auto *reply : m_pendingThumbs) {
        if (reply) {
            reply->abort();
            reply->deleteLater();
        }
    }
    m_pendingThumbs.clear();

    // Remove all widgets from grid
    while (m_gridLayout->count() > 0) {
        QLayoutItem *item = m_gridLayout->takeAt(0);
        if (item) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
    }

    m_nextRow = 0;
    m_nextCol = 0;
    m_offset = 0;
    m_hasMore = false;
    m_loading = false;
    m_statusLabel->clear();

    // Reset scroll position to top so user sees results immediately
    if (m_scrollArea->verticalScrollBar())
        m_scrollArea->verticalScrollBar()->setValue(0);
}

void GifPickerDialog::clearCategoryBar()
{
    auto *layout = qobject_cast<QHBoxLayout *>(m_categoryBar->layout());
    if (!layout)
        return;

    while (layout->count() > 1) {
        QLayoutItem *item = layout->takeAt(0);
        if (item) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
    }
}

void GifPickerDialog::updateProviderUi()
{
    const auto provider = m_currentProvider;
    const QString providerName = Discord::GifProvider::providerDisplayName(provider);
    m_searchEdit->setPlaceholderText(tr("Search %1").arg(providerName));

    if (m_providerCombo) {
        const QSignalBlocker blocker(m_providerCombo);
        const int idx = m_providerCombo->findData(static_cast<int>(provider));
        if (idx >= 0)
            m_providerCombo->setCurrentIndex(idx);
    }

    if (Discord::GifProvider::providerNeedsKey(provider)) {
        const bool configured = !Discord::GifProvider::apiKey(provider).isEmpty();
        m_apiKeyWarning->setVisible(!configured);
        if (!configured) {
            if (provider == Discord::GifProvider::Provider::Giphy) {
                m_apiKeyWarning->setText(
                        tr("GIPHY API key not configured. Set <code>giphy/apiKey</code> in settings or get a free key at <a style='color:#00a8fc' href='https://developers.giphy.com/'>developers.giphy.com</a>"));
            } else {
                m_apiKeyWarning->setText(
                        tr("Klipy API key not configured. Set <code>klipy/apiKey</code> in settings or get a key at <a style='color:#00a8fc' href='https://klipy.com/developers/'>klipy.com/developers</a>"));
            }
        }
    } else {
        m_apiKeyWarning->setVisible(false);
    }
}

void GifPickerDialog::handleProviderChanged()
{
    const int idx = m_providerCombo->currentIndex();
    if (idx < 0)
        return;

    const auto provider = static_cast<Discord::GifProvider::Provider>(
            m_providerCombo->itemData(idx).toInt());
    if (provider == m_currentProvider)
        return;

    m_currentProvider = provider;
    m_provider->setProvider(provider);

    m_searchDebounce->stop();
    m_currentQuery = m_searchEdit->text().trimmed();
    m_mode = m_currentQuery.isEmpty() ? Mode::Trending : Mode::Search;

    clearGrid();
    clearCategoryBar();
    ++m_categoriesGeneration; // invalidate any in-flight categories response
    updateProviderUi();

    if (m_currentQuery.isEmpty()) {
        loadCategories();
        loadTrending();
    } else {
        loadSearch(m_currentQuery, 0);
    }
}

void GifPickerDialog::buildCategoryBar(const QList<Discord::GifCategory> &cats)
{
    auto *layout = qobject_cast<QHBoxLayout *>(m_categoryBar->layout());
    if (!layout)
        return;

    // Remove existing category buttons (keep the trailing stretch)
    while (layout->count() > 1) {
        QLayoutItem *item = layout->takeAt(0);
        if (item) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
    }

    // Add "Trending" button at the start
    auto *trendingBtn = new QPushButton(tr("Trending"), m_categoryBar);
    trendingBtn->setStyleSheet(
            QStringLiteral(
                    "QPushButton { background: #4e5058; color: #dbdee1; border: none; "
                    "padding: 4px 12px; border-radius: 4px; font-size: 12px; }"
                    "QPushButton:hover { background: #6d6f78; }"));
    layout->insertWidget(0, trendingBtn);
    connect(trendingBtn, &QPushButton::clicked, this, [this]() {
        m_searchDebounce->stop();
        m_searchEdit->clear();
        m_mode = Mode::Trending;
        m_currentQuery.clear();
        clearGrid();
        loadTrending();
    });

    for (const auto &cat : cats) {
        auto *btn = new QPushButton(cat.name, m_categoryBar);
        btn->setStyleSheet(
                QStringLiteral(
                        "QPushButton { background: #2b2d31; color: #dbdee1; border: none; "
                        "padding: 4px 12px; border-radius: 4px; font-size: 12px; }"
                        "QPushButton:hover { background: #4e5058; }"));
        layout->insertWidget(layout->count() - 1, btn);

        // Capture button name for safe category loading
        const QString categoryTerm = cat.searchTerm;
        const QPointer<QWidget> categoryBarGuard(m_categoryBar);
        connect(btn, &QPushButton::clicked, this, [this, categoryTerm, categoryBarGuard]() {
            if (!categoryBarGuard)
                return; // dialog was destroyed
            m_searchDebounce->stop();
            m_searchEdit->clear();
            m_mode = Mode::Search;
            m_currentQuery = categoryTerm;
            m_offset = 0;
            clearGrid();
            loadSearch(categoryTerm, 0);
        });
    }
}

void GifPickerDialog::onGifWidgetClicked(const Discord::GifItem &item)
{
    selectGif(item);
}

void GifPickerDialog::selectGif(const Discord::GifItem &item)
{
    m_selected = item;
    m_provider->recordSelection(item, m_currentQuery);
    emit gifSelected(item);
    accept();
}

void GifPickerDialog::onScrollChanged(int value)
{
    auto *bar = m_scrollArea->verticalScrollBar();
    if (value >= bar->maximum() - kGridCellHeight * 2) {
        loadMore();
    }
}

QWidget *GifPickerDialog::createGifWidget(const Discord::GifItem &item)
{
    auto *container = new QToolButton(m_gridContainer);
    container->setFixedSize(kGridCellWidth, kGridCellHeight);
    container->setCheckable(false);
    container->setAutoRaise(true);
    container->setToolButtonStyle(Qt::ToolButtonIconOnly);
    container->setStyleSheet(
            QStringLiteral("QToolButton { border: 1px solid #3f4147; border-radius: 6px; "
                           "background: #2b2d31; }"
                           "QToolButton:hover { border-color: #5865f2; }"));

    container->setToolTip(item.contentDescription.isEmpty()
                                  ? item.title.isEmpty() ? item.url : item.title
                                  : item.contentDescription);

    // Choose the best thumbnail URL
    QUrl thumbUrl = item.preview.url;
    if (!thumbUrl.isValid())
        thumbUrl = item.tinygif.url;
    if (!thumbUrl.isValid())
        thumbUrl = item.full.url;
    if (!thumbUrl.isValid())
        return nullptr;

    // Set initial placeholder
    QPixmap placeholder(kGridCellWidth - 2, kGridCellHeight - 2);
    placeholder.fill(QColor(43, 45, 49));
    container->setIcon(QIcon(placeholder));
    container->setIconSize(QSize(kGridCellWidth - 2, kGridCellHeight - 2));

    // Fetch thumbnail and set as icon
    fetchThumbnail(container, thumbUrl);

    // Store the animated preview URL for hover-to-play
    QUrl hoverUrl = item.preview.url;
    if (!hoverUrl.isValid())
        hoverUrl = item.full.url;
    if (hoverUrl.isValid()) {
        m_hoverUrls[container] = hoverUrl;
        container->installEventFilter(this);
    }

    connect(container, &QToolButton::clicked, this, [this, item]() {
        onGifWidgetClicked(item);
    });

    return container;
}

void GifPickerDialog::fetchThumbnail(QToolButton *btn, const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Acheron/1.0"));
    QNetworkReply *reply = m_thumbNam->get(request);
    m_pendingThumbs.insert(reply);

    QPointer<QToolButton> guard(btn);
    connect(reply, &QNetworkReply::finished, this, [this, guard, reply]() {
        m_pendingThumbs.remove(reply);
        reply->deleteLater();

        if (!guard || reply->error() != QNetworkReply::NoError)
            return;

        QByteArray data = reply->readAll();
        QPixmap pix;
        if (!pix.loadFromData(data))
            return;

        // Scale to fit cell
        QSize target(kGridCellWidth - 2, kGridCellHeight - 2);
        pix = pix.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        // Composite to center-aligned icon
        QPixmap containerPixmap(target);
        containerPixmap.fill(Qt::transparent);
        QPainter p(&containerPixmap);
        int x = (target.width() - pix.width()) / 2;
        int y = (target.height() - pix.height()) / 2;
        p.drawPixmap(x, y, pix);
        p.end();

        guard->setIcon(QIcon(containerPixmap));
        guard->setProperty("thumbLoaded", true);

        // Fade in the thumbnail, then remove effect to avoid offscreen rendering cost
        auto *effect = new QGraphicsOpacityEffect(guard);
        guard->setGraphicsEffect(effect);
        effect->setOpacity(0.01);
        auto *anim = new QPropertyAnimation(effect, "opacity", guard);
        anim->setDuration(180);
        anim->setStartValue(0.01);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(anim, &QPropertyAnimation::finished, guard.data(), [guard]() {
            if (guard)
                guard->setGraphicsEffect(nullptr);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

void GifPickerDialog::startHoverMovie(QToolButton *btn, const QUrl &url)
{
    if (m_hoverMovies.contains(btn))
        return;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Acheron/1.0"));
    QNetworkReply *reply = m_thumbNam->get(request);
    m_pendingThumbs.insert(reply);

    QPointer<QToolButton> guard(btn);
    connect(reply, &QNetworkReply::finished, this, [this, guard, reply, url]() {
        m_pendingThumbs.remove(reply);
        reply->deleteLater();

        if (!guard || reply->error() != QNetworkReply::NoError)
            return;

        // Another request already set a movie for this button (rapid enter/leave/enter)
        if (m_hoverMovies.contains(guard.data()))
            return;

        // The cursor may have left the button while the fetch was in flight;
        // don't start an animation for a cell that is no longer hovered.
        if (!guard->underMouse())
            return;

        QByteArray data = reply->readAll();

        auto *buffer = new QBuffer(guard.data());
        buffer->setData(data);
        buffer->open(QIODevice::ReadOnly);

        auto *movie = new QMovie(guard);
        movie->setDevice(buffer);

        // Detect actual format from URL extension and fall back to data header detection
        QByteArray fmt = detectImageFormat(url, data);
        if (fmt.isEmpty()) {
            // If format detection fails, try GIF as default
            fmt = QByteArrayLiteral("gif");
        }
        movie->setFormat(fmt);
        buffer->setParent(movie);

        QSize target(kGridCellWidth - 2, kGridCellHeight - 2);
        movie->setScaledSize(target);

        if (!movie->isValid()) {
            delete movie;
            return;
        }

        m_hoverMovies[guard.data()] = movie;
        movie->start();

        connect(movie, &QMovie::frameChanged, guard.data(), [this, guard, movie, target]() {
            if (!guard || !m_hoverMovies.contains(guard.data()))
                return;

            QImage frame = movie->currentImage();
            if (frame.isNull())
                return;

            QPixmap pixmap = QPixmap::fromImage(frame);
            // Composite centered
            QPixmap containerPixmap(target);
            containerPixmap.fill(Qt::transparent);
            QPainter p(&containerPixmap);
            int x = (target.width() - pixmap.width()) / 2;
            int y = (target.height() - pixmap.height()) / 2;
            p.drawPixmap(x, y, pixmap);
            p.end();

            guard->setIcon(QIcon(containerPixmap));
            guard->setIconSize(target);
        });
    });
}

void GifPickerDialog::stopHoverMovie(QToolButton *btn)
{
    auto it = m_hoverMovies.find(btn);
    if (it != m_hoverMovies.end()) {
        it.value()->stop();
        it.value()->deleteLater();
        m_hoverMovies.erase(it);
    }
}

} // namespace UI
} // namespace Acheron
