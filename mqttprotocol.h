#ifndef MQTTPROTOCOL_H
#define MQTTPROTOCOL_H

#include <QByteArray>
#include <QString>
#include <QList>

// MQTT 3.1.1 协议编解码工具（MQTT客户端与服务器共用）
namespace MqttProtocol {

// 控制报文类型（固定报头高4位）
enum PacketType : quint8 {
    Connect     = 0x10, // 客户端请求连接
    ConnAck     = 0x20, // 连接确认
    Publish     = 0x30, // 发布消息
    PubAck      = 0x40, // QoS1发布确认
    PubRec      = 0x50, // QoS2发布收到
    PubRel      = 0x60, // QoS2发布释放
    PubComp     = 0x70, // QoS2发布完成
    Subscribe   = 0x80, // 订阅主题
    SubAck      = 0x90, // 订阅确认
    Unsubscribe = 0xA0, // 取消订阅
    UnsubAck    = 0xB0, // 取消订阅确认
    PingReq     = 0xC0, // 心跳请求
    PingResp    = 0xD0, // 心跳响应
    Disconnect  = 0xE0  // 断开连接
};

// 报文解析结果：完整 / 数据不足 / 格式错误
enum class DecodeResult { Ok, Incomplete, Malformed };

quint8 packetType(quint8 typeAndFlags);   // 取高4位类型
quint8 packetFlags(quint8 typeAndFlags);  // 取低4位标志

QByteArray encodeRemainingLength(int length);                      // 变长整数编码
bool decodeRemainingLength(const QByteArray &data, int &pos, int &length); // 变长整数解码

QByteArray encodeUtf8String(const QByteArray &utf8);               // 2字节长度+UTF8内容
bool decodeUtf8String(const QByteArray &data, int &pos, QByteArray &out);

// 从data的pos处解析一个完整报文；Ok时pos前进到该报文末尾
DecodeResult decodePacket(const QByteArray &data, int &pos,
                          quint8 &typeAndFlags, QByteArray &body);
QByteArray encodePacket(quint8 typeAndFlags, const QByteArray &body); // 组装完整报文

// 主题过滤匹配，支持通配符 '+'（单层）和 '#'（多层）
bool topicMatches(const QString &filter, const QString &topic);

// 报文构造便捷函数
QByteArray makeConnectPacket(const QString &clientId, quint16 keepAliveSeconds = 60,
                             bool cleanSession = true);
QByteArray makeConnAckPacket(bool sessionPresent, quint8 returnCode);
QByteArray makeSubscribePacket(quint16 packetId, const QString &topicFilter, quint8 qos = 0);
QByteArray makeSubAckPacket(quint16 packetId, const QList<quint8> &grantedQos);
QByteArray makeUnsubscribePacket(quint16 packetId, const QString &topicFilter);
QByteArray makeUnsubAckPacket(quint16 packetId);
QByteArray makePublishPacket(const QString &topic, const QByteArray &payload,
                             quint8 qos = 0, bool retain = false, quint16 packetId = 0);
QByteArray makePubAckPacket(quint16 packetId);
QByteArray makePubRecPacket(quint16 packetId);
QByteArray makePubCompPacket(quint16 packetId);
QByteArray makePingReqPacket();
QByteArray makePingRespPacket();
QByteArray makeDisconnectPacket();

} // namespace MqttProtocol

#endif // MQTTPROTOCOL_H
