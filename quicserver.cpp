#include "quicserver.h"
#include "quiccommon.h"

#include <QDebug>
#include <QHostAddress>
#include <QList>

#include <chrono>
#include <cstring>
#include <thread>

// 服务器证书：当前用户"My"证书库中的自签名证书 SHA1 指纹
// （Schannel 不支持从 PKCS12 文件直接加载，故用证书库指纹方式）
static const char kServerCertThumbprint[] = "D980149B717769F06CB1ADEE9E1730C119CD4AC5";

// hex字符串（40字符）解码为20字节SHA1指纹
static bool HexToSha1(const char *hex, uint8_t out[20])
{
    if (strlen(hex) != 40) {
        return false;
    }
    auto nib = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };
    for (int i = 0; i < 20; ++i) {
        int hi = nib(hex[i * 2]), lo = nib(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

// QUIC_ADDR 格式化为 "ip:port"（IPv4；其他族显示"未知地址"）
static QString quicAddrToString(const QUIC_ADDR *addr)
{
    if (!addr) {
        return QStringLiteral("未知地址");
    }
    if (addr->Ipv4.sin_family == AF_INET) {
        const uint8_t *b = (const uint8_t *)&addr->Ipv4.sin_addr;
        return QStringLiteral("%1.%2.%3.%4:%5")
            .arg(b[0]).arg(b[1]).arg(b[2]).arg(b[3])
            .arg(ntohs(addr->Ipv4.sin_port));
    }
    return QStringLiteral("未知地址");
}

// 功能：构造函数——初始化MsQuic Registration
QuicServer::QuicServer(QObject *parent)
    : QObject(parent)
{
    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api) {
        m_lastError = QStringLiteral("MsQuicOpen2失败");
        return;
    }
    QUIC_REGISTRATION_CONFIG regCfg = {"NetToolsQuicServer", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    if (QUIC_FAILED(api->RegistrationOpen(&regCfg, &m_registration))) {
        m_registration = nullptr;
        m_lastError = QStringLiteral("注册失败");
    }
}

// 析构函数：停止监听、断开所有连接并回收句柄
QuicServer::~QuicServer()
{
    stop();
    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api) {
        return;
    }
    if (m_configuration) {
        api->ConfigurationClose(m_configuration);
        m_configuration = nullptr;
    }
    if (m_registration) {
        api->RegistrationClose(m_registration); // 阻塞至全部子句柄关闭
        m_registration = nullptr;
    }
}

// 功能：加载服务器证书（证书库指纹方式，Schannel 后端）
bool QuicServer::loadServerCredential()
{
    const QUIC_API_TABLE *api = GetQuicApi();
    QUIC_CERTIFICATE_HASH_STORE hashStore{};
    if (!HexToSha1(kServerCertThumbprint, hashStore.ShaHash)) {
        m_lastError = QStringLiteral("证书指纹格式错误");
        return false;
    }
    strcpy_s(hashStore.StoreName, sizeof(hashStore.StoreName), "My");

    QUIC_CREDENTIAL_CONFIG cred{};
    cred.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE;
    cred.Flags = QUIC_CREDENTIAL_FLAG_NONE;
    cred.CertificateHashStore = &hashStore;
    QUIC_STATUS st = api->ConfigurationLoadCredential(m_configuration, &cred);
    if (QUIC_FAILED(st)) {
        m_lastError = QStringLiteral("加载证书失败: %1").arg(quicStatusToString(st));
        return false;
    }
    return true;
}

// 功能：启动监听——建立Configuration并开始监听端口
bool QuicServer::start(int port, const QString &bindIp)
{
    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api || !m_registration) {
        m_lastError = QStringLiteral("MsQuic未初始化");
        return false;
    }
    if (m_listener) {
        m_lastError = QStringLiteral("服务器已在运行");
        return false;
    }

    // 上次启动遗留的Configuration（stop后未释放）先关闭，支持反复启停
    if (m_configuration) {
        api->ConfigurationClose(m_configuration);
        m_configuration = nullptr;
    }

    QUIC_SETTINGS settings;
    InitQuicSettings(settings);
    QUIC_BUFFER alpn = NetToolsAlpn();
    if (QUIC_FAILED(api->ConfigurationOpen(m_registration, &alpn, 1, &settings,
                                           sizeof(settings), nullptr, &m_configuration))) {
        m_lastError = QStringLiteral("创建配置失败");
        return false;
    }
    if (!loadServerCredential()) {
        api->ConfigurationClose(m_configuration);
        m_configuration = nullptr;
        return false;
    }

    m_stopComplete = false;
    if (QUIC_FAILED(api->ListenerOpen(m_registration, ListenerCallback, this, &m_listener))) {
        m_lastError = QStringLiteral("创建监听失败");
        api->ConfigurationClose(m_configuration);
        m_configuration = nullptr;
        m_listener = nullptr;
        return false;
    }

    // 绑定地址：bindIp 为空则监听所有地址
    QUIC_ADDR addr{};
    addr.Ipv4.sin_family = AF_INET;
    addr.Ipv4.sin_port = htons((uint16_t)port);
    if (bindIp.isEmpty()) {
        addr.Ipv4.sin_addr.s_addr = INADDR_ANY;
    } else {
        QHostAddress ha(bindIp);
        if (ha.protocol() != QAbstractSocket::IPv4Protocol || !ha.toIPv4Address()) {
            m_lastError = QStringLiteral("监听地址无效: %1").arg(bindIp);
            api->ListenerClose(m_listener);
            api->ConfigurationClose(m_configuration);
            m_listener = nullptr;
            m_configuration = nullptr;
            return false;
        }
        addr.Ipv4.sin_addr.s_addr = htonl(ha.toIPv4Address());
    }

    QUIC_STATUS st = api->ListenerStart(m_listener, &alpn, 1, &addr);
    if (QUIC_FAILED(st)) {
        m_lastError = quicStatusToString(st); // ADDRESS_IN_USE→"端口被占用"
        api->ListenerClose(m_listener);
        api->ConfigurationClose(m_configuration);
        m_listener = nullptr;
        m_configuration = nullptr;
        return false;
    }
    return true;
}

// 功能：停止监听——等待STOP_COMPLETE，然后逐个断开客户端连接
void QuicServer::stop()
{
    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api) {
        return;
    }

    if (m_listener) {
        api->ListenerStop(m_listener);
        {
            std::unique_lock<std::mutex> lk(m_stopMx);
            m_stopCv.wait_for(lk, std::chrono::seconds(3),
                              [this]() { return m_stopComplete.load(); });
        }
        api->ListenerClose(m_listener);
        m_listener = nullptr;
    }

    // 逐个优雅断开连接，等待连接表排空（SHUTDOWN_COMPLETE 回调里移除）
    {
        QList<HQUIC> conns;
        {
            QMutexLocker locker(&m_connMx);
            for (ConnCtx *ctx : qAsConst(m_conns)) {
                conns.append(ctx->conn);
            }
        }
        for (HQUIC conn : conns) {
            api->ConnectionShutdown(conn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        }
        // 等待连接回调移除上下文（3秒兜底后强制关闭残留）
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                QMutexLocker locker(&m_connMx);
                if (m_conns.isEmpty()) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        {
            QMutexLocker locker(&m_connMx);
            for (ConnCtx *ctx : qAsConst(m_conns)) {
                if (ctx->conn) {
                    api->ConnectionClose(ctx->conn);
                }
                delete ctx;
            }
            m_conns.clear();
        }
    }
}

// 功能：向所有已连接的客户端广播消息（自动补'\n'分帧）
void QuicServer::sendToAll(const QString &message)
{
    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api) {
        return;
    }
    QString framed = message;
    if (!framed.endsWith('\n')) {
        framed += '\n';
    }

    QMutexLocker locker(&m_connMx); // 持锁期间上下文不会被 SHUTDOWN_COMPLETE 释放
    for (ConnCtx *ctx : qAsConst(m_conns)) {
        if (!ctx->stream) {
            continue;
        }
        QuicSendBuffer *sb = new QuicSendBuffer;
        sb->data = framed.toUtf8();
        sb->buf = {(uint32_t)sb->data.size(), (uint8_t *)sb->data.constData()};
        QUIC_STATUS st = api->StreamSend(ctx->stream, &sb->buf, 1, QUIC_SEND_FLAG_NONE, sb);
        if (QUIC_FAILED(st)) {
            delete sb;
        }
    }
}

// 功能：当前客户端连接数
int QuicServer::clientCount()
{
    QMutexLocker locker(&m_connMx);
    return m_conns.size();
}

// 功能：从连接表移除并释放连接上下文（调用方须未持 m_connMx）
void QuicServer::removeConn(uint64_t id)
{
    ConnCtx *ctx = nullptr;
    {
        QMutexLocker locker(&m_connMx);
        ctx = m_conns.take(id);
    }
    if (ctx) {
        emit onConnClosed();
        delete ctx;
    }
}

// 功能：监听事件回调（MsQuic worker 线程触发）
QUIC_STATUS QuicServer::ListenerCallback(HQUIC listener, void *context, QUIC_LISTENER_EVENT *ev)
{
    Q_UNUSED(listener);
    QuicServer *s = (QuicServer *)context;
    const QUIC_API_TABLE *api = GetQuicApi();

    if (ev->Type == QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        ConnCtx *ctx = new ConnCtx;
        ctx->owner = s;
        ctx->conn = ev->NEW_CONNECTION.Connection;
        ctx->peerAddr = quicAddrToString(ev->NEW_CONNECTION.Info->RemoteAddress);
        {
            QMutexLocker locker(&s->m_connMx);
            ctx->id = s->m_nextConnId++;
            s->m_conns.insert(ctx->id, ctx);
        }
        // 两个动作都必须在回调内完成，否则握手不进行
        api->SetCallbackHandler(ev->NEW_CONNECTION.Connection, ConnCallback, ctx);
        api->ConnectionSetConfiguration(ev->NEW_CONNECTION.Connection, s->m_configuration);
        emit s->onNewConn(ctx->peerAddr);
    } else if (ev->Type == QUIC_LISTENER_EVENT_STOP_COMPLETE) {
        s->m_stopComplete = true;
        s->m_stopCv.notify_all();
    }
    return QUIC_STATUS_SUCCESS;
}

// 功能：连接事件回调（context 为 ConnCtx*）
QUIC_STATUS QuicServer::ConnCallback(HQUIC conn, void *context, QUIC_CONNECTION_EVENT *ev)
{
    ConnCtx *ctx = (ConnCtx *)context;
    QuicServer *s = ctx->owner;
    const QUIC_API_TABLE *api = GetQuicApi();

    switch (ev->Type) {
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        // 对端发起的流本端无需StreamStart（实测返回INVALID_STATE），只设回调
        ctx->stream = ev->PEER_STREAM_STARTED.Stream;
        api->SetCallbackHandler(ev->PEER_STREAM_STARTED.Stream, StreamCallback, ctx);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        api->ConnectionShutdown(conn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        // 该事件是此连接的最后一个回调，其后可安全释放上下文。
        // removeConn 内部发 onConnClosed 信号并 delete ctx；返回后不再触碰 ctx。
        api->ConnectionClose(conn);
        s->removeConn(ctx->id);
        break;

    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

// 功能：流事件回调（context 为 ConnCtx*）
QUIC_STATUS QuicServer::StreamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *ev)
{
    ConnCtx *ctx = (ConnCtx *)context;
    QuicServer *s = ctx->owner;
    const QUIC_API_TABLE *api = GetQuicApi();

    switch (ev->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        // 关键：内联归还流量窗口后必须返回 QUIC_STATUS_PENDING（见 quicclient.h 注释）
        api->StreamReceiveComplete(stream, ev->RECEIVE.TotalBufferLength);
        {
            std::lock_guard<std::mutex> lk(ctx->bufMx);
            for (uint32_t i = 0; i < ev->RECEIVE.BufferCount; ++i) {
                ctx->recvBuf.append((const char *)ev->RECEIVE.Buffers[i].Buffer,
                                    ev->RECEIVE.Buffers[i].Length);
            }
        }
        // 按\n逐帧提取完整消息：发信号给表单并自动回显
        for (;;) {
            QString frame;
            {
                std::lock_guard<std::mutex> lk(ctx->bufMx);
                int pos = ctx->recvBuf.indexOf('\n');
                if (pos < 0) {
                    break;
                }
                frame = QString::fromUtf8(ctx->recvBuf.left(pos));
                ctx->recvBuf.remove(0, pos + 1);
            }
            emit s->onDataReceived(frame);
            // 自动回显（对齐TCP服务端行为）：客户端据此确认消息送达
            QuicSendBuffer *sb = new QuicSendBuffer;
            sb->data = ("Server response: " + frame + "\n").toUtf8();
            sb->buf = {(uint32_t)sb->data.size(), (uint8_t *)sb->data.constData()};
            QUIC_STATUS st = api->StreamSend(stream, &sb->buf, 1, QUIC_SEND_FLAG_NONE, sb);
            if (QUIC_FAILED(st)) {
                delete sb;
            }
        }
        return QUIC_STATUS_PENDING;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        // 无论是否被取消都要释放发送缓冲区
        delete (QuicSendBuffer *)ev->SEND_COMPLETE.ClientContext;
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        api->StreamClose(stream);
        ctx->stream = nullptr;
        break;

    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}
