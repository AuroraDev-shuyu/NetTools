#ifndef FORMAVANALYZER_H
#define FORMAVANALYZER_H

#include <QWidget>

#include "formh264analyzer.h"
#include "formrtpanalyzer.h"

namespace Ui {
class FormAvAnalyzer;
}

// 「音视频解析」复合页：内部 QTabWidget 挂 RTP/RTCP 解析与 H.264 码流分析两个子页，
// saveLog 转发给两个子表单（模式同 formdataprocessor）。
class FormAvAnalyzer : public QWidget
{
    Q_OBJECT

public:
    explicit FormAvAnalyzer(QWidget *parent = nullptr);
    ~FormAvAnalyzer();

    void saveLog();

private:
    Ui::FormAvAnalyzer *ui;
};

#endif // FORMAVANALYZER_H
