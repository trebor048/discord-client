#include "StickerPickerDialog.hpp"

#include "StickerPreferences.hpp"
#include "Core/AnimationUtils.hpp"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Discord/CdnUrls.hpp"

namespace Acheron {
namespace UI {

StickerPickerDialog::StickerPickerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Sticker Picker"));
    setMinimumSize(420, 360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText(tr("Search stickers..."));
    searchEdit->setClearButtonEnabled(true);
    layout->addWidget(searchEdit);

    packTabs = new QTabWidget(this);
    packTabs->setTabPosition(QTabWidget::North);
    layout->addWidget(packTabs, 1);

    allTab = new QWidget(this);
    allTabLayout = new QVBoxLayout(allTab);
    allTabLayout->setContentsMargins(0, 0, 0, 0);
    allTabLayout->setSpacing(0);

    scrollArea = new QScrollArea(allTab);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    allTabLayout->addWidget(scrollArea);

    packTabs->addTab(allTab, tr("All"));

    nam = new QNetworkAccessManager(this);

    searchDebounce = new QTimer(this);
    searchDebounce->setSingleShot(true);
    searchDebounce->setInterval(200);
    connect(searchDebounce, &QTimer::timeout, this, &StickerPickerDialog::rebuildGrid);
    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        searchDebounce->start();
    });
    installEventFilter(this);
}

StickerPickerDialog::~StickerPickerDialog()
{
    // Clean up all shared movies to prevent dangling QMovie pointers
    for (auto it = hoveredMovies.begin(); it != hoveredMovies.end(); ++it) {
        QMovie *movie = it.value();
        disconnect(movie, &QMovie::frameChanged, it.key(), nullptr);
        movie->stop();
    }
    hoveredMovies.clear();

    for (auto it = sharedMovies.begin(); it != sharedMovies.end(); ++it) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    sharedMovies.clear();
}

void StickerPickerDialog::setStickerPacks(const QList<StickerPackGroup> &stickerPacks)
{
    packs = stickerPacks;

    allEntries.clear();
    for (const auto &pack : packs) {
        for (const auto &sticker : pack.stickers) {
            StickerGridEntry entry;
            entry.stickerId = sticker.id;
            entry.name = sticker.name.get();
            entry.formatType = sticker.formatType.get();
            entry.guildId = pack.guildId;
            entry.guildName = pack.guildName;
            if (entry.formatType != Discord::StickerFormatType::Lottie) {
                entry.cdnUrl = Discord::Cdn::stickerImage(entry.stickerId, entry.formatType,
                                                           kStickerPreviewSize);
            }
            allEntries.append(entry);
        }
    }

    // Order matters: buildPackTabs() aborts pendingRequests, so the All tab's
    // thumbnail requests must be started AFTER it (otherwise the first batch is
    // cancelled before it ever renders).
    buildPackTabs();
    buildAllTab();
    buildRecentsTab();
}

void StickerPickerDialog::setGuildIconProvider(
        std::function<QUrl(Core::Snowflake, const QString &)> provider)
{
    guildIconProvider = std::move(provider);
}

void StickerPickerDialog::buildAllTab()
{
    // Same hover/movie cleanup as rebuildGrid(): scrollArea->setWidget()
    // deletes the previous All-tab widget synchronously, so entries keyed by
    // its buttons must be purged first and the old widget taken + deferred —
    // otherwise a hovered sticker leaves a dangling QToolButton* in
    // hoveredMovies and an orphaned, still-decoding QMovie.
    for (auto it = hoveredMovies.begin(); it != hoveredMovies.end(); ++it) {
        QMovie *movie = it.value();
        disconnect(movie, &QMovie::frameChanged, it.key(), nullptr);
        movie->stop();
    }
    hoveredMovies.clear();
    for (auto it = sharedMovies.begin(); it != sharedMovies.end(); ++it) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    sharedMovies.clear();

    auto *oldWidget = scrollArea->takeWidget();
    if (oldWidget)
        oldWidget->deleteLater();

    auto *container = buildStickerGrid(packs);
    scrollArea->setWidget(container);
}

void StickerPickerDialog::buildPackTabs()
{
    // Cancel any in-flight requests from previous pack tabs
    for (auto *reply : pendingRequests) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    pendingRequests.clear();

    // Purge hover animations whose buttons live in the old pack tabs.
    // hoveredMovies keys shared QMovie pointers by raw QToolButton*, so these
    // entries must be dropped before the tabs (and their buttons) are deleted,
    // otherwise stopHoverAnimation()/the destructor disconnect() on a dangling
    // receiver.
    for (auto it = hoveredMovies.begin(); it != hoveredMovies.end();) {
        QToolButton *button = it.key();
        bool inOldTab = false;
        for (int i = 1; i < packTabs->count(); ++i) {
            if (packTabs->widget(i)->isAncestorOf(button)) {
                inOldTab = true;
                break;
            }
        }
        if (!inOldTab) {
            ++it;
            continue;
        }

        QMovie *movie = it.value();
        disconnect(movie, &QMovie::frameChanged, button, nullptr);
        it = hoveredMovies.erase(it);

        // Mirror stopHoverAnimation(): release the shared movie when no
        // remaining button references it.
        bool hasOtherUsers = false;
        for (auto other = hoveredMovies.constBegin(); other != hoveredMovies.constEnd(); ++other) {
            if (other.value() == movie) {
                hasOtherUsers = true;
                break;
            }
        }
        if (!hasOtherUsers) {
            movie->stop();
            QString movieKey;
            for (auto sharedIt = sharedMovies.begin(); sharedIt != sharedMovies.end(); ++sharedIt) {
                if (sharedIt.value() == movie) {
                    movieKey = sharedIt.key();
                    break;
                }
            }
            if (!movieKey.isEmpty())
                sharedMovies.remove(movieKey);
            movie->deleteLater();
        }
    }

    // Remove and delete old pack tabs (keep "All" at index 0)
    while (packTabs->count() > 1) {
        QWidget *tab = packTabs->widget(1);
        packTabs->removeTab(1);
        tab->deleteLater();
    }

    for (const auto &pack : packs) {
        auto *tab = new QWidget(this);
        buildPackTab(tab, pack);
        QString tabLabel = pack.guildName.isEmpty() ? tr("Server") : pack.guildName;
        packTabs->addTab(tab, tabLabel);
    }
}

void StickerPickerDialog::buildPackTab(QWidget *tab, const StickerPackGroup &group)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);

    // Pack header with guild icon
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(8, 4, 8, 4);

    if (guildIconProvider && !group.guildIconHash.isEmpty()) {
        auto *iconLabel = new QLabel(tab);
        iconLabel->setFixedSize(20, 20);
        QUrl iconUrl = guildIconProvider(group.guildId, group.guildIconHash);
        if (!iconUrl.isEmpty()) {
            QNetworkReply *reply = nam->get(QNetworkRequest(iconUrl));
            pendingRequests.insert(reply);
            QPointer<QLabel> iconGuard(iconLabel);
            connect(reply, &QNetworkReply::finished, this, [iconGuard, reply, this]() {
                pendingRequests.remove(reply);
                reply->deleteLater();
                if (!iconGuard || reply->error() != QNetworkReply::NoError)
                    return;
                QPixmap pix;
                pix.loadFromData(reply->readAll());
                iconGuard->setPixmap(pix.scaled(20, 20, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
            });
        }
        headerLayout->addWidget(iconLabel);
    }

    auto *nameLabel = new QLabel(group.guildName.isEmpty() ? tr("Server") : group.guildName, tab);
    QFont boldFont = nameLabel->font();
    boldFont.setBold(true);
    nameLabel->setFont(boldFont);
    headerLayout->addWidget(nameLabel);
    headerLayout->addStretch();

    layout->addLayout(headerLayout);

    QList<StickerPackGroup> singlePack;
    singlePack.append(group);

    auto *scroll = new QScrollArea(tab);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *gridWidget = buildStickerGrid(singlePack);
    scroll->setWidget(gridWidget);
    layout->addWidget(scroll, 1);
    Acheron::Core::AnimationUtils::fadeIn(tab, 200);
}

void StickerPickerDialog::buildRecentsTab()
{
    const QStringList recents = StickerPreferences::recents();
    if (recents.isEmpty())
        return;

    StickerPackGroup recentsGroup;
    recentsGroup.guildId = Core::Snowflake::Invalid;
    recentsGroup.guildName = tr("Recently Used");

    for (const QString &idStr : recents) {
        bool ok = false;
        const quint64 raw = idStr.toULongLong(&ok);
        if (!ok || raw == 0)
            continue;
        const Core::Snowflake id(raw);
        bool found = false;
        for (const auto &pack : packs) {
            for (const auto &sticker : pack.stickers) {
                if (sticker.id.get() == id) {
                    recentsGroup.stickers.append(sticker);
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
    }

    if (recentsGroup.stickers.isEmpty())
        return;

    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(tab);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(buildStickerGrid({ recentsGroup }));
    layout->addWidget(scroll, 1);

    // Insert right after the "All" tab for visibility.
    packTabs->insertTab(1, tab, tr("Recents"));
}

QWidget *StickerPickerDialog::buildStickerGrid(const QList<StickerPackGroup> &gridPacks)
{
    auto *container = new QWidget();
    auto *gridLayout = new QGridLayout(container);
    gridLayout->setContentsMargins(8, 8, 8, 8);
    gridLayout->setSpacing(8);

    int row = 0;
    int col = 0;

    // Collect thumbnails to load lazily (batch via timer)
    struct PendingThumb {
        QPointer<QToolButton> button;
        StickerGridEntry entry;
    };
    QList<PendingThumb> pendingThumbs;

    for (const auto &pack : gridPacks) {
        for (const auto &sticker : pack.stickers) {
            auto *button = new QToolButton(container);
            button->setFixedSize(kStickerPreviewSize + 8, kStickerPreviewSize + 8);
            button->setToolTip(sticker.name.get());
            button->setAutoRaise(true);

            StickerGridEntry entry;
            entry.stickerId = sticker.id;
            entry.name = sticker.name.get();
            entry.formatType = sticker.formatType.get();
            entry.guildId = pack.guildId;
            entry.guildName = pack.guildName;
            entry.button = button;

            if (sticker.formatType.get() == Discord::StickerFormatType::Lottie) {
                // Lottie stickers: show a static placeholder instead of silently skipping
                button->setText(QStringLiteral("\u25B6")); // play triangle
                button->setToolTip(entry.name + QStringLiteral(" (Lottie animation)"));
                entry.cdnUrl = QUrl();
            } else {
                entry.cdnUrl = Discord::Cdn::stickerImage(entry.stickerId, entry.formatType,
                                                          kStickerPreviewSize);
                pendingThumbs.append({QPointer<QToolButton>(button), entry});
            }

            // Hover event for animated preview
            button->installEventFilter(this);
            button->setProperty("stickerId", QVariant::fromValue(entry.stickerId));

            connect(button, &QToolButton::clicked, this,
                    [this, sid = entry.stickerId]() { onStickerClicked(sid); });

            gridLayout->addWidget(button, row, col);

            col++;
            if (col >= kStickerGridColumns) {
                col = 0;
                row++;
            }
        }
    }

    // Load first visible batch immediately, defer rest via timer
    const int immediateCount = qMin(pendingThumbs.size(), 12);
    for (int i = 0; i < immediateCount; ++i) {
        const auto &pt = pendingThumbs[i];
        if (pt.button && !pt.entry.cdnUrl.isEmpty())
            loadStickerThumbnail(pt.button, pt.entry, kStickerPreviewSize);
    }
    if (pendingThumbs.size() > immediateCount) {
        auto *deferTimer = new QTimer(container);
        deferTimer->setSingleShot(false);
        deferTimer->setInterval(50);
        struct DeferState { int index; };
        auto *deferState = new DeferState{immediateCount};
        QObject::connect(deferTimer, &QObject::destroyed,
                         [deferState]() { delete deferState; });
        connect(deferTimer, &QTimer::timeout, container,
                [deferTimer, pendingThumbs, deferState, this]() {
                    constexpr int kBatchSize = 6;
                    for (int b = 0; b < kBatchSize && deferState->index < pendingThumbs.size(); ++b, ++deferState->index) {
                        const auto &pt = pendingThumbs[deferState->index];
                        if (pt.button && !pt.entry.cdnUrl.isEmpty())
                            loadStickerThumbnail(pt.button, pt.entry, kStickerPreviewSize);
                    }
                    if (deferState->index >= pendingThumbs.size())
                        deferTimer->stop();
                });
        deferTimer->start();
    }

    if (row == 0 && col == 0) {
        auto *emptyLabel = new QLabel(tr("No stickers available"), container);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("color: palette(placeholder-text); padding: 40px;");
        gridLayout->addWidget(emptyLabel, 0, 0, 1, kStickerGridColumns);
    }

    gridLayout->setRowStretch(gridLayout->rowCount(), 1);
    return container;
}

void StickerPickerDialog::loadStickerThumbnail(QToolButton *button,
                                               const StickerGridEntry &entry, int size)
{
    QString cacheKey = QStringLiteral("sticker_picker_%1_%2")
                               .arg(QString::number(static_cast<quint64>(entry.stickerId)))
                               .arg(size);

    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        button->setIcon(QIcon(cached));
        button->setIconSize(QSize(size, size));
        return;
    }

    // Set placeholder
    button->setIcon(QIcon());
    button->setText(QStringLiteral("..."));

    if (!nam)
        return;

    QPointer<QToolButton> btn = button;
    QNetworkReply *reply = nam->get(QNetworkRequest(entry.cdnUrl));
    pendingRequests.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [btn, reply, cacheKey, size, this]() {
        reply->deleteLater();
        pendingRequests.remove(reply);

        if (!btn)
            return; // button was destroyed (e.g. by rebuildGrid)

        if (reply->error() != QNetworkReply::NoError)
            return;

        QByteArray data = reply->readAll();

        QPixmap pix;
        if (pix.loadFromData(data)) {
            QPixmap scaled = pix.scaled(size, size, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
            QPixmapCache::insert(cacheKey, scaled);
            btn->setIcon(QIcon(scaled));
            btn->setIconSize(QSize(size, size));
            btn->setText(QString());
        }
    });
}

void StickerPickerDialog::startHoverAnimation(QToolButton *button,
                                              const StickerGridEntry &entry)
{
    // Only for animated stickers
    if (entry.formatType != Discord::StickerFormatType::APNG &&
        entry.formatType != Discord::StickerFormatType::GIF) {
        return;
    }

    // Check if we already have a movie for this button
    if (hoveredMovies.contains(button))
        return;

    // Check if we already have shared movie data
    QString movieKey = QString::number(static_cast<quint64>(entry.stickerId));
    auto sharedIt = sharedMovies.find(movieKey);

    if (sharedIt != sharedMovies.end()) {
        QMovie *movie = sharedIt.value();
        QPointer<QToolButton> btn = button;
        connect(movie, &QMovie::frameChanged, button,
                [btn, movie](int) {
                    if (!btn)
                        return;
                    btn->setIcon(QIcon(movie->currentPixmap()));
                    btn->setIconSize(QSize(kStickerPreviewSize, kStickerPreviewSize));
                });
        movie->start();
        hoveredMovies.insert(button, movie);
        return;
    }

    // Need to fetch sticker data and create a movie
    if (!nam)
        return;

    QPointer<QToolButton> btn = button;
    QNetworkReply *reply = nam->get(QNetworkRequest(entry.cdnUrl));
    pendingRequests.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, btn, reply, movieKey, entry]() {
        reply->deleteLater();
        pendingRequests.remove(reply);

        if (!btn)
            return; // button was destroyed while request was in flight

        if (reply->error() != QNetworkReply::NoError)
            return;

        // Another request already set a movie for this button (rapid enter/leave/enter)
        if (hoveredMovies.contains(btn))
            return;

        QByteArray data = reply->readAll();

        // Create a shared movie
        auto *movie = new QMovie(this);
        movie->setCacheMode(QMovie::CacheAll);
        QBuffer *buffer = new QBuffer(movie);
        buffer->setData(data);
        buffer->open(QIODevice::ReadOnly);
        movie->setDevice(buffer);

        if (!movie->isValid() || movie->frameCount() <= 1) {
            movie->deleteLater();
            return;
        }

        sharedMovies.insert(movieKey, movie);

        connect(movie, &QMovie::frameChanged, btn,
                [btn, movie](int) {
                    if (!btn)
                        return;
                    btn->setIcon(QIcon(movie->currentPixmap()));
                    btn->setIconSize(QSize(kStickerPreviewSize, kStickerPreviewSize));
                });

        movie->start();
        hoveredMovies.insert(btn, movie);
    });
}

void StickerPickerDialog::stopHoverAnimation(QToolButton *button)
{
    auto it = hoveredMovies.find(button);
    if (it != hoveredMovies.end()) {
        QMovie *movie = *it;
        // Disconnect the frameChanged for this button
        disconnect(movie, &QMovie::frameChanged, button, nullptr);
        hoveredMovies.erase(it);

        // Count how many buttons still reference this shared movie
        bool hasOtherUsers = false;
        for (auto otherIt = hoveredMovies.begin(); otherIt != hoveredMovies.end(); ++otherIt) {
            if (otherIt.value() == movie) {
                hasOtherUsers = true;
                break;
            }
        }

        if (!hasOtherUsers) {
            // No more buttons using this movie — stop, delete, and remove from shared pool
            movie->stop();
            QString movieKey;
            for (auto sharedIt = sharedMovies.begin(); sharedIt != sharedMovies.end(); ++sharedIt) {
                if (sharedIt.value() == movie) {
                    movieKey = sharedIt.key();
                    break;
                }
            }
            if (!movieKey.isEmpty())
                sharedMovies.remove(movieKey);
            movie->deleteLater();
        }
    }
}

QList<StickerPickerDialog::StickerGridEntry> StickerPickerDialog::filterEntries(
        const QString &query) const
{
    QList<StickerGridEntry> results;
    if (query.isEmpty()) {
        results = allEntries;
    } else {
        for (const auto &entry : allEntries) {
            if (entry.name.contains(query, Qt::CaseInsensitive) ||
                entry.guildName.contains(query, Qt::CaseInsensitive)) {
                results.append(entry);
            }
        }
    }
    return results;
}

void StickerPickerDialog::rebuildGrid()
{
    // We rebuild the All tab with filtered results
    QString query = searchEdit->text().trimmed();

    auto *container = new QWidget();
    auto *gridLayout = new QGridLayout(container);
    gridLayout->setContentsMargins(8, 8, 8, 8);
    gridLayout->setSpacing(8);

    QList<StickerGridEntry> entries = filterEntries(query);

    // Collect thumbnails to load lazily
    struct PendingThumb {
        QPointer<QToolButton> button;
        StickerGridEntry entry;
    };
    QList<PendingThumb> pendingThumbs;

    int row = 0;
    int col = 0;
    bool hadResults = false;

    for (const auto &entry : entries) {
        hadResults = true;
        auto *button = new QToolButton(container);
        button->setFixedSize(kStickerPreviewSize + 8, kStickerPreviewSize + 8);
        button->setToolTip(entry.name);
        button->setAutoRaise(true);
        button->setProperty("stickerId", QVariant::fromValue(entry.stickerId));
        button->installEventFilter(this);

        if (entry.formatType == Discord::StickerFormatType::Lottie) {
            button->setText(QStringLiteral("\u25B6"));
            button->setToolTip(entry.name + QStringLiteral(" (Lottie animation)"));
        } else {
            pendingThumbs.append({QPointer<QToolButton>(button), entry});
        }

        connect(button, &QToolButton::clicked, this,
                [this, sid = entry.stickerId]() { onStickerClicked(sid); });

        gridLayout->addWidget(button, row, col);

        col++;
        if (col >= kStickerGridColumns) {
            col = 0;
            row++;
        }
    }

    // Batch-load visible thumbnails lazily
    const int immediateCount = qMin(pendingThumbs.size(), 12);
    for (int i = 0; i < immediateCount; ++i) {
        const auto &pt = pendingThumbs[i];
        if (pt.button && !pt.entry.cdnUrl.isEmpty())
            loadStickerThumbnail(pt.button, pt.entry, kStickerPreviewSize);
    }
    if (pendingThumbs.size() > immediateCount) {
        auto *deferTimer = new QTimer(container);
        deferTimer->setSingleShot(false);
        deferTimer->setInterval(50);
        struct DeferState { int index; };
        auto *deferState = new DeferState{immediateCount};
        QObject::connect(deferTimer, &QObject::destroyed,
                         [deferState]() { delete deferState; });
        connect(deferTimer, &QTimer::timeout, container,
                [deferTimer, pendingThumbs, deferState, this]() {
                    constexpr int kBatchSize = 6;
                    for (int b = 0; b < kBatchSize && deferState->index < pendingThumbs.size(); ++b, ++deferState->index) {
                        const auto &pt = pendingThumbs[deferState->index];
                        if (pt.button && !pt.entry.cdnUrl.isEmpty())
                            loadStickerThumbnail(pt.button, pt.entry, kStickerPreviewSize);
                    }
                    if (deferState->index >= pendingThumbs.size())
                        deferTimer->stop();
                });
        deferTimer->start();
    }

    if (!hadResults) {
        auto *emptyLabel = new QLabel(tr("No sticker found"), container);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("color: palette(placeholder-text); padding: 40px;");
        gridLayout->addWidget(emptyLabel, 0, 0, 1, kStickerGridColumns);
    }

    gridLayout->setRowStretch(gridLayout->rowCount(), 1);

    // Clean up hover state before replacing the old widget
    for (auto it = hoveredMovies.begin(); it != hoveredMovies.end(); ++it) {
        QMovie *movie = it.value();
        disconnect(movie, &QMovie::frameChanged, it.key(), nullptr);
        movie->stop();
    }
    hoveredMovies.clear();
    for (auto it = sharedMovies.begin(); it != sharedMovies.end(); ++it) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    sharedMovies.clear();

    // Replace the scroll area widget
    auto *oldWidget = scrollArea->takeWidget();
    if (oldWidget)
        oldWidget->deleteLater();
    scrollArea->setWidget(container);

    // Fade in the new container (LOW #15)
    Core::AnimationUtils::fadeIn(container, 200);
}

void StickerPickerDialog::onSearchChanged(const QString &query)
{
    rebuildGrid();
}

void StickerPickerDialog::onStickerClicked(Core::Snowflake stickerId)
{
    currentStickerId = stickerId;
    StickerPreferences::addRecent(QString::number(static_cast<quint64>(stickerId)));
    emit stickerSelected(stickerId);
    accept();
}

void StickerPickerDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (currentStickerId.isValid()) {
            accept();
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

bool StickerPickerDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Enter) {
        auto *button = qobject_cast<QToolButton *>(watched);
        if (button) {
            auto sidVar = button->property("stickerId");
            if (sidVar.isValid()) {
                Core::Snowflake sid = sidVar.value<Core::Snowflake>();
                // Find the entry
                for (const auto &entry : allEntries) {
                    if (entry.stickerId == sid) {
                        startHoverAnimation(button, entry);
                        break;
                    }
                }
            }
        }
    } else if (event->type() == QEvent::Leave) {
        auto *button = qobject_cast<QToolButton *>(watched);
        if (button) {
            stopHoverAnimation(button);
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace UI
} // namespace Acheron
