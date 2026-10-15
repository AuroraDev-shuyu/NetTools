#ifndef RTPPROTOCOL_H
#define RTPPROTOCOL_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

// RTP/RTCP 协议解析（RFC 3550），纯逻辑无 GUI 依赖，供表单与单元测试共用。
namespace RtpProtocol {

// ---------- RTP 数据包（RFC 3550 5.1） ----------
struct RtpPacket {
    bool valid = false;
    quint8 version = 0;
    bool padding = false;
    bool extension = false;
    quint8 csrcCount = 0;
    bool marker = false;
    quint8 payloadType = 0;
    quint16 seq = 0;
    quint32 timestamp = 0;
    quint32 ssrc = 0;
    QList<quint32> csrcs;
    int payloadOffset = 0;
    int payloadSize = 0;
    int paddingBytes = 0;   // 尾部填充字节数（含最后一个填充长度字节本身）
};

// 解析单个 RTP 报文；version!=2 或长度不足（含 CSRC/扩展头/填充校验）返回 false。
bool parseRtpPacket(const QByteArray &datagram, RtpPacket &pkt);

// 载荷类型名称：0-35 静态映射，96-127 动态，其余“未知”。
QString payloadTypeName(quint8 pt);

// ---------- RTCP（RFC 3550 6） ----------
// 判定依据（RFC 5761）：版本 2 且第二字节（报文类型）在 192-223。
bool isRtcp(const QByteArray &data);

// SR/RR 接收报告块
struct RtcpReportBlock {
    quint32 ssrc = 0;           // 被统计的源
    quint8 fractionLost = 0;    // 丢包比例 * 256
    qint32 cumulativeLost = 0;  // 24 位有符号累计丢包
    quint32 highestSeq = 0;     // 扩展最高序号（低 16 位为真实 seq）
    quint32 jitter = 0;
    quint32 lastSrTimestamp = 0;
    quint32 delaySinceLastSr = 0;
};

struct RtcpPacket {
    bool valid = false;
    quint8 type = 0;
    int lengthWords = 0;        // 长度字段（32 位字数，不含 4 字节头）
    // SR 发送者信息
    bool hasSenderInfo = false;
    quint32 senderSsrc = 0;
    quint64 ntpTimestamp = 0;
    quint32 rtpTimestamp = 0;
    quint32 packetCount = 0;
    quint32 octetCount = 0;
    QList<RtcpReportBlock> reports;
    // SDES
    QStringList cnames;
    // BYE
    QList<quint32> byeSsrcs;
    QString byeReason;
    // APP
    QByteArray appName;         // 4 字节应用名
    QByteArray rawData;         // 整块原始字节（含头）
};

// 解析 RTCP 复合报文（一个数据报可含多个 RTCP 块）；截断的尾块丢弃，未知类型仅保留头部。
QList<RtcpPacket> parseRtcp(const QByteArray &data);

QString rtcpTypeName(quint8 type);

// ---------- 单流统计（每个 SSRC 一组，RFC 3550 A.3/A.8） ----------
class RtpStreamStats
{
public:
    void update(const RtpPacket &pkt, qint64 arrivalMs);
    void reset();

    bool hasData() const { return m_packetCount > 0; }
    int packetCount() const { return m_packetCount; }
    qint64 octetCount() const { return m_octetCount; }
    quint32 ssrc() const { return m_ssrc; }
    quint8 payloadType() const { return m_payloadType; }
    qint64 expectedPackets() const; // 扩展序号空间内应收包数
    qint64 lostPackets() const;     // 累计丢包（含环绕）
    double lossRate() const;        // 丢包率百分比 0-100
    double jitter() const { return m_jitter; }  // 抖动（毫秒，按 90kHz 视频时钟换算）
    quint32 highestSeq() const { return m_maxSeq; }
    qint64 firstArrivalMs() const { return m_firstArrivalMs; }
    qint64 lastArrivalMs() const { return m_lastArrivalMs; }

private:
    quint32 m_ssrc = 0;
    quint8 m_payloadType = 0;
    int m_packetCount = 0;
    qint64 m_octetCount = 0;
    quint16 m_baseSeq = 0;
    quint16 m_maxSeq = 0;
    int m_cycles = 0;
    bool m_first = true;
    double m_jitter = 0.0;
    double m_lastTransit = 0.0;
    qint64 m_firstArrivalMs = 0;
    qint64 m_lastArrivalMs = 0;
};

} // namespace RtpProtocol

#endif // RTPPROTOCOL_H
