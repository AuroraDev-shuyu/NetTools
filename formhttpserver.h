#ifndef FORMHTTPSERVER_H
#define FORMHTTPSERVER_H

#include <QWidget>

class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponse;
class QTextEdit;

namespace Ui {
class FormHttpServer;
}

class FormHttpServer : public QWidget
{
    Q_OBJECT

public:
    explicit FormHttpServer(QWidget *parent = nullptr);
    ~FormHttpServer();

    void saveLog();

private slots:
    void on_pushButton_HTTPServerStart_clicked();
    void on_pushButton_HTTPServerStop_clicked();
    void on_pushButton_HTTPServerSendMsg_clicked();
    void on_pushButton_HTTPServerClose_clicked();

private:
    Ui::FormHttpServer *ui;
    QHttpServer *m_httpServer = nullptr;   // HTTP服务器实例，销毁即停止监听
    bool m_serverRunning = false;          // 是否正在监听

    QHttpServerResponse makeResponse(const QHttpServerRequest &request); // 构造响应
    void appendRequestLog(const QHttpServerRequest &request);            // 记录请求日志
    void appendLog(const QString &text);                                 // 追加日志并自动滚动
    void trimLog(int keepBlocks = 1000, int trimStep = 200);             // 裁剪日志防止卡顿
    void saveTextEditToFile(QTextEdit *textEdit);                        // 保存日志到文件
};

#endif // FORMHTTPSERVER_H
