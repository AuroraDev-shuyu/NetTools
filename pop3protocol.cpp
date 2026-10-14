#include "pop3protocol.h"

#include <QList>

namespace Pop3Protocol {

// 功能：判断是否为 +OK 响应行（RFC 1939 要求大写，区分大小写）
bool isOkLine(const QByteArray &line)
{
    return line.startsWith("+OK");
}

// 功能：判断是否为 -ERR 响应行
bool isErrLine(const QByteArray &line)
{
    return line.startsWith("-ERR");
}

// 功能：判断是否为多行响应的终止行 "."
bool isDotTerminator(const QByteArray &line)
{
    return line == ".";
}

// 功能：点还原——多行响应正文中行首的填充点剥离（"..abc"→".abc"）
QByteArray unstuffDotLine(const QByteArray &line)
{
    if (line.startsWith('.')) {
        return line.mid(1);
    }
    return line;
}

// 功能：从缓冲中剥出完整一行（CRLF 或裸 LF 结尾）；无完整行则缓冲原样保留
FeedResult feedLine(QByteArray &buffer)
{
    FeedResult result;
    for (int i = 0; i < buffer.size(); ++i) {
        if (buffer.at(i) == '\n') {
            int len = i;
            if (len > 0 && buffer.at(len - 1) == '\r') {
                --len; // 去掉行尾 \r
            }
            result.line = buffer.left(len);
            buffer.remove(0, i + 1);
            result.hasLine = true;
            return result;
        }
    }
    return result;
}

// 功能：解析 STAT 响应 "+OK 邮件数 总字节数"
StatInfo parseStatLine(const QByteArray &line)
{
    StatInfo info;
    if (!isOkLine(line)) {
        return info;
    }
    const QList<QByteArray> tokens = line.simplified().split(' ');
    if (tokens.size() < 3) { // 需要 "+OK" + 数量 + 字节数
        return info;
    }
    bool okCount = false;
    bool okOctets = false;
    const qint64 count = tokens.at(1).toLongLong(&okCount);
    const qint64 octets = tokens.at(2).toLongLong(&okOctets);
    if (!okCount || !okOctets) {
        return info;
    }
    info.valid = true;
    info.count = count;
    info.octets = octets;
    return info;
}

// 功能：解析 LIST 数据行 "邮件序号 字节数"
ListInfo parseListLine(const QByteArray &line)
{
    ListInfo info;
    const QList<QByteArray> tokens = line.simplified().split(' ');
    if (tokens.size() != 2) {
        return info;
    }
    bool okNo = false;
    bool okSize = false;
    const int msgNo = tokens.at(0).toInt(&okNo);
    const qint64 size = tokens.at(1).toLongLong(&okSize);
    if (!okNo || !okSize) {
        return info;
    }
    info.valid = true;
    info.msgNo = msgNo;
    info.size = size;
    return info;
}

// 功能：解析 UIDL 数据行 "邮件序号 唯一标识"
UidlInfo parseUidlLine(const QByteArray &line)
{
    UidlInfo info;
    const QList<QByteArray> tokens = line.simplified().split(' ');
    if (tokens.size() != 2) {
        return info;
    }
    bool okNo = false;
    const int msgNo = tokens.at(0).toInt(&okNo);
    if (!okNo) {
        return info;
    }
    info.valid = true;
    info.msgNo = msgNo;
    info.uid = tokens.at(1);
    return info;
}

} // namespace Pop3Protocol
