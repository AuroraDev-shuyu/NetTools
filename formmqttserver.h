#ifndef FORMMQTTSERVER_H
#define FORMMQTTSERVER_H

#include <QWidget>
#include <QHash>
#include <QList>
#include <QByteArray>
#include <QStringList>

class QTcpServer;
class QTcpSocket;
class QTextEdit;

namespace Ui {
class FormMqttServer;
}

// 单个客户端的会话信息
struct MqttClientSession {
    QString clientId;      // 客户端ID
    QString username;      // 用户名（CONNECT报文中携带）
    QStringList topicFilters; // 该客户端订阅的主题过滤器
};

class FormMqttServer : public QWidget
{
    Q_OBJECT

public:
    explicit FormMqttServer(QWidget *parent = nullptr);
    ~FormMqttServer();

    void saveLog();

private slots:
    void on_pushButton_MQTTServerStart_clicked();
    void on_pushButton_MQTTServerStop_clicked();
    void on_pushButton_MQTTServerSendMsg_clicked();
    void on_pushButton_MQTTServerClose_clicked();

private:
    Ui::FormMqttServer *ui;
    QTcpServer *m_tcpServer = nullptr;                 // TCP监听
    bool m_serverRunning = false;                      // 是否正在监听
    QHash<QTcpSocket *, QByteArray> m_recvBuffers;     // 各连接接收缓冲区
    QHash<QTcpSocket *, MqttClientSession> m_clients;  // 已连接的客户端
    QHash<QString, QList<QTcpSocket *>> m_subscriptions; // 主题过滤器 → 订阅者列表

    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

    void parsePackets(QTcpSocket *socket);
    void handlePacket(QTcpSocket *socket, quint8 typeAndFlags, const QByteArray &body);
    void handleConnect(QTcpSocket *socket, const QByteArray &body);
    void handlePublish(QTcpSocket *socket, quint8 flags, const QByteArray &body);
    void handleSubscribe(QTcpSocket *socket, const QByteArray &body);
    void handleUnsubscribe(QTcpSocket *socket, const QByteArray &body);

    void sendToClient(QTcpSocket *socket, const QByteArray &packet);
    void deliverMessage(const QString &topic, const QByteArray &payload); // 路由发布给订阅者
    void removeClient(QTcpSocket *socket);                                // 清理连接与订阅
    void appendLog(const QString &text);                                  // 追加日志并自动滚动
    void trimLog(int keepBlocks = 1000, int trimStep = 200);              // 裁剪日志防止卡顿
    void saveTextEditToFile(QTextEdit *textEdit);                         // 保存日志到文件
};

#endif // FORMMQTTSERVER_H
