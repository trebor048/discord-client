#pragma once

#include <QByteArray>

#include <cstdint>

namespace Acheron {
namespace Discord {
namespace AV {

struct RtpHeader
{
    uint8_t version = 2;
    bool padding = false;
    bool extension = false;
    uint8_t csrcCount = 0;
    bool marker = false;
    uint8_t payloadType = 0;
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;

    static constexpr int FIXED_SIZE = 12;

    QByteArray serialize() const;
    // Writes the FIXED_SIZE-byte header into dst (which must point to at least
    // FIXED_SIZE bytes); byte-identical to the QByteArray-returning overload.
    void serialize(char *dst) const;
    static RtpHeader parse(const QByteArray &data);
};

// Returns the total RTP header size including CSRCs and extensions.
// Returns -1 if the packet is too short to determine the size.
int rtpHeaderSize(const QByteArray &packet);

} // namespace AV
} // namespace Discord
} // namespace Acheron
