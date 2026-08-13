#include "VoiceStateController.hpp"

#include <QSettings>
#include <QPointer>

#include "MainWindow.hpp"
#include "Core/Session.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/Logging.hpp"
#include "ChannelList/ChannelTreeModel.hpp"
#include "ChannelList/ChannelFilterProxyModel.hpp"
#include "ChannelList/ChannelNode.hpp"
#include "Discord/CdnUrls.hpp"
#ifndef ACHERON_NO_VOICE
#include "Core/AV/VoiceManager.hpp"
#include "Core/AV/PushToTalkListener.hpp"
#include "VoiceStatusBar.hpp"
#endif

using namespace Acheron::Core;

namespace Acheron {
namespace UI {

VoiceStateController::VoiceStateController(MainWindow *window)
    : QObject(window), m_window(window)
{
}

void VoiceStateController::createPushToTalkListener()
{
#ifndef ACHERON_NO_VOICE
    pushToTalkListener = new Core::AV::PushToTalkListener(this);
    pushToTalkListener->setPushToTalkEnabled(QSettings().value("voice/push_to_talk", false).toBool());
#endif
}

void VoiceStateController::connectInstanceVoice(Core::ClientInstance *instance)
{
#ifndef ACHERON_NO_VOICE
    instance->voice()->setPushToTalkEnabled(QSettings().value("voice/push_to_talk", false).toBool());
    connect(pushToTalkListener, &Core::AV::PushToTalkListener::pushToTalkKeyHeld,
            instance->voice(), &Core::AV::VoiceManager::setPushToTalkKeyHeld);

    connect(instance->voice(), &Core::AV::VoiceManager::voiceStateChanged,
            m_window, &MainWindow::updateVoiceStatusLabel);

    connect(instance->voice(), &Core::AV::VoiceManager::channelVoiceMemberChanged,
            this, [this, instance](Core::Snowflake channelId, Core::Snowflake userId, bool joined) {
                int count = instance->voice()->channelVoiceUserCount(channelId);
                m_window->channelTreeModel->updateVoiceCount(channelId, count, instance->accountId());
                m_window->channelTreeModel->updateVoiceParticipant(channelId, userId, joined,
                                                                   instance->accountId());
            });

    connect(instance->voice(), &Core::AV::VoiceManager::participantVoiceStateChanged,
            this, [this, instance](Core::Snowflake channelId, Core::Snowflake userId) {
                m_window->channelTreeModel->updateVoiceParticipantState(channelId, userId,
                                                                        instance->accountId());
            });
#else
    Q_UNUSED(instance);
#endif
}

void VoiceStateController::updateVoiceStatusLabel()
{
#ifndef ACHERON_NO_VOICE
    using VState = Discord::AV::VoiceClient::State;

    // Find any account that is in voice or has an active voice client
    Core::ClientInstance *voiceInstance = nullptr;
    for (const auto &inst : m_window->session->getClients()) {
        if (inst && (inst->isInVoice() || inst->voice()->clientState() != VState::Disconnected)) {
            voiceInstance = inst;
            break;
        }
    }

    Core::AV::VoiceManager *vm = voiceInstance ? voiceInstance->voice() : nullptr;
    m_window->voiceStatusBar->setVoiceManager(vm);

    if (voiceInstance) {
        QPointer<Core::UserManager> um = voiceInstance->users();
        Core::Snowflake vGuildId = voiceInstance->voiceGuildId();
        m_window->voiceStatusBar->setNameResolver([um, vGuildId](Core::Snowflake userId) -> QString {
            if (!um)
                return QString::number(userId);
            return um->getDisplayName(userId, vGuildId);
        });
        m_window->voiceStatusBar->setAvatarResolver([um](Core::Snowflake userId) -> QUrl {
            if (!um)
                return {};
            auto user = um->getUser(userId);
            if (!user || user->avatar.isNull())
                return {};
            return Discord::Cdn::userAvatar(userId, user->avatar.get(), 32);
        });
    } else {
        m_window->voiceStatusBar->setNameResolver(nullptr);
        m_window->voiceStatusBar->setAvatarResolver(nullptr);
    }

    QString channelName;
    if (voiceInstance) {
        Core::Snowflake vcId = voiceInstance->voiceChannelId();
        if (vcId.isValid()) {
            ChannelNode *node = m_window->channelTreeModel->findChannelTreeNode(vcId);
            if (node) {
                bool isDm = (node->type == ChannelNode::Type::DMChannel);
                channelName = isDm ? node->name : ("#" + node->name);
            } else {
                channelName = QString::number(vcId);
            }
        }
    }
    m_window->voiceStatusBar->setChannelName(channelName);
#endif
}

void VoiceStateController::setPushToTalkEnabledForAll(bool enabled)
{
#ifndef ACHERON_NO_VOICE
    if (pushToTalkListener)
        pushToTalkListener->setPushToTalkEnabled(enabled);

    for (const auto &inst : m_window->session->getClients()) {
        if (inst && inst->voice())
            inst->voice()->setPushToTalkEnabled(enabled);
    }

    // Re-enabling PTT while the key is already held must not leave the mic
    // gated: forward the live held state so transmit resumes immediately
    // instead of waiting for the next press/release cycle.
    if (enabled) {
        const bool held = pushToTalkListener ? pushToTalkListener->isHeld() : false;
        for (const auto &inst : m_window->session->getClients()) {
            if (inst && inst->voice())
                inst->voice()->setPushToTalkKeyHeld(held);
        }
    }
#else
    Q_UNUSED(enabled);
#endif
}

void VoiceStateController::setPushToTalkKey(const QString &key)
{
#ifndef ACHERON_NO_VOICE
    if (pushToTalkListener)
        pushToTalkListener->setKey(key);
#else
    Q_UNUSED(key);
#endif
}

void VoiceStateController::disconnectActiveVoice()
{
#ifndef ACHERON_NO_VOICE
    for (const auto &inst : m_window->session->getClients()) {
        if (inst && inst->isInVoice()) {
            inst->discord()->sendVoiceStateUpdate(inst->voiceGuildId(), Snowflake::Invalid, false, false);
            break;
        }
    }
#endif
}

void VoiceStateController::joinVoiceChannel(const QModelIndex &proxyIndex)
{
#ifndef ACHERON_NO_VOICE
    QModelIndex sourceIndex = m_window->channelFilterProxy->mapToSource(proxyIndex);
    auto *node = m_window->channelTreeModel->nodeFromIndex(sourceIndex);
    if (!node)
        return;

    bool isDM = (node->type == ChannelNode::Type::DMChannel);
    bool isVoice = (node->type == ChannelNode::Type::VoiceChannel);
    if (!isDM && !isVoice)
        return;

    ChannelNode *accountNode = m_window->channelTreeModel->getAccountNodeFor(node);
    if (!accountNode)
        return;

    ClientInstance *instance = m_window->session->client(accountNode->id);
    if (!instance)
        return;

    if (isDM) {
        qCInfo(LogVoice) << "Joining DM call" << node->name << node->id;
        instance->discord()->sendVoiceStateUpdate(Snowflake::Invalid, node->id, false, false);
    } else {
        ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);
        if (!guildNode)
            return;

        qCInfo(LogVoice) << "Joining voice channel" << node->name << node->id
                         << "in guild" << guildNode->id;
        instance->discord()->sendVoiceStateUpdate(guildNode->id, node->id, false, false);
    }
#else
    Q_UNUSED(proxyIndex);
#endif
}

void VoiceStateController::disconnectVoiceChannel(const QModelIndex &proxyIndex)
{
#ifndef ACHERON_NO_VOICE
    QModelIndex sourceIndex = m_window->channelFilterProxy->mapToSource(proxyIndex);
    auto *node = m_window->channelTreeModel->nodeFromIndex(sourceIndex);
    if (!node)
        return;

    ChannelNode *accountNode = m_window->channelTreeModel->getAccountNodeFor(node);
    if (!accountNode)
        return;

    ClientInstance *instance = m_window->session->client(accountNode->id);
    if (!instance || !instance->isInVoice())
        return;

    qCInfo(LogVoice) << "Disconnecting from voice";
    instance->discord()->sendVoiceStateUpdate(instance->voiceGuildId(), Snowflake::Invalid, false, false);
#else
    Q_UNUSED(proxyIndex);
#endif
}

} // namespace UI
} // namespace Acheron
