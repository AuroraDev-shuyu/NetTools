#ifndef FORMQUICSERVER_H
#define FORMQUICSERVER_H

#include <QWidget>
#include <QColor>

#include "quicserver.h"

namespace Ui {
class FormQuicServer;
}

class QTextEdit;
class QCloseEvent;

class FormQuicServer : public QWidget
{
    Q_OBJECT

public:
    explicit FormQuicServer(QWidget *parent = nullptr);
    ~FormQuicServer();

    void saveLog(); // 保存日志（供主窗口关闭时调用）

protected:
    // 窗口关闭事件：保存日志
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_pushButton_QUICServerStart_clicked();   // 启动服务器
    void on_pushButton_QUICServerStop_clicked();    // 停止服务器
    void on_pushButton_QUICServerSendMsg_clicked(); // 广播消息
    void on_pushButton_QUICServerClose_clicked();   // 退出程序

private:
    void appendLog(const QString &text, const QColor &color = QColor("#666666")); // 追加彩色日志
    void trimLog(int keepBlocks = 0, int trimStep = 0);     // 裁剪日志防止UI卡顿
    void saveTextEditToFile(QTextEdit *textEdit);           // 日志落盘

    Ui::FormQuicServer *ui;
    QuicServer m_quicServer;   // QUIC服务器封装
    bool serverRunning = false; // 服务器运行状态
};

#endif // FORMQUICSERVER_H
