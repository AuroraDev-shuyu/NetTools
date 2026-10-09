#ifndef NETWORK_H
#define NETWORK_H
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QtCore>
#include <QHostAddress>
#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QMutex>
#include <QAtomicInt>
#include <QStringBuilder>
#include <memory.h>
#include <atomic>
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
class Network : public QObject
{
    Q_OBJECT
public:
    explicit Network(QObject *parent = nullptr);
    ~Network(void);
public:
    bool ClientConnectToServer(QString ServerIpAddress, int ServerPort);
    void ClientSendMsgToServer(const QString &StrData);
    void ClientSendBytesToServer(const QByteArray &data);
    void setSocketOptions();
    struct ConnectionStats{
        qint64 connectionTime=0;
        qint64 bytesSent=0;
        qint64 bytesReceived=0;
        qint64 messagesCount=0;
    };
    ConnectionStats getConnectionStats() const;
    QString strTempData;
private:
    QTcpSocket *socket;
    QTimer *timer;
    std::atomic<int> testMessageCount;
    QString autoTestMessage;
    QByteArray receiveBuffer;
    QElapsedTimer performanceTimer;
    mutable QMutex bufferMutex;

Q_SIGNALS:
    void connectionEstablished();
    void connectionFailed(const QString &errorString);
    void dataReceived(const QString &data);
public Q_SLOTS: //
    void ClientDisconnectedFunc(void);
    void DisconnectFromHost();
    void ReadServerMsg();
    void StartTimerOutFunc();
    void setAutoTestMessage(const QString &message);
    void StopTimerOutFunc();
private Q_SLOTS:
    void onConnected();
    void onSocketError(QAbstractSocket::SocketError error);
};
#endif // NETWORK_H
