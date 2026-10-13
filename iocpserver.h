#ifndef IOCPSERVER_H
#define IOCPSERVER_H

// 注意：winsock2.h必须包含在windows.h之前，否则与winsock.h冲突
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <thread>
#include <cstdint>
#include <functional>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib") // AcceptEx

// 事件回调：上层业务回调（给MQTT/HTTP层）
using OnNewConnection = std::function<void(uint64_t connId)>;
using OnRecvData = std::function<void(uint64_t connId, const char *data, size_t len)>;
using OnClientDisconnect = std::function<void(uint64_t connId)>;

// PerSocket:每个连接的上下文
struct PerSocketData
{
    SOCKET sock = INVALID_SOCKET;
    uint64_t connId = 0;
    char recvBuf[4096]; // 单次接收缓冲区
};

// PerIoData:单次IO操作的上下文
struct PerIoData
{
    OVERLAPPED overlapped{};
    enum OpType
    {
        OP_ACCEPT,
        OP_RECV,
        OP_SEND,
    } op;
    PerSocketData *sockCtx = nullptr;
};

class IocpServer
{
public:
    IocpServer();
    ~IocpServer();
    bool start(uint64_t port, int workerThreadCount = 4);
    void stop();
    bool sendData(uint64_t connId, const char *data, size_t dataLen);
    void setCallback(OnNewConnection onNew, OnRecvData onRecv,
                     OnClientDisconnect onDisc)
    {
        m_onNewConn = std::move(onNew);
        m_onRecv = std::move(onRecv);
        m_onDisconnect = std::move(onDisc);
    }

private:
    void workerThreadFunc();
    bool postAccept();
    bool postRecv(PerSocketData *sockCtx);
    void closeConnection(PerSocketData *sockCtx); // 关闭并释放连接上下文

    HANDLE m_hIocp = INVALID_HANDLE_VALUE;
    SOCKET m_listenSock = INVALID_SOCKET;
    std::unordered_map<uint64_t, PerSocketData *> m_connMap;
    std::mutex m_connMapMutex; // 保护m_connMap（多个工作线程并发访问）
    uint64_t m_nextConnId = 1;
    bool m_running = false;
    std::vector<std::thread> m_workers; // 工作线程（stop时join回收）
    OnNewConnection m_onNewConn;
    OnRecvData m_onRecv;
    OnClientDisconnect m_onDisconnect;
};
#endif
