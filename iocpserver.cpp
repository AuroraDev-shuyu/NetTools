#include "iocpserver.h"
#include <cstring>

IocpServer::IocpServer()
{
    // 初始化winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

IocpServer::~IocpServer()
{
    stop();
    WSACleanup();
}

bool IocpServer::start(uint64_t port, int workerThreadCount)
{
    // 1.创建IO完成端口
    m_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (m_hIocp == nullptr)
    {
        return false;
    }
    // 2.创建重叠socket用于监听
    m_listenSock = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (m_listenSock == INVALID_SOCKET)
    {
        return false;
    }
    // 端口复用
    BOOL opt = TRUE;
    setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(m_listenSock, (sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(m_listenSock, SOMAXCONN) != 0)
    {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
        return false;
    }
    // 把监听socket绑定到IOCP
    CreateIoCompletionPort((HANDLE)m_listenSock, m_hIocp, (ULONG_PTR)0, 0);
    m_running = true;
    // 启动工作线程
    for (int i = 0; i < workerThreadCount; i++)
    {
        m_workers.emplace_back(&IocpServer::workerThreadFunc, this);
    }
    // 同时投递多个Accept，避免accept成为瓶颈（大量并发建连时backlog会溢出）
    for (int i = 0; i < workerThreadCount; i++)
    {
        if (!postAccept())
        {
            break;
        }
    }
    return true;
}

void IocpServer::workerThreadFunc()
{
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED pOverlapped = nullptr;
    while (m_running)
    {
        BOOL ret = GetQueuedCompletionStatus(m_hIocp, &bytesTransferred,
                                             &completionKey, &pOverlapped, INFINITE);
        PerIoData *pIoData = reinterpret_cast<PerIoData *>(pOverlapped);
        if (pIoData == nullptr)
        {
            continue; // stop()投递的空完成包，用于唤醒线程
        }
        if (!ret)
        {
            // IO失败：连接断开场景
            PerSocketData *sockCtx = pIoData->sockCtx;
            if (pIoData->op == PerIoData::OP_RECV && sockCtx != nullptr)
            {
                if (m_onDisconnect)
                {
                    m_onDisconnect(sockCtx->connId);
                }
                closeConnection(sockCtx);
            }
            else if (pIoData->op == PerIoData::OP_ACCEPT && sockCtx != nullptr)
            {
                // AcceptEx失败：释放预分配的socket上下文
                closesocket(sockCtx->sock);
                delete sockCtx;
            }
            delete pIoData;
            continue;
        }
        switch (pIoData->op)
        {
        case PerIoData::OP_ACCEPT:
        {
            // 新连接到来
            PerSocketData *newSockCtx = pIoData->sockCtx;
            {
                std::lock_guard<std::mutex> lock(m_connMapMutex);
                m_connMap[newSockCtx->connId] = newSockCtx;
            }
            if (m_onNewConn)
            {
                m_onNewConn(newSockCtx->connId);
            }
            // 投递这个新连接的Recv
            postRecv(newSockCtx);
            // 继续投递下一个Accept,等待下一个客户端
            postAccept();
            break;
        }
        case PerIoData::OP_RECV:
        {
            PerSocketData *sockCtx = pIoData->sockCtx;
            if (bytesTransferred == 0)
            {
                // 对端关闭连接
                if (m_onDisconnect)
                {
                    m_onDisconnect(sockCtx->connId);
                }
                closeConnection(sockCtx);
                break;
            }
            if (m_onRecv)
            {
                m_onRecv(sockCtx->connId, sockCtx->recvBuf, bytesTransferred);
            }
            // 继续投递接收,等待下一包数据
            postRecv(sockCtx);
            break;
        }
        case PerIoData::OP_SEND:
            // 发送完成,这里简单处理,高级版本可以做发送队列
            break;
        }
        delete pIoData;
    }
}

// 投递异步Accept
bool IocpServer::postAccept()
{
    PerIoData *pIoData = new PerIoData{};
    pIoData->op = PerIoData::OP_ACCEPT;
    PerSocketData *sockCtx = new PerSocketData{};
    sockCtx->sock = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    sockCtx->connId = m_nextConnId++;
    pIoData->sockCtx = sockCtx;
    DWORD dwBytes;
    BOOL ok = AcceptEx(m_listenSock, sockCtx->sock, sockCtx->recvBuf, 0,
                       sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &dwBytes,
                       &pIoData->overlapped);
    if (!ok && WSAGetLastError() != WSA_IO_PENDING)
    {
        closesocket(sockCtx->sock);
        delete pIoData;
        delete sockCtx;
        return false;
    }
    return true;
}

// 投递异步Recv
bool IocpServer::postRecv(PerSocketData *sockCtx)
{
    PerIoData *pIoData = new PerIoData{};
    pIoData->op = PerIoData::OP_RECV;
    pIoData->sockCtx = sockCtx;
    WSABUF wsaBuf{};
    wsaBuf.buf = sockCtx->recvBuf;
    wsaBuf.len = sizeof(sockCtx->recvBuf);
    DWORD flags = 0;
    BOOL ok = WSARecv(sockCtx->sock, &wsaBuf, 1, nullptr, &flags,
                      &pIoData->overlapped, nullptr);
    if (!ok && WSAGetLastError() != WSA_IO_PENDING)
    {
        delete pIoData;
        return false;
    }
    return true;
}

bool IocpServer::sendData(uint64_t connId, const char *data, size_t dataLen)
{
    PerSocketData *sockCtx = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_connMapMutex);
        auto it = m_connMap.find(connId);
        if (it == m_connMap.end())
        {
            return false;
        }
        sockCtx = it->second;
    }
    // 注意：异步发送期间data必须保持有效（由调用方保证生命周期）
    PerIoData *pIoData = new PerIoData{};
    pIoData->op = PerIoData::OP_SEND;
    pIoData->sockCtx = sockCtx;
    WSABUF wsaBuf{};
    wsaBuf.buf = const_cast<char *>(data);
    wsaBuf.len = (ULONG)dataLen;
    DWORD sent = 0;
    BOOL ok = WSASend(sockCtx->sock, &wsaBuf, 1, &sent, 0,
                      &pIoData->overlapped, nullptr);
    if (!ok && WSAGetLastError() != WSA_IO_PENDING)
    {
        delete pIoData;
        return false;
    }
    return true;
}

// 关闭并释放连接上下文（须由工作线程在处理断开时调用）
void IocpServer::closeConnection(PerSocketData *sockCtx)
{
    if (sockCtx == nullptr)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_connMapMutex);
        m_connMap.erase(sockCtx->connId);
    }
    closesocket(sockCtx->sock);
    delete sockCtx;
}

void IocpServer::stop()
{
    if (!m_running)
    {
        return;
    }
    m_running = false;
    // 唤醒所有阻塞的工作线程并等待退出
    for (size_t i = 0; i < m_workers.size(); i++)
    {
        PostQueuedCompletionStatus(m_hIocp, 0, 0, nullptr);
    }
    for (std::thread &t : m_workers)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    m_workers.clear();
    // 工作线程已全部退出，剩余连接由本线程清理
    std::vector<PerSocketData *> socks;
    {
        std::lock_guard<std::mutex> lock(m_connMapMutex);
        for (auto &kv : m_connMap)
        {
            socks.push_back(kv.second);
        }
        m_connMap.clear();
    }
    for (PerSocketData *s : socks)
    {
        closesocket(s->sock);
        delete s;
    }
    if (m_listenSock != INVALID_SOCKET)
    {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
    }
    if (m_hIocp != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hIocp);
        m_hIocp = INVALID_HANDLE_VALUE;
    }
}
