/*
 * SimLower — ATCP 下位机 (MCU 端) 模拟器
 *
 * 通过 TCP 中间件与上位机通信，替代真实声卡/ADC/DAC。
 * 作为被动连接方 (atcp_accept)，接收数据并回显。
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
DualWriter.Init("lower");

Console.WriteLine("╔══════════════════════════════════════════╗");
Console.WriteLine("║   ATCP Lower Computer Simulator (MCU)    ║");
Console.WriteLine("╚══════════════════════════════════════════╝");
Console.WriteLine();

// 命令行参数
string host = args.Length > 0 ? args[0] : "127.0.0.1";
Console.WriteLine($"[Lower] 中间件地址: {host}");

/* ================================================================
 * 1. 建立 TCP 连接（替代 ADC/DAC）
 * ================================================================ */

using var transport = new TcpAudioTransport();
transport.Connect(host, lPort: 9001, rPort: 9002, micPort: 9003, role: 'L');

/* ================================================================
 * 2. 创建 ATCP 实例
 * ================================================================ */

var platform = transport.CreatePlatform();
IntPtr inst = AtcpNative.atcp_create(IntPtr.Zero, ref platform);

if (inst == IntPtr.Zero)
{
    Console.WriteLine("[Lower] ✗ atcp_create 失败！请检查 atcp.dll 是否存在。");
    return 1;
}
Console.WriteLine("[Lower] ✓ ATCP 实例已创建。");
transport.SetAudioBufSize(AtcpNative.atcp_get_audio_buf_size(inst));

/* ================================================================
 * 3. 等待连接（被动端监听握手）
 * ================================================================ */

int rc = AtcpNative.atcp_accept(inst);
Console.WriteLine($"[Lower] atcp_accept → {(AtcpStatus)rc}");

/* ================================================================
 * 4. 主循环 — 接收数据并回显
 * ================================================================ */

bool running = true;
Console.CancelKeyPress += (_, e) => { e.Cancel = true; running = false; };

var prevState = AtcpState.Idle;
int recvCount = 0;
int echoCount = 0;
var lastStatsTime = DateTime.UtcNow;

Console.WriteLine("[Lower] 主循环启动 (Ctrl+C 退出)...\n");

while (running)
{
    try
    {
        // 驱动协议栈
        AtcpNative.atcp_tick(inst);
    }
    catch (Exception ex)
    {
        Console.WriteLine($"[Lower] atcp_tick 异常: {ex.Message}");
    }

    // 状态变化通知
    var state = AtcpNative.atcp_get_state(inst);
    if (state != prevState)
    {
        Console.WriteLine($"[Lower] 状态变化: {prevState} → {state}");
        prevState = state;
    }

    // 已连接状态：接收并回显
    if (state == AtcpState.Connected)
    {
        byte[] recvBuf = new byte[4096];
        rc = AtcpNative.atcp_recv(inst, recvBuf, (nuint)recvBuf.Length, out nuint received);
        if ((AtcpStatus)rc == AtcpStatus.Ok && received > 0)
        {
            recvCount++;
            string recvMsg = Encoding.UTF8.GetString(recvBuf, 0, (int)received);
            Console.WriteLine($"[Lower] 收到 #{recvCount}: {recvMsg} 接收时间 {DateTime.Now:HH:mm:ss.fff}");

            // 回显：添加前缀后发回
            string echoMsg = $"[Echo] {recvMsg}";
            byte[] echoData = Encoding.UTF8.GetBytes(echoMsg);

            rc = AtcpNative.atcp_send(inst, echoData, (nuint)echoData.Length);
            if ((AtcpStatus)rc == AtcpStatus.Ok)
            {
                echoCount++;
                Console.WriteLine($"[Lower] 回显 #{echoCount}: {echoMsg}");
            }
            else if ((AtcpStatus)rc != AtcpStatus.ErrBusy)
            {
                Console.WriteLine($"[Lower] 回显失败: {(AtcpStatus)rc}");
            }
        }

        // 每 10 秒打印统计
        if ((DateTime.UtcNow - lastStatsTime).TotalSeconds >= 10)
        {
            var stats = AtcpNative.atcp_get_stats(inst);
            Console.WriteLine($"[Lower] 统计: 发送帧={stats.FramesSent}, 接收帧={stats.FramesReceived}, " +
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

Console.WriteLine("\n[Lower] 断开连接...");
AtcpNative.atcp_disconnect(inst);
AtcpNative.atcp_destroy(inst);

// 防止委托被 GC 回收
GC.KeepAlive(transport);

Console.WriteLine($"[Lower] 已退出。总计接收 {recvCount} 条，回显 {echoCount} 条。");
return 0;
