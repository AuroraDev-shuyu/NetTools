# NetTools — Qt 网络调试助手与协议分析工具

基于 **Qt 6.5 / C++17**（MSVC 2022）的 Windows 网络调试与协议分析工具。集成 TCP/UDP、HTTP、MQTT、POP3、QUIC 客户端/服务端与 **RTP/RTCP + H.264 音视频码流分析**，配套 140 个 GTest 单元测试，并含手写 IOCP 高并发服务器的压测对比数据。

## 功能模块

| 模块 | 说明 |
|---|---|
| TCP / UDP 服务器、客户端 | 文本/十六进制收发、多客户端连接管理 |
| HTTP 服务器 / 客户端 | 基于 Qt HttpServer 的本地服务与请求工具 |
| MQTT 服务器 / 客户端 | 协议解析：主题通配符匹配、QoS 报文、可变头字段 |
| POP3 客户端 | 邮件收取全流程：USER/PASS/STAT/LIST/RETR/DELE/TOP… |
| QUIC 服务器 / 客户端 | 基于微软 MsQuic 的异步回调接入（QUIC_STATUS_PENDING 语义处理） |
| 音视频解析 | RTP/RTCP 解析（文件导入 + UDP 实时监听）、H.264 码流分析 |
| 数据转换工具 | 常用数据格式转换与校验 |

## 音视频解析模块（重点）

**RTP/RTCP 解析**（RFC 3550 / RFC 5761）：

- RTP 头全字段解析：CSRC 列表、扩展头（BEDE 两字节长度）、填充位与尾部填充字节数
- RTCP 复合包解析：SR / RR（接收报告块：丢包比例、累计丢包、抖动）/ SDES（CNAME）/ BYE / APP
- 每 SSRC 流统计：丢包率、RFC 3550 抖动公式（`jitter += (|D| − jitter) / 16`，含时间戳 90kHz 换算与序号环绕处理）
- 文件模式：`ffmpeg -f rtp` 输出的原始记录流无长度前缀，RTP 包边界按**「序号链三元验证」**锚定——候选起点须与前一锚点序号连续、且其后存在连续两个序号 +1/+2 的合法继任者，载荷内伪头部字节几乎不可能连中，实测 45 包 + 1 RTCP SR 的样例零误判
- 实时模式：UDP 监听任意端口，`ffmpeg -re` 推流可直接联调分析（表格实时滚动、上限 2000 行自动裁头）

**H.264 码流分析**（ITU-T H.264 Annex-B）：

- Annex-B 起始码（00 00 01 / 00 00 00 01）切分、NAL 头解析（forbidden_zero_bit / nal_ref_idc / 类型）
- 手写 Exp-Golomb 位读取器（ue/se），构造时自动剔除防竞争字节 00 00 03（RBSP → NAL 映射表），读越界返回 0 不崩溃
- SPS 解析：Profile / Level（名称映射表）、分辨率（宏块尺寸 − 裁剪量，cropUnit 按色度格式取值）、参考帧数、VUI 定时信息 → 帧率（`time_scale / (2 × num_units_in_tick)`）
- PPS 解析：熵编码模式（CABAC/CAVLC）、加权预测、去块滤波控制等
- 帧型判定：type 5 → IDR；type 1 → 解析 slice header 的 `slice_type % 5` 映射 I/P/B；GOP 统计（IDR 数、最小/最大/平均 GOP）
- 单测用例嵌入 x264 真实编码的 SPS 字节（320×240 Baseline、裁剪 318×238、High profile），与 ffprobe 输出交叉验证

样例码流见 `testdata/`（ffmpeg + x264 生成），GUI 手测步骤：

1. 「音视频解析 → H.264码流分析」打开 `testdata/test.h264` → NALU 表 + SPS 320×240 / Baseline / 15fps + GOP 统计
2. 「音视频解析 → RTP/RTCP解析」打开 `testdata/test.rtp` → 45 个 RTP + 1 个 RTCP SR，流统计面板有丢包率/抖动
3. RTP 实时监听 5004 端口后执行：
   ```
   ffmpeg -re -i testdata/test.h264 -c:v copy -f rtp rtp://127.0.0.1:5004
   ```

## 技术亮点

- **手写 Windows IOCP 服务器**：1 万并发连接 accept 压测 **482–495 conn/s**，同机 Qt QTcpServer 实现 138–164 conn/s，**约 3 倍**（详见 [压测对比](#压测对比)）
- **140 个 GTest 单元测试**：协议解析层核心路径全覆盖（含 RFC 3550 抖动手工核算、序号环绕丢包、Exp-Golomb 边界等边界用例）
- **协议层与界面层分离**：解析库为纯逻辑命名空间（无 GUI 依赖），可直接单测、跨项目复用
- CMake + qmake 双构建体系；日志自动落盘 `%APPDATA%\NDATools`

## 构建

依赖：Qt 6.5.3（msvc2019_64）、Visual Studio 2022、CMake ≥ 3.16（推荐 Ninja）。QUIC 模块依赖的 MsQuic 头文件/导入库已置于 `third_party/msquic/`。

```
cmake -S . -B build\cmake-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.5.3/msvc2019_64
cmake --build build\cmake-debug
```

或用 Qt Creator 直接打开 `NetTools.pro`（qmake 工程，需 MSVC 套件）。

## 单元测试

基于 GoogleTest（Debug 构建），共 140 用例：

| 工程 | 用例数 | 覆盖内容 | 构建运行 |
|---|---|---|---|
| mqtt_test | 73 | MQTT 协议解析（主题匹配、剩余长度、报文编解码） | `build_mqtt_test.bat` → `run_mqtt_test.bat` |
| pop3_test | 27 | POP3 命令/响应解析 | `build_pop3_test.bat` → `run_pop3_test.bat` |
| av_test | 40 | RTP/RTCP + H.264 解析 | `build_av_test.bat` → `run_av_test.bat` |

## 压测对比（IOCP vs Qt 服务器）

`build_bench.bat` 构建压测三件套：`iocp_bench`（手写 IOCP 服务器，接受后注册零字节接收、按完成包分派）、`qt_bench`（QTcpServer 实现）、`bench_client`（多线程连接客户端，SO_LINGER RST 断开避免 TIME_WAIT 占满端口）。

本机 1 万并发连接 accept 速率：

| 实现 | accept 速率 |
|---|---|
| 手写 IOCP | 482–495 conn/s（关闭后每连接内存归还） |
| Qt QTcpServer | 138–164 conn/s |

IOCP 版本胜在：接受与 IO 全异步、无每连接线程、接收缓冲预分配；Qt 版本需 `setMaxPendingConnections(65535)`（默认仅 30）且关闭后内存池不归还。

## 目录结构

```
NetTools/
├── third_party/msquic/    # QUIC 依赖（头文件/导入库/DLL）
├── testdata/              # 样例码流（ffmpeg 生成）
├── rtpprotocol.*          # RTP/RTCP 解析协议库（纯逻辑）
├── h264parser.*           # H.264 解析协议库（纯逻辑）
├── hexdump.h              # 十六进制转储（header-only）
├── form*.{h,cpp,ui}       # 各功能页界面
├── mainwidget.*           # 主窗口与标签页集成
├── iocpserver.*           # 手写 IOCP 服务器（压测用）
├── *_test.cpp / *_test.pro  # GTest 单测工程
└── build_*.bat / run_*.bat  # 构建与运行脚本
```
