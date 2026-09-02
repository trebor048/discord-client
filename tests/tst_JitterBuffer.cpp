#include "Core/AV/JitterBuffer.hpp"
#include <QTest>

using namespace Acheron::Core::AV;

class TestJitterBuffer : public QObject
{
    Q_OBJECT

private slots:
    void testBasicOrdered();
    void testPacketLossReturnsEmpty();
    void testMultiFrameAdvance();
    void testSequenceWraparound();
    void testResetClearsState();
    void testFarAheadResets();
};

void TestJitterBuffer::testBasicOrdered()
{
    JitterBuffer jb;
    jb.push(1, "a");
    jb.push(2, "b");
    jb.push(3, "c"); // prebuffer (3 frames) now satisfied

    QCOMPARE(jb.pop(), QByteArray("a"));
    QCOMPARE(jb.pop(), QByteArray("b"));
    QCOMPARE(jb.pop(), QByteArray("c"));
}

void TestJitterBuffer::testPacketLossReturnsEmpty()
{
    JitterBuffer jb;
    jb.push(1, "a");
    jb.push(3, "c");
    jb.push(4, "d"); // 3 frames buffered -> prebuffer satisfied, seq 2 missing

    QCOMPARE(jb.pop(), QByteArray("a"));
    // Sequence 2 was never received: pop() returns empty (PLC trigger upstream).
    QVERIFY(jb.pop().isEmpty());
    // Sequences 3 and 4 are still buffered and come next.
    QCOMPARE(jb.pop(), QByteArray("c"));
    QCOMPARE(jb.pop(), QByteArray("d"));
}

void TestJitterBuffer::testMultiFrameAdvance()
{
    // Regression: a packet that decodes to N>1 Opus frames plays N-1 frames
    // from the caller's pending buffer without calling pop(). advanceFrames()
    // fast-forwards the play pointer so the next packet is found instead of
    // reporting N-1 consecutive misses (PLC garbage, spurious prebuffering,
    // and eventually a full reset that drops the buffered audio).
    JitterBuffer jb;
    // Consecutive packets as a 120ms-frame sender emits them: seq 100, then
    // 105 (each packet covers 5 frames).
    jb.push(100, "pktA");
    jb.push(105, "pktB");
    jb.push(110, "pktC"); // prebuffer satisfied

    QCOMPARE(jb.pop(), QByteArray("pktA")); // plays frame 100; nextSequence=101
    // The remaining 4 frames of pktA play from the caller's pending buffer:
    jb.advanceFrames(4);                    // nextSequence -> 105
    // The next pop must find pktB rather than missing 101..104.
    QCOMPARE(jb.pop(), QByteArray("pktB"));
    jb.advanceFrames(4);
    QCOMPARE(jb.pop(), QByteArray("pktC"));
}

void TestJitterBuffer::testSequenceWraparound()
{
    JitterBuffer jb;
    // Start near the top of the uint16 range.
    jb.push(65534, "a");
    jb.push(65535, "b");
    jb.push(0, "c");   // wraps past 65535
    jb.push(1, "d");   // prebuffer satisfied

    QCOMPARE(jb.pop(), QByteArray("a"));
    QCOMPARE(jb.pop(), QByteArray("b"));
    QCOMPARE(jb.pop(), QByteArray("c"));
    QCOMPARE(jb.pop(), QByteArray("d"));
}

void TestJitterBuffer::testResetClearsState()
{
    JitterBuffer jb;
    jb.push(1, "a");
    jb.reset();

    // After a reset the buffer is uninitialized: no frames, not ready.
    QVERIFY(!jb.isReady());
    QVERIFY(jb.pop().isEmpty());

    // A fresh push re-seeds from the new sequence (prebuffer must re-arm).
    jb.push(50, "b");
    QVERIFY(jb.pop().isEmpty()); // still prebuffering
    jb.push(51, "c");
    jb.push(52, "d");
    QCOMPARE(jb.pop(), QByteArray("b"));
}

void TestJitterBuffer::testFarAheadResets()
{
    JitterBuffer jb(3);
    jb.push(1, "a");
    jb.push(2, "b");
    jb.push(3, "c"); // prebuffer satisfied
    QCOMPARE(jb.pop(), QByteArray("a")); // nextSequence = 2

    // A packet way ahead of the play pointer (speaker resumed after a long
    // silence): the buffer resets and re-seeds on the new sequence instead of
    // buffering a huge gap.
    jb.push(40, "z");
    jb.push(41, "y");
    jb.push(42, "x"); // re-prebuffered
    QCOMPARE(jb.pop(), QByteArray("z"));
    QCOMPARE(jb.pop(), QByteArray("y"));
    QCOMPARE(jb.pop(), QByteArray("x"));
}

QTEST_APPLESS_MAIN(TestJitterBuffer)
#include "tst_JitterBuffer.moc"
