// GTest unit tests for H264Parser (Annex-B splitting, Exp-Golomb, SPS/PPS, GOP).
// Real SPS/PPS byte vectors come from FFmpeg-generated streams and were
// cross-checked against ffprobe + FFmpeg trace_headers.
#include <gtest/gtest.h>
#include "h264parser.h"
#include "hexdump.h"
#include <QByteArray>
#include <QList>
#include <QString>

using namespace H264Parser;

namespace {

// ---------- Annex-B splitting ----------

TEST(SplitAnnexB, ThreeByteStartCode)
{
    const QByteArray d = QByteArray::fromHex("00000167" "00000168");
    const QList<Nalu> nalus = splitAnnexB(d);
    ASSERT_EQ(nalus.size(), 2);
    EXPECT_EQ(nalus[0].startCodeLen, 3);
    EXPECT_EQ(nalus[0].offset, 0);
    EXPECT_EQ(nalus[0].data, QByteArray::fromHex("67"));
    EXPECT_EQ(nalus[1].startCodeLen, 3);
    EXPECT_EQ(nalus[1].offset, 4);
    EXPECT_EQ(nalus[1].data, QByteArray::fromHex("68"));
}

TEST(SplitAnnexB, FourByteStartCode)
{
    const QByteArray d = QByteArray::fromHex("0000000167" "0000000168");
    const QList<Nalu> nalus = splitAnnexB(d);
    ASSERT_EQ(nalus.size(), 2);
    EXPECT_EQ(nalus[0].startCodeLen, 4);
    EXPECT_EQ(nalus[0].offset, 0);
    EXPECT_EQ(nalus[0].data, QByteArray::fromHex("67"));
    EXPECT_EQ(nalus[1].startCodeLen, 4);
    EXPECT_EQ(nalus[1].offset, 5);
    EXPECT_EQ(nalus[1].data, QByteArray::fromHex("68"));
}

TEST(SplitAnnexB, TrailingZerosTrimmed)
{
    // NALU followed by trailing zeros before the next start code
    const QByteArray d = QByteArray::fromHex("00000167" "000000" "0000000168");
    const QList<Nalu> nalus = splitAnnexB(d);
    ASSERT_EQ(nalus.size(), 2);
    EXPECT_EQ(nalus[0].data, QByteArray::fromHex("67"));  // zeros trimmed
    EXPECT_EQ(nalus[1].data, QByteArray::fromHex("68"));
}

TEST(SplitAnnexB, LeadingGarbageIgnored)
{
    const QByteArray d = QByteArray::fromHex("FFFE" "00000167");
    const QList<Nalu> nalus = splitAnnexB(d);
    ASSERT_EQ(nalus.size(), 1);
    EXPECT_EQ(nalus[0].offset, 2);
    EXPECT_EQ(nalus[0].data, QByteArray::fromHex("67"));
}

TEST(SplitAnnexB, NoStartCode)
{
    EXPECT_TRUE(splitAnnexB(QByteArray("ABCDEF")).isEmpty());
    EXPECT_TRUE(splitAnnexB(QByteArray()).isEmpty());
}

// ---------- NAL header ----------

TEST(NalHeader, FieldsAndTypes)
{
    NaluHeader h = parseNaluHeader(QByteArray::fromHex("67"));
    EXPECT_TRUE(h.valid);
    EXPECT_FALSE(h.forbiddenZeroBit);
    EXPECT_EQ(h.nalRefIdc, 3);
    EXPECT_EQ(h.type, 7);
    h = parseNaluHeader(QByteArray::fromHex("41"));
    EXPECT_TRUE(h.valid);
    EXPECT_EQ(h.nalRefIdc, 2);
    EXPECT_EQ(h.type, 1);
    h = parseNaluHeader(QByteArray::fromHex("80"));  // forbidden_zero_bit set
    EXPECT_FALSE(h.valid);
    EXPECT_TRUE(h.forbiddenZeroBit);
    h = parseNaluHeader(QByteArray());
    EXPECT_FALSE(h.valid);
    EXPECT_EQ(h.type, 0);
}

TEST(NaluTypeName, KnownAndUnknown)
{
    EXPECT_TRUE(naluTypeName(1).contains(QStringLiteral("非IDR")));
    EXPECT_TRUE(naluTypeName(5).contains(QStringLiteral("IDR")));
    EXPECT_TRUE(naluTypeName(6).contains(QStringLiteral("SEI")));
    EXPECT_TRUE(naluTypeName(7).contains(QStringLiteral("SPS")));
    EXPECT_TRUE(naluTypeName(8).contains(QStringLiteral("PPS")));
    EXPECT_TRUE(naluTypeName(9).contains(QStringLiteral("AUD")));
    EXPECT_TRUE(naluTypeName(31).contains(QStringLiteral("31")));  // 未知(31)
}

// ---------- SPS with real stream bytes (FFmpeg-generated, ffprobe-verified) ----------

TEST(SpsParse, RealBaseline320x240)
{
    // x264 baseline 320x240@15fps: profile 66, level 12, ref=3, fps 15.0
    const QByteArray sps = QByteArray::fromHex(
        "6742C00CD90141FB0110000003001000000301E0F142A48000");
    const SpsInfo info = parseSps(sps);
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.profileIdc, 66);
    EXPECT_EQ(info.profileName, QStringLiteral("Baseline"));
    EXPECT_EQ(info.levelIdc, 12);
    EXPECT_EQ(info.levelName, QStringLiteral("1.2"));
    EXPECT_EQ(info.width, 320);
    EXPECT_EQ(info.height, 240);
    EXPECT_EQ(info.refFrames, 3);
    EXPECT_NEAR(info.frameRate, 15.0, 1e-6);
    EXPECT_EQ(info.chromaFormatIdc, 1);
    EXPECT_TRUE(info.frameMbsOnly);
}

TEST(SpsParse, RealCrop318x238)
{
    // x264 with cropping: 318x238@10fps, level 11
    const QByteArray sps = QByteArray::fromHex(
        "6742C00BD90141FEAB011000000300100000030140F142A48000");
    const SpsInfo info = parseSps(sps);
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.profileIdc, 66);
    EXPECT_EQ(info.levelIdc, 11);
    EXPECT_EQ(info.levelName, QStringLiteral("1.1"));
    EXPECT_EQ(info.width, 318);
    EXPECT_EQ(info.height, 238);
    EXPECT_EQ(info.refFrames, 3);
    EXPECT_NEAR(info.frameRate, 10.0, 1e-6);
}

TEST(SpsParse, RealHighProfile)
{
    // x264 high profile 320x240: profile 100, level 12, ref=4, fps 15.0
    const QByteArray sps = QByteArray::fromHex(
        "6764000CACD94141FB0110000003001000000301E0F142996000");
    const SpsInfo info = parseSps(sps);
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.profileIdc, 100);
    EXPECT_EQ(info.profileName, QStringLiteral("High"));
    EXPECT_EQ(info.levelIdc, 12);
    EXPECT_EQ(info.width, 320);
    EXPECT_EQ(info.height, 240);
    EXPECT_EQ(info.refFrames, 4);
    EXPECT_NEAR(info.frameRate, 15.0, 1e-6);
}

TEST(SpsParse, InvalidInputs)
{
    EXPECT_FALSE(parseSps(QByteArray()).valid);                 // empty
    EXPECT_FALSE(parseSps(QByteArray::fromHex("6701")).valid);  // too short
    EXPECT_FALSE(parseSps(QByteArray::fromHex("68CB83CB20")).valid);  // not type 7
    // real SPS truncated mid-way: core fields overrun -> invalid
    EXPECT_FALSE(parseSps(QByteArray::fromHex("6742C00CD90141")).valid);
}

// ---------- PPS ----------

TEST(PpsParse, RealCavlcBaseline)
{
    // x264 baseline PPS: CAVLC (entropy=0), no weighted pred, qp minus26 = -3
    const QByteArray pps = QByteArray::fromHex("68CB83CB20");
    const PpsInfo info = parsePps(pps);
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.ppsId, 0);
    EXPECT_EQ(info.spsId, 0);
    EXPECT_FALSE(info.entropyCodingModeFlag);
    EXPECT_FALSE(info.weightedPredFlag);
    EXPECT_EQ(info.picInitQpMinus26, -3);
}

TEST(PpsParse, RealCabacHighProfile)
{
    // x264 high profile PPS: CABAC (entropy=1), weighted pred enabled
    const QByteArray pps = QByteArray::fromHex("68EBE3CB22C0");
    const PpsInfo info = parsePps(pps);
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.ppsId, 0);
    EXPECT_EQ(info.spsId, 0);
    EXPECT_TRUE(info.entropyCodingModeFlag);
    EXPECT_TRUE(info.weightedPredFlag);
}

TEST(PpsParse, InvalidInputs)
{
    EXPECT_FALSE(parsePps(QByteArray()).valid);
    EXPECT_FALSE(parsePps(QByteArray::fromHex("67CB83CB20")).valid);  // not type 8
}

// ---------- frame classification (hand-built slice headers) ----------

TEST(FrameType, SliceHeaderTypes)
{
    EXPECT_TRUE(classifyFrame(QByteArray::fromHex("41C0")) == FrameType::P);   // slice_type 0
    EXPECT_TRUE(classifyFrame(QByteArray::fromHex("41A0")) == FrameType::B);   // slice_type 1
    EXPECT_TRUE(classifyFrame(QByteArray::fromHex("41B0")) == FrameType::I);   // slice_type 2
    EXPECT_TRUE(classifyFrame(QByteArray::fromHex("4198")) == FrameType::P);   // slice_type 5
    EXPECT_TRUE(classifyFrame(QByteArray::fromHex("4187E0")) == FrameType::Unknown);  // >9
    EXPECT_TRUE(classifyFrame(QByteArray::fromHex("6588840E")) == FrameType::Idr);    // type 5
    EXPECT_TRUE(classifyFrame(QByteArray::fromHex("67")) == FrameType::Unknown);      // SPS
    EXPECT_TRUE(classifyFrame(QByteArray::fromHex("06")) == FrameType::Unknown);      // SEI
    EXPECT_TRUE(classifyFrame(QByteArray()) == FrameType::Unknown);
}

TEST(FrameType, Names)
{
    EXPECT_EQ(frameTypeName(FrameType::I), QStringLiteral("I"));
    EXPECT_EQ(frameTypeName(FrameType::P), QStringLiteral("P"));
    EXPECT_EQ(frameTypeName(FrameType::B), QStringLiteral("B"));
    EXPECT_EQ(frameTypeName(FrameType::Idr), QStringLiteral("IDR"));
    EXPECT_EQ(frameTypeName(FrameType::Unknown), QStringLiteral("--"));
}

// ---------- BitReader (Exp-Golomb) ----------

TEST(BitReader, UeSeBoundaries)
{
    // Bit stream '1' '010' '011' '00100' '00101' '00110' = ue 0,1,2,3,4,5
    BitReader br(QByteArray::fromHex("A64298"));
    EXPECT_EQ(br.readUe(), 0u);
    EXPECT_EQ(br.readUe(), 1u);
    EXPECT_EQ(br.readUe(), 2u);
    EXPECT_EQ(br.readUe(), 3u);
    EXPECT_EQ(br.readUe(), 4u);
    EXPECT_EQ(br.readUe(), 5u);
    EXPECT_FALSE(br.overrun());
    BitReader brSe(QByteArray::fromHex("A64298"));
    EXPECT_EQ(brSe.readSe(), 0);
    EXPECT_EQ(brSe.readSe(), 1);
    EXPECT_EQ(brSe.readSe(), -1);
    EXPECT_EQ(brSe.readSe(), 2);
    EXPECT_EQ(brSe.readSe(), -2);
    EXPECT_FALSE(brSe.overrun());
}

TEST(BitReader, Overrun)
{
    BitReader br(QByteArray::fromHex("80"));
    EXPECT_EQ(br.readBits(8), 0x80u);
    EXPECT_FALSE(br.overrun());
    EXPECT_EQ(br.readBits(1), 0u);  // past the end
    EXPECT_TRUE(br.overrun());
    BitReader brUe(QByteArray::fromHex("00"));
    EXPECT_EQ(brUe.readUe(), 0u);  // hits overrun inside the leading-zero loop
    EXPECT_TRUE(brUe.overrun());
}

TEST(BitReader, EmulationPreventionSkipped)
{
    // Raw bytes 00 00 03 00 FF: the 03 is an emulation prevention byte and
    // must be invisible to RBSP parsing -> read 24 zero bits, then 0xFF.
    BitReader br(QByteArray::fromHex("00000300FF"));
    EXPECT_EQ(br.readBits(24), 0u);
    EXPECT_EQ(br.readBits(8), 0xFFu);
    EXPECT_EQ(br.bitPos(), 32);
    EXPECT_FALSE(br.overrun());
}

// ---------- GOP stats ----------

TEST(GopStats, MixedSequence)
{
    const QList<FrameType> frames = {FrameType::Idr, FrameType::P, FrameType::B,
                                     FrameType::P, FrameType::Idr, FrameType::P};
    const GopStats s = computeGopStats(frames);
    EXPECT_EQ(s.idrCount, 2);
    EXPECT_EQ(s.totalFrames, 6);
    EXPECT_EQ(s.iFrames, 2);  // includes IDR
    EXPECT_EQ(s.pFrames, 3);
    EXPECT_EQ(s.bFrames, 1);
    EXPECT_EQ(s.unknownFrames, 0);
    ASSERT_EQ(s.keyframeIndices.size(), 2);
    EXPECT_EQ(s.keyframeIndices[0], 0);
    EXPECT_EQ(s.keyframeIndices[1], 4);
    EXPECT_EQ(s.minGop, 4);
    EXPECT_EQ(s.maxGop, 4);
    EXPECT_NEAR(s.avgGop, 4.0, 1e-9);
}

TEST(GopStats, SingleKeyframe)
{
    const QList<FrameType> frames = {FrameType::Idr, FrameType::P};
    const GopStats s = computeGopStats(frames);
    EXPECT_EQ(s.idrCount, 1);
    EXPECT_EQ(s.minGop, 0);
    EXPECT_EQ(s.maxGop, 0);
    EXPECT_EQ(s.avgGop, 0.0);
}

// ---------- hex dump ----------

TEST(HexDump, Format)
{
    const QString out = hexDump(QByteArray("ABCD"));
    EXPECT_TRUE(out.startsWith(QStringLiteral("0000  ")));
    EXPECT_TRUE(out.contains(QStringLiteral("41 42 43 44")));
    EXPECT_TRUE(out.contains(QStringLiteral("|ABCD|")));
}

TEST(HexDump, MultiLine)
{
    const QString out = hexDump(QByteArray(20, '\x41'));
    EXPECT_EQ(out.count(QLatin1Char('\n')), 2);
    EXPECT_TRUE(out.contains(QStringLiteral("0010  41 41 41 41")));
    EXPECT_TRUE(out.contains(QStringLiteral("|AAAA|")));
}

} // namespace
