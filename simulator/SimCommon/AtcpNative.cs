/*
 * AtcpNative.cs - ATCP C 库 P/Invoke 绑定
 * 提供 C# 调用 ATCP 动态库 (atcp.dll) 的接口
 */

using System.Runtime.InteropServices;

namespace SimCommon;

/* ================================================================
 * 委托类型 — 匹配 atcp_platform_t 中的函数指针签名
 * ================================================================ */

/// <summary>写音频采样到输出设备 (Speaker/DAC)</summary>
/// <param name="samples">交错格式浮点采样数据指针</param>
/// <param name="nSamples">每通道采样数</param>
/// <param name="nChannels">通道数 (下行=2, 上行=1)</param>
/// <param name="userData">用户自定义数据指针</param>
/// <returns>实际写入的每通道采样数，负值表示错误</returns>
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int AudioWriteFunc(IntPtr samples, int nSamples, int nChannels, IntPtr userData);

/// <summary>从输入设备 (Mic/ADC) 读取音频采样</summary>
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int AudioReadFunc(IntPtr samples, int nSamples, int nChannels, IntPtr userData);

/// <summary>获取当前系统时间 (毫秒)</summary>
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate uint GetTimeMsFunc(IntPtr userData);

/* ================================================================
 * 结构体 — 匹配 C 侧的内存布局
 * ================================================================ */

/// <summary>平台抽象接口 (对应 atcp_platform_t)</summary>
[StructLayout(LayoutKind.Sequential)]
public struct AtcpPlatform
{
    public IntPtr audio_write;   // AudioWriteFunc 的函数指针
    public IntPtr audio_read;    // AudioReadFunc 的函数指针
    public IntPtr get_time_ms;   // GetTimeMsFunc 的函数指针
    public IntPtr user_data;     // 用户自定义数据
}

/// <summary>统计信息 (对应 atcp_stats_t)</summary>
[StructLayout(LayoutKind.Sequential)]
public struct AtcpStats
{
    public float Ber;              // 误码率
    public float SnrDb;            // 信噪比 (dB)
    public float ThroughputBps;    // 有效吞吐率 (bps)
    public uint FramesSent;
    public uint FramesReceived;
    public uint RetransmitCount;
    public uint AckMissCount;
}

/* ================================================================
 * 枚举 — 匹配 C 侧定义
 * ================================================================ */

/// <summary>连接状态 (对应 atcp_state_t)</summary>
public enum AtcpState
{
    Idle = 0,
    Connecting = 1,
    Connected = 2,
    Disconnecting = 3,
    Disconnected = 4,
}

/// <summary>状态/错误码 (对应 atcp_status_t)</summary>
public enum AtcpStatus
{
    Ok = 0,
    ErrInvalidParam = -1,
    ErrNoMemory = -2,
    ErrTimeout = -3,
    ErrCrcFail = -4,
    ErrRsDecodeFail = -5,
    ErrSyncFail = -6,
    ErrHandshakeFail = -7,
    ErrDisconnected = -8,
    ErrBusy = -9,
    ErrBufferFull = -10,
    ErrBufferEmpty = -11,
    ErrNotConnected = -12,
    ErrQualityLow = -13,
}

/* ================================================================
 * P/Invoke 导入 — ATCP 公开 API
 * ================================================================ */

public static class AtcpNative
{
    /// <summary>
    /// ATCP 动态库名称。
    /// 运行时需确保 atcp.dll 在可执行文件旁或系统 PATH 中。
    /// </summary>
    private const string DllName = "atcp";

    /* --- 生命周期 --- */

    /// <summary>创建 ATCP 实例</summary>
    /// <param name="config">配置指针 (IntPtr.Zero = 使用默认配置)</param>
    /// <param name="platform">平台回调接口</param>
    /// <returns>实例指针，失败返回 IntPtr.Zero</returns>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr atcp_create(IntPtr config, ref AtcpPlatform platform);

    /// <summary>销毁实例并释放所有资源</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void atcp_destroy(IntPtr inst);

    /* --- 连接管理 --- */

    /// <summary>发起连接 (主动端)</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int atcp_connect(IntPtr inst);

    /// <summary>等待连接 (被动端)</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int atcp_accept(IntPtr inst);

    /// <summary>断开连接</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int atcp_disconnect(IntPtr inst);

    /* --- 数据传输 --- */

    /// <summary>发送数据</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int atcp_send(IntPtr inst, byte[] data, nuint len);

    /// <summary>接收数据</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int atcp_recv(IntPtr inst, byte[] buf, nuint bufLen, out nuint received);

    /* --- 主循环 --- */

    /// <summary>事件驱动 tick，建议 10-15ms 周期调用</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int atcp_tick(IntPtr inst);

    /* --- 状态查询 --- */

    /// <summary>获取当前连接状态</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern AtcpState atcp_get_state(IntPtr inst);

    /// <summary>获取统计信息</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern AtcpStats atcp_get_stats(IntPtr inst);

    /// <summary>获取音频缓冲区大小（float 个数），平台层 audio_read 返回值不应超过此值</summary>
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int atcp_get_audio_buf_size(IntPtr inst);
}
