#include "ThreadBrowserPopup.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Theme/Icons.hpp"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

using ThreadListPage = Core::ClientInstance::ThreadListPage;

ThreadBrowserPopup::ThreadBrowserPopup(QWidget *parent) : BasePopup(parent)
{
    QFrame *c = getContainer();
    c->setMinimumWidth(440);

    auto *layout = new QVBoxLayout(c);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    titleLabel = new QLabel(tr("Threads"), c);
    {
        QFont f = titleLabel->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 2);
        titleLabel->setFont(f);
    }
    layout->addWidget(titleLabel);

    list = new QListWidget(c);
    list->setMinimumHeight(360);
    list->setIconSize(QSize(16, 16));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setUniformItemSizes(false);
    connect(list, &QListWidget::itemClicked, this, &ThreadBrowserPopup::onItemClicked);
    layout->addWidget(list, 1);

    loadMoreButton = new QPushButton(tr("Load more"), c);
    loadMoreButton->setCursor(Qt::PointingHandCursor);
    loadMoreButton->hide();
    connect(loadMoreButton, &QPushButton::clicked, this, &ThreadBrowserPopup::loadArchived);
    layout->addWidget(loadMoreButton, 0);
}

void ThreadBrowserPopup::configure(Core::ClientInstance *inst, Core::Snowflake chId,
                                   const QString &channelName)
{
    instance = inst;
    channelId = chId;
    titleLabel->setText(channelName.isEmpty() ? tr("Threads")
                                              : tr("Threads — #%1").arg(channelName));
    reload();
}

void ThreadBrowserPopup::reload()
{
    ++loadGeneration;
    list->clear();
    archivedOffset = 0;
    archivedLoaded = false;
    loadMoreButton->hide();
    loadActive();
}

void ThreadBrowserPopup::addSectionHeader(const QString &text)
{
    auto *item = new QListWidgetItem(text, list);
    QFont f = item->font();
    f.setBold(true);
    item->setFont(f);
    item->setFlags(Qt::NoItemFlags);
}

void ThreadBrowserPopup::addNoticeItem(const QString &text)
{
    auto *item = new QListWidgetItem(text, list);
    item->setFlags(Qt::NoItemFlags);
}

void ThreadBrowserPopup::addThreadItem(const Discord::Channel &thread)
{
    QString name = thread.name.hasValue() ? thread.name.get()
                                          : QString::number(static_cast<quint64>(thread.id.get()));
    const bool isPrivate = thread.type.hasValue() && thread.type.get() == Discord::ChannelType::PRIVATE_THREAD;
    auto *item = new QListWidgetItem(name, list);
    item->setIcon(Core::Theme::Icons::icon(isPrivate ? Core::Theme::Icons::Name::Lock
                                                     : Core::Theme::Icons::Name::Spool,
                                           Core::Theme::Token::PrimaryText));
    item->setData(Qt::UserRole, static_cast<qulonglong>(thread.id.get()));
}

void ThreadBrowserPopup::loadActive()
{
    if (!instance)
        return;

    addSectionHeader(tr("Active"));

    QPointer<ThreadBrowserPopup> self(this);
    int gen = loadGeneration;
    instance->fetchThreadList(
            channelId,
            false,
            0,
            [this, self, gen](const Core::Result<ThreadListPage> &res) {
                if (!self || gen != loadGeneration)
                    return;
                if (!res.success())
                    addNoticeItem(tr("Failed to load threads"));
                else if (res.value->threads.isEmpty())
                    addNoticeItem(tr("No active threads"));
                else
                    for (const auto &t : res.value->threads)
                        addThreadItem(t);
                loadArchived();
            });
}

void ThreadBrowserPopup::loadArchived()
{
    if (!instance)
        return;

    if (!archivedLoaded) {
        addSectionHeader(tr("Archived"));
        archivedLoaded = true;
    }
    loadMoreButton->setEnabled(false);

    QPointer<ThreadBrowserPopup> self(this);
    int gen = loadGeneration;
    instance->fetchThreadList(
            channelId,
            true,
            archivedOffset,
            [this, self, gen](const Core::Result<ThreadListPage> &res) {
                if (!self || gen != loadGeneration)
                    return;
                loadMoreButton->setEnabled(true);
                if (!res.success()) {
                    addNoticeItem(tr("Failed to load archived threads"));
                    loadMoreButton->hide();
                    return;
                }
                for (const auto &t : res.value->threads)
                    addThreadItem(t);
                archivedOffset += res.value->threads.size();
                loadMoreButton->setVisible(res.value->hasMore);
            });
}

void ThreadBrowserPopup::onItemClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    QVariant idVar = item->data(Qt::UserRole);
    if (!idVar.isValid())
        return;

    emit threadActivated(Core::Snowflake(idVar.toULongLong()));
    accept();
}

} // namespace UI
} // namespace Acheron
