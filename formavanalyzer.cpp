#include "formavanalyzer.h"
#include "ui_formavanalyzer.h"

FormAvAnalyzer::FormAvAnalyzer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormAvAnalyzer)
{
    ui->setupUi(this);
}

FormAvAnalyzer::~FormAvAnalyzer()
{
    delete ui;
}

// 保存日志（公共接口，供主窗口关闭时调用）：转发给两个子表单
void FormAvAnalyzer::saveLog()
{
    if (ui && ui->tabWidget_AvAnalyzer) {
        try {
            FormRtpAnalyzer *rtpTab = qobject_cast<FormRtpAnalyzer *>(
                ui->tabWidget_AvAnalyzer->widget(0));  // 索引0：RTP/RTCP解析
            if (rtpTab) {
                rtpTab->saveLog();
            }
            FormH264Analyzer *h264Tab = qobject_cast<FormH264Analyzer *>(
                ui->tabWidget_AvAnalyzer->widget(1));  // 索引1：H.264码流分析
            if (h264Tab) {
                h264Tab->saveLog();
            }
        } catch (const std::exception &e) {
            qWarning() << "保存音视频解析日志失败：" << e.what();
        }
    }
}
