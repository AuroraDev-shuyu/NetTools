#ifndef FORMMQTTCLIENT_H
#define FORMMQTTCLIENT_H

#include <QWidget>

class QTcpSocket;
class QTimer;
class QPlainTextEdit;

namespace Ui {
class FormMqttClient;
}

class FormMqttClient : public QWidget
{
    Q_OBJECT

public:
    explicit FormMqttClient(QWidget *parent = nullptr);
    ~FormMqttClient();

    void saveLog();

private slots:
    void on_pushButton_MQTTClientConnect_clicked();
    void on_pushButton_MQTTClientDisconnect_clicked();
    void on_pushButton_MQTTClientSendMsg_clicked();
    void on_pushButton_MQTTClientClose_clicked();
    void on_checkBox_MQTTClientAutoTesting_clicked();

private:
    Ui::FormMqttClient *ui;
    QTcpSocket *m_socket = nullptr;        // 到Broker的TCP连接
    QByteArray m_recvBuf;                  // 接收缓冲区
    QTimer *m_keepAliveTimer = nullptr;    // 心跳定时器
    QTimer *m_autoTimer = nullptr;         // 自动测试定时器
    bool m_connected = false;              // 是否已完成MQTT连接（收到CONNACK）
    quint16 m_nextPacketId = 1;            // 报文标识符分配器
    QString m_clientId;                    // 当前使用的客户端ID

    void sendPacket(const QByteArray &packet);         // 发送报文
    void parsePackets();                               // 解析接收缓冲区中的报文
    void handlePacket(quint8 typeAndFlags, const QByteArray &body);
    quint16 nextPacketId();                            // 分配下一个报文标识符
    void publishMessage();                             // 发布一条测试消息
    void resetConnectionState();                       // 复位连接状态与UI
    void setCommunicationControls(bool connected);     // 批量更新按钮可用状态
    void appendLog(const QString &text);               // 追加日志并自动滚动
    void trimLog(int keepBlocks = 1000, int trimStep = 200); // 裁剪日志防止卡顿
    void savePlainTextEditToFile(QPlainTextEdit *plainTextEdit);
};

#endif // FORMMQTTCLIENT_H
