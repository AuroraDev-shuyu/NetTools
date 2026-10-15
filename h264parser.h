#ifndef H264PARSER_H
#define H264PARSER_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

// H.264 Annex-B 码流解析（GB/T ITU-T H.264 7.3/7.4），纯逻辑无 GUI 依赖。
namespace H264Parser {

// 帧类型：IDR 单独区分（GOP 关键帧判定需要），展示时可映射为 I
enum class FrameType { Unknown, I, P, B, Idr };

QString frameTypeName(FrameType type);  // I/P/B/IDR/--

// ---------- Annex-B 切分 ----------
struct Nalu {
    int startCodeLen = 0;   // 起始码长度 3 或 4
    int offset = 0;         // 起始码在原始数据中的偏移
    QByteArray data;        // NAL 单元内容（含 NAL 头字节，不含起始码与尾随零）
};

// 按 00 00 01 / 00 00 00 01 切分；起始码前的垃圾字节与尾随零被忽略。
QList<Nalu> splitAnnexB(const QByteArray &data);

// ---------- NAL 头 ----------
struct NaluHeader {
    bool valid = false;
    bool forbiddenZeroBit = false;  // 1 表示有传输错误
    int nalRefIdc = 0;
    int type = 0;
};

NaluHeader parseNaluHeader(const QByteArray &nalu);
QString naluTypeName(int type);

// 帧类型判定：type 5 → IDR；type 1 → 解析 slice header 的 slice_type
//（0/5 → P，1/6 → B，2/7 → I，3/8/4/9 切换片 → I）
FrameType classifyFrame(const QByteArray &nalu);

// ---------- 位读取器（Exp-Golomb） ----------
class BitReader
{
public:
    // bitOffset 为起始位（NAL 头 1 字节 → 传 8）；构造时自动剔除 00 00 03 防竞争字节
    explicit BitReader(const QByteArray &data, int bitOffset = 0);

    quint32 readBits(int n);    // 超出码流末尾返回 0 并置 overrun
    quint32 readUe();           // 无符号 Exp-Golomb
    qint32 readSe();            // 有符号 Exp-Golomb
    bool overrun() const { return m_overrun; }
    int bitPos() const { return m_bitPos; }

private:
    QByteArray m_data;          // 数据副本（隐式共享，开销仅引用计数）
    QList<int> m_rbspToNal;     // 第 i 个 RBSP 字节对应原数据的索引
    int m_bitPos = 0;
    bool m_overrun = false;
};

// ---------- SPS ----------
struct SpsInfo {
    bool valid = false;
    int profileIdc = 0;
    int levelIdc = 0;
    QString profileName;
    QString levelName;
    int width = 0;
    int height = 0;
    int refFrames = 0;
    double frameRate = 0.0;     // VUI 定时信息；无则为 0
    int chromaFormatIdc = 1;    // 非高 profile 时默认 4:2:0
    bool frameMbsOnly = true;
};

// 解析 SPS NAL 单元（含 NAL 头字节），提取 profile/level/分辨率/参考帧/帧率。
SpsInfo parseSps(const QByteArray &nalu);

QString profileName(int profileIdc);
QString levelName(int levelIdc);

// ---------- PPS ----------
struct PpsInfo {
    bool valid = false;
    int ppsId = 0;
    int spsId = 0;
    bool entropyCodingModeFlag = true;  // 1=CABAC，0=CAVLC
    bool weightedPredFlag = false;
    int picInitQpMinus26 = 0;
    bool deblockingFilterControlPresentFlag = false;
    bool constrainedIntraPredFlag = false;
    bool redundantPicCntPresentFlag = false;
};

PpsInfo parsePps(const QByteArray &nalu);

// ---------- GOP 统计 ----------
struct GopStats {
    int idrCount = 0;
    int totalFrames = 0;
    int iFrames = 0;        // 含 IDR
    int pFrames = 0;
    int bFrames = 0;
    int unknownFrames = 0;
    QList<int> keyframeIndices; // IDR 帧下标
    int minGop = 0;         // 相邻 IDR 间隔的最小/最大/平均帧数
    int maxGop = 0;
    double avgGop = 0.0;
};

GopStats computeGopStats(const QList<FrameType> &frames);

} // namespace H264Parser

#endif // H264PARSER_H
