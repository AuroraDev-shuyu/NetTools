#ifndef FORMHTTPCLIENT_H
#define FORMHTTPCLIENT_H

#include <QWidget>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QList>

class QTimer;
class QPlainTextEdit;

namespace Ui {
class FormHttpClient;
}

class FormHttpClient : public QWidget
{
    Q_OBJECT

public:
    explicit FormHttpClient(QWidget *parent = nullptr);
    ~FormHttpClient();

    void saveLog();

private slots:
    void on_pushButton_HTTPClientConnect_clicked();
    void on_pushButton_HTTPClientDisconnect_clicked();
    void on_pushButton_HTTPClientSendMsg_clicked();
    void on_pushButton_HTTPClientClose_clicked();
    void on_checkBox_HTTPClientAutoTesting_clicked();

private:
    Ui::FormHttpClient *ui;
    QNetworkAccessManager *m_netManager = nullptr; // HTTP网络管理器
    QTimer *m_autoTimer = nullptr;                 // 自动测试定时器
    QString m_baseUrl;                             // http基础地址，如 http://127.0.0.1:12345
    QList<QNetworkReply *> m_pendingReplies;       // 未完成的请求列表
    bool m_connected = false;                      // 是否已连接（已设置目标服务器）

    void sendRequest();                            // 按当前参数发送一次HTTP请求
    void abortPendingReplies();                    // 中止所有未完成的请求
    void setCommunicationControls(bool connected); // 批量更新按钮可用状态
    void appendLog(const QString &text);           // 追加日志并自动滚动
    void trimLog(int keepBlocks = 1000, int trimStep = 200); // 裁剪日志防止卡顿
    void savePlainTextEditToFile(QPlainTextEdit *plainTextEdit);
};

#endif // FORMHTTPCLIENT_H
