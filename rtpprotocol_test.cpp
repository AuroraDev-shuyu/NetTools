// GTest unit tests for RtpProtocol (RFC 3550 parsing + stream stats).
// All test vectors are synthetic byte arrays built in-place.
#include <gtest/gtest.h>
#include "rtpprotocol.h"
#include <QByteArray>
#include <QString>

using namespace RtpProtocol;

namespace {

QByteArray makeHeader(quint8 b0, quint8 b1, quint16 seq, quint32 ts, quint32 ssrc)
{
    QByteArray d;
    d.append(char(b0));
    d.append(char(b1));
    d.append(char((seq >> 8) & 0xFF));
    d.append(char(seq & 0xFF));
    d.append(char((ts >> 24) & 0xFF));
    d.append(char((ts >> 16) & 0xFF));
    d.append(char((ts >> 8) & 0xFF));
    d.append(char(ts & 0xFF));
    d.append(char((ssrc >> 24) & 0xFF));
    d.append(char((ssrc >> 16) & 0xFF));
    d.append(char((ssrc >> 8) & 0xFF));
    d.append(char(ssrc & 0xFF));
    return d;
}

QByteArray be32(quint32 v)
{
    QByteArray b;
    b.append(char((v >> 24) & 0xFF));
    b.append(char((v >> 16) & 0xFF));
    b.append(char((v >> 8) & 0xFF));
    b.append(char(v & 0xFF));
    return b;
}

QByteArray rtcpHeader(quint8 type, quint8 count, int lengthWords)
{
    QByteArray h;
    h.append(char(0x80 | count));
    h.append(char(type));
    h.append(char((lengthWords >> 8) & 0xFF));
    h.append(char(lengthWords & 0xFF));
    return h;
}

// ---------- RTP packet parsing ----------

TEST(RtpParse, MinimalValidHeader)
{
    // v2, no pad/ext/csrc, marker=0, PT=96, seq=1, ts=2, ssrc=3
    const QByteArray d = makeHeader(0x80, 0x60, 1, 2, 3);
    RtpPacket p;
    ASSERT_TRUE(parseRtpPacket(d, p));
    EXPECT_TRUE(p.valid);
    EXPECT_EQ(p.version, 2);
    EXPECT_FALSE(p.padding);
    EXPECT_FALSE(p.extension);
    EXPECT_EQ(p.csrcCount, 0);
    EXPECT_FALSE(p.marker);
    EXPECT_EQ(p.payloadType, 96);
    EXPECT_EQ(p.seq, 1);
    EXPECT_EQ(p.timestamp, 2u);
    EXPECT_EQ(p.ssrc, 3u);
    EXPECT_EQ(p.payloadOffset, 12);
    EXPECT_EQ(p.payloadSize, 0);
    EXPECT_EQ(p.paddingBytes, 0);
}

TEST(RtpParse, InvalidVersion)
{
    RtpPacket p;
    EXPECT_FALSE(parseRtpPacket(makeHeader(0x00, 0x60, 0, 0, 0), p));  // version 0
    EXPECT_FALSE(parseRtpPacket(makeHeader(0x40, 0x60, 0, 0, 0), p));  // version 1
    EXPECT_FALSE(parseRtpPacket(makeHeader(0xC0, 0x60, 0, 0, 0), p));  // version 3
}

TEST(RtpParse, TruncatedHeader)
{
    RtpPacket p;
    EXPECT_FALSE(parseRtpPacket(QByteArray(11, '\0'), p));
    EXPECT_FALSE(parseRtpPacket(QByteArray(), p));
}

TEST(RtpParse, CsrcAndMarker)
{
    // cc=3, marker=1, PT=96
    QByteArray d = makeHeader(0x83, 0xE0, 7, 8, 9);
    d += QByteArray::fromHex("AABBCCDD1122334455667788");
    d += "PAYLOAD";
    RtpPacket p;
    ASSERT_TRUE(parseRtpPacket(d, p));
    EXPECT_TRUE(p.marker);
    EXPECT_EQ(p.csrcCount, 3);
    ASSERT_EQ(p.csrcs.size(), 3);
    EXPECT_EQ(p.csrcs[0], 0xAABBCCDDu);
    EXPECT_EQ(p.csrcs[1], 0x11223344u);
    EXPECT_EQ(p.csrcs[2], 0x55667788u);
    EXPECT_EQ(p.payloadOffset, 24);
    EXPECT_EQ(p.payloadSize, 7);
    EXPECT_EQ(d.mid(p.payloadOffset, p.payloadSize), QByteArray("PAYLOAD"));
}

TEST(RtpParse, ExtensionHeaderSkipped)
{
    // X=1: profile=0xBEDE, length=1 word (4 bytes of extension data), then payload
    QByteArray d = makeHeader(0x90, 0x60, 0, 0, 1);
    d += QByteArray::fromHex("BEDE000101020304");
    d += "XY";
    RtpPacket p;
    ASSERT_TRUE(parseRtpPacket(d, p));
    EXPECT_TRUE(p.extension);
    EXPECT_EQ(p.payloadOffset, 20);
    EXPECT_EQ(p.payloadSize, 2);
    EXPECT_EQ(d.mid(p.payloadOffset, p.payloadSize), QByteArray("XY"));
}

TEST(RtpParse, Padding)
{
    // P=1: payload "ABCD" + 3 padding bytes (count byte says 3, includes itself)
    QByteArray d = makeHeader(0xA0, 0x60, 0, 0, 1);
    d += "ABCD";
    d.append(char(0));
    d.append(char(0));
    d.append(char(3));
    RtpPacket p;
    ASSERT_TRUE(parseRtpPacket(d, p));
    EXPECT_TRUE(p.padding);
    EXPECT_EQ(p.paddingBytes, 3);
    EXPECT_EQ(p.payloadSize, 4);
    EXPECT_EQ(d.mid(p.payloadOffset, p.payloadSize), QByteArray("ABCD"));
}

TEST(RtpParse, PaddingCountInvalid)
{
    QByteArray d = makeHeader(0xA0, 0x60, 0, 0, 1);
    d += "AB";
    d.append(char(5));  // claims 5 > available
    RtpPacket p;
    EXPECT_FALSE(parseRtpPacket(d, p));
}

TEST(RtpParse, PayloadTypeNames)
{
    EXPECT_TRUE(payloadTypeName(0).startsWith(QStringLiteral("PCMU")));
    EXPECT_TRUE(payloadTypeName(8).startsWith(QStringLiteral("PCMA")));
    EXPECT_TRUE(payloadTypeName(26).contains(QStringLiteral("JPEG")));
    EXPECT_TRUE(payloadTypeName(33).contains(QStringLiteral("MP2T")));
    EXPECT_TRUE(payloadTypeName(96).contains(QStringLiteral("96")));    // dynamic
    EXPECT_TRUE(payloadTypeName(127).contains(QStringLiteral("127")));  // dynamic
    EXPECT_TRUE(payloadTypeName(35).contains(QStringLiteral("未知")));  // unknown
    EXPECT_TRUE(payloadTypeName(200).contains(QStringLiteral("未知")));
}

// ---------- RTCP ----------

TEST(RtcpIsRtcp, DiscriminatorBoundary)
{
    QByteArray sr;
    sr.append(char(0x80));
    sr.append(char(0xC8));  // type 200 SR
    EXPECT_TRUE(isRtcp(sr));
    QByteArray rtp;
    rtp.append(char(0x80));
    rtp.append(char(0x60));  // RTP PT=96
    EXPECT_FALSE(isRtcp(rtp));
    QByteArray low;
    low.append(char(0x80));
    low.append(char(0xBF));  // type 191, below RTCP range
    EXPECT_FALSE(isRtcp(low));
    QByteArray ver0;
    ver0.append(char(0x00));
    ver0.append(char(0xC8));  // version 0
    EXPECT_FALSE(isRtcp(ver0));
    EXPECT_FALSE(isRtcp(QByteArray()));
    EXPECT_FALSE(isRtcp(QByteArray(1, '\x80')));
}

TEST(RtcpParse, SrSdesCompound)
{
    QByteArray sr = rtcpHeader(200, 0, 6);  // 28 bytes
    sr += be32(0x11111111u);
    sr += be32(0x22222222u);
    sr += be32(0x33333333u);
    sr += be32(0x44444444u);
    sr += be32(0x55555555u);
    sr += be32(0x66666666u);
    QByteArray sdes = rtcpHeader(202, 1, 3);  // 16 bytes with padding
    sdes += be32(0x11111111u);
    sdes.append(char(1));   // CNAME item
    sdes.append(char(4));
    sdes += "abcd";
    sdes.append(char(0));   // terminator
    sdes.append(char(0));   // pad to 32-bit boundary
    const QList<RtcpPacket> pkts = parseRtcp(sr + sdes);
    ASSERT_EQ(pkts.size(), 2);
    EXPECT_EQ(pkts[0].type, 200);
    EXPECT_TRUE(pkts[0].hasSenderInfo);
    EXPECT_EQ(pkts[0].senderSsrc, 0x11111111u);
    EXPECT_EQ(pkts[0].ntpTimestamp, 0x2222222233333333ull);
    EXPECT_EQ(pkts[0].rtpTimestamp, 0x44444444u);
    EXPECT_EQ(pkts[0].packetCount, 0x55555555u);
    EXPECT_EQ(pkts[0].octetCount, 0x66666666u);
    EXPECT_TRUE(pkts[0].reports.isEmpty());
    EXPECT_EQ(pkts[1].type, 202);
    ASSERT_EQ(pkts[1].cnames.size(), 1);
    EXPECT_EQ(pkts[1].cnames[0], QStringLiteral("abcd"));
}

TEST(RtcpParse, RrReportBlock)
{
    QByteArray d = rtcpHeader(201, 1, 7);  // 32 bytes
    d += be32(0x99999999u);  // sender ssrc
    d += be32(0xAAAAAAAAu);  // report ssrc
    d.append(char(0x80));    // fraction lost = 50%
    d.append(char(0x00));
    d.append(char(0x00));
    d.append(char(0xFF));    // cumulative lost = 255
    d += be32(0x00010020u);  // extended highest seq: cycle 1, seq 32
    d += be32(0x00001000u);  // jitter
    d += be32(0x00000000u);  // last SR
    d += be32(0x00000001u);  // delay since last SR
    const QList<RtcpPacket> pkts = parseRtcp(d);
    ASSERT_EQ(pkts.size(), 1);
    EXPECT_EQ(pkts[0].type, 201);
    EXPECT_EQ(pkts[0].senderSsrc, 0x99999999u);
    ASSERT_EQ(pkts[0].reports.size(), 1);
    const RtcpReportBlock &rb = pkts[0].reports[0];
    EXPECT_EQ(rb.ssrc, 0xAAAAAAAAu);
    EXPECT_EQ(rb.fractionLost, 0x80);
    EXPECT_EQ(rb.cumulativeLost, 255);
    EXPECT_EQ(rb.highestSeq, 0x00010020u);
    EXPECT_EQ(rb.jitter, 0x1000u);
    EXPECT_EQ(rb.lastSrTimestamp, 0u);
    EXPECT_EQ(rb.delaySinceLastSr, 1u);
}

TEST(RtcpParse, ByeAppAndUnknownType)
{
    QByteArray bye = rtcpHeader(203, 1, 2);  // 12 bytes
    bye += be32(0x11111111u);
    bye.append(char(2));  // reason length
    bye += "ok";
    bye.append(char(0));  // pad
    QByteArray app = rtcpHeader(204, 0, 2);  // 12 bytes
    app += be32(0x22222222u);
    app += "TEST";
    QByteArray unk = rtcpHeader(210, 0, 2);  // unknown type, skipped payload
    unk += QByteArray(8, '\xAB');
    const QList<RtcpPacket> pkts = parseRtcp(bye + app + unk);
    ASSERT_EQ(pkts.size(), 3);
    EXPECT_EQ(pkts[0].type, 203);
    ASSERT_EQ(pkts[0].byeSsrcs.size(), 1);
    EXPECT_EQ(pkts[0].byeSsrcs[0], 0x11111111u);
    EXPECT_EQ(pkts[0].byeReason, QStringLiteral("ok"));
    EXPECT_EQ(pkts[1].type, 204);
    EXPECT_EQ(pkts[1].appName, QByteArray("TEST"));
    EXPECT_EQ(pkts[2].type, 210);
    EXPECT_TRUE(pkts[2].reports.isEmpty());
    EXPECT_EQ(pkts[2].rawData.size(), 12);
}

TEST(RtcpParse, TruncatedTailDropped)
{
    QByteArray d = rtcpHeader(200, 0, 6);  // claims 28 bytes
    d += QByteArray(10, '\0');             // only 14 present
    EXPECT_TRUE(parseRtcp(d).isEmpty());
    // valid block followed by a truncated tail: only the valid one survives
    QByteArray ok = rtcpHeader(201, 0, 1);  // 8 bytes: header + sender ssrc
    ok += be32(0x99999999u);
    const QList<RtcpPacket> pkts = parseRtcp(ok + d);
    ASSERT_EQ(pkts.size(), 1);
    EXPECT_EQ(pkts[0].type, 201);
}

TEST(RtcpTypeNames, KnownTypes)
{
    EXPECT_TRUE(rtcpTypeName(200).contains(QStringLiteral("SR")));
    EXPECT_TRUE(rtcpTypeName(201).contains(QStringLiteral("RR")));
    EXPECT_TRUE(rtcpTypeName(203).contains(QStringLiteral("BYE")));
    EXPECT_TRUE(rtcpTypeName(199).contains(QStringLiteral("未知")));
}

// ---------- per-SSRC stream stats ----------

TEST(RtpStats, SeqWrapAndLoss)
{
    RtpStreamStats s;
    RtpPacket p;
    // seq 65534, 65535, 0, 1 received, seq 2 lost, seq 3 received
    ASSERT_TRUE(parseRtpPacket(makeHeader(0x80, 0x60, 65534, 0, 1), p));
    s.update(p, 1000);
    ASSERT_TRUE(parseRtpPacket(makeHeader(0x80, 0x60, 65535, 90, 1), p));
    s.update(p, 1001);
    ASSERT_TRUE(parseRtpPacket(makeHeader(0x80, 0x60, 0, 180, 1), p));
    s.update(p, 1002);
    ASSERT_TRUE(parseRtpPacket(makeHeader(0x80, 0x60, 1, 270, 1), p));
    s.update(p, 1003);
    ASSERT_TRUE(parseRtpPacket(makeHeader(0x80, 0x60, 3, 450, 1), p));
    s.update(p, 1004);
    EXPECT_EQ(s.packetCount(), 5);
    EXPECT_EQ(s.expectedPackets(), 6);  // base 65534 .. extended 65539
    EXPECT_EQ(s.lostPackets(), 1);
    EXPECT_NEAR(s.lossRate(), 100.0 / 6.0, 1e-6);
    EXPECT_EQ(s.highestSeq(), 3);
    EXPECT_EQ(s.firstArrivalMs(), 1000);
    EXPECT_EQ(s.lastArrivalMs(), 1004);
}

TEST(RtpStats, JitterRfc3550)
{
    // 90kHz clock: ts 90000 = 1000ms arrives at 1000ms (transit 0);
    // ts 90090 = 1001ms arrives at 1002ms (transit 1); d = 1, jitter = 1/16
    RtpStreamStats s;
    RtpPacket p;
    ASSERT_TRUE(parseRtpPacket(makeHeader(0x80, 0x60, 0, 90000, 1), p));
    s.update(p, 1000);
    ASSERT_TRUE(parseRtpPacket(makeHeader(0x80, 0x60, 1, 90090, 1), p));
    s.update(p, 1002);
    EXPECT_NEAR(s.jitter(), 1.0 / 16.0, 1e-9);
    EXPECT_EQ(s.packetCount(), 2);
    EXPECT_EQ(s.octetCount(), 0);  // payload sizes are zero
}

TEST(RtpStats, ResetAndFirstPacket)
{
    RtpStreamStats s;
    EXPECT_FALSE(s.hasData());
    RtpPacket p;
    ASSERT_TRUE(parseRtpPacket(makeHeader(0x80, 0x60, 100, 1000, 7), p));
    s.update(p, 500);
    EXPECT_TRUE(s.hasData());
    EXPECT_EQ(s.ssrc(), 7u);
    EXPECT_EQ(s.payloadType(), 96);
    EXPECT_EQ(s.jitter(), 0.0);
    s.reset();
    EXPECT_FALSE(s.hasData());
    EXPECT_EQ(s.packetCount(), 0);
}

} // namespace
