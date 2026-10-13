#include "quicclient.h"
#include "quiccommon.h"

#include <QDebug>
#include <QTimer>

// 功能：构造函数——初始化MsQuic（Registration/Configuration/无证书凭据）与自动测试定时器
QuicClient::QuicClient(QObject *parent)
    : QObject(parent)
{
    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api) {
        qWarning() << "QuicClient: MsQuicOpen2 失败，无法初始化";
        return;
    }

    QUIC_REGISTRATION_CONFIG regCfg = {"NetToolsQuicClient", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    if (QUIC_FAILED(api->RegistrationOpen(&regCfg, &m_registration))) {
        qWarning() << "QuicClient: RegistrationOpen 失败";
        m_registration = nullptr;
        return;
    }

    QUIC_SETTINGS settings;
    InitQuicSettings(settings);
    QUIC_BUFFER alpn = NetToolsAlpn();
    if (QUIC_FAILED(api->ConfigurationOpen(m_registration, &alpn, 1, &settings,
                                           sizeof(settings), nullptr, &m_configuration))) {
        qWarning() << "QuicClient: ConfigurationOpen 失败";
        m_configuration = nullptr;
        return;
    }

    // 客户端：无证书 + 跳过证书校验（调试工具连自签证书服务器）
    QUIC_CREDENTIAL_CONFIG cred{};
    cred.Type = QUIC_CREDENTIAL_TYPE_NONE;
    cred.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    if (QUIC_FAILED(api->ConfigurationLoadCredential(m_configuration, &cred))) {
        qWarning() << "QuicClient: LoadCredential 失败";
    }

    // 自动测试定时器：每1.5秒发送一次（对齐 TCP 客户端）
    m_timer = new QTimer(this);
    m_timer->setInterval(1500);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (!isConnected()) {
            m_timer->stop();
            return;
        }
        const int currentCount = m_testMessageCount.fetch_add(1);
        QString messageToSend;
        if (!m_autoTestMessage.isEmpty()) {
            messageToSend = m_autoTestMessage;
            if (messageToSend.contains("%1")) {
                messageToSend = messageToSend.arg(currentCount);
            }
        } else {
            messageToSend = QStringLiteral("\n[Prompt:Client automatic testing(%1)]").arg(currentCount);
        }
        ClientSendMsgToServer(messageToSend);
    });
}

// 析构函数：断开连接，等待回调全部结束后再回收句柄
QuicClient::~QuicClient()
{
    DisconnectFromHost();

    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api) {
        return;
    }
    if (m_configuration) {
        api->ConfigurationClose(m_configuration);
        m_configuration = nullptr;
    }
    if (m_registration) {
        // 阻塞至全部子句柄关闭；此前已等待 SHUTDOWN_COMPLETE，无在途回调
        api->RegistrationClose(m_registration);
        m_registration = nullptr;
    }
}

// 功能：发起连接——建立QUIC连接并打开双向流
bool QuicClient::ClientConnectToServer(QString ServerIpAddress, int ServerPort)
{
    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api || !m_registration || !m_configuration) {
        emit connectionFailed(QStringLiteral("MsQuic未初始化"));
        return false;
    }
    if (m_conn) {
        DisconnectFromHost(); // 已有连接时先断开
    }

    m_shutdownByApp = false;
    m_shutdownComplete = false;
    m_streamReady = false;
    m_recvBuf.clear();

    if (QUIC_FAILED(api->ConnectionOpen(m_registration, ConnCallback, this, &m_conn))) {
        m_conn = nullptr;
        emit connectionFailed(QStringLiteral("创建连接失败"));
        return false;
    }

    // 端口为主机序；IP非法或网络不通时握手阶段以 transport shutdown 报错
    QUIC_STATUS st = api->ConnectionStart(m_conn, m_configuration, QUIC_ADDRESS_FAMILY_INET,
                                          ServerIpAddress.toUtf8().constData(),
                                          (uint16_t)ServerPort);
    if (QUIC_FAILED(st)) {
        emit connectionFailed(QStringLiteral("连接启动失败: %1").arg(quicStatusToString(st)));
        api->ConnectionClose(m_conn);
        m_conn = nullptr;
        return false;
    }
    return true;
}

// 功能：发送消息——自动补'\n'分帧（QUIC流是字节流，接收端按行分帧）
void QuicClient::ClientSendMsgToServer(const QString &StrData)
{
    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api || !m_conn || !m_stream || !m_streamReady) {
        qWarning() << "QuicClient::ClientSendMsgToServer: 未连接或流未就绪";
        return;
    }

    QString framed = StrData;
    if (!framed.endsWith('\n')) {
        framed += '\n';
    }

    QuicSendBuffer *sb = new QuicSendBuffer;
    sb->data = framed.toUtf8();
    sb->buf = {(uint32_t)sb->data.size(), (uint8_t *)sb->data.constData()};
    QUIC_STATUS st = api->StreamSend(m_stream, &sb->buf, 1, QUIC_SEND_FLAG_NONE, sb);
    if (QUIC_FAILED(st)) {
        delete sb;
        emit connectionFailed(QStringLiteral("发送失败: %1").arg(quicStatusToString(st)));
    }
}

// 功能：断开连接——优雅关闭并等待 SHUTDOWN_COMPLETE 后再回收句柄
void QuicClient::DisconnectFromHost()
{
    const QUIC_API_TABLE *api = GetQuicApi();
    if (!api || !m_conn) {
        m_stream = nullptr;
        return;
    }

    m_shutdownByApp = true; // 抑制后续失败信号

    if (m_stream) {
        api->StreamShutdown(m_stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
    }
    api->ConnectionShutdown(m_conn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);

    // 等待 SHUTDOWN_COMPLETE（回调里 ConnectionClose 并通知条件变量），3秒兜底强制关闭
    {
        std::unique_lock<std::mutex> lk(m_mx);
        m_cv.wait_for(lk, std::chrono::seconds(3), [this]() { return m_shutdownComplete.load(); });
    }
    if (!m_shutdownComplete && m_conn) {
        qWarning() << "QuicClient: 等待SHUTDOWN_COMPLETE超时，强制关闭连接";
        api->ConnectionClose(m_conn);
    }
    m_conn = nullptr;
    m_stream = nullptr;
    m_streamReady = false;
    StopTimerOutFunc();
}

// 功能：启动自动测试定时器（每1.5秒自动发送测试消息）
void QuicClient::StartTimerOutFunc()
{
    if (m_timer) {
        m_timer->start();
    }
}

// 功能：停止自动测试定时器
void QuicClient::StopTimerOutFunc()
{
    if (m_timer) {
        m_timer->stop();
    }
}

// 功能：设置自动测试消息（支持%1替换为发送序号）
void QuicClient::setAutoTestMessage(const QString &message)
{
    m_autoTestMessage = message;
}

// 功能：连接事件回调（MsQuic worker 线程触发，直接emit跨线程信号）
QUIC_STATUS QuicClient::ConnCallback(HQUIC conn, void *context, QUIC_CONNECTION_EVENT *ev)
{
    QuicClient *c = (QuicClient *)context;
    const QUIC_API_TABLE *api = GetQuicApi();

    switch (ev->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        // 握手完成：打开双向流并启动，同时告知表单连接成功
        if (QUIC_SUCCEEDED(api->StreamOpen(conn, QUIC_STREAM_OPEN_FLAG_NONE,
                                           StreamCallback, c, &c->m_stream))) {
            api->StreamStart(c->m_stream, QUIC_STREAM_START_FLAG_NONE);
        }
        emit c->connectionEstablished();
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        // 对端主动关闭：回敬关闭帧；应用未主动断开时通知表单
        api->ConnectionShutdown(conn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        if (!c->m_shutdownByApp) {
            emit c->connectionFailed(QStringLiteral("远程主机已关闭连接"));
        }
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        // 握手失败/网络错误走这里；正常关闭（SUCCESS）不通知
        if (QUIC_FAILED(ev->SHUTDOWN_INITIATED_BY_TRANSPORT.Status) && !c->m_shutdownByApp) {
            emit c->connectionFailed(QStringLiteral("连接失败: %1")
                                         .arg(quicStatusToString(ev->SHUTDOWN_INITIATED_BY_TRANSPORT.Status)));
        }
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        api->ConnectionClose(conn);
        c->m_conn = nullptr;
        {
            std::lock_guard<std::mutex> lk(c->m_mx);
            c->m_shutdownComplete = true;
        }
        c->m_cv.notify_all();
        break;

    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

// 功能：流事件回调（MsQuic worker 线程触发）
QUIC_STATUS QuicClient::StreamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *ev)
{
    QuicClient *c = (QuicClient *)context;
    const QUIC_API_TABLE *api = GetQuicApi();

    switch (ev->Type) {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        if (QUIC_FAILED(ev->START_COMPLETE.Status)) {
            emit c->connectionFailed(QStringLiteral("打开流失败: %1")
                                         .arg(quicStatusToString(ev->START_COMPLETE.Status)));
        } else {
            c->m_streamReady = true;
        }
        break;

    case QUIC_STREAM_EVENT_RECEIVE:
    {
        // 关键：内联归还流量窗口后，回调必须返回 QUIC_STATUS_PENDING。
        // 若返回 SUCCESS，MsQuic 会按 TotalBufferLength 再自动完成一次，
        // 导致 RecvPendingLength 下溢、后续 RECEIVE 事件永久停发。
        api->StreamReceiveComplete(stream, ev->RECEIVE.TotalBufferLength);
        {
            std::lock_guard<std::mutex> lk(c->m_mx);
            for (uint32_t i = 0; i < ev->RECEIVE.BufferCount; ++i) {
                c->m_recvBuf.append((const char *)ev->RECEIVE.Buffers[i].Buffer,
                                    ev->RECEIVE.Buffers[i].Length);
            }
        }
        // 按\n逐帧提取完整消息
        for (;;) {
            QString frame;
            {
                std::lock_guard<std::mutex> lk(c->m_mx);
                int pos = c->m_recvBuf.indexOf('\n');
                if (pos < 0) {
                    break;
                }
                frame = QString::fromUtf8(c->m_recvBuf.left(pos));
                c->m_recvBuf.remove(0, pos + 1);
            }
            emit c->dataReceived(frame);
        }
        return QUIC_STATUS_PENDING;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        // 无论是否被取消都要释放发送缓冲区
        delete (QuicSendBuffer *)ev->SEND_COMPLETE.ClientContext;
        break;

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        // 对端不再发送数据，无需处理
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        api->StreamClose(stream);
        c->m_stream = nullptr;
        c->m_streamReady = false;
        break;

    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}
