#ifndef POP3PROTOCOL_H
#define POP3PROTOCOL_H

#include <QByteArray>

// POP3 协议解析辅助（RFC 1939）：静态纯函数，供表单与 GTest 单测共用。
// 多行响应的状态机编排放在表单层（只有 LIST/RETR/UIDL/TOP/CAPA 后面才跟
// 多行数据，客户端自己知道刚发了什么命令），这里只提供可独立测试的纯函数。
namespace Pop3Protocol {

// ---- 响应行分类（line 为已剥除行尾的原始行）----
bool isOkLine(const QByteArray &line);         // 以 "+OK" 开头（RFC 要求大写，区分大小写）
bool isErrLine(const QByteArray &line);        // 以 "-ERR" 开头
bool isDotTerminator(const QByteArray &line);  // 恰为 "."（多行响应结束标记）

// ---- 点还原（RFC 1939 dot-stuffing 解码）----
// 行首一个 '.' 剥离："..abc" → ".abc"、".abc" → "abc"；非 '.' 开头原样返回。
// 注意：终止行 "." 应先由调用方用 isDotTerminator 判定，不要对它调用本函数。
QByteArray unstuffDotLine(const QByteArray &line);

// ---- 行馈送：从累积缓冲剥出完整一行 ----
struct FeedResult {
    bool hasLine = false;   // false = 缓冲中还没有完整行，等待更多数据（buffer 原样保留）
    QByteArray line;        // 剥除 CRLF/LF 后的行内容
};
FeedResult feedLine(QByteArray &buffer);

// ---- 数值行解析 ----
struct StatInfo { bool valid = false; qint64 count = 0; qint64 octets = 0; };
StatInfo parseStatLine(const QByteArray &line);   // "+OK 2 320"

struct ListInfo { bool valid = false; int msgNo = 0; qint64 size = 0; };
ListInfo parseListLine(const QByteArray &line);   // LIST 数据行："1 12345"

struct UidlInfo { bool valid = false; int msgNo = 0; QByteArray uid; };
UidlInfo parseUidlLine(const QByteArray &line);   // UIDL 数据行："1 whqtswO00..."

} // namespace Pop3Protocol

#endif // POP3PROTOCOL_H
