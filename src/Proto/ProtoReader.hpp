#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <optional>

namespace Acheron {
namespace Proto {

enum class WireType : uint8_t {
    VARINT = 0,
    FIXED64 = 1,
    LENGTH_DELIMITED = 2,
    START_GROUP = 3,
    END_GROUP = 4,
    FIXED32 = 5
};

struct Tag
{
    uint32_t fieldNumber;
    WireType wireType;
};

class ProtoReader
{
public:
    explicit ProtoReader(const QByteArray &data, uint32_t maxDepth = 32);

    bool readTag(Tag &tag);
    bool atEnd() const;

    bool readVarint(uint64_t &value);
    bool readFixed64(uint64_t &value);
    bool readLengthDelimited(QByteArray &value);
    bool readFixed32(uint32_t &value);

    bool skipField(WireType wireType);

    /// Read a length-delimited sub-message, checking depth limit against
    /// stack overflow from maliciously nested protobuf messages.
    /// Returns std::nullopt if the depth limit is exceeded or the read fails.
    std::optional<ProtoReader> subReader();

    size_t position() const { return pos; }
    size_t remaining() const { return data.size() - pos; }
    uint32_t maxDepth() const { return m_maxDepth; }
    uint32_t depth() const { return m_depth; }

private:
    const QByteArray data;
    size_t pos = 0;
    uint32_t m_depth = 0;
    uint32_t m_maxDepth;

    bool readByte(uint8_t &byte);
    bool readBytes(void *dest, size_t count);
};

QString readString(ProtoReader &reader);
std::optional<int64_t> readInt64Value(ProtoReader &reader);
std::optional<QString> readStringValue(ProtoReader &reader);
std::optional<uint64_t> readUInt64Value(ProtoReader &reader);

} // namespace Proto
} // namespace Acheron
