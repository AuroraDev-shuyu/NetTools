// IOCP服务器1万连接压测：统计accept速率、内存占用、断开耗时
// 用法: iocp_bench.exe [port=8765] [connections=10000] [workers=4]
#include "iocpserver.h"
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "psapi.lib")

static std::atomic<int> g_acceptCount{0};
static std::atomic<int> g_disconnectCount{0};

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
    int port = argc > 1 ? atoi(argv[1]) : 8765;
    int total = argc > 2 ? atoi(argv[2]) : 10000;
    int workers = argc > 3 ? atoi(argv[3]) : 4;

    using Clock = std::chrono::steady_clock;
    static Clock::time_point acceptStart;
    static Clock::time_point acceptEnd;

    IocpServer server;
    server.setCallback(
        // 新连接回调：计数并记录首个/最后一个accept时刻
        [&](uint64_t)
        {
            int n = ++g_acceptCount;
            if (n == 1)
            {
                acceptStart = Clock::now();
            }
            else if (n == total)
            {
                acceptEnd = Clock::now();
                double secs = std::chrono::duration<double>(acceptEnd - acceptStart).count();
                printf("IOCP: %d connections accepted in %.2fs (%.0f conn/s)\n",
                       total, secs, total / secs);
                printMemory("IOCP after accept");
                fflush(stdout);
            }
        },
        // 压测只测建连，不回显数据
        [](uint64_t, const char *, size_t) {},
        [&](uint64_t)
        {
            ++g_disconnectCount;
        });

    printf("IOCP server: port=%d workers=%d target=%d\n", port, workers, total);
    printMemory("IOCP baseline");
    fflush(stdout);
    if (!server.start(port, workers))
    {
        printf("start failed\n");
        return 1;
    }

    while (g_acceptCount < total)
    {
        Sleep(20);
    }
    Sleep(5000); // 稳定期
    printMemory("IOCP steady(5s)");

    auto t0 = Clock::now();
    server.stop(); // 关闭所有连接
    auto t1 = Clock::now();
    printf("IOCP: closed all connections in %.2fs (disconnect callbacks=%d)\n",
           std::chrono::duration<double>(t1 - t0).count(), (int)g_disconnectCount);
    printMemory("IOCP after close");
    fflush(stdout);
    return 0;
}
