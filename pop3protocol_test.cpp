// Pop3Protocol GTest 单测：响应行分类、行馈送、点还原、数值行解析与多行响应状态机场景
#include <gtest/gtest.h>

#include <QByteArray>
#include <QList>

#include "pop3protocol.h"

using namespace Pop3Protocol;

// 辅助：用 feedLine 循环抽取缓冲中所有完整行
static QList<QByteArray> drainLines(QByteArray &buffer)
{
    QList<QByteArray> lines;
    while (true) {
        const FeedResult fr = feedLine(buffer);
        if (!fr.hasLine) {
            break;
        }
        lines.append(fr.line);
    }
    return lines;
}

// 辅助：模拟表单层多行响应状态机（+OK 起始 → 行序列 → "." 终止），返回点还原后的正文行。
// inMulti 由调用方持有并跨调用保持（对应表单的 m_inMultiLine 成员），模拟分包场景必需。
static QList<QByteArray> drainMultiLine(QByteArray &buffer, bool &inMulti)
{
    QList<QByteArray> body;
    while (true) {
        const FeedResult fr = feedLine(buffer);
        if (!fr.hasLine) {
            break;
        }
        if (!inMulti) {
            inMulti = isOkLine(fr.line); // 首行 +OK 进入多行态
            continue;
        }
        if (isDotTerminator(fr.line)) {
            inMulti = false; // 终止行，响应结束
            break;
        }
        body.append(unstuffDotLine(fr.line));
    }
    return body;
}

// ==================== 响应行分类 ====================

TEST(OkErrClassification, OkLineBasic)
{
    EXPECT_TRUE(isOkLine("+OK"));
    EXPECT_TRUE(isOkLine("+OK POP3 server ready"));
    EXPECT_FALSE(isErrLine("+OK POP3 server ready"));
}

TEST(OkErrClassification, OkLineCaseSensitive)
{
    EXPECT_FALSE(isOkLine("+ok"));     // RFC 要求大写
    EXPECT_FALSE(isOkLine(" +OK"));    // 前导空格不算
    EXPECT_FALSE(isOkLine("OK"));
}

TEST(OkErrClassification, ErrLineBasic)
{
    EXPECT_TRUE(isErrLine("-ERR"));
    EXPECT_TRUE(isErrLine("-ERR bad login"));
    EXPECT_FALSE(isOkLine("-ERR bad login"));
}

TEST(OkErrClassification, EmptyAndGarbage)
{
    EXPECT_FALSE(isOkLine(""));
    EXPECT_FALSE(isErrLine(""));
    EXPECT_FALSE(isOkLine("garbage"));
    EXPECT_FALSE(isErrLine("garbage"));
    EXPECT_FALSE(isOkLine("+"));
}

// ==================== 行馈送 ====================

TEST(FeedLine, NoNewlineKeepsBuffer)
{
    QByteArray buf("abc");
    const FeedResult fr = feedLine(buf);
    EXPECT_FALSE(fr.hasLine);
    EXPECT_EQ(buf, QByteArray("abc")); // 缓冲原样保留
}

TEST(FeedLine, CrlfSplit)
{
    QByteArray buf("abc\r\n");
    const FeedResult fr = feedLine(buf);
    ASSERT_TRUE(fr.hasLine);
    EXPECT_EQ(fr.line, QByteArray("abc"));
    EXPECT_TRUE(buf.isEmpty());
}

TEST(FeedLine, BareLf)
{
    QByteArray buf("abc\n");
    const FeedResult fr = feedLine(buf);
    ASSERT_TRUE(fr.hasLine);
    EXPECT_EQ(fr.line, QByteArray("abc"));
}

TEST(FeedLine, SplitAcrossFeeds)
{
    QByteArray buf("hel");
    EXPECT_FALSE(feedLine(buf).hasLine);
    buf.append("lo\r\n");
    const FeedResult fr = feedLine(buf);
    ASSERT_TRUE(fr.hasLine);
    EXPECT_EQ(fr.line, QByteArray("hello"));
}

TEST(FeedLine, MultipleLinesInOneFeed)
{
    QByteArray buf("a\r\nb\r\n");
    const QList<QByteArray> lines = drainLines(buf);
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines.at(0), QByteArray("a"));
    EXPECT_EQ(lines.at(1), QByteArray("b"));
    EXPECT_TRUE(buf.isEmpty());
}

TEST(FeedLine, EmptyLine)
{
    QByteArray buf("\r\n+OK\r\n");
    const QList<QByteArray> lines = drainLines(buf);
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines.at(0), QByteArray(""));
    EXPECT_EQ(lines.at(1), QByteArray("+OK"));
}

TEST(FeedLine, CrOnlyWithoutLfIsNotALine)
{
    QByteArray buf("abc\r");
    EXPECT_FALSE(feedLine(buf).hasLine);
    EXPECT_EQ(buf, QByteArray("abc\r"));
}

// ==================== 终止行与点还原 ====================

TEST(DotHandling, TerminatorDetection)
{
    EXPECT_TRUE(isDotTerminator("."));
    EXPECT_FALSE(isDotTerminator(".."));
    EXPECT_FALSE(isDotTerminator("..."));
    EXPECT_FALSE(isDotTerminator(""));
    EXPECT_FALSE(isDotTerminator(". "));
}

TEST(DotHandling, UnstuffPlainLine)
{
    EXPECT_EQ(unstuffDotLine("abc"), QByteArray("abc"));
    EXPECT_EQ(unstuffDotLine(""), QByteArray(""));
}

TEST(DotHandling, UnstuffSingleDot)
{
    EXPECT_EQ(unstuffDotLine(".abc"), QByteArray("abc"));
}

TEST(DotHandling, UnstuffDoubleDot)
{
    EXPECT_EQ(unstuffDotLine("..abc"), QByteArray(".abc"));
    EXPECT_EQ(unstuffDotLine("..."), QByteArray(".."));
}

// ==================== 数值行解析 ====================

TEST(StatListUidl, StatOk)
{
    const StatInfo info = parseStatLine("+OK 2 320");
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.count, 2);
    EXPECT_EQ(info.octets, 320);
}

TEST(StatListUidl, StatZeroMails)
{
    const StatInfo info = parseStatLine("+OK 0 0");
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.count, 0);
    EXPECT_EQ(info.octets, 0);
}

TEST(StatListUidl, StatErrLineInvalid)
{
    EXPECT_FALSE(parseStatLine("-ERR no such").valid);
    EXPECT_FALSE(parseStatLine("garbage").valid);
    EXPECT_FALSE(parseStatLine("+OK 2").valid);    // 缺字节数
    EXPECT_FALSE(parseStatLine("+OK a b").valid);  // 非数字
}

TEST(StatListUidl, ListOk)
{
    const ListInfo info = parseListLine("1 12345");
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.msgNo, 1);
    EXPECT_EQ(info.size, 12345);
    const ListInfo zero = parseListLine("10 0");
    ASSERT_TRUE(zero.valid);
    EXPECT_EQ(zero.msgNo, 10);
    EXPECT_EQ(zero.size, 0);
}

TEST(StatListUidl, ListGarbage)
{
    EXPECT_FALSE(parseListLine("x y").valid);
    EXPECT_FALSE(parseListLine("1 2 3").valid);
    EXPECT_FALSE(parseListLine("1").valid);
    EXPECT_FALSE(parseListLine("").valid);
}

TEST(StatListUidl, UidlOk)
{
    const UidlInfo info = parseUidlLine("1 whqtswO00WBw418f9t5JxYwZ");
    ASSERT_TRUE(info.valid);
    EXPECT_EQ(info.msgNo, 1);
    EXPECT_EQ(info.uid, QByteArray("whqtswO00WBw418f9t5JxYwZ"));
}

TEST(StatListUidl, UidlGarbage)
{
    EXPECT_FALSE(parseUidlLine("1").valid);
    EXPECT_FALSE(parseUidlLine("x y").valid);   // 序号非数字
    EXPECT_FALSE(parseUidlLine("1 a b").valid);
}

// ==================== 多行响应状态机场景 ====================

TEST(MultiLineScenario, ListResponseComplete)
{
    QByteArray buf("+OK 2 messages\r\n1 320\r\n2 410\r\n.\r\n");
    bool inMulti = false;
    const QList<QByteArray> body = drainMultiLine(buf, inMulti);
    ASSERT_EQ(body.size(), 2);
    EXPECT_EQ(body.at(0), QByteArray("1 320"));
    EXPECT_EQ(body.at(1), QByteArray("2 410"));
    EXPECT_TRUE(buf.isEmpty());
    EXPECT_FALSE(inMulti); // 终止行后退出多行态
}

TEST(MultiLineScenario, ListResponseSplitAcrossThreeChunks)
{
    QByteArray buf("+OK 2 mes");        // 首行被截断
    bool inMulti = false;
    EXPECT_TRUE(drainMultiLine(buf, inMulti).isEmpty());
    EXPECT_EQ(buf, QByteArray("+OK 2 mes"));
    buf.append("sages\r\n1 320\r\n");   // 补全首行 + 第一条数据行
    const QList<QByteArray> first = drainMultiLine(buf, inMulti);
    ASSERT_EQ(first.size(), 1);         // 首行补全进入多行态，第一条数据行完整
    EXPECT_EQ(first.at(0), QByteArray("1 320"));
    EXPECT_TRUE(inMulti);
    EXPECT_TRUE(buf.isEmpty());
    buf.append("2 410\r\n.\r\n");       // 剩余数据行 + 终止行
    const QList<QByteArray> body = drainMultiLine(buf, inMulti);
    ASSERT_EQ(body.size(), 1);
    EXPECT_EQ(body.at(0), QByteArray("2 410"));
    EXPECT_FALSE(inMulti);
}

TEST(MultiLineScenario, RetrBodyDotStuffed)
{
    // 服务端存储的原始正文：".hidden" 行被填充为 "..hidden"
    QByteArray buf("+OK 120 octets\r\nFrom: test@example.com\r\n..hidden line\r\nNormal line\r\n.\r\n");
    bool inMulti = false;
    const QList<QByteArray> body = drainMultiLine(buf, inMulti);
    ASSERT_EQ(body.size(), 3);
    EXPECT_EQ(body.at(0), QByteArray("From: test@example.com"));
    EXPECT_EQ(body.at(1), QByteArray(".hidden line")); // 点还原
    EXPECT_EQ(body.at(2), QByteArray("Normal line"));
}

TEST(MultiLineScenario, TerminatorThenNextResponseInSameBuffer)
{
    QByteArray buf("+OK 1 message\r\nhello\r\n.\r\n+OK Bye\r\n");
    bool inMulti = false;
    const QList<QByteArray> body = drainMultiLine(buf, inMulti);
    ASSERT_EQ(body.size(), 1);
    EXPECT_EQ(body.at(0), QByteArray("hello"));
    // 终止行之后紧跟的单行响应仍在缓冲中，可继续以单行模式抽取
    const QList<QByteArray> rest = drainLines(buf);
    ASSERT_EQ(rest.size(), 1);
    EXPECT_EQ(rest.at(0), QByteArray("+OK Bye"));
    EXPECT_TRUE(buf.isEmpty());
}

TEST(MultiLineScenario, ErrAfterMultiLineCommandEndsSingleLine)
{
    // RETR 不存在时服务端回 -ERR 单行（不跟多行），状态机按单行处理
    QByteArray buf("-ERR no such message\r\n");
    const QList<QByteArray> lines = drainLines(buf);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(isErrLine(lines.at(0)));
}
