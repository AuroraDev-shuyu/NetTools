#include "rtpprotocol.h"

#include <QtGlobal>

namespace RtpProtocol {

static quint16 readU16(const QByteArray &d, int off)
{
    return quint16((quint16(quint8(d[off])) << 8) | quint8(d[off + 1]));
}

static quint32 readU32(const QByteArray &d, int off)
{
    return (quint32(quint8(d[off])) << 24) | (quint32(quint8(d[off + 1])) << 16)
           | (quint32(quint8(d[off + 2])) << 8) | quint32(quint8(d[off + 3]));
}

// ---------- RTP ----------

bool parseRtpPacket(const QByteArray &datagram, RtpPacket &pkt)
{
    pkt = RtpPacket();
    if (datagram.size() < 12) {
        return false;
    }
    const uchar b0 = uchar(datagram[0]);
    const uchar b1 = uchar(datagram[1]);
    pkt.version = b0 >> 6;
    pkt.padding = b0 & 0x20;
    pkt.extension = b0 & 0x10;
    pkt.csrcCount = b0 & 0x0F;
    pkt.marker = b1 & 0x80;
    pkt.payloadType = b1 & 0x7F;
    if (pkt.version != 2) {
        return false;
    }
    pkt.seq = readU16(datagram, 2);
    pkt.timestamp = readU32(datagram, 4);
    pkt.ssrc = readU32(datagram, 8);
    int offset = 12;
    if (datagram.size() < offset + 4 * int(pkt.csrcCount)) {
        return false;
    }
    for (int i = 0; i < pkt.csrcCount; ++i) {
        pkt.csrcs.append(readU32(datagram, offset));
        offset += 4;
    }
    if (pkt.extension) {
        if (datagram.size() < offset + 4) {
            return false;
        }
        // 扩展头：16 位 profile + 16 位长度（32 位字），内容整体跳过
        const int extWords = readU16(datagram, offset + 2);
        offset += 4 + extWords * 4;
        if (datagram.size() < offset) {
            return false;
        }
    }
    int payloadSize = datagram.size() - offset;
    if (pkt.padding) {
        // RFC 3550：最后一个字节为填充字节数（含该字节本身）
        if (payloadSize <= 0) {
            return false;
        }
        const int padCount = quint8(datagram[datagram.size() - 1]);
        if (padCount <= 0 || padCount > payloadSize) {
            return false;
        }
        pkt.paddingBytes = padCount;
        payloadSize -= padCount;
    }
    pkt.payloadOffset = offset;
    pkt.payloadSize = payloadSize;
    pkt.valid = true;
    return true;
}

QString payloadTypeName(quint8 pt)
{
    switch (pt) {
    case 0: return QStringLiteral("PCMU(G.711 μ律)");
    case 1: return QStringLiteral("1016");
    case 2: return QStringLiteral("G721");
    case 3: return QStringLiteral("GSM");
    case 4: return QStringLiteral("G723");
    case 5: return QStringLiteral("DVI4(8kHz)");
    case 6: return QStringLiteral("DVI4(16kHz)");
    case 7: return QStringLiteral("LPC");
    case 8: return QStringLiteral("PCMA(G.711 A律)");
    case 9: return QStringLiteral("G722");
    case 10: return QStringLiteral("L16(双声道)");
    case 11: return QStringLiteral("L16(单声道)");
    case 12: return QStringLiteral("QCELP");
    case 13: return QStringLiteral("CN");
    case 14: return QStringLiteral("MPA(MPEG音频)");
    case 15: return QStringLiteral("G728");
    case 16: return QStringLiteral("DVI4(11kHz)");
    case 17: return QStringLiteral("DVI4(22kHz)");
    case 18: return QStringLiteral("G729");
    case 25: return QStringLiteral("CelB");
    case 26: return QStringLiteral("JPEG");
    case 28: return QStringLiteral("nv");
    case 31: return QStringLiteral("H.261");
    case 32: return QStringLiteral("MPV(MPEG视频)");
    case 33: return QStringLiteral("MP2T(TS流)");
    case 34: return QStringLiteral("H.263");
    default:
        return (pt >= 96 && pt <= 127) ? QStringLiteral("动态(%1)").arg(pt)
                                       : QStringLiteral("未知(%1)").arg(pt);
    }
}

// ---------- RTCP ----------

bool isRtcp(const QByteArray &data)
{
    if (data.size() < 2) {
        return false;
    }
    const uchar b0 = uchar(data[0]);
    const uchar b1 = uchar(data[1]);
    // RFC 5761：版本 2 且报文类型 192-223 判定为 RTCP
    return (b0 & 0xC0) == 0x80 && b1 >= 192 && b1 <= 223;
}

QString rtcpTypeName(quint8 type)
{
    switch (type) {
    case 192: return QStringLiteral("FIR(全内刷新请求)");
    case 193: return QStringLiteral("NACK(否定确认)");
    case 200: return QStringLiteral("SR(发送者报告)");
    case 201: return QStringLiteral("RR(接收者报告)");
    case 202: return QStringLiteral("SDES(源描述)");
    case 203: return QStringLiteral("BYE(离开)");
    case 204: return QStringLiteral("APP(应用自定义)");
    case 205: return QStringLiteral("RTPFB(通用反馈)");
    case 206: return QStringLiteral("PSFB(载荷特定反馈)");
    case 207: return QStringLiteral("XR(扩展报告)");
    default: return QStringLiteral("未知(%1)").arg(type);
    }
}

static void parseReportBlocks(const QByteArray &block, int p, int count, int end,
                              QList<RtcpReportBlock> &reports)
{
    for (int i = 0; i < count && p + 24 <= end; ++i) {
        RtcpReportBlock rb;
        rb.ssrc = readU32(block, p);
        rb.fractionLost = quint8(block[p + 4]);
        const quint32 cl = (quint32(quint8(block[p + 5])) << 16)
                           | (quint32(quint8(block[p + 6])) << 8) | quint8(block[p + 7]);
        // 24 位有符号累计丢包
        rb.cumulativeLost = (cl & 0x800000u) ? qint32(cl | 0xFF000000u) : qint32(cl);
        rb.highestSeq = readU32(block, p + 8);
        rb.jitter = readU32(block, p + 12);
        rb.lastSrTimestamp = readU32(block, p + 16);
        rb.delaySinceLastSr = readU32(block, p + 20);
        reports.append(rb);
        p += 24;
    }
}

QList<RtcpPacket> parseRtcp(const QByteArray &data)
{
    QList<RtcpPacket> result;
    const int size = data.size();
    int pos = 0;
    while (pos + 4 <= size) {
        const uchar b0 = uchar(data[pos]);
        const quint8 type = quint8(data[pos + 1]);
        const int lengthWords = readU16(data, pos + 2);
        const int blockLen = 4 * (lengthWords + 1);
        if (blockLen <= 0 || pos + blockLen > size) {
            break;  // 截断的尾块直接丢弃
        }
        const QByteArray block = data.mid(pos, blockLen);
        pos += blockLen;

        RtcpPacket pkt;
        pkt.valid = true;
        pkt.type = type;
        pkt.lengthWords = lengthWords;
        pkt.rawData = block;
        const int rc = b0 & 0x1F;
        int end = blockLen;
        if (b0 & 0x20) {  // P 位：末字节为填充长度
            const int pad = quint8(block[blockLen - 1]);
            if (pad > 0 && pad <= blockLen - 4) {
                end -= pad;
            }
        }

        switch (type) {
        case 200:  // SR
            if (block.size() >= 28) {
                pkt.hasSenderInfo = true;
                pkt.senderSsrc = readU32(block, 4);
                pkt.ntpTimestamp = (quint64(readU32(block, 8)) << 32) | readU32(block, 12);
                pkt.rtpTimestamp = readU32(block, 16);
                pkt.packetCount = readU32(block, 20);
                pkt.octetCount = readU32(block, 24);
                parseReportBlocks(block, 28, rc, end, pkt.reports);
            }
            break;
        case 201:  // RR
            if (block.size() >= 8) {
                pkt.senderSsrc = readU32(block, 4);
                parseReportBlocks(block, 8, rc, end, pkt.reports);
            }
            break;
        case 202: {  // SDES：chunk = SSRC(4) + 若干 item，0 结束，32 位对齐
            int p = 4;
            while (p + 5 <= end) {
                p += 4;  // 跳过 chunk 的 SSRC（展示无需）
                while (p < end) {
                    const quint8 itemType = quint8(block[p]);
                    if (itemType == 0) {
                        ++p;
                        break;  // 本 chunk 结束
                    }
                    if (p + 1 >= end) {
                        p = end;
                        break;
                    }
                    const int len = quint8(block[p + 1]);
                    p += 2;
                    if (p + len > end) {
                        p = end;
                        break;
                    }
                    if (itemType == 1) {  // CNAME
                        pkt.cnames.append(QString::fromLatin1(block.mid(p, len)));
                    }
                    p += len;
                }
                p = (p + 3) & ~3;  // 对齐到 32 位边界
            }
            break;
        }
        case 203: {  // BYE：rc 个 SSRC + 可选 reason
            int p = 4;
            for (int i = 0; i < rc && p + 4 <= end; ++i) {
                pkt.byeSsrcs.append(readU32(block, p));
                p += 4;
            }
            if (p + 1 <= end) {
                const int reasonLen = quint8(block[p]);
                if (p + 1 + reasonLen <= end) {
                    pkt.byeReason = QString::fromLatin1(block.mid(p + 1, reasonLen));
                }
            }
            break;
        }
        case 204:  // APP：SSRC + 4 字节名称 + 应用数据
            if (block.size() >= 12) {
                pkt.senderSsrc = readU32(block, 4);
                pkt.appName = block.mid(8, 4);
            }
            break;
        default:  // 未知类型：仅保留头部与原始字节
            break;
        }
        result.append(pkt);
    }
    return result;
}

// ---------- 单流统计 ----------

void RtpStreamStats::update(const RtpPacket &pkt, qint64 arrivalMs)
{
    if (m_first) {
        m_first = false;
        m_ssrc = pkt.ssrc;
        m_payloadType = pkt.payloadType;
        m_baseSeq = pkt.seq;
        m_maxSeq = pkt.seq;
        m_cycles = 0;
        m_firstArrivalMs = arrivalMs;
        m_jitter = 0.0;
        // transit = 到达时间 - RTP 时间戳（按 90kHz 视频时钟换算成毫秒）
        m_lastTransit = double(arrivalMs) - double(pkt.timestamp) / 90.0;
    } else {
        // 序号扩展（RFC 3550 A.1 简化版，乱序窗口 ±3000）
        qint32 udelta = qint32(pkt.seq) - qint32(m_maxSeq);
        if (udelta < -3000) {  // 反向大跳变 → 环绕前进
            udelta += 65536;
            ++m_cycles;
        } else if (udelta > 3000) {  // 异常跳变 → 重新基准化
            m_cycles = 0;
            m_baseSeq = pkt.seq;
        }
        if (udelta >= 0) {
            m_maxSeq = pkt.seq;
        }
        // 到达间隔抖动：J += (|D| - J) / 16
        const double transit = double(arrivalMs) - double(pkt.timestamp) / 90.0;
        const double d = qAbs(transit - m_lastTransit);
        m_jitter += (d - m_jitter) / 16.0;
        m_lastTransit = transit;
    }
    ++m_packetCount;
    m_octetCount += pkt.payloadSize;
    m_lastArrivalMs = arrivalMs;
}

void RtpStreamStats::reset()
{
    *this = RtpStreamStats();
}

qint64 RtpStreamStats::expectedPackets() const
{
    const qint64 extendedMax = qint64(m_cycles) * 65536 + m_maxSeq;
    return extendedMax - m_baseSeq + 1;
}

qint64 RtpStreamStats::lostPackets() const
{
    const qint64 lost = expectedPackets() - m_packetCount;
    return lost > 0 ? lost : 0;
}

double RtpStreamStats::lossRate() const
{
    const qint64 expected = expectedPackets();
    return expected > 0 ? double(lostPackets()) * 100.0 / double(expected) : 0.0;
}

} // namespace RtpProtocol
