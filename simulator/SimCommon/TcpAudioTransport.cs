/*
 * TcpAudioTransport.cs - TCP 音频传输层
 *
 * 将 ATCP 的 audio_write / audio_read 平台回调映射到 3 个 TCP 连接：
 *   - L Channel (port 9001): 差分信号左声道  (下行: 上位机→下位机)
 *   - R Channel (port 9002): 差分信号右声道  (下行: 上位机→下位机)
 *   - Mic Channel (port 9003): 上行单声道    (上行: 下位机→上位机)
 *
 * 重要：ATCP 库内部始终以 n_channels=2 调用 audio_write（差分编码），
 *       以 n_channels=1 调用 audio_read（单声道）。
 *       因此路由必须基于角色（上位机/下位机），而非 n_channels。
 *
 * 路由规则（基于角色）：
 *
 *   上位机 (Upper, role='U'):
 *     audio_write(2ch) → 拆分 L/R，发送到 TCP 9001 + 9002  (下行输出)
 *     audio_read(1ch)  → 从 TCP 9003 读取                    (上行输入)
 *
 *   下位机 (Lower, role='L'):
 *     audio_write(2ch) → 提取单声道，发送到 TCP 9003          (上行输出)
 *     audio_read(1ch)  → 从 TCP 9001 读取 L 声道              (下行输入)
 *                        (同时排空 TCP 9002 的 R 声道数据)
 */

using System.Net.Sockets;
using System.Runtime.InteropServices;

namespace SimCommon;

/// <summary>
/// TCP 音频传输层 — 替代真实声卡 I/O，通过 TCP 连接中间件转发音频数据。
/// 路由策略基于角色（上位机/下位机），而非 n_channels 参数。
/// </summary>
public class TcpAudioTransport : IDisposable
{
    /* ================================================================
     * TCP 连接
     * ================================================================ */

    private TcpClient? _lClient;      // L 声道连接 (port 9001)
    private TcpClient? _rClient;      // R 声道连接 (port 9002)
    private TcpClient? _micClient;    // Mic 声道连接 (port 9003)

    private NetworkStream? _lStream;
    private NetworkStream? _rStream;
    private NetworkStream? _micStream;

    /* ================================================================
     * 角色标识
     * ================================================================ */

    private char _role;  // 'U' = 上位机, 'L' = 下位机

    /* ================================================================
     * 接收缓冲区 — 字节级，处理 TCP 流中的部分 float 问题
     * ================================================================ */

    private const int RecvBufSize = 65536;

    private readonly byte[] _lRecvBuf = new byte[RecvBufSize];
    private int _lRecvCount;

    private readonly byte[] _rRecvBuf = new byte[RecvBufSize];
    private int _rRecvCount;

    private readonly byte[] _micRecvBuf = new byte[RecvBufSize];
    private int _micRecvCount;

    /* ================================================================
     * 托管委托实例 — 必须保持引用以防止 GC 回收
     * ================================================================ */

    private readonly AudioWriteFunc _writeDelegate;
    private readonly AudioReadFunc _readDelegate;
    private readonly GetTimeMsFunc _timeDelegate;

    private readonly long _startTicks;

    // 调试统计
    private long _writeCallCount;
    private long _writeTotalSamples;
    private long _readCallCount;
    private long _readTotalSamples;
    private long _readZeroCount;   // audio_read 返回 0 的次数
    private long _lastDiagTick;
    private const int DetailLogCount = 0;  // 前 N 次调用打印详细日志（0=关闭）
    private long _readReturnedDataCount;  // audio_read 返回有效数据的次数

    public TcpAudioTransport()
    {
        _writeDelegate = OnAudioWrite;
        _readDelegate = OnAudioRead;
        _timeDelegate = OnGetTimeMs;
        _startTicks = Environment.TickCount64;
        _lastDiagTick = _startTicks;
    }

    /* ================================================================
     * 连接管理
     * ================================================================ */

    /// <summary>
    /// 连接到中间件。
    /// </summary>
    /// <param name="host">中间件地址</param>
    /// <param name="lPort">L 声道端口 (默认 9001)</param>
    /// <param name="rPort">R 声道端口 (默认 9002)</param>
    /// <param name="micPort">Mic 声道端口 (默认 9003)</param>
    /// <param name="role">'U' = 上位机, 'L' = 下位机</param>
    public void Connect(string host, int lPort, int rPort, int micPort, char role)
    {
        _role = role;
        Console.WriteLine($"[Transport] 正在连接中间件 {host} (角色: {role})...");

        try
        {
            _lClient = ConnectChannel(host, lPort, role, "L-Channel");
            _lStream = _lClient.GetStream();

            _rClient = ConnectChannel(host, rPort, role, "R-Channel");
            _rStream = _rClient.GetStream();

            _micClient = ConnectChannel(host, micPort, role, "Mic-Channel");
            _micStream = _micClient.GetStream();
        }
        catch
        {
            // 连接过程中某个通道失败，清理已建立的连接
            _micStream?.Dispose(); _micStream = null;
            _micClient?.Dispose(); _micClient = null;
            _rStream?.Dispose();   _rStream = null;
            _rClient?.Dispose();   _rClient = null;
            _lStream?.Dispose();   _lStream = null;
            _lClient?.Dispose();   _lClient = null;
            throw;
        }

        Console.WriteLine("[Transport] 所有通道已连接。");
    }

    private static TcpClient ConnectChannel(string host, int port, char role, string name)
    {
        var client = new TcpClient { NoDelay = true };

        // 重试连接，等待中间件启动
        for (int attempt = 1; ; attempt++)
        {
            try
            {
                client.Connect(host, port);
                break;
            }
            catch (SocketException) when (attempt < 30)
            {
                if (attempt == 1)
                    Console.WriteLine($"[Transport] 等待 {name} (port {port})...");
                Thread.Sleep(1000);
            }
        }

        // 发送角色标识字节
        client.GetStream().WriteByte((byte)role);
        Console.WriteLine($"[Transport] {name} (port {port}) 已连接。");
        return client;
    }

    /* ================================================================
     * 创建 ATCP 平台接口
     * ================================================================ */

    /// <summary>
    /// 创建 AtcpPlatform 结构体，将函数指针指向本对象的回调方法。
    /// 注意：本对象的生命周期必须覆盖 ATCP 实例的整个生命周期。
    /// </summary>
    public AtcpPlatform CreatePlatform()
    {
        return new AtcpPlatform
        {
            audio_write = Marshal.GetFunctionPointerForDelegate(_writeDelegate),
            audio_read = Marshal.GetFunctionPointerForDelegate(_readDelegate),
            get_time_ms = Marshal.GetFunctionPointerForDelegate(_timeDelegate),
            user_data = IntPtr.Zero,
        };
    }

    /* ================================================================
     * 平台回调实现 — 基于角色路由
     * ================================================================ */

    /// <summary>
    /// audio_write 回调：ATCP 库始终以 n_channels=2 调用（差分编码）。
    ///
    /// 上位机: 拆分 L/R → 发送到 TCP 9001 + 9002 (下行)
    /// 下位机: 提取单声道 → 发送到 TCP 9003 (上行)
    /// </summary>
    private int OnAudioWrite(IntPtr samples, int nSamples, int nChannels, IntPtr userData)
    {
        try
        {
            int totalFloats = nSamples * nChannels;
            float[] all = new float[totalFloats];
            Marshal.Copy(samples, all, 0, totalFloats);

            // 详细日志：前 N 次调用
            if (_writeCallCount < DetailLogCount)
            {
                // 计算信号能量
                double energy = 0;
                for (int i = 0; i < Math.Min(totalFloats, 32); i++)
                    energy += all[i] * all[i];
                Console.WriteLine($"[W-{_role}] #{_writeCallCount} nSamples={nSamples} nCh={nChannels} " +
                                $"energy={energy:F4} first4=[{Fmt(all, 4)}]");
            }

            if (_role == 'U')
            {
                // 上位机：下行输出 → L + R 声道
                if (nChannels == 2)
                {
                    float[] lBuf = new float[nSamples];
                    float[] rBuf = new float[nSamples];
                    for (int i = 0; i < nSamples; i++)
                    {
                        lBuf[i] = all[2 * i];
                        rBuf[i] = all[2 * i + 1];
                    }
                    SendFloats(_lStream!, lBuf, nSamples);
                    SendFloats(_rStream!, rBuf, nSamples);
                }
                else
                {
                    SendFloats(_lStream!, all, nSamples);
                }
            }
            else
            {
                // 下位机：上行输出 → Mic 声道
                if (nChannels == 2)
                {
                    float[] mono = new float[nSamples];
                    for (int i = 0; i < nSamples; i++)
                    {
                        mono[i] = all[2 * i];  // 取 L 通道 = +signal
                    }
                    SendFloats(_micStream!, mono, nSamples);
                }
                else
                {
                    SendFloats(_micStream!, all, nSamples);
                }
            }
            _writeCallCount++;
            _writeTotalSamples += nSamples;
            PrintDiag();
            return nSamples;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[Transport] audio_write 错误: {ex.Message}");
            return -1;
        }
    }

    /// <summary>
    /// audio_read 回调：ATCP 库始终以 n_channels=1 调用（单声道输入）。
    ///
    /// 重要：ATCP 的 atcp_tick() 内部需要一次性读到完整帧
    /// （训练符号 + 数据符号），但 nSamples 参数仅为一个 OFDM 符号大小。
    /// 真实声卡会在两次 tick 之间累积大量采样，因此这里应返回尽可能多的
    /// 可用数据（最多 2048 = audio_in_buf 大小），而非仅限 nSamples。
    ///
    /// 上位机: 从 TCP 9003 (Mic) 读取 → 接收下位机的上行数据
    /// 下位机: 从 TCP 9001 (L) 读取   → 接收上位机的下行数据
    ///         (同时排空 TCP 9002 R 声道，避免缓冲区堆积)
    /// </summary>
    private int _audioBufSize;  // 从 ATCP 库获取的实际缓冲区大小

    /// <summary>设置音频缓冲区大小，在 atcp_create 之后调用</summary>
    public void SetAudioBufSize(int size)
    {
        _audioBufSize = size;
        Console.WriteLine($"[Transport] 音频缓冲区大小: {size} floats ({size * 4 / 1024.0:F1} KB)");
    }

    private int OnAudioRead(IntPtr samples, int nSamples, int nChannels, IntPtr userData)
    {
        // 严格使用调用方传入的缓冲区剩余空间，防止写越界
        int maxSamples = nSamples;

        try
        {
            if (_role == 'U')
            {
                // 上位机：从 Mic 通道读取上行数据
                TryReceiveMore(_micClient!, _micRecvBuf, ref _micRecvCount);
                int available = Math.Min(_micRecvCount / sizeof(float), maxSamples);

                if (available > 0)
                {
                    float[] buf = new float[available];
                    ConsumeFloats(_micRecvBuf, ref _micRecvCount, buf, available);
                    Marshal.Copy(buf, 0, samples, available);
                    _readCallCount++;
                    _readTotalSamples += available;
                    _readReturnedDataCount++;

                    if (_readCallCount <= DetailLogCount)
                    {
                        double energy = 0;
                        for (int i = 0; i < Math.Min(available, 32); i++)
                            energy += buf[i] * buf[i];
                        Console.WriteLine($"[R-{_role}] #{_readCallCount} req={nSamples} got={available} " +
                                        $"energy={energy:F4} first4=[{Fmt(buf, 4)}]");
                    }
                }
                else
                {
                    _readZeroCount++;
                    if (_readZeroCount == 1)
                        Console.WriteLine($"[R-{_role}] 无数据 (首次) req={nSamples}");
                }
                PrintDiag();
                return available;
            }
            else
            {
                // 下位机：从 L 声道读取下行数据
                TryReceiveMore(_lClient!, _lRecvBuf, ref _lRecvCount);
                int available = Math.Min(_lRecvCount / sizeof(float), maxSamples);

                if (available > 0)
                {
                    float[] buf = new float[available];
                    ConsumeFloats(_lRecvBuf, ref _lRecvCount, buf, available);
                    Marshal.Copy(buf, 0, samples, available);
                    _readCallCount++;
                    _readTotalSamples += available;
                    _readReturnedDataCount++;

                    if (_readCallCount <= DetailLogCount)
                    {
                        double energy = 0;
                        for (int i = 0; i < Math.Min(available, 32); i++)
                            energy += buf[i] * buf[i];
                        Console.WriteLine($"[R-{_role}] #{_readCallCount} req={nSamples} got={available} " +
                                        $"energy={energy:F4} first4=[{Fmt(buf, 4)}]");
                    }
                }
                else
                {
                    _readZeroCount++;
                    if (_readZeroCount == 1)
                        Console.WriteLine($"[R-{_role}] 无数据 (首次) req={nSamples}");
                }

                // 同时排空 R 声道缓冲区
                DrainChannel(_rClient!, _rRecvBuf, ref _rRecvCount);

                PrintDiag();
                return available;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[Transport] audio_read 错误: {ex.Message}");
            return 0;
        }
    }

    /// <summary>格式化前 N 个 float 值</summary>
    private static string Fmt(float[] arr, int n)
    {
        int count = Math.Min(arr.Length, n);
        var parts = new string[count];
        for (int i = 0; i < count; i++)
            parts[i] = arr[i].ToString("F4");
        return string.Join(", ", parts);
    }

    /// <summary>每 3 秒打印一次诊断信息</summary>
    private void PrintDiag()
    {
        long now = Environment.TickCount64;
        if (now - _lastDiagTick >= 10000)
        {
            Console.WriteLine($"[Transport-{_role}] 诊断: write 调用={_writeCallCount} 总采样={_writeTotalSamples}, " +
                            $"read 返回={_readCallCount} 总采样={_readTotalSamples}, read有数据={_readReturnedDataCount}, read空={_readZeroCount}, " +
                            $"缓冲 L={_lRecvCount}B R={_rRecvCount}B Mic={_micRecvCount}B");
            _lastDiagTick = now;
        }
    }

    /// <summary>获取系统毫秒时间戳</summary>
    private uint OnGetTimeMs(IntPtr userData)
    {
        return (uint)((Environment.TickCount64 - _startTicks) & 0xFFFFFFFF);
    }

    /* ================================================================
     * TCP 收发辅助方法
     * ================================================================ */

    /// <summary>将 float 数组作为原始字节发送到 TCP 流</summary>
    private static void SendFloats(NetworkStream stream, float[] data, int count)
    {
        byte[] bytes = new byte[count * sizeof(float)];
        Buffer.BlockCopy(data, 0, bytes, 0, bytes.Length);
        stream.Write(bytes, 0, bytes.Length);
    }

    /// <summary>非阻塞地从 TCP 读取可用字节到接收缓冲区</summary>
    private static void TryReceiveMore(TcpClient client, byte[] buffer, ref int count)
    {
        if (!client.Connected) return;

        int available = client.Available;
        if (available <= 0) return;

        int space = buffer.Length - count;
        if (space <= 0) return;

        int toRead = Math.Min(available, space);
        int n = client.GetStream().Read(buffer, count, toRead);
        if (n > 0) count += n;
    }

    /// <summary>排空 TCP 通道的接收数据（读取但丢弃）</summary>
    private static void DrainChannel(TcpClient client, byte[] buffer, ref int count)
    {
        if (!client.Connected) return;

        int available = client.Available;
        if (available <= 0) return;

        // 直接读取并丢弃
        int toRead = Math.Min(available, buffer.Length);
        client.GetStream().Read(buffer, 0, toRead);
        count = 0;  // 重置缓冲区
    }

    /// <summary>从字节缓冲区中提取完整的 float 值</summary>
    private static void ConsumeFloats(byte[] buffer, ref int count, float[] output, int maxFloats)
    {
        int bytesToConsume = maxFloats * sizeof(float);
        if (bytesToConsume > count) bytesToConsume = (count / sizeof(float)) * sizeof(float);

        if (bytesToConsume > 0)
        {
            Buffer.BlockCopy(buffer, 0, output, 0, bytesToConsume);

            // 移动剩余数据到缓冲区头部
            count -= bytesToConsume;
            if (count > 0)
                Buffer.BlockCopy(buffer, bytesToConsume, buffer, 0, count);
        }
    }

    /* ================================================================
     * IDisposable
     * ================================================================ */

    public void Dispose()
    {
        _lStream?.Dispose();
        _rStream?.Dispose();
        _micStream?.Dispose();
        _lClient?.Dispose();
        _rClient?.Dispose();
        _micClient?.Dispose();
        GC.SuppressFinalize(this);
    }
}
