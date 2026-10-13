#ifndef QUICSERVER_H
#define QUICSERVER_H

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>

#include <msquic.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

// MsQuic QUIC 服务器封装
//
// 线程模型：MsQuic 回调在内部 worker 线程触发，本类直接在回调线程 emit 信号
// （Qt 跨线程自动 queued 到表单线程）。连接表由 m_connMx 保护。
//
// 证书：Schannel 加载 PKCS12 文件失败（E_NOINTERFACE），改用
// QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE 从当前用户"My"证书库按
// SHA1 指纹加载（见 quicserver.cpp 的 kServerCertThumbprint）。
//
// 关键 MsQuic 语义（已实测验证）：
//   1. LISTENER NEW_CONNECTION 回调内必须完成 SetCallbackHandler +
//      ConnectionSetConfiguration，否则握手不进行。
//   2. 对端发起的流本端无需（也不能）StreamStart——实测返回 INVALID_STATE
//      （0x8007139F）；只需在 PEER_STREAM_STARTED 里对流 SetCallbackHandler。
//   3. RECEIVE 回调内调用 StreamReceiveComplete 后必须返回
//      QUIC_STATUS_PENDING，否则接收会永久停发（详见 quicclient.h）。
class QuicServer : public QObject
{
    Q_OBJECT
public:
    explicit QuicServer(QObject *parent = nullptr);
    ~QuicServer();

    bool start(int port, const QString &bindIp = QString()); // 启动监听
    void stop();                                             // 停止监听并断开所有客户端
    void sendToAll(const QString &message);                  // 向所有客户端广播
    int clientCount();                                       // 当前连接数
    QString lastError() const { return m_lastError; }        // 最近一次错误（中文）

Q_SIGNALS:
    void onNewConn(const QString &peerAddr); // 新客户端接入（ip:port）
    void onDataReceived(const QString &data); // 收到一条客户端消息（已按\n分帧）
    void onConnClosed();                     // 有客户端断开

private:
    // 单个连接的上下文（连接与流回调共享；连接 SHUTDOWN_COMPLETE 时释放）
    struct ConnCtx
    {
        QuicServer *owner = nullptr;
        HQUIC conn = nullptr;
        HQUIC stream = nullptr;
        uint64_t id = 0;
        QString peerAddr;
        QByteArray recvBuf; // 接收缓冲：按\n分帧
        std::mutex bufMx;
    };

    static QUIC_STATUS ListenerCallback(HQUIC listener, void *context, QUIC_LISTENER_EVENT *ev);
    static QUIC_STATUS ConnCallback(HQUIC conn, void *context, QUIC_CONNECTION_EVENT *ev);
    static QUIC_STATUS StreamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *ev);

    bool loadServerCredential();  // 从证书库加载服务器证书
    void removeConn(uint64_t id); // 移除并释放连接上下文（须未持 m_connMx）

    HQUIC m_registration = nullptr;
    HQUIC m_configuration = nullptr;
    HQUIC m_listener = nullptr;

    QHash<uint64_t, ConnCtx *> m_conns; // 连接ID → 连接上下文
    QMutex m_connMx;                    // 保护 m_conns
    uint64_t m_nextConnId = 1;

    std::atomic<bool> m_stopComplete{false}; // Listener STOP_COMPLETE 已到达
    std::mutex m_stopMx;
    std::condition_variable m_stopCv;

    QString m_lastError; // 最近一次错误描述（仅 Qt 线程访问）
};

#endif // QUICSERVER_H
