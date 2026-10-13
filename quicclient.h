#ifndef QUICCLIENT_H
#define QUICCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QString>

#include <msquic.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

class QTimer;

// MsQuic QUIC 客户端封装（接口对齐 network.h，表单可照抄 formtcpclient 用法）
//
// 线程模型：MsQuic 回调在内部 worker 线程触发，本类直接在回调线程 emit 信号
// （Qt 跨线程自动 queued 到表单线程）。成员跨线程访问均加锁。
//
// 关键 MsQuic 语义（已实测验证）：
//   1. RECEIVE 回调内调用 StreamReceiveComplete 后，必须返回
//      QUIC_STATUS_PENDING——若返回 SUCCESS，MsQuic 会按 TotalBufferLength
//      再自动完成一次，导致 RecvPendingLength 下溢、接收永久停发。
//   2. SEND_COMPLETE 事件里 delete ClientContext 指向的 QuicSendBuffer
//      （Canceled=true 同样要释放）。
//   3. 回调内禁止调用 RegistrationClose（文档注明会死锁），必须等
//      SHUTDOWN_COMPLETE 后再收尾。
class QuicClient : public QObject
{
    Q_OBJECT
public:
    explicit QuicClient(QObject *parent = nullptr);
    ~QuicClient();

    bool ClientConnectToServer(QString ServerIpAddress, int ServerPort);
    void ClientSendMsgToServer(const QString &StrData);
    void DisconnectFromHost();
    void StartTimerOutFunc();
    void StopTimerOutFunc();
    void setAutoTestMessage(const QString &message);

    bool isConnected() const { return m_conn != nullptr && m_streamReady; }
    QString strTempData; // 兼容 network.h 的公共成员

Q_SIGNALS:
    void connectionEstablished();
    void connectionFailed(const QString &errorString);
    void dataReceived(const QString &data);

private:
    static QUIC_STATUS ConnCallback(HQUIC conn, void *context, QUIC_CONNECTION_EVENT *ev);
    static QUIC_STATUS StreamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *ev);

    HQUIC m_registration = nullptr; // RegistrationOpen
    HQUIC m_configuration = nullptr; // ConfigurationOpen
    HQUIC m_conn = nullptr;          // ConnectionOpen
    HQUIC m_stream = nullptr;        // StreamOpen

    std::atomic<bool> m_streamReady{false};  // START_COMPLETE 已到达，可发送
    std::atomic<bool> m_shutdownByApp{false}; // 应用主动断开（抑制失败信号）
    std::atomic<bool> m_shutdownComplete{false}; // SHUTDOWN_COMPLETE 已到达

    std::mutex m_mx;               // 保护以下成员
    std::condition_variable m_cv;  // 等待 SHUTDOWN_COMPLETE
    QByteArray m_recvBuf;          // 接收缓冲：QUIC流是字节流，按\n分帧
    int m_expectedPeerShutdown = 0; // 0=正常，1=收到对端关闭

    QTimer *m_timer = nullptr;     // 自动测试定时器
    std::atomic<int> m_testMessageCount{0};
    QString m_autoTestMessage;
};

#endif // QUICCLIENT_H
