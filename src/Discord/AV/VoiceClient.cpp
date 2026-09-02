#include "VoiceClient.hpp"
#include "VoiceGateway.hpp"
#include "UdpTransport.hpp"
#include "VoiceEncryption.hpp"
#include "RtpPacket.hpp"

#include "DaveSession.hpp"

#include "Core/AV/IAudioBackend.hpp"
#include "Core/Logging.hpp"

#include <QPointer>
#include <array>
#include <cstring>

namespace Acheron {
namespace Discord {
namespace AV {

// 20ms at 48khz
static constexpr uint32_t OPUS_FRAME_SAMPLES = 960;

static QString formatDisplayableCode(const std::vector<uint8_t> &data, int bytesToConsume = 30, int groupSize = 5)
{
    if (data.empty())
        return {};

    // Clamp to the largest whole group of groupSize bytes that fits instead of
    // bailing when the fingerprint is shorter than requested: MLS pairwise
    // fingerprints are commonly 32 bytes, so a fixed 45-byte request would
    // otherwise render an empty code (the DAVE verification UI shows nothing).
    bytesToConsume = std::min(bytesToConsume, static_cast<int>(data.size()));
    int numGroups = bytesToConsume / groupSize;
    if (numGroups <= 0)
        return {};

    uint64_t modulus = 1;
    for (int i = 0; i < groupSize; i++)
        modulus *= 10;

    QString result;
    for (int g = 0; g < numGroups; g++) {
        uint64_t value = 0;
        for (int b = 0; b < groupSize; b++)
            value = (value << 8) | data[g * groupSize + b];
        value %= modulus;

        if (g > 0)
            result += QLatin1Char(' ');
        result += QStringLiteral("%1").arg(value, groupSize, 10, QLatin1Char('0'));
    }
    return result;
}

VoiceClient::VoiceClient(const QString &endpoint, const QString &token,
                         Core::Snowflake serverId, Core::Snowflake channelId,
                         Core::Snowflake userId, const QString &sessionId,
                         QObject *parent)
    : QObject(parent),
      endpoint(endpoint),
      token(token),
      serverId(serverId),
      channelId(channelId),
      userId(userId),
      sessionId(sessionId)
{
}

VoiceClient::~VoiceClient()
{
    stop();
}

void VoiceClient::seedConnectedUsers(const QList<Core::Snowflake> &userIds)
{
    for (auto id : userIds)
        connectedUserIds.insert(std::to_string(id));
}

void VoiceClient::start()
{
    if (currentState != State::Disconnected) {
        qCWarning(LogVoice) << "VoiceClient::start called in non-disconnected state";
        return;
    }

    VoiceEncryption::initialize();

    setState(State::Connecting);

    gateway = new VoiceGateway(endpoint, serverId, channelId, userId, sessionId, token, this);

    connect(gateway, &VoiceGateway::connected, this, &VoiceClient::onGatewayConnected);
    connect(gateway, &VoiceGateway::disconnected, this, &VoiceClient::onGatewayDisconnected);
    connect(gateway, &VoiceGateway::readyReceived, this, &VoiceClient::onGatewayReady);
    connect(gateway, &VoiceGateway::sessionDescriptionReceived, this, &VoiceClient::onSessionDescription);
    connect(gateway, &VoiceGateway::speakingReceived, this, &VoiceClient::onSpeaking);
    connect(gateway, &VoiceGateway::clientConnected, this, &VoiceClient::onClientConnect);
    connect(gateway, &VoiceGateway::clientsConnected, this, &VoiceClient::onClientsConnect);
    connect(gateway, &VoiceGateway::clientDisconnected, this, &VoiceClient::onClientDisconnect);
    connect(gateway, &VoiceGateway::resumed, this, &VoiceClient::onGatewayResumed);

    connect(gateway, &VoiceGateway::binaryPayloadReceived,
            this, [guard = QPointer<VoiceClient>(this)](int opcode, const QByteArray &payload) {
                auto *self = guard.data();
                if (!self || !self->daveSession)
                    return;
                switch (opcode) {
                case static_cast<int>(VoiceOpCode::DAVE_MLS_EXTERNAL_SENDER_PACKAGE):
                    self->daveSession->onExternalSenderPackage(payload);
                    break;
                case static_cast<int>(VoiceOpCode::DAVE_MLS_PROPOSALS):
                    self->daveSession->onProposals(payload);
                    break;
                case static_cast<int>(VoiceOpCode::DAVE_MLS_ANNOUNCE_COMMIT_TRANSITION):
                    self->daveSession->onAnnounceCommitTransition(payload);
                    break;
                case static_cast<int>(VoiceOpCode::DAVE_MLS_WELCOME):
                    self->daveSession->onWelcome(payload);
                    break;
                default:
                    qCDebug(LogVoice) << "Unhandled DAVE binary opcode:" << opcode;
                    break;
                }
            });
    connect(gateway, &VoiceGateway::daveTransitionPrepare,
            this, [guard = QPointer<VoiceClient>(this)](int protocolVersion, int transitionId) {
                auto *self = guard.data();
                if (self && self->daveSession)
                    self->daveSession->onPrepareTransition(protocolVersion, transitionId);
            });
    connect(gateway, &VoiceGateway::daveTransitionExecute,
            this, [guard = QPointer<VoiceClient>(this)](int transitionId) {
                auto *self = guard.data();
                if (self && self->daveSession)
                    self->daveSession->onExecuteTransition(transitionId);
            });
    connect(gateway, &VoiceGateway::daveEpochPrepare,
            this, [guard = QPointer<VoiceClient>(this)](int protocolVersion, int epoch) {
                auto *self = guard.data();
                if (!self)
                    return;
                if (!self->daveSession && protocolVersion > 0) {
                    // mid-call upgrade attempt. return cuz onPrepareEpoch also reinits
                    self->ensureDaveSession(static_cast<uint16_t>(protocolVersion));
                    return;
                }
                if (self->daveSession)
                    self->daveSession->onPrepareEpoch(protocolVersion, epoch);
            });

    gateway->start();
}

void VoiceClient::stop()
{
    if (currentState == State::Disconnected) {
        // A terminal gateway disconnect already ran the gateway teardown, but
        // the transport (UDP socket, timers, DAVE session) may still be live —
        // release it here so stop() is always a full teardown, not just a
        // state flip.
        cleanupTransport();
        return;
    }

    if (gateway) {
        gateway->hardStop();
        gateway->setParent(nullptr);
        gateway->deleteLater();
        gateway = nullptr;
    }

    cleanupTransport();

    localSsrc = 0;
    selectedMode.clear();
    sessionKey.clear();

    connectedUserIds.clear();
    ssrcToUserIdMap.clear();

    setState(State::Disconnected);
}

void VoiceClient::cleanupTransport()
{
    if (keepaliveTimer) {
        keepaliveTimer->stop();
        delete keepaliveTimer;
        keepaliveTimer = nullptr;
    }

    encryption.reset();

    daveSession.reset();

    delete udpTransport;
    udpTransport = nullptr;

    rtpSequence = 0;
    rtpTimestamp = 0;
    rtpEpoch = {};
}

void VoiceClient::onGatewayConnected()
{
    qCInfo(LogVoice) << "Voice gateway WebSocket connected, waiting for Hello + Identify";
    setState(State::Identifying);
}

void VoiceClient::onGatewayDisconnected(VoiceCloseCode code, const QString &reason)
{
    qCWarning(LogVoice) << "Voice gateway disconnected, code:" << code << "reason:" << reason;

    // the gateway only reports terminal disconnects here - resumable reconnects
    // keep the transport and session state alive so RESUME can pick them back up
    cleanupTransport();

    // done if not reconnected
    if (currentState != State::Disconnected) {
        setState(State::Disconnected);
        emit disconnected();
    }
}

void VoiceClient::onGatewayReady(const VoiceReady &data)
{
    qCInfo(LogVoice) << "Voice Ready: SSRC =" << data.ssrc
                     << "server =" << data.ip << ":" << data.port
                     << "modes =" << data.modes.get();

    localSsrc = data.ssrc;
    serverIp = data.ip;
    serverPort = data.port;
    serverModes = data.modes;

    static const std::array preferred = {
        EncryptionMode::AEAD_AES256_GCM_RTPSIZE,
        EncryptionMode::AEAD_XCHACHA20_POLY1305_RTPSIZE,
    };

    EncryptionMode mode = EncryptionMode::UNKNOWN;
    for (auto candidate : preferred) {
        if (serverModes.contains(encryptionModeToString(candidate)) && VoiceEncryption::isModeAvailable(candidate)) {
            mode = candidate;
            break;
        }
    }

    if (mode == EncryptionMode::UNKNOWN) {
        qCCritical(LogVoice) << "No supported encryption mode found! Server offered:" << serverModes;
        stop();
        return;
    }
    selectedMode = encryptionModeToString(mode);
    qCInfo(LogVoice) << "Selected encryption mode:" << selectedMode;

    setState(State::DiscoveringIP);

    cleanupTransport();

    udpTransport = new UdpTransport(this);
    connect(udpTransport, &UdpTransport::ipDiscovered, this, &VoiceClient::onIpDiscovered);
    connect(udpTransport, &UdpTransport::ipDiscoveryFailed, this, &VoiceClient::onIpDiscoveryFailed);
    connect(udpTransport, &UdpTransport::datagramReceived, this, &VoiceClient::onDatagram);

    udpTransport->startIpDiscovery(serverIp, serverPort, localSsrc);
}

void VoiceClient::onSessionDescription(const SessionDescription &desc)
{
    qCInfo(LogVoice) << "Session established: mode =" << desc.mode
                     << "key length =" << desc.secretKey->size();

    sessionKey = desc.secretKey;
    selectedMode = desc.mode;

    EncryptionMode mode = encryptionModeFromString(selectedMode);
    encryption = std::make_unique<VoiceEncryption>(mode, sessionKey);

    int daveVersion = desc.daveProtocolVersion.hasValue() ? desc.daveProtocolVersion.get() : 0;
    qCInfo(LogVoice) << "dave_protocol_version =" << daveVersion;

    if (daveVersion > 0)
        ensureDaveSession(static_cast<uint16_t>(daveVersion));

    rtpEpoch = std::chrono::steady_clock::now();

    setState(State::Connected);
    emit connected();

    // send silence so discord sends us audio immediately
    sendSilence();

    if (!keepaliveTimer) {
        keepaliveTimer = new QTimer(this);
        connect(keepaliveTimer, &QTimer::timeout, this, &VoiceClient::sendSilence);
    }
    keepaliveTimer->start(KEEPALIVE_INTERVAL_MS);
}

void VoiceClient::sendAudio(const QByteArray &opusData)
{
    if (currentState != State::Connected || !encryption || !udpTransport)
        return;

    // snap rtp timestamp back to wall clock after a period of silence
    // otherwise its a little behind and it will be played back delayed by discord
    auto now = std::chrono::steady_clock::now();
    if (newTalkspurt) {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - rtpEpoch);
        rtpTimestamp = static_cast<uint32_t>(static_cast<uint64_t>(elapsed.count()) * 48 / 1000);
    } else {
        rtpTimestamp += OPUS_FRAME_SAMPLES;
    }

    RtpHeader header;
    header.payloadType = 120;
    header.marker = newTalkspurt;
    header.sequence = rtpSequence++;
    header.timestamp = rtpTimestamp;
    header.ssrc = localSsrc;

    newTalkspurt = false;

    QByteArray payloadForTransport = opusData;

    if (isDaveEnabled()) {
        auto *enc = daveSession->encryptor();
        auto maxSize = enc->GetMaxCiphertextByteSize(discord::dave::MediaType::Audio, payloadForTransport.size());
        // Reuse the member scratch's capacity across packets (send path is
        // single-threaded on the voice thread).
        m_daveEncryptScratch.resize(maxSize);
        size_t bytesWritten = 0;
        auto result = enc->Encrypt(
                discord::dave::MediaType::Audio,
                localSsrc,
                discord::dave::ArrayView<const uint8_t>(
                        reinterpret_cast<const uint8_t *>(payloadForTransport.constData()),
                        payloadForTransport.size()),
                discord::dave::ArrayView<uint8_t>(m_daveEncryptScratch.data(), m_daveEncryptScratch.size()),
                &bytesWritten);

        if (result == discord::dave::IEncryptor::Success) {
            payloadForTransport = QByteArray(reinterpret_cast<const char *>(m_daveEncryptScratch.data()), bytesWritten);
        } else {
            qCWarning(LogVoice) << "DAVE encrypt failed: result =" << static_cast<int>(result);
            return;
        }
    }

    // Assemble the datagram in a single buffer: serialize the 12-byte RTP
    // header in place and copy the encrypted section once, avoiding the
    // intermediate headerBytes / headerBytes+encryptedSection allocations on
    // the per-packet send path. fromRawData is a zero-copy view valid only
    // during the encrypt() call below.
    char headerBytes[RtpHeader::FIXED_SIZE];
    header.serialize(headerBytes);
    QByteArray headerAad = QByteArray::fromRawData(headerBytes, RtpHeader::FIXED_SIZE);
    QByteArray encryptedSection = encryption->encrypt(headerAad, payloadForTransport);
    if (encryptedSection.isEmpty())
        return;

    QByteArray packet;
    packet.resize(RtpHeader::FIXED_SIZE + encryptedSection.size());
    std::memcpy(packet.data(), headerBytes, RtpHeader::FIXED_SIZE);
    std::memcpy(packet.data() + RtpHeader::FIXED_SIZE, encryptedSection.constData(),
                encryptedSection.size());
    udpTransport->send(packet);

    lastAudioSendTime = now;
}

void VoiceClient::setSpeaking(bool speaking)
{
    if (!gateway)
        return;

    if (speaking)
        newTalkspurt = true;

    int flags = speaking ? static_cast<int>(SpeakingFlag::MICROPHONE) : 0;
    gateway->sendSpeaking(flags, 0, localSsrc);
}

void VoiceClient::sendSilence()
{
    if (currentState != State::Connected || !encryption || !udpTransport)
        return;

    // no need to keepalive by sending silence if we spoke recently
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastAudioSendTime);
    if (elapsed.count() < 5)
        return;

    QByteArray silencePayload(reinterpret_cast<const char *>(Core::AV::OPUS_SILENCE), sizeof(Core::AV::OPUS_SILENCE));
    sendAudio(silencePayload);

    qCDebug(LogVoice) << "Sent keepalive silence frame";
}

void VoiceClient::onDatagram(const QByteArray &data)
{
    if (data.size() < RtpHeader::FIXED_SIZE)
        return;

    const auto *p = reinterpret_cast<const uint8_t *>(data.constData());

    // rtp version = 2
    if (((p[0] >> 6) & 0x03) != 2)
        return;

    // ignore all non-opus packets. theres rtcp and other stuff
    uint8_t payloadType = p[1] & 0x7F;
    if (payloadType != 120)
        return;

    // too small to contain meaningful audio after encryption overhead
    // Minimal valid packet: RTP header (12) + AEAD tag (16) + nonce (4) + 1 byte payload
    static constexpr int kMinDecryptableSize = RtpHeader::FIXED_SIZE + 16 + 4 + 1;
    if (data.size() < kMinDecryptableSize)
        return;

    // The RTP header (fixed header + CSRC list + full extension header) is
    // unencrypted and used as AAD for DAVE decryption. Use the shared helper so
    // the AAD covers the complete extension header — previously only the 4-byte
    // extension prefix was included, which broke decryption of packets that
    // carry an extension (and wrongly fed the extension data into the cipher).
    const int headerSize = rtpHeaderSize(data);
    if (headerSize < RtpHeader::FIXED_SIZE || data.size() < headerSize + 16 + 4 + 1)
        return;

    RtpHeader header = RtpHeader::parse(data);

    // ignore our own packets
    if (header.ssrc == localSsrc)
        return;

    if (!encryption) {
        qCDebug(LogVoice) << "Received RTP but no encryption context, SSRC =" << header.ssrc;
        return;
    }

    // Decrypt straight from the datagram buffer (no left()/mid() slicing
    // copies): libsodium reads the AAD from the header prefix and the
    // ciphertext from the tail. m_decryptScratch's capacity is reused across
    // packets.
    const int payloadLen = data.size() - headerSize;
    // isEmpty() keeps the old semantics: an empty decrypted payload (which the
    // old API reported as a failure) is dropped exactly as before.
    if (!encryption->decrypt(data, headerSize, payloadLen, m_decryptScratch)
        || m_decryptScratch.isEmpty()) {
        qCDebug(LogVoice) << "Decrypt failed: SSRC =" << header.ssrc
                          << "seq =" << header.sequence
                          << "pktSize =" << data.size()
                          << "hdrSize =" << headerSize
                          << "encSize =" << payloadLen;
        return;
    }
    const QByteArray &decrypted = m_decryptScratch;

    if (daveSession) {
        // https://daveprotocol.com/#silence-packets
        if (decrypted.size() == sizeof(Core::AV::OPUS_SILENCE) && memcmp(decrypted.constData(), Core::AV::OPUS_SILENCE, sizeof(Core::AV::OPUS_SILENCE)) == 0) {
            emit audioReceived(header.ssrc, header.sequence, header.timestamp, decrypted);
            return;
        }

        if (daveSession->isDaveEnabled()) {
            uint64_t ssrcUid = ssrcToUserIdMap.value(header.ssrc, 0);
            auto *dec = daveSession->getOrCreateDecryptor(header.ssrc, ssrcUid);
            auto maxSize = dec->GetMaxPlaintextByteSize(discord::dave::MediaType::Audio, decrypted.size());
            // Reuse the member scratch's capacity across packets.
            m_daveDecryptScratch.resize(maxSize);
            size_t bytesWritten = 0;
            auto result = dec->Decrypt(
                    discord::dave::MediaType::Audio,
                    discord::dave::ArrayView<const uint8_t>(
                            reinterpret_cast<const uint8_t *>(decrypted.constData()),
                            decrypted.size()),
                    discord::dave::ArrayView<uint8_t>(m_daveDecryptScratch.data(), m_daveDecryptScratch.size()),
                    &bytesWritten);

            if (result == discord::dave::IDecryptor::Success) {
                // The DAVE plaintext must outlive the vector, so copy it into
                // the decrypted buffer (single copy; capacity reused).
                m_decryptScratch.resize(static_cast<int>(bytesWritten));
                std::memcpy(m_decryptScratch.data(), m_daveDecryptScratch.data(), bytesWritten);
            } else {
                static constexpr const char *kResultNames[] = {
                    "Success",
                    "DecryptionFailure",
                    "MissingKeyRatchet",
                    "InvalidNonce",
                    "MissingCryptor",
                };
                int ri = static_cast<int>(result);
                const char *rn = (ri >= 0 && ri < 5) ? kResultNames[ri] : "Unknown";
                bool hasMagic = decrypted.size() >= 2 && static_cast<uint8_t>(decrypted[decrypted.size() - 2]) == 0xFA && static_cast<uint8_t>(decrypted[decrypted.size() - 1]) == 0xFA;
                qCDebug(LogDave) << "DAVE decrypt failed: SSRC =" << header.ssrc
                                 << "result =" << rn
                                 << "frameSize =" << decrypted.size()
                                 << "hasMagicMarker =" << hasMagic;
                return;
            }
        } else if (daveSession->isDowngraded()) {
            // fallthrough
        } else {
            return;
        }
    }

    emit audioReceived(header.ssrc, header.sequence, header.timestamp, m_decryptScratch);
}

bool VoiceClient::isDaveEnabled() const
{
    return daveSession && daveSession->isDaveEnabled();
}

void VoiceClient::requestVerificationCode(Core::Snowflake targetUserId,
                                          std::function<void(const QString &)> callback)
{
    if (!isDaveEnabled()) {
        callback(QString());
        return;
    }
    std::string uid = std::to_string(targetUserId);
    daveSession->getPairwiseFingerprint(uid,
                                        [cb = std::move(callback)](const std::vector<uint8_t> &fingerprint) {
                                            if (fingerprint.empty()) {
                                                cb(QString());
                                                return;
                                            }
                                            cb(formatDisplayableCode(fingerprint, 45));
                                        });
}

void VoiceClient::onSpeaking(const SpeakingData &data)
{
    if (data.userId.hasValue() && data.userId->isValid()) {
        std::string uid = std::to_string(data.userId.get());
        connectedUserIds.insert(uid);
        if (data.ssrc.get() != 0)
            ssrcToUserIdMap[data.ssrc] = data.userId.get();
        if (daveSession) {
            daveSession->addConnectedUser(uid);
            if (data.ssrc.get() != 0)
                daveSession->applyKeyRatchetForSsrc(data.ssrc, data.userId.get());
        }
    }
    emit speakingReceived(data);
}

void VoiceClient::onClientsConnect(const QStringList &userIds)
{
    for (const auto &id : userIds) {
        std::string uid = id.toStdString();
        connectedUserIds.insert(uid);
        if (daveSession)
            daveSession->addConnectedUser(uid);
    }
}

void VoiceClient::onClientConnect(const ClientConnectData &data)
{
    if (data.userId.hasValue() && data.userId->isValid()) {
        std::string uid = std::to_string(data.userId.get());
        connectedUserIds.insert(uid);
        if (data.audioSsrc.get() != 0)
            ssrcToUserIdMap[data.audioSsrc] = data.userId.get();
        if (daveSession)
            daveSession->addConnectedUser(uid);
    }
    emit clientConnected(data);
}

void VoiceClient::onClientDisconnect(Core::Snowflake uid)
{
    std::string uidStr = std::to_string(uid);
    connectedUserIds.erase(uidStr);

    if (daveSession)
        daveSession->removeConnectedUser(uidStr);

    for (auto it = ssrcToUserIdMap.begin(); it != ssrcToUserIdMap.end(); ++it) {
        if (it.value() == static_cast<quint64>(uid)) {
            ssrcToUserIdMap.erase(it);
            break;
        }
    }

    emit clientDisconnected(uid);
}

void VoiceClient::onGatewayResumed()
{
    qCInfo(LogVoice) << "Voice session resumed, restoring to Connected state";

    // assume session intact if we could resume
    if (localSsrc != 0 && !sessionKey.isEmpty()) {
        // if the transport was torn down while the gateway was away,
        // re-run the discovery/select-protocol path with the stored Ready data
        if (!udpTransport && !serverIp.isEmpty()) {
            qCWarning(LogVoice) << "UDP transport missing after resume, re-running IP discovery";

            setState(State::DiscoveringIP);

            udpTransport = new UdpTransport(this);
            connect(udpTransport, &UdpTransport::ipDiscovered, this, &VoiceClient::onIpDiscovered);
            connect(udpTransport, &UdpTransport::ipDiscoveryFailed, this, &VoiceClient::onIpDiscoveryFailed);
            connect(udpTransport, &UdpTransport::datagramReceived, this, &VoiceClient::onDatagram);

            udpTransport->startIpDiscovery(serverIp, serverPort, localSsrc);
            return;
        }

        rtpEpoch = std::chrono::steady_clock::now();
        setState(State::Connected);

        // just in case
        if (!encryption) {
            EncryptionMode mode = encryptionModeFromString(selectedMode);
            encryption = std::make_unique<VoiceEncryption>(mode, sessionKey);
        }

        sendSilence();
        if (!keepaliveTimer) {
            keepaliveTimer = new QTimer(this);
            connect(keepaliveTimer, &QTimer::timeout, this, &VoiceClient::sendSilence);
        }
        if (!keepaliveTimer->isActive())
            keepaliveTimer->start(KEEPALIVE_INTERVAL_MS);
    }
}

void VoiceClient::onIpDiscovered(const QString &ip, int port)
{
    qCInfo(LogVoice) << "IP Discovery: external" << ip << ":" << port;

    setState(State::SelectingProtocol);

    gateway->sendSelectProtocol(ip, port, selectedMode);

    setState(State::WaitingForSession);
}

void VoiceClient::onIpDiscoveryFailed(const QString &error)
{
    qCCritical(LogVoice) << "IP Discovery failed:" << error;
    stop();
}

void VoiceClient::ensureDaveSession(uint16_t protocolVersion)
{
    if (daveSession)
        return;

    qCInfo(LogVoice) << "Creating DAVE session, protocol version =" << protocolVersion;

    daveSession = std::make_unique<DaveSession>(channelId, userId, ssrcToUserIdMap, this);
    daveSession->setLocalSsrc(localSsrc);

    for (const auto &uid : connectedUserIds)
        daveSession->addConnectedUser(uid);

    // binary
    connect(daveSession.get(), &DaveSession::sendKeyPackage,
            gateway, &VoiceGateway::sendBinaryPayload);
    connect(daveSession.get(), &DaveSession::sendCommitWelcome,
            gateway, &VoiceGateway::sendBinaryPayload);

    // json
    connect(daveSession.get(), &DaveSession::sendReadyForTransition,
            gateway, &VoiceGateway::sendDaveReadyForTransition);
    connect(daveSession.get(), &DaveSession::sendInvalidCommitWelcome,
            gateway, &VoiceGateway::sendDaveInvalidCommitWelcome);

    connect(daveSession.get(), &DaveSession::daveStateChanged,
            this, [guard = QPointer<VoiceClient>(this)](bool enabled) {
                auto *self = guard.data();
                if (!self)
                    return;
                if (enabled) {
                    auto auth = self->daveSession->lastEpochAuthenticator();
                    if (!auth.empty())
                        emit self->privacyCodeChanged(formatDisplayableCode(auth));
                    else
                        emit self->privacyCodeChanged(QString());
                } else {
                    emit self->privacyCodeChanged(QString());
                }
            });

    daveSession->init(protocolVersion);
}

void VoiceClient::setState(State state)
{
    if (currentState == state)
        return;

    qCDebug(LogVoice) << "VoiceClient state:" << currentState.load() << "->" << state;
    currentState = state;
    emit stateChanged(state);
}

} // namespace AV
} // namespace Discord
} // namespace Acheron
