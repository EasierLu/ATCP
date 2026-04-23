/*
 * SimUpper — ATCP 上位机 (PC 端) 模拟器
 *
 * 通过 TCP 中间件与下位机通信，替代真实声卡 I/O。
 * 作为主动连接方 (atcp_connect)，发送测试数据并接收回显。
 *
 * 使用方法：
 *   1. 先启动 SimMiddleware
 *   2. 启动 SimUpper 和 SimLower（顺序无关）
 *   3. 确保 atcp.dll 在可执行文件目录或 PATH 中
 */

using System.Runtime.InteropServices;
using System.Text;
using SimCommon;

Console.OutputEncoding = Encoding.UTF8;
DualWriter.Init("upper");

Console.WriteLine("╔══════════════════════════════════════════╗");
Console.WriteLine("║    ATCP Upper Computer Simulator (PC)    ║");
Console.WriteLine("╚══════════════════════════════════════════╝");
Console.WriteLine();

// 命令行参数
string host = args.Length > 0 ? args[0] : "127.0.0.1";
Console.WriteLine($"[Upper] 中间件地址: {host}");

/* ================================================================
 * 1. 建立 TCP 连接（替代声卡）
 * ================================================================ */

using var transport = new TcpAudioTransport();
transport.Connect(host, lPort: 9001, rPort: 9002, micPort: 9003, role: 'U');

/* ================================================================
 * 2. 创建 ATCP 实例
 * ================================================================ */

var platform = transport.CreatePlatform();
IntPtr inst = AtcpNative.atcp_create(IntPtr.Zero, ref platform);

if (inst == IntPtr.Zero)
{
    Console.WriteLine("[Upper] ✗ atcp_create 失败！请检查 atcp.dll 是否存在。");
    return 1;
}
Console.WriteLine("[Upper]  ✓ ATCP 实例已创建。");
transport.SetAudioBufSize(AtcpNative.atcp_get_audio_buf_size(inst));

/* ================================================================
 * 3. 发起连接（主动端握手）
 * ================================================================ */

int rc = AtcpNative.atcp_connect(inst);
Console.WriteLine($"[Upper] atcp_connect → {(AtcpStatus)rc}");

/* ================================================================
 * 4. 主循环
 * ================================================================ */

bool running = true;
Console.CancelKeyPress += (_, e) => { e.Cancel = true; running = false; };

var prevState = AtcpState.Idle;
int sendCount = 0;
int recvCount = 0;
var lastSendTime = DateTime.MinValue;
var lastStatsTime = DateTime.UtcNow;

Console.WriteLine("[Upper] 主循环启动 (Ctrl+C 退出)...\n");

while (running)
{
    try
    {
        // 驱动协议栈
        AtcpNative.atcp_tick(inst);
    }
    catch (Exception ex)
    {
        Console.WriteLine($"[Upper] atcp_tick 异常: {ex.Message}");
    }

    // 状态变化通知
    var state = AtcpNative.atcp_get_state(inst);
    if (state != prevState)
    {
        Console.WriteLine($"[Upper] 状态变化: {prevState} → {state}");
        prevState = state;
    }

    // 已连接状态：发送测试数据
    if (state == AtcpState.Connected)
    {
        // 定时发送测试消息（BUSY 时不更新时间戳，下次 tick 重试）
        if ((DateTime.UtcNow - lastSendTime).TotalMilliseconds >= 10)
        {
            string msg = $"[Upper→Lower #{sendCount}] Hello MCU! Time={DateTime.Now:HH:mm:ss.fff}";
            byte[] data = Encoding.UTF8.GetBytes(msg);

            rc = AtcpNative.atcp_send(inst, data, (nuint)data.Length);
            if ((AtcpStatus)rc == AtcpStatus.Ok)
            {
                sendCount++;
                Console.WriteLine($"[Upper] 发送 #{sendCount}: {msg}");
                lastSendTime = DateTime.UtcNow;  // 成功时才更新时间戳
            }
            else if ((AtcpStatus)rc == AtcpStatus.ErrBusy)
            {
                // 协议栈忙，保留当前时间戳，下次 tick 自动重试
            }
            else
            {
                Console.WriteLine($"[Upper] 发送失败: {(AtcpStatus)rc}");
                lastSendTime = DateTime.UtcNow;  // 真正的错误才跳过
            }
        }

        // 尝试接收数据
        byte[] recvBuf = new byte[4096];
        rc = AtcpNative.atcp_recv(inst, recvBuf, (nuint)recvBuf.Length, out nuint received);
        if ((AtcpStatus)rc == AtcpStatus.Ok && received > 0)
        {
            recvCount++;
            string recvMsg = Encoding.UTF8.GetString(recvBuf, 0, (int)received);
            Console.WriteLine($"[Upper] 收到 #{recvCount}: {recvMsg} 接收时间 {DateTime.Now:HH:mm:ss.fff}");
        }

        // 每 10 秒打印统计
        if ((DateTime.UtcNow - lastStatsTime).TotalSeconds >= 10)
        {
            var stats = AtcpNative.atcp_get_stats(inst);
            Console.WriteLine($"[Upper] 统计: 发送帧={stats.FramesSent}, 接收帧={stats.FramesReceived}, " +
                            $"重传={stats.RetransmitCount}, BER={stats.Ber:E2}, " +
                            $"吞吐={stats.ThroughputBps:F0} bps");
            lastStatsTime = DateTime.UtcNow;
        }
    }

    // 与 OFDM 符号时长匹配 (~12ms)
    Thread.Sleep(12);
}

/* ================================================================
 * 5. 清理
 * ================================================================ */

Console.WriteLine("\n[Upper] 断开连接...");
AtcpNative.atcp_disconnect(inst);
AtcpNative.atcp_destroy(inst);

// 防止委托被 GC 回收
GC.KeepAlive(transport);

Console.WriteLine($"[Upper] 已退出。总计发送 {sendCount} 条，接收 {recvCount} 条。");
return 0;
