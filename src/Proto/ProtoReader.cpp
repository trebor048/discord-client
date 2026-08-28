#include "ProtoReader.hpp"

#include "Core/Logging.hpp"

#include <limits>

namespace Acheron {
namespace Proto {

ProtoReader::ProtoReader(const QByteArray &data, uint32_t maxDepth)
    : data(data), m_maxDepth(maxDepth) { }

bool ProtoReader::readByte(uint8_t &byte)
{
    if (pos >= static_cast<size_t>(data.size()))
        return false;

    byte = static_cast<uint8_t>(data[static_cast<int>(pos++)]);
    return true;
}

bool ProtoReader::readBytes(void *dest, size_t count)
{
    if (pos + count > static_cast<size_t>(data.size()))
        return false;

    memcpy(dest, data.data() + pos, count);
    pos += count;
    return true;
}

bool ProtoReader::atEnd() const
{
    return pos >= static_cast<size_t>(data.size());
}

bool ProtoReader::readVarint(uint64_t &value)
{
    value = 0;
    int shift = 0;
    uint8_t byte;

    do {
        if (shift >= 64) {
            qCWarning(LogProto) << "Varint overflow";
            return false;
        }

        if (!readByte(byte))
            return false;

        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);

    return true;
}

bool ProtoReader::readFixed64(uint64_t &value)
{
    uint8_t bytes[8];
    if (!readBytes(bytes, 8))
        return false;

    // le decode
    value = static_cast<uint64_t>(bytes[0]) | (static_cast<uint64_t>(bytes[1]) << 8) |
            (static_cast<uint64_t>(bytes[2]) << 16) | (static_cast<uint64_t>(bytes[3]) << 24) |
            (static_cast<uint64_t>(bytes[4]) << 32) | (static_cast<uint64_t>(bytes[5]) << 40) |
            (static_cast<uint64_t>(bytes[6]) << 48) | (static_cast<uint64_t>(bytes[7]) << 56);
    return true;
}

bool ProtoReader::readFixed32(uint32_t &value)
{
    uint8_t bytes[4];
    if (!readBytes(bytes, 4))
        return false;

    // le decode
    value = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
            (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

bool ProtoReader::readLengthDelimited(QByteArray &value)
{
    uint64_t length;
    if (!readVarint(length))
        return false;

    if (length > static_cast<uint64_t>(remaining())) {
        qCWarning(LogProto) << "Length-delimited field exceeds remaining data";
        return false;
    }

    // BUG 2: protect against negative-size UB when length exceeds INT_MAX
    if (length > static_cast<uint64_t>(std::numeric_limits<int>::max()))
        return false;

    value.resize(static_cast<int>(length));
    if (length > 0 && !readBytes(value.data(), static_cast<size_t>(length)))
        return false;

    return true;
}

std::optional<ProtoReader> ProtoReader::subReader()
{
    // BUG 1: reject if already at max nesting depth
    if (m_depth >= m_maxDepth)
        return std::nullopt;

    QByteArray data;
    if (!readLengthDelimited(data))
        return std::nullopt;

    ProtoReader sub(data, m_maxDepth);
    sub.m_depth = m_depth + 1;
    return sub;
}

bool ProtoReader::readTag(Tag &tag)
{
    if (atEnd())
        return false;

    uint64_t tagValue;
    if (!readVarint(tagValue))
        return false;

    tag.fieldNumber = static_cast<uint32_t>(tagValue >> 3);
    tag.wireType = static_cast<WireType>(tagValue & 0x7);
    return true;
}

bool ProtoReader::skipField(WireType wireType)
{
    switch (wireType) {
    case WireType::VARINT: {
        uint64_t value;
        return readVarint(value);
    }
    case WireType::FIXED64: {
        uint64_t value;
        return readFixed64(value);
    }
    case WireType::LENGTH_DELIMITED: {
        // Skip without materializing the payload: read the varint length,
        // apply the same bounds checks as readLengthDelimited(), then just
        // advance the cursor.
        uint64_t length;
        if (!readVarint(length))
            return false;

        if (length > static_cast<uint64_t>(remaining())) {
            qCWarning(LogProto) << "Length-delimited field exceeds remaining data";
            return false;
        }

        if (length > static_cast<uint64_t>(std::numeric_limits<int>::max()))
            return false;

        pos += static_cast<size_t>(length);
        return true;
    }
    case WireType::FIXED32: {
        uint32_t value;
        return readFixed32(value);
    }
    case WireType::START_GROUP:
    case WireType::END_GROUP:
        qCWarning(LogProto) << "Deprecated group wire types not supported";
        return false;
    default:
        qCWarning(LogProto) << "Unknown wire type:" << static_cast<int>(wireType);
        return false;
    }
}

QString readString(ProtoReader &reader)
{
    QByteArray bytes;
    if (!reader.readLengthDelimited(bytes))
        return QString();

    return QString::fromUtf8(bytes);
}

std::optional<int64_t> readInt64Value(ProtoReader &reader)
{
    // google.protobuf.Int64Value is a message with field 1 = int64
    Tag tag;
    while (reader.readTag(tag)) {
        if (tag.fieldNumber == 1 && tag.wireType == WireType::VARINT) {
            uint64_t value;
            if (reader.readVarint(value))
                return static_cast<int64_t>(value);
        } else {
            if (!reader.skipField(tag.wireType))
                break; // unskippable wire type — abort to avoid misparse
        }
    }
    return std::nullopt;
}

std::optional<QString> readStringValue(ProtoReader &reader)
{
    // google.protobuf.StringValue is a message with field 1 = string
    Tag tag;
    while (reader.readTag(tag)) {
        if (tag.fieldNumber == 1 && tag.wireType == WireType::LENGTH_DELIMITED) {
            QByteArray bytes;
            if (reader.readLengthDelimited(bytes))
                return QString::fromUtf8(bytes);
        } else {
            if (!reader.skipField(tag.wireType))
                break; // unskippable wire type — abort to avoid misparse
        }
    }
    return std::nullopt;
}

std::optional<uint64_t> readUInt64Value(ProtoReader &reader)
{
    // google.protobuf.UInt64Value is a message with field 1 = uint64
    Tag tag;
    while (reader.readTag(tag)) {
        if (tag.fieldNumber == 1 && tag.wireType == WireType::VARINT) {
            uint64_t value;
            if (reader.readVarint(value))
                return value;
        } else {
            if (!reader.skipField(tag.wireType))
                break; // unskippable wire type — abort to avoid misparse
        }
    }
    return std::nullopt;
}

} // namespace Proto
} // namespace Acheron
