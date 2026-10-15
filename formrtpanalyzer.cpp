#include "formrtpanalyzer.h"
#include "ui_formrtpanalyzer.h"
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
#include <QUdpSocket>

namespace {

// 表格行数上限：超出裁掉最旧行，防止长时间监听内存/渲染无界增长
const int kMaxRows = 2000;
// 文件模式继任者扫描窗口：单包载荷超过 64KB 视为异常（超出则链式验证失败）
const int kMaxPayloadScan = 65536;

// 读取大端 16 位（文件游走辅助）
quint16 readU16At(const QByteArray &d, int pos)
{
    return quint16((quint8(d[pos]) << 8) | quint8(d[pos + 1]));
}

// 计算 pos 处 RTP 头长度（12 + CSRC + 扩展头）；结构不合法返回 -1。
// 不校验填充（边界未知时最后一个填充计数字节无法定位）。
int rtpHeaderLenAt(const QByteArray &d, int pos)
{
    if (pos + 12 > d.size()) {
        return -1;
    }
    const uchar b0 = uchar(d[pos]);
    if ((b0 >> 6) != 2) {
        return -1;
    }
    int len = 12 + (b0 & 0x0F) * 4;
    if (b0 & 0x10) {  // 扩展头：16 位 profile + 16 位长度（32 位字）
        if (pos + len + 4 > d.size()) {
            return -1;
        }
        const int extWords = (quint8(d[pos + len + 2]) << 8) | quint8(d[pos + len + 3]);
        len += 4 + extWords * 4;
    }
    if (pos + len > d.size()) {
        return -1;
    }
    return len;
}

} // namespace

FormRtpAnalyzer::FormRtpAnalyzer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormRtpAnalyzer)
{
    ui->setupUi(this);

    ui->tableWidget_RtpPackets->setColumnCount(9);
    ui->tableWidget_RtpPackets->setHorizontalHeaderLabels(
        {QStringLiteral("编号"), QStringLiteral("时间"), QStringLiteral("类型"),
         QStringLiteral("SSRC"), QStringLiteral("载荷类型"), QStringLiteral("RTP序号"),
         QStringLiteral("时间戳"), QStringLiteral("长度"), QStringLiteral("标记")});
    ui->tableWidget_RtpPackets->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget_RtpPackets->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget_RtpPackets->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget_RtpPackets->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget_RtpPackets->verticalHeader()->setVisible(false);

    connect(ui->tableWidget_RtpPackets, &QTableWidget::currentCellChanged,
            this, &FormRtpAnalyzer::onCurrentRowChanged);

    // 恢复上次监听端口与文件目录
    QSettings settings;
    const int lastPort = settings.value(QStringLiteral("RtpAnalyzer/lastPort"), 5004).toInt();
    if (lastPort >= 1 && lastPort <= 65535) {
        ui->spinBox_RtpListenPort->setValue(lastPort);
    }
    m_lastFileDir = settings.value(QStringLiteral("RtpAnalyzer/lastFileDir")).toString();
}

FormRtpAnalyzer::~FormRtpAnalyzer()
{
    if (m_socket) {
        m_socket->close();
    }
    delete ui;
}

// ---------- 控制按钮 ----------

void FormRtpAnalyzer::on_pushButton_RtpOpenFile_clicked()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择RTP文件"), m_lastFileDir,
        QStringLiteral("RTP文件 (*.rtp);;所有文件 (*.*)"));
    if (fileName.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("RtpAnalyzer/lastFileDir"),
                      QFileInfo(fileName).absolutePath());
    m_lastFileDir = QFileInfo(fileName).absolutePath();
    loadFile(fileName);
}

void FormRtpAnalyzer::on_pushButton_RtpStartListen_clicked()
{
    const int port = ui->spinBox_RtpListenPort->value();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("已在监听中"));
        return;
    }
    if (!m_socket) {
        m_socket = new QUdpSocket(this);
        connect(m_socket, &QUdpSocket::readyRead, this, &FormRtpAnalyzer::onReadyRead);
    }
    if (!m_socket->bind(QHostAddress::AnyIPv4, port)) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("端口 %1 绑定失败：%2")
                                 .arg(port)
                                 .arg(m_socket->errorString()));
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("RtpAnalyzer/lastPort"), port);
    ui->pushButton_RtpStartListen->setEnabled(false);
    ui->pushButton_RtpStopListen->setEnabled(true);
    updateStatusText();
}

void FormRtpAnalyzer::on_pushButton_RtpStopListen_clicked()
{
    if (m_socket) {
        m_socket->close();
    }
    ui->pushButton_RtpStartListen->setEnabled(true);
    ui->pushButton_RtpStopListen->setEnabled(false);
    updateStatusText();
}

void FormRtpAnalyzer::on_pushButton_RtpClear_clicked()
{
    ui->tableWidget_RtpPackets->setRowCount(0);
    m_rows.clear();
    m_stats.clear();
    m_packetCounter = 0;
    m_garbageCount = 0;
    ui->label_RtpStats->setText(QStringLiteral("暂无数据"));
    ui->plainTextEdit_RtpDetail->clear();
    updateStatusText();
}

// ---------- 监听模式 ----------

void FormRtpAnalyzer::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        const qint64 datagramSize = m_socket->pendingDatagramSize();
        if (datagramSize <= 0) {
            break;
        }
        QByteArray data(int(datagramSize), Qt::Uninitialized);
        const qint64 bytesRead = m_socket->readDatagram(data.data(), data.size());
        if (bytesRead <= 0) {
            continue;
        }
        data.resize(int(bytesRead));
        const QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
        addDatagram(data, timeStr);
    }
    refreshStatsText();
}

void FormRtpAnalyzer::addDatagram(const QByteArray &data, const QString &timeStr)
{
    RtpRecord rec;
    rec.timeStr = timeStr;
    rec.raw = data;
    RtpProtocol::RtpPacket pkt;
    if (RtpProtocol::parseRtpPacket(data, pkt)) {
        rec.rtp = pkt;
        m_stats[pkt.ssrc].update(pkt, QDateTime::currentMSecsSinceEpoch());
        addRecord(rec);
        return;
    }
    if (RtpProtocol::isRtcp(data)) {
        const QList<RtpProtocol::RtcpPacket> pkts = RtpProtocol::parseRtcp(data);
        if (pkts.isEmpty()) {
            ++m_garbageCount;
        } else {
            for (const RtpProtocol::RtcpPacket &p : pkts) {
                RtpRecord rtcpRec;
                rtcpRec.isRtcpPacket = true;
                rtcpRec.rtcp = p;
                rtcpRec.raw = p.rawData;
                rtcpRec.timeStr = timeStr;
                addRecord(rtcpRec);
            }
        }
        return;
    }
    ++m_garbageCount;
}

void FormRtpAnalyzer::addRecord(const RtpRecord &rec)
{
    if (ui->tableWidget_RtpPackets->rowCount() >= kMaxRows) {
        ui->tableWidget_RtpPackets->removeRow(0);
        if (!m_rows.isEmpty()) {
            m_rows.removeFirst();
        }
    }
    ++m_packetCounter;
    const int row = ui->tableWidget_RtpPackets->rowCount();
    ui->tableWidget_RtpPackets->insertRow(row);

    auto setCell = [this, row](int col, const QString &text) {
        ui->tableWidget_RtpPackets->setItem(row, col, new QTableWidgetItem(text));
    };
    setCell(0, QString::number(m_packetCounter));
    setCell(1, rec.timeStr);
    if (!rec.isRtcpPacket) {
        const RtpProtocol::RtpPacket &p = rec.rtp;
        setCell(2, QStringLiteral("RTP"));
        setCell(3, QStringLiteral("0x%1").arg(p.ssrc, 8, 16, QLatin1Char('0')));
        setCell(4, RtpProtocol::payloadTypeName(p.payloadType));
        setCell(5, QString::number(p.seq));
        setCell(6, QString::number(p.timestamp));
        setCell(7, QString::number(rec.raw.size()));
        setCell(8, p.marker ? QStringLiteral("●") : QString());
    } else {
        const RtpProtocol::RtcpPacket &p = rec.rtcp;
        setCell(2, RtpProtocol::rtcpTypeName(p.type));
        setCell(3, p.hasSenderInfo || !p.reports.isEmpty() || p.senderSsrc
                       ? QStringLiteral("0x%1").arg(p.senderSsrc, 8, 16, QLatin1Char('0'))
                       : QStringLiteral("--"));
        setCell(4, QStringLiteral("--"));
        setCell(5, QStringLiteral("--"));
        setCell(6, QStringLiteral("--"));
        setCell(7, QString::number(rec.raw.size()));
        setCell(8, QStringLiteral("--"));
    }
    m_rows.append(rec);
    ui->tableWidget_RtpPackets->scrollToBottom();
}

void FormRtpAnalyzer::refreshStatsText()
{
    if (m_stats.isEmpty()) {
        ui->label_RtpStats->setText(QStringLiteral("暂无数据"));
        return;
    }
    QString text;
    for (auto it = m_stats.cbegin(); it != m_stats.cend(); ++it) {
        const RtpProtocol::RtpStreamStats &st = it.value();
        text += QStringLiteral("SSRC 0x%1 [%2]\n"
                               "  包数 %3  字节 %4  应收 %5  丢包 %6  丢包率 %7%\n"
                               "  平均抖动 %8 ms  首个包 %9 ms  末个包 %10 ms\n")
                    .arg(it.key(), 8, 16, QLatin1Char('0'))
                    .arg(RtpProtocol::payloadTypeName(st.payloadType()))
                    .arg(st.packetCount())
                    .arg(st.octetCount())
                    .arg(st.expectedPackets())
                    .arg(st.lostPackets())
                    .arg(st.lossRate(), 0, 'f', 2)
                    .arg(st.jitter(), 0, 'f', 3)
                    .arg(st.firstArrivalMs())
                    .arg(st.lastArrivalMs());
    }
    ui->label_RtpStats->setText(text.trimmed());
}

void FormRtpAnalyzer::onCurrentRowChanged(int current, int /*previous*/)
{
    fillDetail(current);
}

void FormRtpAnalyzer::fillDetail(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        ui->plainTextEdit_RtpDetail->clear();
        return;
    }
    const RtpRecord &rec = m_rows.at(row);
    QString text;
    if (!rec.isRtcpPacket) {
        const RtpProtocol::RtpPacket &p = rec.rtp;
        text += QStringLiteral("RTP 数据报\n");
        text += QStringLiteral("版本: %1  载荷类型: %2 (%3)\n")
                    .arg(p.version)
                    .arg(p.payloadType)
                    .arg(RtpProtocol::payloadTypeName(p.payloadType));
        text += QStringLiteral("序列号: %1  时间戳: %2  SSRC: 0x%3\n")
                    .arg(p.seq)
                    .arg(p.timestamp)
                    .arg(p.ssrc, 8, 16, QLatin1Char('0'));
        text += QStringLiteral("标记位: %1  填充: %2 (%3 字节)  扩展头: %4  CSRC数: %5\n")
                    .arg(p.marker ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(p.padding ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(p.paddingBytes)
                    .arg(p.extension ? QStringLiteral("是") : QStringLiteral("否"))
                    .arg(p.csrcCount);
        if (!p.csrcs.isEmpty()) {
            QStringList csrcs;
            for (quint32 c : p.csrcs) {
                csrcs << QStringLiteral("0x%1").arg(c, 8, 16, QLatin1Char('0'));
            }
            text += QStringLiteral("CSRC: %1\n").arg(csrcs.join(QStringLiteral(" ")));
        }
        text += QStringLiteral("载荷偏移: %1  载荷长度: %2  总长度: %3\n")
                    .arg(p.payloadOffset)
                    .arg(p.payloadSize)
                    .arg(rec.raw.size());
    } else {
        const RtpProtocol::RtcpPacket &p = rec.rtcp;
        text += QStringLiteral("RTCP 报文：%1\n").arg(RtpProtocol::rtcpTypeName(p.type));
        text += QStringLiteral("长度: %1 字节（长度字段 %2 个 32 位字）\n")
                    .arg(rec.raw.size())
                    .arg(p.lengthWords);
        if (p.hasSenderInfo) {
            text += QStringLiteral("发送者 SSRC: 0x%1\n")
                        .arg(p.senderSsrc, 8, 16, QLatin1Char('0'));
            text += QStringLiteral("NTP 时间戳: %1  RTP 时间戳: %2\n")
                        .arg(p.ntpTimestamp)
                        .arg(p.rtpTimestamp);
            text += QStringLiteral("发送包数: %1  发送字节数: %2\n")
                        .arg(p.packetCount)
                        .arg(p.octetCount);
        }
        for (const RtpProtocol::RtcpReportBlock &rb : p.reports) {
            text += QStringLiteral("报告块 SSRC: 0x%1  丢包比例: %2/256  累计丢包: %3\n"
                                   "  最高序号: %4  抖动: %5  距上次SR: %6 ms\n")
                        .arg(rb.ssrc, 8, 16, QLatin1Char('0'))
                        .arg(rb.fractionLost)
                        .arg(rb.cumulativeLost)
                        .arg(rb.highestSeq)
                        .arg(rb.jitter)
                        .arg(rb.delaySinceLastSr);
        }
        if (!p.cnames.isEmpty()) {
            text += QStringLiteral("CNAME: %1\n").arg(p.cnames.join(QStringLiteral(", ")));
        }
        if (!p.byeSsrcs.isEmpty()) {
            QStringList ssrcs;
            for (quint32 s : p.byeSsrcs) {
                ssrcs << QStringLiteral("0x%1").arg(s, 8, 16, QLatin1Char('0'));
            }
            text += QStringLiteral("离开源: %1").arg(ssrcs.join(QStringLiteral(", ")));
            if (!p.byeReason.isEmpty()) {
                text += QStringLiteral("  原因: %1").arg(p.byeReason);
            }
            text += QLatin1Char('\n');
        }
        if (!p.appName.isEmpty()) {
            text += QStringLiteral("应用名: %1  发送者 SSRC: 0x%2\n")
                        .arg(QString::fromLatin1(p.appName))
                        .arg(p.senderSsrc, 8, 16, QLatin1Char('0'));
        }
    }
    const int dumpLen = qMin(512, rec.raw.size());
    text += QStringLiteral("\n原始数据（前 %1 字节）:\n").arg(dumpLen);
    text += hexDump(rec.raw.left(dumpLen));
    if (rec.raw.size() > dumpLen) {
        text += QStringLiteral("（共 %1 字节，仅显示前 %2 字节）\n")
                    .arg(rec.raw.size())
                    .arg(dumpLen);
    }
    ui->plainTextEdit_RtpDetail->setPlainText(text);
}

void FormRtpAnalyzer::updateStatusText()
{
    const bool listening = m_socket && m_socket->state() != QAbstractSocket::UnconnectedState;
    QString text = listening ? QStringLiteral("监听中 端口 %1").arg(ui->spinBox_RtpListenPort->value())
                             : QStringLiteral("未监听");
    if (m_garbageCount > 0) {
        text += QStringLiteral(" | 无效数据报 %1").arg(m_garbageCount);
    }
    ui->label_RtpStatus->setText(text);
}

// ---------- 文件模式 ----------

void FormRtpAnalyzer::loadFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("错误"),
                             QStringLiteral("打开文件失败：%1").arg(file.errorString()));
        return;
    }
    const QByteArray all = file.readAll();
    file.close();
    if (all.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("文件为空"));
        return;
    }

    const int size = all.size();
    int rtpCount = 0;
    int rtcpCount = 0;
    // ffmpeg -f rtp 文件输出为原始 RTP/RTCP 记录顺序拼接（无长度前缀）。
    // RTP 无长度字段，且载荷内可能出现伪头部字节，因此先按“序号链”锚定所有
    // RTP 记录起点：候选起点必须与前一锚点 seq 连续，且其后存在连续两个
    // seq+1、seq+2 的合法继任者（链式三元验证，载荷内假起点几乎不可能连中）。
    // RTCP 样式的字节位置（RFC 5761 判定）不参与锚定，避免把 SR 头误当 RTP
    // 起点；局限：PT 64-95 且无标记位的真实 RTP 包会被同样跳过（该区间保留，
    // 音视频实际使用 PT>=96 动态载荷）。
    QList<int> anchors;      // 锚定的 RTP 记录起点
    int expectedSeq = -1;    // 链上期待的下一个 RTP 序号
    int pos = 0;
    while (pos + 12 <= size) {
        int found = -1;
        for (int q = pos; q + 12 <= size; ++q) {
            const int hlen = rtpHeaderLenAt(all, q);
            if (hlen <= 0 || RtpProtocol::isRtcp(all.mid(q, 4))) {
                continue;
            }
            const quint16 seq = readU16At(all, q + 2);
            if (expectedSeq >= 0 && seq != quint16(expectedSeq + 1)) {
                continue;
            }
            // 链式验证：q 之后须存在 seq+1，其后再存在 seq+2。
            // 每级继任者搜索限制在 kMaxPayloadScan 窗口内（单包载荷超过
            // 64KB 视为异常），既防跨包误连也避免全文件二次扫描的开销。
            bool hasChain = false;
            for (int r = q + hlen + 1; r + 12 <= size && r <= q + hlen + kMaxPayloadScan; ++r) {
                const int h2 = rtpHeaderLenAt(all, r);
                if (h2 <= 0 || RtpProtocol::isRtcp(all.mid(r, 4))) {
                    continue;
                }
                if (readU16At(all, r + 2) != quint16(seq + 1)) {
                    continue;
                }
                for (int t = r + h2 + 1; t + 12 <= size && t <= r + h2 + kMaxPayloadScan; ++t) {
                    const int h3 = rtpHeaderLenAt(all, t);
                    if (h3 > 0 && !RtpProtocol::isRtcp(all.mid(t, 4))
                        && readU16At(all, t + 2) == quint16(seq + 2)) {
                        hasChain = true;
                        break;
                    }
                }
                if (hasChain) {
                    break;
                }
            }
            if (hasChain) {
                found = q;
                expectedSeq = seq;
                break;
            }
        }
        if (found < 0) {
            break;  // 无链式一致候选，主链结束
        }
        anchors.append(found);
        pos = found + 1;
    }
    // 流尾：主链（三元验证）无法锚定最后两包，按 seq 连续顺序补齐
    // 最后 1~2 个候选（无后继验证，假起点概率已由 seq 连续性压低）
    if (expectedSeq >= 0 && !anchors.isEmpty()) {
        for (int i = 0; i < 2; ++i) {
            int found = -1;
            for (int q = anchors.last() + 1; q + 12 <= size; ++q) {
                const int hlen = rtpHeaderLenAt(all, q);
                if (hlen <= 0 || RtpProtocol::isRtcp(all.mid(q, 4))) {
                    continue;
                }
                if (readU16At(all, q + 2) == quint16(expectedSeq + 1)) {
                    found = q;
                    break;
                }
            }
            if (found < 0) {
                break;
            }
            anchors.append(found);
            expectedSeq = quint16(expectedSeq + 1);
        }
    }
    // 按锚点顺序游走整文件：锚点处优先按 RTP 解析（锚点是链式验证过的真包
    // 起点，即使首字节形似 RTCP 也以锚点为准）；非锚点的 RTCP 块须严丝合缝
    // 地结束于下一个锚点（或文件尾），否则视为载荷内伪起点跳过。
    pos = 0;
    int anchorIdx = 0;
    while (pos + 4 <= size) {
        if (anchorIdx < anchors.size() && pos == anchors.at(anchorIdx)) {
            const int end = (anchorIdx + 1 < anchors.size()) ? anchors.at(anchorIdx + 1) : size;
            const QByteArray slice = all.mid(pos, end - pos);
            RtpProtocol::RtpPacket pkt;
            if (!RtpProtocol::parseRtpPacket(slice, pkt)) {
                ++pos;
                ++anchorIdx;
                continue;
            }
            RtpRecord rec;
            rec.rtp = pkt;
            rec.raw = slice;
            rec.timeStr = QStringLiteral("--");
            addRecord(rec);
            m_stats[pkt.ssrc].update(pkt, QDateTime::currentMSecsSinceEpoch());
            ++rtpCount;
            pos = end;
            ++anchorIdx;
            continue;
        }
        if (RtpProtocol::isRtcp(all.mid(pos, 4))) {
            const int lengthWords = readU16At(all, pos + 2);
            const int blockLen = 4 * (lengthWords + 1);
            if (pos + blockLen > size) {
                ++pos;
                continue;
            }
            const int nextAnchor = (anchorIdx < anchors.size()) ? anchors.at(anchorIdx) : size;
            if (pos + blockLen != nextAnchor && pos + blockLen != size) {
                ++pos;  // 载荷内伪 RTCP 起点
                continue;
            }
            const QList<RtpProtocol::RtcpPacket> pkts = RtpProtocol::parseRtcp(all.mid(pos, blockLen));
            if (pkts.isEmpty()) {
                ++pos;
                continue;
            }
            for (const RtpProtocol::RtcpPacket &p : pkts) {
                RtpRecord rec;
                rec.isRtcpPacket = true;
                rec.rtcp = p;
                rec.raw = p.rawData;
                rec.timeStr = QStringLiteral("--");
                addRecord(rec);
                ++rtcpCount;
            }
            pos += blockLen;
            continue;
        }
        ++pos;  // 记录间垃圾字节
    }

    refreshStatsText();
    updateStatusText();
    ui->label_RtpStatus->setText(QStringLiteral("文件解析完成：RTP %1 包 / RTCP %2 块，共 %3 行")
                                     .arg(rtpCount)
                                     .arg(rtcpCount)
                                     .arg(ui->tableWidget_RtpPackets->rowCount()));
}

// ---------- 日志 ----------

QString FormRtpAnalyzer::buildSummary() const
{
    if (m_rows.isEmpty()) {
        return QString();
    }
    int rtpCount = 0;
    int rtcpCount = 0;
    for (const RtpRecord &rec : m_rows) {
        rec.isRtcpPacket ? ++rtcpCount : ++rtpCount;
    }
    QString text = QStringLiteral("RTP/RTCP 解析摘要  %1\n")
                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    text += QStringLiteral("数据报总数 %1（RTP %2 / RTCP %3）  无效数据报 %4\n\n")
                .arg(m_rows.size())
                .arg(rtpCount)
                .arg(rtcpCount)
                .arg(m_garbageCount);
    if (!m_stats.isEmpty()) {
        for (auto it = m_stats.cbegin(); it != m_stats.cend(); ++it) {
            const RtpProtocol::RtpStreamStats &st = it.value();
            text += QStringLiteral("SSRC 0x%1 [%2]: 包数 %3 字节 %4 丢包 %5 丢包率 %6% 抖动 %7 ms\n")
                        .arg(it.key(), 8, 16, QLatin1Char('0'))
                        .arg(RtpProtocol::payloadTypeName(st.payloadType()))
                        .arg(st.packetCount())
                        .arg(st.octetCount())
                        .arg(st.lostPackets())
                        .arg(st.lossRate(), 0, 'f', 2)
                        .arg(st.jitter(), 0, 'f', 3);
        }
    }
    return text;
}

void FormRtpAnalyzer::saveLog()
{
    const QString text = buildSummary();
    if (text.isEmpty()) {
        return;  // 本次会话未解析任何数据，不落盘
    }
    saveTextToFile(text, QStringLiteral("RtpAnalyzerLogfile.txt"));
}

void FormRtpAnalyzer::saveTextToFile(const QString &text, const QString &fileName)
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
        qWarning() << "RTP解析日志保存失败:" << file.errorString();
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
