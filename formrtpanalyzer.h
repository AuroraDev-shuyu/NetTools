#ifndef FORMRTPANALYZER_H
#define FORMRTPANALYZER_H

#include <QWidget>
#include <QHash>
#include <QList>

#include "rtpprotocol.h"

class QUdpSocket;

namespace Ui {
class FormRtpAnalyzer;
}

// RTP/RTCP 协议解析页（「音视频解析」子页）：文件导入一次解析 + UDP 端口实时监听。
// 协议解析用 rtpprotocol.h 的 RtpProtocol 命名空间；本类负责数据编排与展示。
class FormRtpAnalyzer : public QWidget
{
    Q_OBJECT

public:
    explicit FormRtpAnalyzer(QWidget *parent = nullptr);
    ~FormRtpAnalyzer();

    void saveLog();

private slots:
    void on_pushButton_RtpOpenFile_clicked();
    void on_pushButton_RtpStartListen_clicked();
    void on_pushButton_RtpStopListen_clicked();
    void on_pushButton_RtpClear_clicked();
    void onReadyRead();
    void onCurrentRowChanged(int current, int previous);

private:
    struct RtpRecord {
        bool isRtcpPacket = false;
        RtpProtocol::RtpPacket rtp;
        RtpProtocol::RtcpPacket rtcp;
        QByteArray raw;      // 完整数据报（明细 hexDump 用）
        QString timeStr;     // 到达时间（文件模式为“--”）
    };

    Ui::FormRtpAnalyzer *ui;
    QUdpSocket *m_socket = nullptr;         // 监听套接字（仅监听模式使用）
    QList<RtpRecord> m_rows;                // 与表格行一一对应（上限 2000 行）
    QHash<quint32, RtpProtocol::RtpStreamStats> m_stats;  // 每 SSRC 一组统计
    int m_packetCounter = 0;                // 数据报编号（清空后归零）
    int m_garbageCount = 0;                 // 无法识别为 RTP/RTCP 的数据报数
    QString m_lastFileDir;                  // QFileDialog 记忆目录

    void addDatagram(const QByteArray &data, const QString &timeStr); // 监听模式单数据报入口
    void addRecord(const RtpRecord &rec);   // 追加表格行（超过上限裁头）
    void refreshStatsText();                // 重绘底部按 SSRC 统计
    void fillDetail(int row);               // 填充选中包明细
    void loadFile(const QString &fileName); // 文件模式：顺序游走解析 RTP/RTCP 记录
    void updateStatusText();                // 刷新状态栏（含无效数据报计数）
    QString buildSummary() const;           // saveLog 摘要文本
    void saveTextToFile(const QString &text, const QString &fileName);
};

#endif // FORMRTPANALYZER_H
