#pragma once

#include <QByteArray>

#include <cstdint>

#include "VoiceEnums.hpp"

namespace Acheron {
namespace Discord {
namespace AV {

class VoiceEncryption
{
public:
    VoiceEncryption(EncryptionMode mode, const QByteArray &secretKey);

    /// Encrypts an audio payload for an outbound RTP packet.
    /// @param rtpHeader  The serialized RTP header bytes (used as AAD for AEAD modes).
    /// @param audioPayload  The raw audio data (e.g. 3-byte Opus silence).
    /// @return The encrypted section to append after the RTP header:
    ///         [ciphertext + auth tag] + [4-byte supplemental nonce (big-endian)].
    ///         Empty on failure.
    QByteArray encrypt(const QByteArray &rtpHeader, const QByteArray &audioPayload);

    /// Decrypts the encrypted section of an inbound RTP packet directly from
    /// the datagram buffer (no left()/mid() slicing copies).
    /// @param packet  The full received datagram.
    /// @param headerOffset  Size of the RTP header prefix at the start of
    ///                      packet; used as AAD for the AEAD modes.
    /// @param payloadLen  Length of the encrypted section following the header:
    ///                    [ciphertext + auth tag] + [4-byte supplemental nonce
    ///                    (big-endian)].
    /// @param out  Receives the decrypted audio payload on success. Resized as
    ///             needed; capacity is reused across calls.
    /// @return true on success, false on failure (out contents then undefined).
    bool decrypt(const QByteArray &packet, int headerOffset, int payloadLen, QByteArray &out);

    /// Check whether the nonce counter is exhausted (would wrap on next encrypt).
    /// The caller should renegotiate a new key before this happens.
    bool isNonceExhausted() const;

    /// Return the current nonce count.
    uint64_t nonceCount() const { return m_nonce; }

    /// Initialize libsodium. Must be called before any encrypt/decrypt operations.
    /// Safe to call multiple times.
    static bool initialize();

    /// Check whether the given encryption mode is supported on this hardware.
    /// Must call initialize() first.
    static bool isModeAvailable(EncryptionMode mode);

private:
    QByteArray encryptAes256Gcm(const QByteArray &rtpHeader, const QByteArray &payload);
    QByteArray encryptXChacha20(const QByteArray &rtpHeader, const QByteArray &payload);
    bool decryptAes256Gcm(const QByteArray &packet, int headerOffset, int payloadLen, QByteArray &out);
    bool decryptXChacha20(const QByteArray &packet, int headerOffset, int payloadLen, QByteArray &out);

    EncryptionMode m_mode;
    QByteArray m_key;
    uint64_t m_nonce = 0;
};

} // namespace AV
} // namespace Discord
} // namespace Acheron
