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

// 2字节大端长度 + 二进制内容（MQTT Binary Data类型）
QByteArray encodeBinaryData(const QByteArray &data)
{
    QByteArray out;
    out.append(static_cast<char>((data.size() >> 8) & 0xFF));
    out.append(static_cast<char>(data.size() & 0xFF));
    out.append(data);
    return out;
}

// 解码二进制数据（含2字节长度前缀），成功时pos前进
bool decodeBinaryData(const QByteArray &data, int &pos, QByteArray &out)
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

// UTF8字符串与Binary Data同编码方式（长度前缀+内容），语义上前者内容必须是UTF8
QByteArray encodeUtf8String(const QByteArray &utf8)
{
    return encodeBinaryData(utf8);
}

bool decodeUtf8String(const QByteArray &data, int &pos, QByteArray &out)
{
    return decodeBinaryData(data, pos, out);
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
    // 保留空层级：MQTT规范中尾随空层级是有效层级，如"sport/+"须匹配"sport/"
    const QStringList filterLevels = filter.split('/', Qt::KeepEmptyParts);
    const QStringList topicLevels = topic.split('/', Qt::KeepEmptyParts);

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

// CONNECT报文：协议名MQTT、协议级别4(3.1.1)、连接标志、keepalive、客户端ID、[遗嘱主题, 遗嘱消息]
QByteArray makeConnectPacket(const QString &clientId, quint16 keepAliveSeconds,
                             bool cleanSession, const WillConfig &will)
{
    QByteArray body;
    body.append(encodeUtf8String("MQTT"));          // 协议名
    body.append(static_cast<char>(0x04));           // 协议级别：MQTT 3.1.1
    quint8 flags = cleanSession ? 0x02 : 0x00;      // 连接标志：清理会话
    const bool willEnabled = will.isEnabled();
    if (willEnabled) {
        flags |= 0x04;                                        // Will Flag
        flags |= static_cast<quint8>((will.qos & 0x03) << 3); // Will QoS（2位）
        if (will.retain) {
            flags |= 0x20;                                    // Will Retain
        }
    }
    body.append(static_cast<char>(flags));
    body.append(static_cast<char>((keepAliveSeconds >> 8) & 0xFF));
    body.append(static_cast<char>(keepAliveSeconds & 0xFF));
    body.append(encodeUtf8String(clientId.toUtf8())); // 客户端ID
    if (willEnabled) {
        body.append(encodeUtf8String(will.topic.toUtf8())); // 遗嘱主题
        body.append(encodeBinaryData(will.message));        // 遗嘱消息体（Binary Data类型）
    }
    return encodePacket(Connect, body);
}

// 解析CONNECT报文：校验协议名/级别/标志位，提取客户端ID、keepalive与遗嘱配置
ConnectInfo decodeConnectPacket(const QByteArray &packet)
{
    ConnectInfo info;
    int packetPos = 0;
    quint8 typeAndFlags = 0;
    QByteArray body;
    if (decodePacket(packet, packetPos, typeAndFlags, body) != DecodeResult::Ok ||
        packetType(typeAndFlags) != Connect) {
        return info; // 不是完整合法的CONNECT报文
    }

    int pos = 0;
    QByteArray protocolName;
    if (!decodeUtf8String(body, pos, protocolName) || protocolName != "MQTT") {
        return info; // 协议名必须是"MQTT"
    }
    if (pos >= body.size() || static_cast<quint8>(body.at(pos)) != 0x04) {
        return info; // 仅支持协议级别4（MQTT 3.1.1）
    }
    ++pos;
    if (pos >= body.size()) {
        return info;
    }
    const quint8 flags = static_cast<quint8>(body.at(pos++));
    if ((flags & 0x01) != 0) {
        return info; // 保留位必须为0
    }
    if (pos + 2 > body.size()) {
        return info;
    }
    info.keepAliveSeconds = (static_cast<quint8>(body.at(pos)) << 8) |
                            static_cast<quint8>(body.at(pos + 1));
    pos += 2;

    QByteArray clientIdBytes;
    if (!decodeUtf8String(body, pos, clientIdBytes)) {
        return info;
    }
    info.clientId = QString::fromUtf8(clientIdBytes);

    info.cleanSession = (flags & 0x02) != 0;
    info.willFlag = (flags & 0x04) != 0;
    if (info.willFlag) {
        info.will.qos = (flags >> 3) & 0x03;
        info.will.retain = (flags & 0x20) != 0;
        if (info.will.qos == 3) {
            return info; // 遗嘱QoS不能为3
        }
        QByteArray willTopic;
        if (!decodeUtf8String(body, pos, willTopic)) {
            return info;
        }
        info.will.topic = QString::fromUtf8(willTopic);
        if (!decodeBinaryData(body, pos, info.will.message)) {
            return info; // 遗嘱消息体缺失或截断
        }
    } else if (((flags >> 3) & 0x03) != 0 || (flags & 0x20) != 0) {
        return info; // 规范要求：无遗嘱时Will QoS/Will Retain必须为0
    }

    // 用户名/密码标志：本协议栈不处理认证，仅按格式校验并跳过
    if ((flags & 0x80) != 0) {
        QByteArray userName;
        if (!decodeUtf8String(body, pos, userName)) {
            return info;
        }
    }
    if ((flags & 0x40) != 0) {
        QByteArray password;
        if (!decodeBinaryData(body, pos, password)) {  // 密码是Binary Data类型
            return info;
        }
    }

    info.valid = true;
    return info;
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

// 解析PUBLISH报文（校验类型/标志位），提取主题、报文标识符与消息体
PublishInfo decodePublishPacket(quint8 typeAndFlags, const QByteArray &body)
{
    PublishInfo info;
    if (packetType(typeAndFlags) != Publish) {
        return info;
    }
    info.qos = (packetFlags(typeAndFlags) >> 1) & 0x03;
    info.retain = (packetFlags(typeAndFlags) & 0x01) != 0;
    if (info.qos == 3) {
        return info; // QoS=3非法
    }

    int pos = 0;
    QByteArray topicBytes;
    if (!decodeUtf8String(body, pos, topicBytes)) {
        return info;
    }
    info.topic = QString::fromUtf8(topicBytes);

    if (info.qos > 0) {
        if (pos + 2 > body.size()) {
            return info; // QoS1/2必须携带报文标识符
        }
        info.packetId = (static_cast<quint8>(body.at(pos)) << 8) |
                        static_cast<quint8>(body.at(pos + 1));
        pos += 2;
    }

    info.payload = body.mid(pos);
    info.valid = true;
    return info;
}

// PUBACK / PUBREC / PUBREL / PUBCOMP报文：2字节报文标识符
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

QByteArray makePubRelPacket(quint16 packetId)
{
    QByteArray body;
    body.append(static_cast<char>((packetId >> 8) & 0xFF));
    body.append(static_cast<char>(packetId & 0xFF));
    return encodePacket(static_cast<quint8>(PubRel | 0x02), body); // 标志位固定0010
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
