#ifndef QUICCOMMON_H
#define QUICCOMMON_H

// MsQuic 封装层公共定义（header-only）：
//   GetQuicApi()        进程级 QUIC_API_TABLE 单例
//   InitQuicSettings()  两端一致的 QUIC_SETTINGS
//   NetToolsAlpn()      ALPN "nettools"（两端必须一致）
//   QuicSendBuffer      发送缓冲区（SEND_COMPLETE 事件里 delete 释放）
//   quicStatusToString() 错误码中文映射
#include <msquic.h>

#include <QByteArray>
#include <QString>

#include <cstring>

// 进程级 MsQuic API 表单例。只 MsQuicOpen2 一次，进程存活期不 Close
// （RegistrationClose 会阻塞至全部子句柄关闭，析构后无在途回调才安全）。
inline const QUIC_API_TABLE *GetQuicApi()
{
    static const QUIC_API_TABLE *s_api = []() -> const QUIC_API_TABLE * {
        const QUIC_API_TABLE *api = nullptr;
        if (QUIC_FAILED(MsQuicOpen2(&api))) {
            api = nullptr;
        }
        return api;
    }();
    return s_api;
}

// 两端一致的 QUIC_SETTINGS：
//   IdleTimeoutMs=0            关闭空闲超时（调试工具常挂机）
//   PeerBidiStreamCount=1      默认0会对端开流报 STREAM_LIMIT_REACHED
//   StreamRecvWindowDefault    显式流控窗口，避免默认窗口限制吞吐
inline void InitQuicSettings(QUIC_SETTINGS &s)
{
    memset(&s, 0, sizeof(s));
    s.IsSet.IdleTimeoutMs = TRUE;
    s.IdleTimeoutMs = 0;
    s.IsSet.PeerBidiStreamCount = TRUE;
    s.PeerBidiStreamCount = 1;
    s.IsSet.StreamRecvWindowDefault = TRUE;
    s.StreamRecvWindowDefault = 4 * 1024 * 1024;
    s.IsSet.ConnFlowControlWindow = TRUE;
    s.ConnFlowControlWindow = 16 * 1024 * 1024;
}

// ALPN 缓冲区（返回的 QUIC_BUFFER 指向静态内存，调用后立即拷贝使用）
inline QUIC_BUFFER NetToolsAlpn()
{
    static const char kAlpn[] = "nettools";
    static QUIC_BUFFER b = {(uint32_t)sizeof(kAlpn) - 1, (uint8_t *)kAlpn};
    return b;
}

// 发送缓冲区：作为 StreamSend 的 ClientContext 传给 MsQuic，
// 在 SEND_COMPLETE 事件里 delete 释放（Canceled=true 也要释放）。
struct QuicSendBuffer
{
    QByteArray data;
    QUIC_BUFFER buf;
};

// 错误码中文映射（未覆盖的错误码显示 0x%08X）
inline QString quicStatusToString(QUIC_STATUS status)
{
    switch (status) {
    case QUIC_STATUS_SUCCESS:
        return QStringLiteral("成功");
    case QUIC_STATUS_ADDRESS_IN_USE:
        return QStringLiteral("端口被占用");
    case QUIC_STATUS_CONNECTION_REFUSED:
        return QStringLiteral("连接被拒绝");
    case QUIC_STATUS_CONNECTION_TIMEOUT:
        return QStringLiteral("连接超时");
    case QUIC_STATUS_CONNECTION_IDLE:
        return QStringLiteral("连接空闲超时");
    case QUIC_STATUS_INTERNAL_ERROR:
        return QStringLiteral("内部错误");
    case QUIC_STATUS_UNREACHABLE:
        return QStringLiteral("目标不可达");
    case QUIC_STATUS_ALPN_NEG_FAILURE:
        return QStringLiteral("ALPN协商失败");
    case QUIC_STATUS_STREAM_LIMIT_REACHED:
        return QStringLiteral("流数量达到上限");
    case QUIC_STATUS_CERT_NO_CERT:
        return QStringLiteral("证书不存在");
    case QUIC_STATUS_CERT_EXPIRED:
        return QStringLiteral("证书已过期");
    case QUIC_STATUS_CERT_UNTRUSTED_ROOT:
        return QStringLiteral("证书根不受信任");
    case QUIC_STATUS_BAD_CERTIFICATE:
        return QStringLiteral("证书无效");
    case QUIC_STATUS_INVALID_PARAMETER:
        return QStringLiteral("参数无效");
    case QUIC_STATUS_INVALID_STATE:
        return QStringLiteral("状态无效");
    case QUIC_STATUS_ABORTED:
        return QStringLiteral("连接已中止");
    case QUIC_STATUS_TLS_ERROR:
        return QStringLiteral("TLS错误");
    case QUIC_STATUS_HANDSHAKE_FAILURE:
        return QStringLiteral("握手失败");
    default:
        return QStringLiteral("0x%1").arg(status, 8, 16, QLatin1Char('0'));
    }
}

#endif // QUICCOMMON_H
