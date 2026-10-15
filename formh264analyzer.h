#ifndef FORMH264ANALYZER_H
#define FORMH264ANALYZER_H

#include <QWidget>
#include <QList>
#include <QString>

#include "h264parser.h"

namespace Ui {
class FormH264Analyzer;
}

// H.264 Annex-B 码流分析页（「音视频解析」子页）：文件导入、NALU 切分、
// SPS/PPS 参数解析、帧类型分类与 GOP 统计。解析用 h264parser.h 的 H264Parser 命名空间。
class FormH264Analyzer : public QWidget
{
    Q_OBJECT

public:
    explicit FormH264Analyzer(QWidget *parent = nullptr);
    ~FormH264Analyzer();

    void saveLog();

private slots:
    void on_pushButton_H264OpenFile_clicked();
    void onCellDoubleClicked(int row, int column);

private:
    Ui::FormH264Analyzer *ui;
    QList<H264Parser::Nalu> m_nalus;              // 当前文件的 NALU 列表
    QList<H264Parser::FrameType> m_sliceFrames;   // 仅切片 NALU 的帧型（GOP 统计用）
    H264Parser::SpsInfo m_sps;                    // 首个 SPS 解析结果
    bool m_hasSps = false;
    H264Parser::PpsInfo m_pps;                    // 首个 PPS 解析结果
    bool m_hasPps = false;
    H264Parser::GopStats m_gop;                   // GOP 统计结果
    QString m_lastFileDir;                        // QFileDialog 记忆目录
    QString m_fileName;                           // 当前文件名（摘要用）

    void loadFile(const QString &fileName);
    void fillInfoPanel();                         // SPS/PPS/GOP 汇总
    void fillNaluDetail(int row);                 // 双击行的 NALU 明细
    QString buildSummary() const;                 // saveLog 摘要文本
    void saveTextToFile(const QString &text, const QString &fileName);
};

#endif // FORMH264ANALYZER_H
