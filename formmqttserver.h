#ifndef FORMMQTTSERVER_H
#define FORMMQTTSERVER_H

#include <QWidget>
#include <QHash>
#include <QList>
#include <QByteArray>
#include <QStringList>
#include <QElapsedTimer>

#include "mqttprotocol.h"

class QTcpServer;
class QTcpSocket;
class QTextEdit;
class QTimer;

namespace Ui {
class FormMqttServer;
}

// 单个客户端的会话信息
struct MqttClientSession {
    QString clientId;      // 客户端ID
    QString username;      // 用户名（CONNECT报文中携带）
    QStringList topicFilters; // 该客户端订阅的主题过滤器
    MqttProtocol::WillConfig will; // 遗嘱配置
    bool willFlag = false;        // 是否携带遗嘱
    bool cleanDisconnect = false; // 已收到DISCONNECT报文（干净断开，不发布遗嘱）
    quint16 keepAliveSeconds = 0; // 保活周期（0=不启用）
    qint64 lastActivityMs = 0;    // 最近一次收包时间（相对m_retryClock）
};

// 保留消息（retain）：主题最后一次保留发布的内容
struct MqttRetainedMessage {
    QByteArray payload;
    quint8 qos = 0;
};

// 入站QoS2流程中暂存的消息（PUBLISH→PUBREC→等待PUBREL后才投递）
struct MqttPendingQos2 {
    QString topic;
    QByteArray payload;
};

// 出站待确认的QoS1/QoS2投递（含重传状态）
struct MqttOutgoingDelivery {
    QByteArray packet;     // 待确认的完整报文（QoS2第二阶段为PUBREL）
    quint8 state = 0;      // 0=等待PUBACK, 1=等待PUBREC, 2=等待PUBCOMP
    int retries = 0;       // 已重传次数
    qint64 nextRetryMs = 0; // 下次重传时刻（相对m_retryClock）
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
    QHash<QString, QHash<QTcpSocket *, quint8>> m_subscriptions; // 主题过滤器 → (订阅者 → 授权QoS)
    QHash<QString, MqttRetainedMessage> m_retained;    // 保留消息存储
    QHash<QTcpSocket *, QHash<quint16, MqttPendingQos2>> m_qos2Inbound;  // 入站QoS2暂存
    QHash<QTcpSocket *, QHash<quint16, MqttOutgoingDelivery>> m_outgoing; // 出站待确认投递
    QHash<QTcpSocket *, quint16> m_nextPacketId;       // 各连接出站报文标识符分配
    QTimer *m_retryTimer = nullptr;                    // 出站QoS重传定时器
    QElapsedTimer m_retryClock;                        // 重传计时基准

    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

    void parsePackets(QTcpSocket *socket);
    void handlePacket(QTcpSocket *socket, quint8 typeAndFlags, const QByteArray &body);
    void handleConnect(QTcpSocket *socket, const QByteArray &body);
    void handlePublish(QTcpSocket *socket, quint8 flags, const QByteArray &body);
    void handleSubscribe(QTcpSocket *socket, const QByteArray &body);
    void handleUnsubscribe(QTcpSocket *socket, const QByteArray &body);
    void handleOutgoingAck(QTcpSocket *socket, quint8 packetType, const QByteArray &body);
    void retryPendingDeliveries();
    void checkKeepAliveTimeouts();
    void applyRetainedMessage(const QString &topic, const QByteArray &payload, quint8 qos);

    void sendToClient(QTcpSocket *socket, const QByteArray &packet);
    quint16 allocateOutgoingPacketId(QTcpSocket *socket);                  // 分配出站报文标识符
    void sendPublishToSubscriber(QTcpSocket *socket, const QString &topic, // 按授权QoS投递给单个订阅者
                                 const QByteArray &payload, quint8 qos, bool retain);
    void deliverMessage(const QString &topic, const QByteArray &payload,   // 路由发布给所有订阅者
                        quint8 incomingQos, bool retain = false);
    void removeClient(QTcpSocket *socket);                                // 清理连接与订阅
    void appendLog(const QString &text);                                  // 追加日志并自动滚动
    void trimLog(int keepBlocks = 1000, int trimStep = 200);              // 裁剪日志防止卡顿
    void saveTextEditToFile(QTextEdit *textEdit);                         // 保存日志到文件
};

#endif // FORMMQTTSERVER_H
