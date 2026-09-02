#include "Proto/ProtoReader.hpp"
#include <QTest>
#include <limits>

using namespace Acheron::Proto;

class TestProtoReader : public QObject
{
    Q_OBJECT

private slots:
    void testSimpleVarint();
    void testLargeVarint();
    void testMaximumVarint();
    void testVarintOverflow();
    void testVarintTenthByteOverflow();
    void testReadFixed64();
    void testReadLengthDelimited();
    void testReadTag();
    void testTruncatedData();
    void testSubReaderDepthLimit();
    void testLengthDelimitedTooLarge();
};

void TestProtoReader::testSimpleVarint()
{
    // UInt64Value wrapper: tag (field=1, varint=0) = 0x08, value 0 = 0x00
    {
        QByteArray data = QByteArray::fromHex("0800");
        ProtoReader reader(data);
        auto result = readUInt64Value(reader);
        QVERIFY(result.has_value());
        QCOMPARE(result.value(), 0ull);
    }

    // UInt64Value wrapper: tag 0x08, value 1 = 0x01
    {
        QByteArray data = QByteArray::fromHex("0801");
        ProtoReader reader(data);
        auto result = readUInt64Value(reader);
        QVERIFY(result.has_value());
        QCOMPARE(result.value(), 1ull);
    }

    // UInt64Value wrapper: tag 0x08, value 127 = 0x7F (max single-byte varint)
    {
        QByteArray data = QByteArray::fromHex("087F");
        ProtoReader reader(data);
        auto result = readUInt64Value(reader);
        QVERIFY(result.has_value());
        QCOMPARE(result.value(), 127ull);
    }
}

void TestProtoReader::testLargeVarint()
{
    // UInt64Value wrapper: tag 0x08, value 128 = 0x80 0x01 (2-byte varint)
    {
        QByteArray data = QByteArray::fromHex("088001");
        ProtoReader reader(data);
        auto result = readUInt64Value(reader);
        QVERIFY(result.has_value());
        QCOMPARE(result.value(), 128ull);
    }

    // UInt64Value wrapper: tag 0x08, value 16383 = 0xFF 0x7F (max 2-byte varint)
    {
        QByteArray data = QByteArray::fromHex("08FF7F");
        ProtoReader reader(data);
        auto result = readUInt64Value(reader);
        QVERIFY(result.has_value());
        QCOMPARE(result.value(), 16383ull);
    }

    // UInt64Value wrapper: tag 0x08, value 300 = 0xAC 0x02
    {
        QByteArray data = QByteArray::fromHex("08AC02");
        ProtoReader reader(data);
        auto result = readUInt64Value(reader);
        QVERIFY(result.has_value());
        QCOMPARE(result.value(), 300ull);
    }
}

void TestProtoReader::testMaximumVarint()
{
    // Maximum uint64 value in varint encoding uses all 10 bytes.
    // 9 bytes of 0xFF (each has continuation bit set) then 0x01.
    // With UInt64Value wrapper tag (0x08).
    QByteArray data = QByteArray::fromHex("08FFFFFFFFFFFFFFFFFF01");
    ProtoReader reader(data);
    auto result = readUInt64Value(reader);
    QVERIFY(result.has_value());
    QCOMPARE(result.value(), std::numeric_limits<uint64_t>::max());
}

void TestProtoReader::testVarintOverflow()
{
    // 11 bytes of varint with continuation bits set should trigger overflow.
    // Read raw varint (not via wrapper) to test readVarint directly.
    QByteArray data = QByteArray::fromHex("FFFFFFFFFFFFFFFFFFFF01");
    ProtoReader reader(data);
    uint64_t value;
    QVERIFY(!reader.readVarint(value));
}

void TestProtoReader::testVarintTenthByteOverflow()
{
    // A 10-byte varint whose 10th byte carries value bits beyond bit 0
    // (e.g. 0x02) needs >= 65 bits; previously `(byte & 0x7F) << 63` was UB.
    // 9 bytes of 0xFF then 0x02 (continuation cleared, but value bits set).
    QByteArray data = QByteArray::fromHex("FFFFFFFFFFFFFFFFFF02");
    ProtoReader reader(data);
    uint64_t value;
    QVERIFY(!reader.readVarint(value));
}

void TestProtoReader::testReadFixed64()
{
    // Tag (field=1, FIXED64=1): 0x09
    // Little-endian value 0x0102030405060708
    QByteArray data = QByteArray::fromHex("090807060504030201");
    ProtoReader reader(data);
    Tag tag;
    QVERIFY(reader.readTag(tag));
    QCOMPARE(tag.fieldNumber, 1u);
    QCOMPARE(tag.wireType, WireType::FIXED64);

    uint64_t value;
    QVERIFY(reader.readFixed64(value));
    QCOMPARE(value, 0x0102030405060708ull);
}

void TestProtoReader::testReadLengthDelimited()
{
    // Tag (field=1, LENGTH_DELIMITED=2): 0x0A
    // Length varint = 5, data = "hello"
    QByteArray data = QByteArray::fromHex("0A0568656C6C6F");
    ProtoReader reader(data);
    Tag tag;
    QVERIFY(reader.readTag(tag));
    QCOMPARE(tag.fieldNumber, 1u);
    QCOMPARE(tag.wireType, WireType::LENGTH_DELIMITED);

    QByteArray value;
    QVERIFY(reader.readLengthDelimited(value));
    QCOMPARE(value, QByteArray("hello"));
}

void TestProtoReader::testReadTag()
{
    // Tag value 0x08 = field 1, wire type VARINT
    {
        QByteArray data = QByteArray::fromHex("08");
        ProtoReader reader(data);
        Tag tag;
        QVERIFY(reader.readTag(tag));
        QCOMPARE(tag.fieldNumber, 1u);
        QCOMPARE(tag.wireType, WireType::VARINT);
    }

    // Tag value 0x12 = field 2, wire type LENGTH_DELIMITED
    {
        QByteArray data = QByteArray::fromHex("12");
        ProtoReader reader(data);
        Tag tag;
        QVERIFY(reader.readTag(tag));
        QCOMPARE(tag.fieldNumber, 2u);
        QCOMPARE(tag.wireType, WireType::LENGTH_DELIMITED);
    }

    // Tag value 0x09 = field 1, wire type FIXED64
    {
        QByteArray data = QByteArray::fromHex("09");
        ProtoReader reader(data);
        Tag tag;
        QVERIFY(reader.readTag(tag));
        QCOMPARE(tag.fieldNumber, 1u);
        QCOMPARE(tag.wireType, WireType::FIXED64);
    }
}

void TestProtoReader::testTruncatedData()
{
    // Tag present but no varint data follows
    {
        QByteArray data = QByteArray::fromHex("08");
        ProtoReader reader(data);
        Tag tag;
        QVERIFY(reader.readTag(tag));
        uint64_t value;
        QVERIFY(!reader.readVarint(value));
        QVERIFY(reader.atEnd());
    }

    // Empty data
    {
        QByteArray data;
        ProtoReader reader(data);
        Tag tag;
        QVERIFY(!reader.readTag(tag));
        QVERIFY(reader.atEnd());
    }

    // Partial fixed64 (only 4 bytes instead of 8)
    {
        QByteArray data = QByteArray::fromHex("0908070605");
        ProtoReader reader(data);
        Tag tag;
        QVERIFY(reader.readTag(tag));

        uint64_t value;
        QVERIFY(!reader.readFixed64(value));
    }
}

void TestProtoReader::testSubReaderDepthLimit()
{
    // Create reader with maxDepth=0; subReader should fail immediately.
    QByteArray data = QByteArray::fromHex("0A03010203"); // field 1, len=3
    ProtoReader reader(data, 0);
    auto sub = reader.subReader();
    QVERIFY(!sub.has_value());
}

void TestProtoReader::testLengthDelimitedTooLarge()
{
    // Tag 0x0A (field 1, LENGTH_DELIMITED), length varint = 10, but only 3 bytes follow.
    QByteArray data = QByteArray::fromHex("0A0A010203");
    ProtoReader reader(data);
    Tag tag;
    QVERIFY(reader.readTag(tag));
    QCOMPARE(tag.fieldNumber, 1u);

    QByteArray value;
    QVERIFY(!reader.readLengthDelimited(value));
}

QTEST_APPLESS_MAIN(TestProtoReader)
#include "tst_ProtoReader.moc"
