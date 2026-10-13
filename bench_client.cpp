// 1万连接压测客户端：多线程阻塞connect，统计建连耗时；RST断开避免TIME_WAIT占满端口
// 用法: bench_client.exe [ip=127.0.0.1] [port=8765] [connections=10000] [threads=16]
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "ws2_32.lib")

static std::atomic<int> g_connected{0};
static std::atomic<int> g_failed{0};

int main(int argc, char **argv)
{
    const char *ip = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 8765;
    int total = argc > 3 ? atoi(argv[3]) : 10000;
    int threadCount = argc > 4 ? atoi(argv[4]) : 16;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    std::vector<SOCKET> socks(total, INVALID_SOCKET);
    std::vector<std::thread> pool;

    LARGE_INTEGER freq, tStart, tEnd;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&tStart);

    for (int t = 0; t < threadCount; ++t)
    {
        pool.emplace_back([&, t]()
        {
            int begin = (int)((long long)total * t / threadCount);
            int end = (int)((long long)total * (t + 1) / threadCount);
            for (int i = begin; i < end; ++i)
            {
                SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                // RST断开：避免TIME_WAIT占满客户端端口，支持连续多轮压测
                linger lg{1, 0};
                setsockopt(s, SOL_SOCKET, SO_LINGER, (const char *)&lg, sizeof(lg));
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons((u_short)port);
                inet_pton(AF_INET, ip, &addr.sin_addr);
                if (connect(s, (sockaddr *)&addr, sizeof(addr)) != 0)
                {
                    closesocket(s);
                    ++g_failed;
                    continue;
                }
                // 改为非阻塞，便于后续轮询检测服务器断开
                u_long nb = 1;
                ioctlsocket(s, FIONBIO, &nb);
                socks[i] = s;
                ++g_connected;
            }
        });
    }

    // 等待全部连接完成（或失败）
    while (g_connected + g_failed < total)
    {
        Sleep(10);
    }
    QueryPerformanceCounter(&tEnd);
    for (auto &th : pool)
    {
        th.join();
    }

    double secs = (double)(tEnd.QuadPart - tStart.QuadPart) / freq.QuadPart;
    printf("client: %d connected, %d failed in %.2fs (%.0f conn/s)\n",
           (int)g_connected, (int)g_failed, secs, total / secs);
    fflush(stdout);

    // 服务器在连接满后稳定5秒再关闭：轮询检测全部断开
    LARGE_INTEGER tDetectStart, tDetectEnd;
    QueryPerformanceCounter(&tDetectStart);
    int closed = 0;
    int rounds = 0;
    char buf[8];
    while (closed < (int)g_connected && rounds < 600) // 最多等30秒
    {
        for (int i = 0; i < total; ++i)
        {
            if (socks[i] == INVALID_SOCKET)
            {
                continue;
            }
            int r = recv(socks[i], buf, sizeof(buf), 0); // 非阻塞
            if (r == 0)
            {
                // 对端正常关闭（FIN）
                closesocket(socks[i]);
                socks[i] = INVALID_SOCKET;
                ++closed;
            }
            else if (r < 0 && WSAGetLastError() != WSAEWOULDBLOCK)
            {
                // 连接被重置等错误：视为断开（WSAEWOULDBLOCK表示连接仍存活）
                closesocket(socks[i]);
                socks[i] = INVALID_SOCKET;
                ++closed;
            }
        }
        ++rounds;
        Sleep(50);
    }
    QueryPerformanceCounter(&tDetectEnd);
    printf("client: detected server closed %d connections (waited %.1fs)\n",
           closed, (double)(tDetectEnd.QuadPart - tDetectStart.QuadPart) / freq.QuadPart);

    // 清理剩余连接（若服务器未关闭）
    for (int i = 0; i < total; ++i)
    {
        if (socks[i] != INVALID_SOCKET)
        {
            closesocket(socks[i]); // linger(1,0)发RST，不留TIME_WAIT
        }
    }
    WSACleanup();
    printf("client: done\n");
    fflush(stdout);
    return 0;
}
