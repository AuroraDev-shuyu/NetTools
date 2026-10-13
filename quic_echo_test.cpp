// MsQuic 回环测试：验证 lib/dll/证书/ALPN/API 调用序列（先于 Qt 封装层）
// 用法: quic_echo_test.exe server <pfx路径> <端口> [条数N=100]
//       quic_echo_test.exe client <IP> <端口> <条数N>
// 服务端收到 N 条后优雅关闭；客户端收满 N 条回显后退出 exit(0)，15秒看门狗超时 exit(2)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <msquic.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

static const char *kAlpn = "nettools";

// 发送缓冲区：SEND_COMPLETE 事件里释放
struct SendBuf
{
    std::string data;
    QUIC_BUFFER buf;
};

static const QUIC_API_TABLE *g_api = nullptr;
static std::atomic<bool> g_done{false};
static int g_exitCode = 0;

static QUIC_BUFFER AlpnBuf()
{
    static QUIC_BUFFER b = {(uint32_t)strlen(kAlpn), (uint8_t *)kAlpn};
    return b;
}

static void InitSettings(QUIC_SETTINGS &s)
{
    memset(&s, 0, sizeof(s));
    s.IsSet.IdleTimeoutMs = TRUE; // 关闭空闲超时
    s.IdleTimeoutMs = 0;
    s.IsSet.PeerBidiStreamCount = TRUE; // 默认0会导致对端开流报STREAM_LIMIT_REACHED
    s.PeerBidiStreamCount = 1;
    // 诊断：显式放大流控窗口，对照默认值是否导致数据卡在发送队列
    s.IsSet.StreamRecvWindowDefault = TRUE;
    s.StreamRecvWindowDefault = 4 * 1024 * 1024;
    s.IsSet.ConnFlowControlWindow = TRUE;
    s.ConnFlowControlWindow = 16 * 1024 * 1024;
}

// ---------------- 服务端 ----------------
struct ServerCtx
{
    HQUIC registration = nullptr;
    HQUIC configuration = nullptr;
    HQUIC listener = nullptr;
    HQUIC connection = nullptr;
    std::atomic<int> received{0};
    int expected = 0;
    bool connClosed = false;
    std::string recvBuf; // 接收缓冲：QUIC流是字节流，按\n分帧
    std::mutex bufMx;
};

static QUIC_STATUS ServerConnCallback(HQUIC conn, void *context, QUIC_CONNECTION_EVENT *ev);
static QUIC_STATUS ServerStreamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *ev);

static QUIC_STATUS ServerListenerCallback(HQUIC listener, void *context, QUIC_LISTENER_EVENT *ev)
{
    ServerCtx *s = (ServerCtx *)context;
    if (ev->Type == QUIC_LISTENER_EVENT_NEW_CONNECTION)
    {
        // 两个动作都必须在回调内完成，否则握手不进行
        s->connection = ev->NEW_CONNECTION.Connection;
        g_api->SetCallbackHandler(ev->NEW_CONNECTION.Connection, ServerConnCallback, s);
        g_api->ConnectionSetConfiguration(ev->NEW_CONNECTION.Connection, s->configuration);
        printf("[server] new connection, handshaking...\n");
        fflush(stdout);
    }
    else if (ev->Type == QUIC_LISTENER_EVENT_STOP_COMPLETE)
    {
        printf("[server] listener stopped\n");
        fflush(stdout);
    }
    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS ServerConnCallback(HQUIC conn, void *context, QUIC_CONNECTION_EVENT *ev)
{
    ServerCtx *s = (ServerCtx *)context;
    switch (ev->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        printf("[server] connected, ALPN=%.*s\n", (int)ev->CONNECTED.NegotiatedAlpnLength,
               ev->CONNECTED.NegotiatedAlpn);
        fflush(stdout);
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        // 已实测：对端发起的流，本端调StreamStart返回INVALID_STATE（0x8007139F）
        // ——流已被对端start，本端只需设置回调handler
        g_api->SetCallbackHandler(ev->PEER_STREAM_STARTED.Stream, ServerStreamCallback, s);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        printf("[server] peer initiated shutdown\n");
        fflush(stdout);
        g_api->ConnectionShutdown(conn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        printf("[server] transport shutdown: 0x%08X\n",
               (unsigned)ev->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        fflush(stdout);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        g_api->ConnectionClose(conn);
        s->connection = nullptr;
        s->connClosed = true;
        g_done = true;
        break;
    default:
        printf("[server] conn event %d\n", (int)ev->Type);
        fflush(stdout);
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS ServerStreamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *ev)
{
    ServerCtx *s = (ServerCtx *)context;
    switch (ev->Type)
    {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        printf("[server] stream START_COMPLETE: 0x%08X\n", (unsigned)ev->START_COMPLETE.Status);
        fflush(stdout);
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        printf("[server] RECEIVE event: offset=%llu total=%llu buffers=%u flags=%u\n",
               (unsigned long long)ev->RECEIVE.AbsoluteOffset,
               (unsigned long long)ev->RECEIVE.TotalBufferLength,
               (unsigned)ev->RECEIVE.BufferCount, (unsigned)ev->RECEIVE.Flags);
        fflush(stdout);
        // 追加到接收缓冲，按\n分帧
        {
            std::lock_guard<std::mutex> lk(s->bufMx);
            for (uint32_t i = 0; i < ev->RECEIVE.BufferCount; ++i)
            {
                s->recvBuf.append((const char *)ev->RECEIVE.Buffers[i].Buffer,
                                  ev->RECEIVE.Buffers[i].Length);
            }
        }
        // 逐帧处理（每帧为一条消息）
        for (;;)
        {
            std::string frame;
            {
                std::lock_guard<std::mutex> lk(s->bufMx);
                size_t pos = s->recvBuf.find('\n');
                if (pos == std::string::npos)
                {
                    break;
                }
                frame = s->recvBuf.substr(0, pos);
                s->recvBuf.erase(0, pos + 1);
            }
            int n = ++s->received;
            printf("[server] recv #%d: %s\n", n, frame.c_str());
            fflush(stdout);
            // 回显（带回车换行便于客户端分帧）
            std::string resp = "Server response: " + frame + "\n";
            SendBuf *sb = new SendBuf;
            sb->data = std::move(resp);
            sb->buf = {(uint32_t)sb->data.size(), (uint8_t *)sb->data.data()};
            QUIC_STATUS st = g_api->StreamSend(stream, &sb->buf, 1, QUIC_SEND_FLAG_NONE, sb);
            if (QUIC_FAILED(st))
            {
                delete sb;
                printf("[server] echo send failed: 0x%08X\n", (unsigned)st);
                fflush(stdout);
            }
        }
        // 处理完后再归还流量窗口。关键：内联调用了StreamReceiveComplete后，
        // 回调必须返回QUIC_STATUS_PENDING——若返回SUCCESS，MsQuic会按
        // TotalBufferLength再自动完成一次，导致RecvPendingLength下溢、
        // ReceiveEnabled被永久置FALSE，后续RECEIVE事件全部停发（实测症状：
        // 传输层收到全部792字节但只投递首个事件）。
        g_api->StreamReceiveComplete(stream, ev->RECEIVE.TotalBufferLength);
        printf("[server] event done, total recv=%d buf remain=%zu\n", (int)s->received,
               s->recvBuf.size());
        fflush(stdout);
        if (s->received >= s->expected)
        {
            // 不再主动 ConnectionShutdown：会丢弃仍在发送缓冲里的回显。
            // 等待客户端收满回显后主动关闭连接，服务端在 PEER 关闭时响应。
            printf("[server] received all %d messages, waiting for client to close...\n", s->expected);
            fflush(stdout);
        }
        return QUIC_STATUS_PENDING;
    }
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        delete (SendBuf *)ev->SEND_COMPLETE.ClientContext;
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        g_api->StreamClose(stream);
        break;
    default:
        printf("[server] stream event %d\n", (int)ev->Type);
        fflush(stdout);
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

// hex 字符串(40字符)解码为20字节SHA1指纹
static bool HexToSha1(const char *hex, uint8_t out[20])
{
    if (strlen(hex) != 40)
    {
        return false;
    }
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (int i = 0; i < 20; ++i)
    {
        int hi = nib(hex[i * 2]), lo = nib(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static int RunServer(const char *thumbprint, uint16_t port, int expected)
{
    ServerCtx *s = new ServerCtx;
    s->expected = expected;
    if (QUIC_FAILED(MsQuicOpen2(&g_api)))
    {
        printf("[server] MsQuicOpen2 failed\n");
        delete s;
        return 2;
    }
    QUIC_CERTIFICATE_HASH_STORE hashStore{};
    if (!HexToSha1(thumbprint, hashStore.ShaHash))
    {
        printf("[server] invalid thumbprint: %s\n", thumbprint);
        delete s;
        return 2;
    }
    strcpy_s(hashStore.StoreName, sizeof(hashStore.StoreName), "My");

    QUIC_REGISTRATION_CONFIG regCfg = {"NetToolsEcho", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    if (QUIC_FAILED(g_api->RegistrationOpen(&regCfg, &s->registration)))
    {
        printf("[server] RegistrationOpen failed\n");
        delete s;
        return 2;
    }
    QUIC_SETTINGS settings;
    InitSettings(settings);
    QUIC_BUFFER alpn = AlpnBuf();
    if (QUIC_FAILED(g_api->ConfigurationOpen(s->registration, &alpn, 1, &settings, sizeof(settings),
                                            nullptr, &s->configuration)))
    {
        printf("[server] ConfigurationOpen failed\n");
        delete s;
        return 2;
    }
    QUIC_CREDENTIAL_CONFIG cred{};
    cred.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE;
    cred.Flags = QUIC_CREDENTIAL_FLAG_NONE;
    cred.CertificateHashStore = &hashStore;
    QUIC_STATUS st = g_api->ConfigurationLoadCredential(s->configuration, &cred);
    if (QUIC_FAILED(st))
    {
        printf("[server] LoadCredential(HASH_STORE) failed: 0x%08X\n", (unsigned)st);
        delete s;
        return 2;
    }
    if (QUIC_FAILED(g_api->ListenerOpen(s->registration, ServerListenerCallback, s, &s->listener)))
    {
        printf("[server] ListenerOpen failed\n");
        delete s;
        return 2;
    }
    // 显式绑定 0.0.0.0:port（LocalAddress 为 NULL 时 MsQuic 自动选端口）
    QUIC_ADDR addr{};
    addr.Ipv4.sin_family = AF_INET;
    addr.Ipv4.sin_port = htons(port);
    addr.Ipv4.sin_addr.s_addr = INADDR_ANY;
    st = g_api->ListenerStart(s->listener, &alpn, 1, &addr);
    if (QUIC_FAILED(st))
    {
        printf("[server] ListenerStart failed: 0x%08X (port=%u)\n", (unsigned)st, port);
        delete s;
        return 2;
    }
    printf("[server] listening on port %u, waiting for %d messages...\n", port, expected);
    fflush(stdout);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    auto lastStat = std::chrono::steady_clock::now();
    while (!g_done && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (s->connection &&
            std::chrono::steady_clock::now() - lastStat > std::chrono::seconds(2))
        {
            lastStat = std::chrono::steady_clock::now();
            QUIC_STATISTICS st2{};
            uint32_t sz = sizeof(st2);
            if (QUIC_SUCCEEDED(g_api->GetParam(s->connection, QUIC_PARAM_CONN_STATISTICS, &sz, &st2)))
            {
                printf("[server] stats: RecvBytes=%llu RecvStreamBytes=%llu SendStreamBytes=%llu dropped=%llu\n",
                       (unsigned long long)st2.Recv.TotalBytes,
                       (unsigned long long)st2.Recv.TotalStreamBytes,
                       (unsigned long long)st2.Send.TotalStreamBytes,
                       (unsigned long long)st2.Recv.DroppedPackets);
                fflush(stdout);
            }
        }
    }
    if (!g_done)
    {
        printf("[server] TIMEOUT: received %d/%d\n", (int)s->received, expected);
        g_exitCode = 2;
        // 兜底：主动断开残留连接
        if (s->connection)
        {
            g_api->ConnectionShutdown(s->connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            auto d2 = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!s->connClosed && std::chrono::steady_clock::now() < d2)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!s->connClosed && s->connection)
            {
                g_api->ConnectionClose(s->connection);
                s->connection = nullptr;
            }
        }
    }
    else
    {
        printf("[server] done: %d/%d messages echoed\n", (int)s->received, expected);
    }
    if (s->listener)
    {
        g_api->ListenerClose(s->listener);
        s->listener = nullptr;
    }
    if (s->configuration)
    {
        g_api->ConfigurationClose(s->configuration);
    }
    if (s->registration)
    {
        g_api->RegistrationClose(s->registration); // 阻塞至全部子句柄关闭，其后无在途回调
    }
    delete s;
    return g_exitCode;
}

// ---------------- 客户端 ----------------
struct ClientCtx
{
    HQUIC registration = nullptr;
    HQUIC configuration = nullptr;
    HQUIC connection = nullptr;
    HQUIC stream = nullptr;
    std::atomic<int> received{0};
    std::atomic<int> sendCompleted{0};
    std::atomic<bool> streamStarted{false};
    int expected = 0;
    bool started = false;
    std::string recvBuf; // 接收缓冲：按\n分帧
    std::mutex bufMx;
};

static QUIC_STATUS ClientConnCallback(HQUIC conn, void *context, QUIC_CONNECTION_EVENT *ev);
static QUIC_STATUS ClientStreamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *ev);

static QUIC_STATUS ClientConnCallback(HQUIC conn, void *context, QUIC_CONNECTION_EVENT *ev)
{
    ClientCtx *c = (ClientCtx *)context;
    switch (ev->Type)
    {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        printf("[client] connected\n");
        fflush(stdout);
        if (QUIC_SUCCEEDED(g_api->StreamOpen(conn, QUIC_STREAM_OPEN_FLAG_NONE,
                                             ClientStreamCallback, c, &c->stream)))
        {
            g_api->StreamStart(c->stream, QUIC_STREAM_START_FLAG_NONE);
            c->started = true;
        }
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        if (QUIC_FAILED(ev->SHUTDOWN_INITIATED_BY_TRANSPORT.Status) && !g_done)
        {
            printf("[client] transport shutdown: 0x%08X\n",
                   (unsigned)ev->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
            fflush(stdout);
            g_exitCode = 2;
            g_done = true;
        }
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        printf("[client] peer initiated shutdown\n");
        fflush(stdout);
        g_api->ConnectionShutdown(conn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        g_api->ConnectionClose(conn);
        c->connection = nullptr;
        g_done = true;
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS ClientStreamCallback(HQUIC stream, void *context, QUIC_STREAM_EVENT *ev)
{
    ClientCtx *c = (ClientCtx *)context;
    switch (ev->Type)
    {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        if (QUIC_FAILED(ev->START_COMPLETE.Status))
        {
            printf("[client] stream start failed: 0x%08X\n", (unsigned)ev->START_COMPLETE.Status);
            fflush(stdout);
            g_exitCode = 2;
            g_done = true;
        }
        c->streamStarted = true;
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
    {
        g_api->StreamReceiveComplete(stream, ev->RECEIVE.TotalBufferLength);
        {
            std::lock_guard<std::mutex> lk(c->bufMx);
            for (uint32_t i = 0; i < ev->RECEIVE.BufferCount; ++i)
            {
                c->recvBuf.append((const char *)ev->RECEIVE.Buffers[i].Buffer,
                                  ev->RECEIVE.Buffers[i].Length);
            }
        }
        // 按\n逐帧校验回显
        for (;;)
        {
            std::string frame;
            {
                std::lock_guard<std::mutex> lk(c->bufMx);
                size_t pos = c->recvBuf.find('\n');
                if (pos == std::string::npos)
                {
                    break;
                }
                frame = c->recvBuf.substr(0, pos);
                c->recvBuf.erase(0, pos + 1);
            }
            int n = ++c->received;
            printf("[client] recv #%d: %s\n", n, frame.c_str());
            fflush(stdout);
            if (frame.rfind("Server response: ", 0) != 0)
            {
                printf("[client] BAD RESPONSE\n");
                g_exitCode = 2;
                g_done = true;
            }
        }
        if (c->received >= c->expected)
        {
            printf("[client] received all %d echoes, shutting down\n", c->expected);
            fflush(stdout);
            g_api->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
            if (c->connection)
            {
                g_api->ConnectionShutdown(c->connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            }
        }
        // 与服务端相同：内联StreamReceiveComplete后必须返回PENDING，
        // 返回SUCCESS会导致MsQuic自动二次完成、接收永久停发
        return QUIC_STATUS_PENDING;
    }
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        delete (SendBuf *)ev->SEND_COMPLETE.ClientContext;
        printf("[client] send-complete #%d%s\n", ++c->sendCompleted,
               ev->SEND_COMPLETE.Canceled ? " (CANCELED)" : "");
        fflush(stdout);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        g_api->StreamClose(stream);
        c->stream = nullptr;
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

static int RunClient(const char *ip, uint16_t port, int count)
{
    ClientCtx *c = new ClientCtx;
    c->expected = count;
    if (QUIC_FAILED(MsQuicOpen2(&g_api)))
    {
        printf("[client] MsQuicOpen2 failed\n");
        delete c;
        return 2;
    }
    QUIC_REGISTRATION_CONFIG regCfg = {"NetToolsEcho", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    if (QUIC_FAILED(g_api->RegistrationOpen(&regCfg, &c->registration)))
    {
        printf("[client] RegistrationOpen failed\n");
        delete c;
        return 2;
    }
    QUIC_SETTINGS settings;
    InitSettings(settings);
    QUIC_BUFFER alpn = AlpnBuf();
    if (QUIC_FAILED(g_api->ConfigurationOpen(c->registration, &alpn, 1, &settings, sizeof(settings),
                                            nullptr, &c->configuration)))
    {
        printf("[client] ConfigurationOpen failed\n");
        delete c;
        return 2;
    }
    QUIC_CREDENTIAL_CONFIG cred{};
    cred.Type = QUIC_CREDENTIAL_TYPE_NONE;
    cred.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    if (QUIC_FAILED(g_api->ConfigurationLoadCredential(c->configuration, &cred)))
    {
        printf("[client] LoadCredential failed\n");
        delete c;
        return 2;
    }
    if (QUIC_FAILED(g_api->ConnectionOpen(c->registration, ClientConnCallback, c, &c->connection)))
    {
        printf("[client] ConnectionOpen failed\n");
        delete c;
        return 2;
    }
    QUIC_STATUS st = g_api->ConnectionStart(c->connection, c->configuration, QUIC_ADDRESS_FAMILY_INET,
                                            ip, port); // 端口为主机序
    if (QUIC_FAILED(st))
    {
        printf("[client] ConnectionStart failed: 0x%08X\n", (unsigned)st);
        delete c;
        return 2;
    }
    // 等 CONNECTED 后开流
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!c->started && !g_done && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!c->started)
    {
        printf("[client] connect TIMEOUT\n");
        g_exitCode = 2;
        g_done = true;
    }
    else
    {
        // 等 START_COMPLETE 后再发送，避免与流启动竞态
        auto d3 = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!c->streamStarted && !g_done && std::chrono::steady_clock::now() < d3)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (int i = 1; i <= count && !g_done; ++i)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "echo %d\n", i);
            SendBuf *sb = new SendBuf;
            sb->data = msg;
            sb->buf = {(uint32_t)sb->data.size(), (uint8_t *)sb->data.data()};
            st = g_api->StreamSend(c->stream, &sb->buf, 1, QUIC_SEND_FLAG_NONE, sb);
            if (QUIC_FAILED(st))
            {
                delete sb;
                printf("[client] send #%d failed: 0x%08X\n", i, (unsigned)st);
                g_exitCode = 2;
                g_done = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 实验：逐条节流，验证突发是否触发投递停滞
        }
        printf("[client] sent %d messages, waiting for echoes...\n", count);
        fflush(stdout);
    }
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!g_done && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!g_done)
    {
        printf("[client] TIMEOUT: %d/%d echoes\n", (int)c->received, count);
        g_exitCode = 2;
    }
    else if (c->received < c->expected)
    {
        printf("[client] FAIL: only %d/%d echoes\n", (int)c->received, count);
        g_exitCode = 2;
    }
    else
    {
        printf("[client] PASS: %d/%d\n", (int)c->received, count);
    }
    if (c->connection)
    {
        g_api->ConnectionClose(c->connection);
        c->connection = nullptr;
    }
    if (c->configuration)
    {
        g_api->ConfigurationClose(c->configuration);
    }
    if (c->registration)
    {
        g_api->RegistrationClose(c->registration);
    }
    delete c;
    return g_exitCode;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: quic_echo_test.exe server <sha1指纹> <port> [count=100]\n");
        printf("       quic_echo_test.exe client <ip> <port> <count>\n");
        return 2;
    }
    if (strcmp(argv[1], "server") == 0 && argc >= 4)
    {
        return RunServer(argv[2], (uint16_t)atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 100);
    }
    if (strcmp(argv[1], "client") == 0 && argc >= 5)
    {
        return RunClient(argv[2], (uint16_t)atoi(argv[3]), atoi(argv[4]));
    }
    printf("invalid args\n");
    return 2;
}
