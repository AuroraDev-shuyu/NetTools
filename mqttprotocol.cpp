#include "mqttprotocol.h"

#include <QtEndian>

namespace MqttProtocol {

// 取高4位报文类型
quint8 packetType(quint8 typeAndFlags)
{
    return typeAndFlags & 0xF0;
}

// 取低4位标志位
quint8 packetFlags(quint8 typeAndFlags)
{
    return typeAndFlags & 0x0F;
}

// 剩余长度变长整数编码：每字节低7位有效，最高位为继续标志
QByteArray encodeRemainingLength(int length)
{
    QByteArray out;
    do {
        quint8 byte = length % 128;
        length /= 128;
        if (length > 0) {
            byte |= 0x80;
        }
        out.append(static_cast<char>(byte));
    } while (length > 0);
    return out;
}

// 剩余长度变长整数解码，最多4字节；成功时pos前进到变长整数之后
bool decodeRemainingLength(const QByteArray &data, int &pos, int &length)
{
    length = 0;
    int multiplier = 1;
    for (int i = 0; i < 4; ++i) {
        if (pos + i >= data.size()) {
            return false; // 数据不足
        }
        quint8 byte = static_cast<quint8>(data.at(pos + i));
        length += (byte & 0x7F) * multiplier;
        if ((byte & 0x80) == 0) {
            pos += i + 1;
            return true;
        }
        multiplier *= 128;
    }
    return false; // 超过4字节，格式错误
}

// 2字节大端长度 + UTF8内容
QByteArray encodeUtf8String(const QByteArray &utf8)
{
    QByteArray out;
    out.append(static_cast<char>((utf8.size() >> 8) & 0xFF));
    out.append(static_cast<char>(utf8.size() & 0xFF));
    out.append(utf8);
    return out;
}

// 解码UTF8字符串（含2字节长度前缀），成功时pos前进
bool decodeUtf8String(const QByteArray &data, int &pos, QByteArray &out)
{
    if (pos + 2 > data.size()) {
        return false;
    }
    int len = (static_cast<quint8>(data.at(pos)) << 8) | static_cast<quint8>(data.at(pos + 1));
    if (pos + 2 + len > data.size()) {
        return false;
    }
    out = data.mid(pos + 2, len);
    pos += 2 + len;
    return true;
}

// 从data的pos处解析一个完整报文（固定报头 + 剩余长度 + 报文体）
DecodeResult decodePacket(const QByteArray &data, int &pos,
                          quint8 &typeAndFlags, QByteArray &body)
{
    if (pos >= data.size()) {
        return DecodeResult::Incomplete;
    }

    typeAndFlags = static_cast<quint8>(data.at(pos));
    int headerPos = pos + 1;

    int bodyLength = 0;
    if (!decodeRemainingLength(data, headerPos, bodyLength)) {
        // 无法区分"数据不足"与"格式错误"：若已有4字节且仍带继续位则为格式错误
        if (data.size() - pos - 1 >= 4) {
            return DecodeResult::Malformed;
        }
        return DecodeResult::Incomplete;
    }

    if (headerPos + bodyLength > data.size()) {
        return DecodeResult::Incomplete;
    }

    body = data.mid(headerPos, bodyLength);
    pos = headerPos + bodyLength;
    return DecodeResult::Ok;
}

// 组装完整报文：固定报头 + 变长剩余长度 + 报文体
QByteArray encodePacket(quint8 typeAndFlags, const QByteArray &body)
{
    QByteArray out;
    out.append(static_cast<char>(typeAndFlags));
    out.append(encodeRemainingLength(body.size()));
    out.append(body);
    return out;
}

// 主题过滤匹配：'#'匹配剩余所有层级，'+'匹配单个层级
bool topicMatches(const QString &filter, const QString &topic)
{
    const QStringList filterLevels = filter.split('/', Qt::SkipEmptyParts);
    const QStringList topicLevels = topic.split('/', Qt::SkipEmptyParts);

    for (int i = 0; i < filterLevels.size(); ++i) {
        const QString &level = filterLevels.at(i);
        if (level == QLatin1String("#")) {
            return true; // '#'匹配剩余所有层级（含零层）
        }
        if (i >= topicLevels.size()) {
            return false;
        }
        if (level != QLatin1String("+") && level != topicLevels.at(i)) {
            return false;
        }
    }
    return filterLevels.size() == topicLevels.size();
}

// CONNECT报文：协议名MQTT、协议级别4(3.1.1)、清理会话、keepalive、客户端ID
QByteArray makeConnectPacket(const QString &clientId, quint16 keepAliveSeconds, bool cleanSession)
{
    QByteArray body;
    body.append(encodeUtf8String("MQTT"));          // 协议名
    body.append(static_cast<char>(0x04));           // 协议级别：MQTT 3.1.1
    body.append(static_cast<char>(cleanSession ? 0x02 : 0x00)); // 连接标志：清理会话
    body.append(static_cast<char>((keepAliveSeconds >> 8) & 0xFF));
    body.append(static_cast<char>(keepAliveSeconds & 0xFF));
    body.append(encodeUtf8String(clientId.toUtf8())); // 客户端ID
    return encodePacket(Connect, body);
}

// CONNACK报文：会话存在标志 + 返回码（0=接受）
QByteArray makeConnAckPacket(bool sessionPresent, quint8 returnCode)
{
    QByteArray body;
    body.append(static_cast<char>(sessionPresent ? 0x01 : 0x00));
    body.append(static_cast<char>(returnCode));
    return encodePacket(ConnAck, body);
}

// SUBSCRIBE报文：报文标识符 + (主题过滤器, QoS)
QByteArray makeSubscribePacket(quint16 packetId, const QString &topicFilter, quint8 qos)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    body.append(encodeUtf8String(topicFilter.toUtf8()));
    body.append(static_cast<char>(qos & 0x03));
    return encodePacket(static_cast<quint8>(Subscribe | 0x02), body); // 标志位固定0010
}

// SUBACK报文：报文标识符 + 授权QoS列表
QByteArray makeSubAckPacket(quint16 packetId, const QList<quint8> &grantedQos)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    for (quint8 qos : grantedQos) {
        body.append(static_cast<char>(qos));
    }
    return encodePacket(SubAck, body);
}

// UNSUBSCRIBE报文：报文标识符 + 主题过滤器列表
QByteArray makeUnsubscribePacket(quint16 packetId, const QString &topicFilter)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    body.append(encodeUtf8String(topicFilter.toUtf8()));
    return encodePacket(static_cast<quint8>(Unsubscribe | 0x02), body); // 标志位固定0010
}

// UNSUBACK报文
QByteArray makeUnsubAckPacket(quint16 packetId)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    return encodePacket(UnsubAck, body);
}

// PUBLISH报文：主题 + [报文标识符(QoS>0时)] + 消息体
QByteArray makePublishPacket(const QString &topic, const QByteArray &payload,
                             quint8 qos, bool retain, quint16 packetId)
{
    QByteArray body;
    body.append(encodeUtf8String(topic.toUtf8()));
    if (qos > 0) {
        body.append(static_cast<char>((packetId >> 8) & 0xFF));
        body.append(static_cast<char>(packetId & 0xFF));
    }
    body.append(payload);

    quint8 flags = static_cast<quint8>((qos & 0x03) << 1) | (retain ? 0x01 : 0x00);
    return encodePacket(static_cast<quint8>(Publish | flags), body);
}

// PUBACK / PUBREC / PUBCOMP报文：2字节报文标识符
QByteArray makePubAckPacket(quint16 packetId)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    return encodePacket(PubAck, body);
}

QByteArray makePubRecPacket(quint16 packetId)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    return encodePacket(PubRec, body);
}

QByteArray makePubCompPacket(quint16 packetId)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    return encodePacket(PubComp, body);
}

// PINGREQ / PINGRESP / DISCONNECT：仅固定报头，无报文体
QByteArray makePingReqPacket()
{
    return encodePacket(PingReq, QByteArray());
}

QByteArray makePingRespPacket()
{
    return encodePacket(PingResp, QByteArray());
}

QByteArray makeDisconnectPacket()
{
    return encodePacket(Disconnect, QByteArray());
}

} // namespace MqttProtocol
