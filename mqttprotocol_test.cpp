#include <gtest/gtest.h>
#include "mqttprotocol.h"

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")  // MSVC编译器：设置执行字符集为UTF-8
#endif

using namespace MqttProtocol;

// 解析单个报文的便捷封装：返回解析结果、类型标志和报文体
namespace {
struct Decoded {
    DecodeResult result = DecodeResult::Incomplete;
    quint8 typeAndFlags = 0;
    QByteArray body;
};
Decoded decodeOne(const QByteArray &data)
{
    Decoded d;
    int pos = 0;
    d.result = decodePacket(data, pos, d.typeAndFlags, d.body);
    return d;
}
} // namespace

// ---------- 固定报头类型与标志位 ----------

TEST(PacketBasics, PacketTypeExtractsHighNibble)
{
    EXPECT_EQ(packetType(Connect), Connect);
    EXPECT_EQ(packetType(0x3D), Publish);      // 低4位不影响类型提取
    EXPECT_EQ(packetType(0xE0), Disconnect);
    EXPECT_EQ(packetType(0x00), 0x00);
}

TEST(PacketBasics, PacketFlagsExtractsLowNibble)
{
    EXPECT_EQ(packetFlags(0x3D), 0x0D);        // QoS2 + retain
    EXPECT_EQ(packetFlags(0x30), 0x00);
    EXPECT_EQ(packetFlags(0x82), 0x02);        // SUBSCRIBE固定标志0010
}

// ---------- 剩余长度变长整数 ----------

TEST(RemainingLength, EncodeKnownBoundaries)
{
    EXPECT_EQ(encodeRemainingLength(0),         QByteArray::fromHex("00"));
    EXPECT_EQ(encodeRemainingLength(127),       QByteArray::fromHex("7f"));
    EXPECT_EQ(encodeRemainingLength(128),       QByteArray::fromHex("8001"));
    EXPECT_EQ(encodeRemainingLength(16383),     QByteArray::fromHex("ff7f"));
    EXPECT_EQ(encodeRemainingLength(16384),     QByteArray::fromHex("808001"));
    EXPECT_EQ(encodeRemainingLength(2097151),   QByteArray::fromHex("ffff7f"));
    EXPECT_EQ(encodeRemainingLength(2097152),   QByteArray::fromHex("80808001"));
    EXPECT_EQ(encodeRemainingLength(268435455), QByteArray::fromHex("ffffff7f"));
}

TEST(RemainingLength, DecodeKnownBoundaries)
{
    // 每字节最高位为继续标志，低7位为有效数据（大端基128）
    const struct { QByteArray bytes; int expected; } cases[] = {
        {QByteArray::fromHex("00"), 0},
        {QByteArray::fromHex("7f"), 127},
        {QByteArray::fromHex("8001"), 128},
        {QByteArray::fromHex("ff7f"), 16383},
        {QByteArray::fromHex("808001"), 16384},
        {QByteArray::fromHex("ffff7f"), 2097151},
        {QByteArray::fromHex("80808001"), 2097152},
        {QByteArray::fromHex("ffffff7f"), 268435455},
    };
    for (const auto &c : cases) {
        int pos = 0;
        int length = 0;
        ASSERT_TRUE(decodeRemainingLength(c.bytes, pos, length));
        EXPECT_EQ(length, c.expected);
        EXPECT_EQ(pos, c.bytes.size());  // pos前进到变长整数末尾
    }
}

TEST(RemainingLength, EncodeDecodeRoundTrip)
{
    const int values[] = {0, 1, 63, 127, 128, 255, 300, 16383, 16384,
                          100000, 2097151, 2097152, 10000000, 268435455};
    for (int v : values) {
        QByteArray encoded = encodeRemainingLength(v);
        int pos = 0;
        int decoded = 0;
        ASSERT_TRUE(decodeRemainingLength(encoded, pos, decoded));
        EXPECT_EQ(decoded, v);
    }
}

TEST(RemainingLength, DecodeTruncatedReturnsFalse)
{
    int pos = 0;
    int length = -1;
    QByteArray data = QByteArray::fromHex("80");  // 只有继续位，无后续字节
    EXPECT_FALSE(decodeRemainingLength(data, pos, length));
    EXPECT_EQ(length, 0);   // 失败时输出被重置为0
    EXPECT_EQ(pos, 0);      // 失败时pos不前进
}

TEST(RemainingLength, DecodeFourContinuationBytesReturnsFalse)
{
    // MQTT规范规定剩余长度最多4字节，第4字节仍带继续位即为格式错误
    int pos = 0;
    int length = 0;
    EXPECT_FALSE(decodeRemainingLength(QByteArray::fromHex("80808080"), pos, length));
    EXPECT_EQ(pos, 0);
}

// ---------- UTF8字符串（2字节长度前缀） ----------

TEST(Utf8String, EncodeEmpty)
{
    EXPECT_EQ(encodeUtf8String(QByteArray()), QByteArray::fromHex("0000"));
}

TEST(Utf8String, EncodeAscii)
{
    EXPECT_EQ(encodeUtf8String("abc"), QByteArray::fromHex("0003616263"));
}

TEST(Utf8String, EncodeUtf8Chinese)
{
    // 长度按UTF8字节数计算："你好"占6字节
    QByteArray encoded = encodeUtf8String(QStringLiteral("你好").toUtf8());
    EXPECT_EQ(encoded.size(), 8);
    EXPECT_EQ(encoded.left(2), QByteArray::fromHex("0006"));
    EXPECT_EQ(encoded.mid(2), QStringLiteral("你好").toUtf8());
}

TEST(Utf8String, DecodeRoundTrip)
{
    QByteArray original = QStringLiteral("设备1/sensor").toUtf8();
    QByteArray data = encodeUtf8String(original);
    int pos = 0;
    QByteArray out;
    ASSERT_TRUE(decodeUtf8String(data, pos, out));
    EXPECT_EQ(out, original);
    EXPECT_EQ(pos, data.size());
}

TEST(Utf8String, DecodeTruncatedHeader)
{
    int pos = 0;
    QByteArray out;
    EXPECT_FALSE(decodeUtf8String(QByteArray::fromHex("00"), pos, out));  // 缺1字节长度头
}

TEST(Utf8String, DecodeTruncatedContent)
{
    int pos = 0;
    QByteArray out;
    // 声明5字节内容但只有1字节数据
    EXPECT_FALSE(decodeUtf8String(QByteArray::fromHex("000561"), pos, out));
    EXPECT_EQ(pos, 0);
}

// ---------- 报文整体编解码 ----------

TEST(PacketCodec, EncodeEmptyBodyPacket)
{
    EXPECT_EQ(encodePacket(PingReq, QByteArray()), QByteArray::fromHex("c000"));
    EXPECT_EQ(encodePacket(PingResp, QByteArray()), QByteArray::fromHex("d000"));
    EXPECT_EQ(encodePacket(Disconnect, QByteArray()), QByteArray::fromHex("e000"));
}

TEST(PacketCodec, EncodeDecodeRoundTrip)
{
    // 300字节报文体，触发2字节剩余长度
    QByteArray body = QByteArray(300, 'x');
    QByteArray packet = encodePacket(Publish, body);
    Decoded d = decodeOne(packet);
    ASSERT_EQ(d.result, DecodeResult::Ok);
    EXPECT_EQ(d.typeAndFlags, static_cast<quint8>(Publish));
    EXPECT_EQ(d.body, body);
}

TEST(PacketCodec, DecodeRemainingLengthBoundaryBodies)
{
    for (int bodySize : {127, 128, 16383, 16384}) {
        QByteArray packet = encodePacket(Publish, QByteArray(bodySize, 'y'));
        Decoded d = decodeOne(packet);
        ASSERT_EQ(d.result, DecodeResult::Ok);
        EXPECT_EQ(d.body.size(), bodySize);
    }
}

TEST(PacketCodec, DecodeEmptyBufferIncomplete)
{
    Decoded d = decodeOne(QByteArray());
    EXPECT_EQ(d.result, DecodeResult::Incomplete);
}

TEST(PacketCodec, DecodeHeaderOnlyIncomplete)
{
    Decoded d = decodeOne(QByteArray::fromHex("30"));
    EXPECT_EQ(d.result, DecodeResult::Incomplete);
}

TEST(PacketCodec, DecodeMissingBodyIncomplete)
{
    // 剩余长度声明5字节但报文体缺失
    Decoded d = decodeOne(QByteArray::fromHex("3005"));
    EXPECT_EQ(d.result, DecodeResult::Incomplete);
}

TEST(PacketCodec, DecodeTruncatedBodyIncomplete)
{
    QByteArray packet = encodePacket(Publish, QByteArray(50, 'z'));
    Decoded d = decodeOne(packet.left(packet.size() - 3));  // 截断最后3字节
    EXPECT_EQ(d.result, DecodeResult::Incomplete);
}

TEST(PacketCodec, DecodeMalformedRemainingLength)
{
    // 剩余长度超过4字节：格式错误
    Decoded d = decodeOne(QByteArray::fromHex("30ffffffffff"));
    EXPECT_EQ(d.result, DecodeResult::Malformed);
}

TEST(PacketCodec, DecodeMultiplePacketsStream)
{
    // 模拟TCP流粘包：一次收到多个完整报文，pos依次前进
    QByteArray stream = makeConnectPacket("c1")
                      + makePingReqPacket()
                      + makePubAckPacket(0x0102);
    int pos = 0;
    quint8 typeAndFlags = 0;
    QByteArray body;

    ASSERT_EQ(decodePacket(stream, pos, typeAndFlags, body), DecodeResult::Ok);
    EXPECT_EQ(packetType(typeAndFlags), static_cast<quint8>(Connect));

    ASSERT_EQ(decodePacket(stream, pos, typeAndFlags, body), DecodeResult::Ok);
    EXPECT_EQ(typeAndFlags, static_cast<quint8>(PingReq));
    EXPECT_TRUE(body.isEmpty());

    ASSERT_EQ(decodePacket(stream, pos, typeAndFlags, body), DecodeResult::Ok);
    EXPECT_EQ(typeAndFlags, static_cast<quint8>(PubAck));
    EXPECT_EQ(body, QByteArray::fromHex("0102"));
    EXPECT_EQ(pos, stream.size());  // 全部消费完毕
}

// ---------- 主题过滤器匹配（MQTT 3.1.1规范示例） ----------

TEST(TopicMatch, ExactMatch)
{
    EXPECT_TRUE(topicMatches("sport/tennis/player1", "sport/tennis/player1"));
    EXPECT_FALSE(topicMatches("sport/tennis/player1", "sport/tennis/player2"));
}

TEST(TopicMatch, HashMatchesEverything)
{
    EXPECT_TRUE(topicMatches("#", "sport"));
    EXPECT_TRUE(topicMatches("#", "sport/tennis/player1/ranking"));
    EXPECT_TRUE(topicMatches("#", QString()));  // 空主题也被#匹配
}

TEST(TopicMatch, HashMatchesZeroLevels)
{
    EXPECT_TRUE(topicMatches("sport/#", "sport"));          // #匹配零层
    EXPECT_TRUE(topicMatches("sport/tennis/#", "sport/tennis"));
}

TEST(TopicMatch, HashMatchesMultipleLevels)
{
    EXPECT_TRUE(topicMatches("sport/tennis/player1/#", "sport/tennis/player1"));
    EXPECT_TRUE(topicMatches("sport/tennis/player1/#", "sport/tennis/player1/ranking"));
    EXPECT_TRUE(topicMatches("sport/tennis/player1/#", "sport/tennis/player1/score/wimbledon"));
}

TEST(TopicMatch, PlusMatchesSingleLevel)
{
    EXPECT_TRUE(topicMatches("sport/tennis/+", "sport/tennis/player1"));
    EXPECT_FALSE(topicMatches("sport/tennis/+", "sport/tennis/player1/ranking"));  // +只匹配一层
}

TEST(TopicMatch, PlusMatchesTrailingEmptyLevel)
{
    // MQTT规范："sport/+"匹配"sport/"（尾随空层级是有效层级）
    EXPECT_TRUE(topicMatches("sport/+", "sport/"));
}

TEST(TopicMatch, PlusDoesNotMatchZeroLevels)
{
    EXPECT_FALSE(topicMatches("sport/+", "sport"));  // +必须匹配一层
}

TEST(TopicMatch, CombinedFilter)
{
    EXPECT_TRUE(topicMatches("+/tennis/#", "sport/tennis/player1"));
    EXPECT_TRUE(topicMatches("sport/+/player1", "sport/tennis/player1"));
    EXPECT_FALSE(topicMatches("sport/+/player1", "sport/tennis/player2"));
}

TEST(TopicMatch, LevelCountMismatch)
{
    EXPECT_FALSE(topicMatches("sport/tennis", "sport/tennis/player1"));
    EXPECT_FALSE(topicMatches("a/b/c/d", "a/b/c"));
}

// ---------- CONNECT / CONNACK报文构造 ----------

TEST(ConnectPacket, ExactBytes)
{
    // 协议名MQTT(6) + 级别4(1) + 连接标志02(1) + keepalive60(2) + 客户端ID "test"(6) = 16字节报文体
    QByteArray packet = makeConnectPacket("test", 60, true);
    EXPECT_EQ(packet, QByteArray::fromHex("101000044d5154540402003c000474657374"));
}

TEST(ConnectPacket, CleanSessionFalse)
{
    QByteArray packet = makeConnectPacket("t", 0, false);
    // 连接标志字节应为0x00（不清理会话）
    Decoded d = decodeOne(packet);
    ASSERT_EQ(d.result, DecodeResult::Ok);
    EXPECT_EQ(d.body.at(7), '\x00');
}

TEST(ConnectPacket, Utf8ClientId)
{
    QString clientId = QStringLiteral("客户端1");
    QByteArray packet = makeConnectPacket(clientId, 30, true);
    Decoded d = decodeOne(packet);
    ASSERT_EQ(d.result, DecodeResult::Ok);
    EXPECT_EQ(d.typeAndFlags, static_cast<quint8>(Connect));
    EXPECT_EQ(d.body.left(6), QByteArray::fromHex("00044d515454"));  // "MQTT"前缀
    EXPECT_EQ(d.body.at(6), '\x04');   // 协议级别 3.1.1
    // keepalive=30：位于协议名(6)+级别(1)+标志(1)之后
    EXPECT_EQ(static_cast<quint8>(d.body.at(8)), 0x00);
    EXPECT_EQ(static_cast<quint8>(d.body.at(9)), 0x1E);
    // 客户端ID在keepalive之后
    int pos = 10;
    QByteArray decodedId;
    ASSERT_TRUE(decodeUtf8String(d.body, pos, decodedId));
    EXPECT_EQ(decodedId, clientId.toUtf8());
}

TEST(ConnAckPacket, SessionPresentAndReturnCode)
{
    Decoded d = decodeOne(makeConnAckPacket(true, 0));
    ASSERT_EQ(d.result, DecodeResult::Ok);
    EXPECT_EQ(d.body, QByteArray::fromHex("0100"));

    d = decodeOne(makeConnAckPacket(false, 5));
    EXPECT_EQ(d.body, QByteArray::fromHex("0005"));
}

// ---------- Binary Data（2字节长度前缀 + 二进制内容） ----------

TEST(BinaryData, EncodeEmpty)
{
    EXPECT_EQ(encodeBinaryData(QByteArray()), QByteArray::fromHex("0000"));
}

TEST(BinaryData, EncodeDecodeRoundTripWithBinaryBytes)
{
    // 含0x00/0xFF的任意字节流须原样保留（与UTF8字符串区分：内容不校验编码）
    QByteArray raw = QByteArray::fromHex("00ff4100");
    QByteArray data = encodeBinaryData(raw);
    int pos = 0;
    QByteArray out;
    ASSERT_TRUE(decodeBinaryData(data, pos, out));
    EXPECT_EQ(out, raw);
    EXPECT_EQ(pos, data.size());
}

TEST(BinaryData, DecodeTruncated)
{
    int pos = 0;
    QByteArray out;
    EXPECT_FALSE(decodeBinaryData(QByteArray::fromHex("00"), pos, out));      // 缺1字节长度头
    EXPECT_FALSE(decodeBinaryData(QByteArray::fromHex("00050001"), pos, out)); // 声明5字节只有2字节
}

// ---------- 遗嘱（Will）CONNECT报文编码 ----------

TEST(WillConnect, ExactBytes)
{
    // 客户端ID"c1" + keepalive60 + 遗嘱主题"will/topic"(QoS1不保留, 消息"offline")
    WillConfig will{"will/topic", "offline", 1, false};
    QByteArray packet = makeConnectPacket("c1", 60, true, will);
    // 连接标志0x0E = 清理会话(0x02) | Will Flag(0x04) | Will QoS1(0x08)
    EXPECT_EQ(packet,
              QByteArray::fromHex("102300044d515454040e003c00026331"
                                  "000a77696c6c2f746f70696300076f66666c696e65"));
}

TEST(WillConnect, Qos2RetainFlags)
{
    WillConfig will{"w", "", 2, true};
    QByteArray packet = makeConnectPacket("c", 10, false, will);
    Decoded d = decodeOne(packet);
    ASSERT_EQ(d.result, DecodeResult::Ok);
    // 0x34 = Will Flag(0x04) | Will QoS2(0x10) | Will Retain(0x20)，不清理会话
    EXPECT_EQ(static_cast<quint8>(d.body.at(7)), 0x34);
}

TEST(WillConnect, EmptyTopicDisablesWill)
{
    // 遗嘱主题为空 = 不启用遗嘱，报文应与不带遗嘱参数时完全一致
    WillConfig will{"", "x", 1, true};
    EXPECT_EQ(makeConnectPacket("c1", 60, true, will),
              makeConnectPacket("c1", 60, true));
}

TEST(WillConnect, ChineseTopicRoundTrip)
{
    WillConfig will{QStringLiteral("遗嘱/主题"), QStringLiteral("设备离线").toUtf8(), 0, false};
    ConnectInfo info = decodeConnectPacket(makeConnectPacket("dev", 30, true, will));
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.will.topic, QStringLiteral("遗嘱/主题"));
    EXPECT_EQ(info.will.message, QStringLiteral("设备离线").toUtf8());
}

// ---------- CONNECT报文解码 ----------

TEST(ConnectDecode, RoundTripWithWill)
{
    WillConfig will{"dev/status", "offline", 2, true};
    ConnectInfo info = decodeConnectPacket(makeConnectPacket("dev1", 90, false, will));
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.clientId, "dev1");
    EXPECT_EQ(info.keepAliveSeconds, 90);
    EXPECT_FALSE(info.cleanSession);
    EXPECT_TRUE(info.willFlag);
    EXPECT_EQ(info.will.qos, 2);
    EXPECT_TRUE(info.will.retain);
    EXPECT_EQ(info.will.topic, "dev/status");
    EXPECT_EQ(info.will.message, "offline");
}

TEST(ConnectDecode, RoundTripNoWill)
{
    ConnectInfo info = decodeConnectPacket(makeConnectPacket("dev2", 120, true));
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.clientId, "dev2");
    EXPECT_EQ(info.keepAliveSeconds, 120);
    EXPECT_TRUE(info.cleanSession);
    EXPECT_FALSE(info.willFlag);
}

TEST(ConnectDecode, BinaryWillMessagePreserved)
{
    // 遗嘱消息是Binary Data：非UTF8字节（0x00/0xFF）须原样保留
    QByteArray raw = QByteArray::fromHex("00ff4100");
    WillConfig will{"status", raw, 1, false};
    ConnectInfo info = decodeConnectPacket(makeConnectPacket("dev", 60, true, will));
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.will.message, raw);
}

TEST(ConnectDecode, RejectsNonConnectPacket)
{
    EXPECT_FALSE(decodeConnectPacket(makePingReqPacket()).valid);
    EXPECT_FALSE(decodeConnectPacket(makePublishPacket("a", "b")).valid);
}

TEST(ConnectDecode, RejectsBadProtocolName)
{
    QByteArray body = encodeUtf8String("MQTX") + QByteArray::fromHex("0402003c")
                    + encodeUtf8String("c");
    EXPECT_FALSE(decodeConnectPacket(encodePacket(Connect, body)).valid);
}

TEST(ConnectDecode, RejectsBadProtocolLevel)
{
    QByteArray body = encodeUtf8String("MQTT") + QByteArray::fromHex("0502003c")
                    + encodeUtf8String("c");
    EXPECT_FALSE(decodeConnectPacket(encodePacket(Connect, body)).valid);
}

TEST(ConnectDecode, RejectsReservedFlagBit)
{
    // 连接标志位0（保留位）必须为0
    QByteArray body = encodeUtf8String("MQTT") + QByteArray::fromHex("0403003c")
                    + encodeUtf8String("c");
    EXPECT_FALSE(decodeConnectPacket(encodePacket(Connect, body)).valid);
}

TEST(ConnectDecode, RejectsWillFlagWithoutFields)
{
    // Will Flag=1但载荷中缺少遗嘱主题/消息
    QByteArray body = encodeUtf8String("MQTT") + QByteArray::fromHex("0406003c")
                    + encodeUtf8String("c");
    EXPECT_FALSE(decodeConnectPacket(encodePacket(Connect, body)).valid);
}

TEST(ConnectDecode, RejectsQos3Will)
{
    // 遗嘱QoS=3非法（0x1E = 清理会话|Will Flag|QoS3）
    QByteArray extra = encodeUtf8String("w") + encodeBinaryData("m");
    QByteArray body = encodeUtf8String("MQTT") + QByteArray::fromHex("041e003c")
                    + encodeUtf8String("c") + extra;
    EXPECT_FALSE(decodeConnectPacket(encodePacket(Connect, body)).valid);
}

TEST(ConnectDecode, RejectsWillBitsWithoutWillFlag)
{
    // 规范要求：Will Flag=0时Will QoS/Retain位必须为0（0x0A=清理会话|Will QoS1位）
    QByteArray body = encodeUtf8String("MQTT") + QByteArray::fromHex("040a003c")
                    + encodeUtf8String("c");
    EXPECT_FALSE(decodeConnectPacket(encodePacket(Connect, body)).valid);
}

TEST(ConnectDecode, SkipsUserNamePasswordFields)
{
    // 用户名/密码标志：仅按格式跳过，报文仍应解析成功
    QByteArray extra = encodeUtf8String("user") + encodeBinaryData("pass");
    QByteArray body = encodeUtf8String("MQTT") + QByteArray::fromHex("04c2003c")
                    + encodeUtf8String("c") + extra;
    ConnectInfo info = decodeConnectPacket(encodePacket(Connect, body));
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.clientId, "c");
}

TEST(ConnectDecode, RejectsTruncatedPacket)
{
    QByteArray packet = makeConnectPacket("c1", 60, true,
                                          WillConfig{"w/t", "msg", 0, false});
    EXPECT_FALSE(decodeConnectPacket(packet.left(packet.size() - 2)).valid);
}

// ---------- SUBSCRIBE / UNSUBSCRIBE报文构造 ----------

TEST(SubscribePacket, ExactBytes)
{
    // 报文标识符0x0102 + 主题"a/b" + QoS1，固定标志0010
    QByteArray packet = makeSubscribePacket(0x0102, "a/b", 1);
    EXPECT_EQ(packet, QByteArray::fromHex("820801020003612f6201"));
}

TEST(SubscribePacket, FixedFlags)
{
    QByteArray packet = makeSubscribePacket(1, "t", 2);
    Decoded d = decodeOne(packet);
    ASSERT_EQ(d.result, DecodeResult::Ok);
    EXPECT_EQ(d.typeAndFlags, 0x82);  // SUBSCRIBE | 0010
    EXPECT_EQ(d.body.last(1).at(0), '\x02');  // QoS按0x03掩码
}

TEST(SubAckPacket, ExactBytes)
{
    QByteArray packet = makeSubAckPacket(1, {0, 1, 2});
    EXPECT_EQ(packet, QByteArray::fromHex("90050001000102"));
}

TEST(UnsubscribePacket, ExactBytes)
{
    QByteArray packet = makeUnsubscribePacket(1, "a/b");
    EXPECT_EQ(packet, QByteArray::fromHex("a20700010003612f62"));
}

TEST(UnsubAckPacket, ExactBytes)
{
    EXPECT_EQ(makeUnsubAckPacket(1), QByteArray::fromHex("b0020001"));
}

// ---------- PUBLISH及QoS确认报文构造 ----------

TEST(PublishPacket, Qos0NoRetain)
{
    QByteArray packet = makePublishPacket("t", "hi", 0, false, 0);
    EXPECT_EQ(packet, QByteArray::fromHex("30050001746869"));
}

TEST(PublishPacket, Qos1WithPacketId)
{
    QByteArray packet = makePublishPacket("t", "hi", 1, false, 0x0102);
    EXPECT_EQ(packet, QByteArray::fromHex("320700017401026869"));  // 标志位QoS1=0010
}

TEST(PublishPacket, Qos2Flags)
{
    QByteArray packet = makePublishPacket("t", "hi", 2, false, 0x0102);
    Decoded d = decodeOne(packet);
    ASSERT_EQ(d.result, DecodeResult::Ok);
    EXPECT_EQ(packetFlags(d.typeAndFlags), 0x04);  // QoS2标志位0100
}

TEST(PublishPacket, RetainFlags)
{
    Decoded d = decodeOne(makePublishPacket("t", "x", 0, true, 0));
    EXPECT_EQ(packetFlags(d.typeAndFlags), 0x01);  // retain置位

    d = decodeOne(makePublishPacket("t", "x", 1, true, 1));
    EXPECT_EQ(packetFlags(d.typeAndFlags), 0x03);  // QoS1 + retain
}

TEST(PublishPacket, DecodeKeepsTopicPayloadAndPacketId)
{
    QByteArray packet = makePublishPacket("sensor/temp", "25.6", 1, false, 0xABCD);
    Decoded d = decodeOne(packet);
    ASSERT_EQ(d.result, DecodeResult::Ok);
    EXPECT_EQ(d.typeAndFlags, 0x32);

    int pos = 0;
    QByteArray topic;
    ASSERT_TRUE(decodeUtf8String(d.body, pos, topic));
    EXPECT_EQ(topic, "sensor/temp");
    // QoS>0时报文标识符位于主题之后
    EXPECT_EQ(static_cast<quint8>(d.body.at(pos)), 0xAB);
    EXPECT_EQ(static_cast<quint8>(d.body.at(pos + 1)), 0xCD);
    EXPECT_EQ(d.body.mid(pos + 2), "25.6");
}

TEST(PubAckPackets, ExactBytes)
{
    EXPECT_EQ(makePubAckPacket(0x0102), QByteArray::fromHex("40020102"));
    EXPECT_EQ(makePubRecPacket(0x0102), QByteArray::fromHex("50020102"));
    EXPECT_EQ(makePubRelPacket(0x0102), QByteArray::fromHex("62020102"));  // 标志位固定0010
    EXPECT_EQ(makePubCompPacket(0x0102), QByteArray::fromHex("70020102"));
}

// ---------- PUBLISH报文解码 ----------

TEST(PublishDecode, RoundTripQos0)
{
    QByteArray packet = makePublishPacket("a/b", "hi", 0, true);
    Decoded d = decodeOne(packet);
    ASSERT_EQ(d.result, DecodeResult::Ok);
    PublishInfo info = decodePublishPacket(d.typeAndFlags, d.body);
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.topic, "a/b");
    EXPECT_EQ(info.payload, "hi");
    EXPECT_EQ(info.qos, 0);
    EXPECT_TRUE(info.retain);
    EXPECT_EQ(info.packetId, 0);  // QoS0无报文标识符
}

TEST(PublishDecode, RoundTripQos2WithBinaryPayload)
{
    QByteArray raw = QByteArray::fromHex("00ff4100");  // 二进制消息体
    QByteArray packet = makePublishPacket(QStringLiteral("中文/主题"), raw, 2, false, 0xABCD);
    Decoded d = decodeOne(packet);
    ASSERT_EQ(d.result, DecodeResult::Ok);
    PublishInfo info = decodePublishPacket(d.typeAndFlags, d.body);
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.topic, QStringLiteral("中文/主题"));
    EXPECT_EQ(info.payload, raw);
    EXPECT_EQ(info.qos, 2);
    EXPECT_EQ(info.packetId, 0xABCD);
    EXPECT_FALSE(info.retain);
}

TEST(PublishDecode, RejectsNonPublishType)
{
    // 传入非PUBLISH报文体应解析失败
    PublishInfo info = decodePublishPacket(PingReq, QByteArray::fromHex("0001746869"));
    EXPECT_FALSE(info.valid);
}

TEST(PublishDecode, RejectsQos3)
{
    // 0x36 = PUBLISH | QoS3(0110)，QoS=3非法
    PublishInfo info = decodePublishPacket(0x36, QByteArray::fromHex("0001746869"));
    EXPECT_FALSE(info.valid);
}

TEST(PublishDecode, RejectsMissingPacketId)
{
    // QoS1但报文体只到主题，缺少报文标识符
    PublishInfo info = decodePublishPacket(0x32, QByteArray::fromHex("000174"));
    EXPECT_FALSE(info.valid);
}

TEST(PublishDecode, RejectsTruncatedTopic)
{
    PublishInfo info = decodePublishPacket(0x30, QByteArray::fromHex("000561"));
    EXPECT_FALSE(info.valid);
}

// ---------- 心跳与断开报文 ----------

TEST(HeaderOnlyPackets, ExactBytes)
{
    EXPECT_EQ(makePingReqPacket(), QByteArray::fromHex("c000"));
    EXPECT_EQ(makePingRespPacket(), QByteArray::fromHex("d000"));
    EXPECT_EQ(makeDisconnectPacket(), QByteArray::fromHex("e000"));
}

// ---------- 各构造报文均可被解码器正确解析 ----------

TEST(AllConstructors, RoundTripThroughDecoder)
{
    const QList<QByteArray> packets = {
        makeConnectPacket("roundtrip", 120, true),
        makeConnAckPacket(true, 0),
        makeSubscribePacket(7, "a/b/c", 1),
        makeSubAckPacket(7, {0, 1}),
        makeUnsubscribePacket(8, "a/b/c"),
        makeUnsubAckPacket(8),
        makePublishPacket("a/b", "payload", 2, true, 9),
        makePubAckPacket(9),
        makePubRecPacket(9),
        makePubCompPacket(9),
        makePingReqPacket(),
        makePingRespPacket(),
        makeDisconnectPacket(),
    };
    for (const QByteArray &packet : packets) {
        Decoded d = decodeOne(packet);
        ASSERT_EQ(d.result, DecodeResult::Ok);
        EXPECT_GE(packetType(d.typeAndFlags), static_cast<quint8>(Connect));  // 类型合法
        EXPECT_LE(packetType(d.typeAndFlags), static_cast<quint8>(Disconnect));
    }
}
