// Qt版服务器1万连接压测：QTcpServer事件循环，统计accept速率、内存占用、断开耗时
// 用法: qt_bench.exe [port=8765] [connections=10000]
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QSet>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

static void printMemory(const char *tag)
{
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        printf("%s: WorkingSet=%.1f MB, CommitBytes=%.1f MB\n",
               tag, pmc.WorkingSetSize / 1048576.0, pmc.PagefileUsage / 1048576.0);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int port = argc > 1 ? atoi(argv[1]) : 8765;
    int total = argc > 2 ? atoi(argv[2]) : 10000;

    QTcpServer server;
    server.setMaxPendingConnections(65535); // 压测必须调大backlog（默认仅30）

    QSet<QTcpSocket *> clients;
    clients.reserve(total);
    QElapsedTimer acceptTimer;
    int accepted = 0;
    int disconnected = 0;

    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]()
    {
        while (server.hasPendingConnections())
        {
            QTcpSocket *sock = server.nextPendingConnection();
            clients.insert(sock);
            QObject::connect(sock, &QTcpSocket::disconnected, sock, [&, sock]()
            {
                clients.remove(sock);
                ++disconnected;
                sock->deleteLater();
            });
            if (++accepted == 1)
            {
                acceptTimer.start();
            }
            if (accepted == total)
            {
                double secs = acceptTimer.elapsed() / 1000.0;
                printf("Qt: %d connections accepted in %.2fs (%.0f conn/s)\n",
                       total, secs, total / secs);
                printMemory("Qt after accept");
                fflush(stdout);
            }
        }
    });

    if (!server.listen(QHostAddress::Any, port))
    {
        printf("listen failed\n");
        return 1;
    }
    printf("Qt server: port=%d target=%d\n", port, total);
    printMemory("Qt baseline");
    fflush(stdout);

    // 等待accept满
    while (accepted < total)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    // 稳定期5秒（保持事件循环运行）
    QElapsedTimer steady;
    steady.start();
    while (steady.elapsed() < 5000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    printMemory("Qt steady(5s)");

    // 关闭全部连接并计时
    QElapsedTimer closeTimer;
    closeTimer.start();
    const QList<QTcpSocket *> snapshot = clients.values();
    for (QTcpSocket *s : snapshot)
    {
        s->abort();
    }
    while (disconnected < total)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    printf("Qt: closed all connections in %.2fs\n", closeTimer.elapsed() / 1000.0);
    printMemory("Qt after close");
    fflush(stdout);
    return 0;
}
