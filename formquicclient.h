#ifndef FORMQUICCLIENT_H
#define FORMQUICCLIENT_H

#include <QWidget>

#include "quicclient.h"

namespace Ui {
class FormQuicClient;
}

class QPlainTextEdit;
class QCloseEvent;

class FormQuicClient : public QWidget
{
    Q_OBJECT

public:
    explicit FormQuicClient(QWidget *parent = nullptr);
    ~FormQuicClient();

    void saveLog(); // 保存日志（供主窗口关闭时调用）

protected:
    // 窗口关闭事件：断开连接并保存日志
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_pushButton_QUICClientConnect_clicked();    // 连接服务器
    void on_pushButton_QUICClientDisconnect_clicked(); // 断开服务器
    void on_pushButton_QUICClientSendMsg_clicked();    // 发送消息
    void on_pushButton_QUICClientClose_clicked();      // 退出程序
    void on_checkBox_QUICClientAutoTesting_clicked();  // 自动化测试开关

private:
    // 批量更新与连接状态相关的按钮/输入控件
    void enableCommunicationControls(bool enable);
    void appendLog(const QString &text);                          // 追加日志并自动滚动
    void trimLog(int keepBlocks = 0, int trimStep = 0);           // 裁剪日志防止UI卡顿
    void savePlainTextEditToFile(QPlainTextEdit *plainTextEdit);  // 日志落盘

    Ui::FormQuicClient *ui;
    QuicClient m_networkClient; // QUIC客户端封装（接口对齐network.h）
    bool connected = false;     // 连接状态标记
};

#endif // FORMQUICCLIENT_H
