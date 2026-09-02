#include "VoiceEncryption.hpp"

#include "Core/Logging.hpp"

#include <sodium.h>
#include <QtEndian>

#include <cstring>

#include <climits>

namespace Acheron {
namespace Discord {
namespace AV {

static constexpr int SUPPLEMENTAL_NONCE_SIZE = 4;

/// Maximum usable nonce value before the 4-byte wire format wraps to zero.
/// When m_nonce exceeds this, encrypt will fail closed to prevent catastrophic
/// nonce reuse in the AEAD cipher.
static constexpr uint64_t MAX_WIRE_NONCE = UINT32_MAX;

/// Warn the caller when fewer than this many packets remain before exhaustion.
static constexpr uint64_t NONCE_EXHAUSTION_WARN_THRESHOLD = 1000;

VoiceEncryption::VoiceEncryption(EncryptionMode mode, const QByteArray &secretKey)
    : m_mode(mode), m_key(secretKey)
{
    // Validate key length early — libsodium expects 32 bytes; a truncated key
    // from a malformed SessionDescription would cause heap OOB read in encrypt().
    const int expected = (mode == EncryptionMode::AEAD_AES256_GCM_RTPSIZE)
                             ? static_cast<int>(crypto_aead_aes256gcm_KEYBYTES)
                             : static_cast<int>(crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    if (m_key.size() != expected) {
        qCCritical(LogVoice) << "VoiceEncryption: wrong key size" << m_key.size() << "expected" << expected;
        m_key.clear();
    }
}

bool VoiceEncryption::isNonceExhausted() const
{
    return m_nonce > MAX_WIRE_NONCE;
}

bool VoiceEncryption::initialize()
{
    static bool success = []() {
        int result = sodium_init();
        if (result < 0) {
            qCCritical(LogVoice) << "Failed to initialize libsodium";
            return false;
        }
        return true;
    }();
    return success;
}

bool VoiceEncryption::isModeAvailable(EncryptionMode mode)
{
    switch (mode) {
    case EncryptionMode::AEAD_AES256_GCM_RTPSIZE:
        return crypto_aead_aes256gcm_is_available() != 0;
    case EncryptionMode::AEAD_XCHACHA20_POLY1305_RTPSIZE:
        return true;
    default:
        return false;
    }
}

QByteArray VoiceEncryption::encrypt(const QByteArray &rtpHeader, const QByteArray &audioPayload)
{
    // Guard: nonce exhaustion — fail closed before catastrophic AEAD nonce reuse.
    // The 4-byte wire format supports only 2^32 unique nonces. Once m_nonce exceeds
    // UINT32_MAX, the truncated wire value would repeat earlier nonces, breaking the
    // AEAD security guarantee (authenticity + confidentiality).
    if (m_nonce > MAX_WIRE_NONCE) {
        qCCritical(LogVoice) << "Encryption nonce exhausted:" << m_nonce
                             << "exceeds 32-bit wire limit — refusing to encrypt."
                             << "Caller must renegotiate a new session key.";
        return {};
    }

    // Proximity warning: approaching exhaustion
    if (m_nonce >= MAX_WIRE_NONCE - NONCE_EXHAUSTION_WARN_THRESHOLD) {
        qCWarning(LogVoice) << "Encryption nonce approaching exhaustion:"
                            << m_nonce << "packets used,"
                            << (MAX_WIRE_NONCE - m_nonce) << "remaining."
                            << "Caller should renegotiate a new session key soon.";
    }

    switch (m_mode) {
    case EncryptionMode::AEAD_AES256_GCM_RTPSIZE:
        return encryptAes256Gcm(rtpHeader, audioPayload);
    case EncryptionMode::AEAD_XCHACHA20_POLY1305_RTPSIZE:
        return encryptXChacha20(rtpHeader, audioPayload);
    default:
        qCWarning(LogVoice) << "Encrypt called with unknown encryption mode";
        return {};
    }
}

QByteArray VoiceEncryption::encryptAes256Gcm(const QByteArray &rtpHeader, const QByteArray &payload)
{
    if (m_key.isEmpty())
        return {};
    // Nonce: 4-byte BE counter padded to 12 bytes
    // Use lower 32 bits; 64-bit counter prevents silent wraparound at 2^32
    uint8_t nonce[crypto_aead_aes256gcm_NPUBBYTES] = {};
    uint32_t nonceBE = qToBigEndian(static_cast<uint32_t>(m_nonce));
    std::memcpy(nonce, &nonceBE, SUPPLEMENTAL_NONCE_SIZE);

    // Ciphertext = payload + 16-byte auth tag
    QByteArray ciphertext(payload.size() + crypto_aead_aes256gcm_ABYTES, Qt::Uninitialized);
    unsigned long long cipherLen = 0;

    int ret = crypto_aead_aes256gcm_encrypt(
            reinterpret_cast<unsigned char *>(ciphertext.data()), &cipherLen,
            reinterpret_cast<const unsigned char *>(payload.constData()), payload.size(),
            reinterpret_cast<const unsigned char *>(rtpHeader.constData()), rtpHeader.size(),
            nullptr, nonce,
            reinterpret_cast<const unsigned char *>(m_key.constData()));

    if (ret != 0) {
        qCWarning(LogVoice) << "AES-256-GCM encrypt failed";
        return {};
    }

    ciphertext.resize(static_cast<int>(cipherLen));
    ciphertext.append(reinterpret_cast<const char *>(&nonceBE), SUPPLEMENTAL_NONCE_SIZE);

    m_nonce++;
    return ciphertext;
}

QByteArray VoiceEncryption::encryptXChacha20(const QByteArray &rtpHeader, const QByteArray &payload)
{
    if (m_key.isEmpty())
        return {};
    // Nonce: 4-byte BE counter padded to 24 bytes
    // Use lower 32 bits; 64-bit counter prevents silent wraparound at 2^32
    uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
    uint32_t nonceBE = qToBigEndian(static_cast<uint32_t>(m_nonce));
    std::memcpy(nonce, &nonceBE, SUPPLEMENTAL_NONCE_SIZE);

    QByteArray ciphertext(payload.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, Qt::Uninitialized);
    unsigned long long cipherLen = 0;

    int ret = crypto_aead_xchacha20poly1305_ietf_encrypt(
            reinterpret_cast<unsigned char *>(ciphertext.data()), &cipherLen,
            reinterpret_cast<const unsigned char *>(payload.constData()), payload.size(),
            reinterpret_cast<const unsigned char *>(rtpHeader.constData()), rtpHeader.size(),
            nullptr, nonce,
            reinterpret_cast<const unsigned char *>(m_key.constData()));

    if (ret != 0) {
        qCWarning(LogVoice) << "XChaCha20-Poly1305 encrypt failed";
        return {};
    }

    ciphertext.resize(static_cast<int>(cipherLen));
    ciphertext.append(reinterpret_cast<const char *>(&nonceBE), SUPPLEMENTAL_NONCE_SIZE);

    m_nonce++;
    return ciphertext;
}

bool VoiceEncryption::decrypt(const QByteArray &packet, int headerOffset, int payloadLen, QByteArray &out)
{
    switch (m_mode) {
    case EncryptionMode::AEAD_AES256_GCM_RTPSIZE:
        return decryptAes256Gcm(packet, headerOffset, payloadLen, out);
    case EncryptionMode::AEAD_XCHACHA20_POLY1305_RTPSIZE:
        return decryptXChacha20(packet, headerOffset, payloadLen, out);
    default:
        return false;
    }
}

bool VoiceEncryption::decryptAes256Gcm(const QByteArray &packet, int headerOffset, int payloadLen, QByteArray &out)
{
    if (m_key.isEmpty())
        return false;
    int minSize = static_cast<int>(crypto_aead_aes256gcm_ABYTES) + SUPPLEMENTAL_NONCE_SIZE;
    if (payloadLen < minSize) {
        qCDebug(LogVoice) << "AES-GCM decrypt: too short, need" << minSize << "got" << payloadLen;
        return false;
    }

    const char *encrypted = packet.constData() + headerOffset;

    // Extract 4-byte supplemental nonce from end of packet, place at start of 12-byte nonce
    uint8_t nonce[crypto_aead_aes256gcm_NPUBBYTES] = {};
    std::memcpy(nonce, encrypted + payloadLen - SUPPLEMENTAL_NONCE_SIZE,
                SUPPLEMENTAL_NONCE_SIZE);

    int cipherLen = payloadLen - SUPPLEMENTAL_NONCE_SIZE;
    out.resize(cipherLen - static_cast<int>(crypto_aead_aes256gcm_ABYTES));
    unsigned long long plainLen = 0;

    int ret = crypto_aead_aes256gcm_decrypt(
            reinterpret_cast<unsigned char *>(out.data()), &plainLen,
            nullptr,
            reinterpret_cast<const unsigned char *>(encrypted), cipherLen,
            reinterpret_cast<const unsigned char *>(packet.constData()), headerOffset,
            nonce,
            reinterpret_cast<const unsigned char *>(m_key.constData()));

    if (ret != 0) {
        qCDebug(LogVoice) << "AES-GCM decrypt failed: cipherLen =" << cipherLen
                          << "aadLen =" << headerOffset
                          << "nonce =" << QByteArray(reinterpret_cast<const char *>(nonce), crypto_aead_aes256gcm_NPUBBYTES).toHex(' ');
        return false;
    }

    out.resize(static_cast<int>(plainLen));
    return true;
}

bool VoiceEncryption::decryptXChacha20(const QByteArray &packet, int headerOffset, int payloadLen, QByteArray &out)
{
    if (m_key.isEmpty())
        return false;
    int minSize = static_cast<int>(crypto_aead_xchacha20poly1305_ietf_ABYTES) + SUPPLEMENTAL_NONCE_SIZE;
    if (payloadLen < minSize) {
        qCDebug(LogVoice) << "XChaCha20 decrypt: too short, need" << minSize << "got" << payloadLen;
        return false;
    }

    const char *encrypted = packet.constData() + headerOffset;

    // Extract 4-byte supplemental nonce from end of packet, place at start of 24-byte nonce
    uint8_t nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES] = {};
    std::memcpy(nonce, encrypted + payloadLen - SUPPLEMENTAL_NONCE_SIZE,
                SUPPLEMENTAL_NONCE_SIZE);

    int cipherLen = payloadLen - SUPPLEMENTAL_NONCE_SIZE;
    out.resize(cipherLen - static_cast<int>(crypto_aead_xchacha20poly1305_ietf_ABYTES));
    unsigned long long plainLen = 0;

    int ret = crypto_aead_xchacha20poly1305_ietf_decrypt(
            reinterpret_cast<unsigned char *>(out.data()), &plainLen,
            nullptr,
            reinterpret_cast<const unsigned char *>(encrypted), cipherLen,
            reinterpret_cast<const unsigned char *>(packet.constData()), headerOffset,
            nonce,
            reinterpret_cast<const unsigned char *>(m_key.constData()));

    if (ret != 0) {
        qCDebug(LogVoice) << "XChaCha20 decrypt failed: cipherLen =" << cipherLen
                          << "aadLen =" << headerOffset
                          << "nonce =" << QByteArray(reinterpret_cast<const char *>(nonce), crypto_aead_xchacha20poly1305_ietf_NPUBBYTES).toHex(' ');
        return false;
    }

    out.resize(static_cast<int>(plainLen));
    return true;
}

} // namespace AV
} // namespace Discord
} // namespace Acheron
