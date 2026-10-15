#include "formh264analyzer.h"
#include "ui_formh264analyzer.h"
#include "hexdump.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTableWidgetItem>
#include <QTextStream>

namespace {
// NALU 明细 hexDump 上限：超过则截断显示，避免巨型切片拖慢界面
const int kNaluDumpLimit = 4096;
} // namespace

FormH264Analyzer::FormH264Analyzer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormH264Analyzer)
{
    ui->setupUi(this);

    ui->tableWidget_H264Nalus->setColumnCount(6);
    ui->tableWidget_H264Nalus->setHorizontalHeaderLabels(
        {QStringLiteral("序号"), QStringLiteral("类型"), QStringLiteral("帧型"),
         QStringLiteral("REF"), QStringLiteral("偏移"), QStringLiteral("大小")});
    ui->tableWidget_H264Nalus->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget_H264Nalus->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget_H264Nalus->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget_H264Nalus->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget_H264Nalus->verticalHeader()->setVisible(false);

    connect(ui->tableWidget_H264Nalus, &QTableWidget::cellDoubleClicked,
            this, &FormH264Analyzer::onCellDoubleClicked);

    QSettings settings;
    m_lastFileDir = settings.value(QStringLiteral("H264Analyzer/lastFileDir")).toString();
}

FormH264Analyzer::~FormH264Analyzer()
{
    delete ui;
}

// ---------- 文件导入 ----------

void FormH264Analyzer::on_pushButton_H264OpenFile_clicked()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择H.264文件"), m_lastFileDir,
        QStringLiteral("H.264文件 (*.264 *.h264);;所有文件 (*.*)"));
    if (fileName.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("H264Analyzer/lastFileDir"),
                      QFileInfo(fileName).absolutePath());
    m_lastFileDir = QFileInfo(fileName).absolutePath();
    loadFile(fileName);
}

void FormH264Analyzer::loadFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("打开文件失败：%1").arg(file.errorString()));
        return;
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("文件为空"));
        return;
    }

    m_fileName = QFileInfo(fileName).fileName();
    m_nalus = H264Parser::splitAnnexB(data);
    if (m_nalus.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("未找到 Annex-B 起始码（00 00 01 / 00 00 00 01），"
                                            "请确认文件为裸 H.264 码流"));
        return;
    }

    // 重置解析结果
    m_sliceFrames.clear();
    m_sps = H264Parser::SpsInfo();
    m_hasSps = false;
    m_pps = H264Parser::PpsInfo();
    m_hasPps = false;

    // 填表 + 首个 SPS/PPS 解析 + 切片帧型分类
    ui->tableWidget_H264Nalus->setRowCount(0);
    for (const H264Parser::Nalu &nalu : m_nalus) {
        const H264Parser::NaluHeader hdr = H264Parser::parseNaluHeader(nalu.data);
        const H264Parser::FrameType frame = H264Parser::classifyFrame(nalu.data);
        if (hdr.type == 5 || hdr.type == 1) {
            m_sliceFrames.append(frame);  // 仅切片 NALU 参与 GOP/帧统计
        }
        if (!m_hasSps && hdr.type == 7) {
            m_sps = H264Parser::parseSps(nalu.data);
            m_hasSps = true;
        }
        if (!m_hasPps && hdr.type == 8) {
            m_pps = H264Parser::parsePps(nalu.data);
            m_hasPps = true;
        }

        const int row = ui->tableWidget_H264Nalus->rowCount();
        ui->tableWidget_H264Nalus->insertRow(row);
        auto setCell = [this, row](int col, const QString &text) {
            ui->tableWidget_H264Nalus->setItem(row, col, new QTableWidgetItem(text));
        };
        setCell(0, QString::number(row + 1));
        setCell(1, H264Parser::naluTypeName(hdr.type));
        setCell(2, H264Parser::frameTypeName(frame));
        setCell(3, QString::number(hdr.nalRefIdc));
        setCell(4, QStringLiteral("0x%1").arg(nalu.offset, 0, 16));
        setCell(5, QString::number(nalu.data.size()));
    }
    m_gop = H264Parser::computeGopStats(m_sliceFrames);

    ui->label_H264FileInfo->setText(
        QStringLiteral("%1（%2 字节，NALU %3 个）")
            .arg(m_fileName)
            .arg(data.size())
            .arg(m_nalus.size()));
    fillInfoPanel();
    ui->plainTextEdit_H264NaluDetail->clear();
}

// ---------- 信息面板 ----------

void FormH264Analyzer::fillInfoPanel()
{
    QString text;
    text += QStringLiteral("文件：%1   总 NALU 数：%2\n\n").arg(m_fileName).arg(m_nalus.size());

    if (m_hasSps) {
        text += QStringLiteral("=== SPS（序列参数集）===\n");
        if (m_sps.valid) {
            text += QStringLiteral("Profile：%1（%2）  Level：%3\n")
                        .arg(m_sps.profileName)
                        .arg(m_sps.profileIdc)
                        .arg(m_sps.levelName);
            text += QStringLiteral("分辨率：%1 × %2\n").arg(m_sps.width).arg(m_sps.height);
            if (m_sps.frameRate > 0.0) {
                text += QStringLiteral("帧率：%1 fps\n").arg(m_sps.frameRate, 0, 'f', 3);
            } else {
                text += QStringLiteral("帧率：未携带 VUI 定时信息\n");
            }
            text += QStringLiteral("参考帧数：%1  色度格式：%2  仅帧编码：%3\n")
                        .arg(m_sps.refFrames)
                        .arg(m_sps.chromaFormatIdc == 0 ? QStringLiteral("单色")
                            : m_sps.chromaFormatIdc == 1 ? QStringLiteral("4:2:0")
                            : m_sps.chromaFormatIdc == 2 ? QStringLiteral("4:2:2")
                                                         : QStringLiteral("4:4:4"))
                        .arg(m_sps.frameMbsOnly ? QStringLiteral("是") : QStringLiteral("否"));
        } else {
            text += QStringLiteral("解析失败（缩放矩阵/扩展语法暂不支持，或 SPS 损坏）\n");
        }
        text += QLatin1Char('\n');
    } else {
        text += QStringLiteral("=== SPS：未找到 ===\n\n");
    }

    if (m_hasPps) {
        text += QStringLiteral("=== PPS（图像参数集）===\n");
        if (m_pps.valid) {
            text += QStringLiteral("pps_id：%1  sps_id：%2  熵编码：%3\n")
                        .arg(m_pps.ppsId)
                        .arg(m_pps.spsId)
                        .arg(m_pps.entropyCodingModeFlag ? QStringLiteral("CABAC")
                                                         : QStringLiteral("CAVLC"));
            text += QStringLiteral("加权预测：%1  初始量化参数：%2（pic_init_qp_minus26）\n")
                        .arg(m_pps.weightedPredFlag ? QStringLiteral("是") : QStringLiteral("否"))
                        .arg(m_pps.picInitQpMinus26);
            text += QStringLiteral("去块滤波控制：%1  受限帧内预测：%2  冗余图像计数：%3\n")
                        .arg(m_pps.deblockingFilterControlPresentFlag ? QStringLiteral("是") : QStringLiteral("否"))
                        .arg(m_pps.constrainedIntraPredFlag ? QStringLiteral("是") : QStringLiteral("否"))
                        .arg(m_pps.redundantPicCntPresentFlag ? QStringLiteral("是") : QStringLiteral("否"));
        } else {
            text += QStringLiteral("解析失败（FMO 片组或 PPS 损坏）\n");
        }
        text += QLatin1Char('\n');
    } else {
        text += QStringLiteral("=== PPS：未找到 ===\n\n");
    }

    text += QStringLiteral("=== GOP / 帧统计 ===\n");
    text += QStringLiteral("切片总数：%1  I 帧：%2（含 IDR %3）  P 帧：%4  B 帧：%5  未知：%6\n")
                .arg(m_gop.totalFrames)
                .arg(m_gop.iFrames)
                .arg(m_gop.idrCount)
                .arg(m_gop.pFrames)
                .arg(m_gop.bFrames)
                .arg(m_gop.unknownFrames);
    if (m_gop.idrCount >= 2) {
        text += QStringLiteral("GOP 长度（相邻 IDR 间隔）：最小 %1  最大 %2  平均 %3 帧\n")
                    .arg(m_gop.minGop)
                    .arg(m_gop.maxGop)
                    .arg(m_gop.avgGop, 0, 'f', 1);
        QStringList keys;
        for (int idx : m_gop.keyframeIndices) {
            keys << QString::number(idx + 1);  // 展示为 1 起序号
        }
        text += QStringLiteral("关键帧序号：%1\n").arg(keys.join(QStringLiteral(", ")));
    } else if (m_gop.idrCount == 1) {
        text += QStringLiteral("仅 1 个 IDR，无法计算 GOP 间隔\n");
    } else {
        text += QStringLiteral("未检测到 IDR 关键帧\n");
    }
    ui->plainTextEdit_H264Info->setPlainText(text);
}

// ---------- NALU 明细 ----------

void FormH264Analyzer::onCellDoubleClicked(int row, int /*column*/)
{
    fillNaluDetail(row);
}

void FormH264Analyzer::fillNaluDetail(int row)
{
    if (row < 0 || row >= m_nalus.size()) {
        ui->plainTextEdit_H264NaluDetail->clear();
        return;
    }
    const H264Parser::Nalu &nalu = m_nalus.at(row);
    const H264Parser::NaluHeader hdr = H264Parser::parseNaluHeader(nalu.data);

    QString text;
    text += QStringLiteral("NALU #%1\n").arg(row + 1);
    text += QStringLiteral("起始码：%1 字节  偏移：0x%2  大小：%3 字节\n")
                .arg(nalu.startCodeLen)
                .arg(nalu.offset, 0, 16)
                .arg(nalu.data.size());
    text += QStringLiteral("NAL 头：0x%1  forbidden_zero_bit：%2  nal_ref_idc：%3  类型：%4（%5）\n")
                .arg(quint8(nalu.data[0]), 2, 16, QLatin1Char('0'))
                .arg(hdr.forbiddenZeroBit ? 1 : 0)
                .arg(hdr.nalRefIdc)
                .arg(hdr.type)
                .arg(H264Parser::naluTypeName(hdr.type));
    text += QStringLiteral("帧型：%1\n").arg(H264Parser::frameTypeName(H264Parser::classifyFrame(nalu.data)));

    const int dumpLen = qMin(kNaluDumpLimit, nalu.data.size());
    text += QStringLiteral("\n原始数据（前 %1 字节）:\n").arg(dumpLen);
    text += hexDump(nalu.data.left(dumpLen));
    if (nalu.data.size() > dumpLen) {
        text += QStringLiteral("（共 %1 字节，仅显示前 %2 字节）\n")
                    .arg(nalu.data.size())
                    .arg(dumpLen);
    }
    ui->plainTextEdit_H264NaluDetail->setPlainText(text);
}

// ---------- 日志 ----------

QString FormH264Analyzer::buildSummary() const
{
    if (m_nalus.isEmpty()) {
        return QString();
    }
    QString text = QStringLiteral("H.264 码流分析摘要  %1\n")
                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    text += QStringLiteral("文件：%1  NALU 总数 %2\n\n").arg(m_fileName).arg(m_nalus.size());
    text += ui->plainTextEdit_H264Info->toPlainText();
    return text;
}

void FormH264Analyzer::saveLog()
{
    const QString text = buildSummary();
    if (text.isEmpty()) {
        return;  // 本次会话未打开任何码流，不落盘
    }
    saveTextToFile(text, QStringLiteral("H264AnalyzerLogfile.txt"));
}

void FormH264Analyzer::saveTextToFile(const QString &text, const QString &fileName)
{
    // 与 QSettings 配置同目录：%APPDATA%\NDATools
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString appName = QApplication::applicationName();
    if (!appName.isEmpty() && (appDataPath.endsWith(QLatin1Char('/') + appName)
                               || appDataPath.endsWith(QLatin1Char('\\') + appName))) {
        QDir parentDir(appDataPath);
        if (parentDir.cdUp()) {
            appDataPath = parentDir.absolutePath();
        }
    }
    QDir().mkpath(appDataPath);
    const QString filePath = QDir(appDataPath).filePath(fileName);

    const bool fileExists = QFile::exists(filePath);
    QFile file(filePath);
    if (!file.open(fileExists ? (QIODevice::Append | QIODevice::Text)
                              : (QIODevice::WriteOnly | QIODevice::Text))) {
        qWarning() << "H264解析日志保存失败:" << file.errorString();
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    if (fileExists) {
        out << "\n========== "
            << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
            << " ==========" << Qt::endl;
    }
    out << text << Qt::endl;
    file.close();
    if (isVisible()) {
        QMessageBox::information(this, QStringLiteral("成功"),
                                 QStringLiteral("日志已保存至 %1").arg(filePath));
    }
}
