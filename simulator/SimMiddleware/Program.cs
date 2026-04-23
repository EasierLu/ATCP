/*
 * SimMiddleware — ATCP 模拟器中间件
 *
 * 3 通道 TCP 中继服务器，替代物理音频线缆：
 *   Port 9001: L Channel (差分左声道)  上位机→下位机
 *   Port 9002: R Channel (差分右声道)  上位机→下位机
 *   Port 9003: Mic Channel (麦克风)    下位机→上位机
 *
 * 每个通道接受 2 个 TCP 连接（上位机 'U' 和下位机 'L'），
 * 在两者之间双向转发原始字节流。
 *
 * 启动顺序：先启动中间件，再启动上位机和下位机（顺序无关）。
 */

using System.Net;
using System.Net.Sockets;
using SimCommon;

Console.OutputEncoding = System.Text.Encoding.UTF8;
DualWriter.Init("middleware");

Console.WriteLine("╔══════════════════════════════════════════╗");
Console.WriteLine("║      ATCP Simulator Middleware           ║");
Console.WriteLine("╠══════════════════════════════════════════╣");
Console.WriteLine("║  Port 9001: L Channel  (差分左声道)      ║");
Console.WriteLine("║  Port 9002: R Channel  (差分右声道)      ║");
Console.WriteLine("║  Port 9003: Mic Channel (上行麦克风)     ║");
Console.WriteLine("╚══════════════════════════════════════════╝");
Console.WriteLine();

var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) =>
{
    e.Cancel = true;
    cts.Cancel();
    Console.WriteLine("\n[Middleware] 正在关闭...");
};

try
{
    await Task.WhenAll(
        RunChannel("L-Channel", 9001, cts.Token),
        RunChannel("R-Channel", 9002, cts.Token),
        RunChannel("Mic-Channel", 9003, cts.Token)
    );
}
catch (OperationCanceledException)
{
    // 正常退出
}

Console.WriteLine("[Middleware] 已退出。");

/* ================================================================
 * 通道中继逻辑
 * ================================================================ */

/// <summary>
/// 运行一个通道：监听端口，等待上位机和下位机各连接一次，
/// 然后在两者之间双向转发数据。断开后等待新连接。
/// </summary>
static async Task RunChannel(string name, int port, CancellationToken ct)
{
    var listener = new TcpListener(IPAddress.Any, port);
    listener.Start();
    Console.WriteLine($"[{name}] 监听端口 {port}...");

    while (!ct.IsCancellationRequested)
    {
        TcpClient? upperClient = null;
        TcpClient? lowerClient = null;

        try
        {
            Console.WriteLine($"[{name}] 等待连接...");

            // 接受第一个连接
            var client1 = await AcceptWithCancel(listener, ct);
            var stream1 = client1.GetStream();
            char role1 = (char)stream1.ReadByte();
            Console.WriteLine($"[{name}] 客户端 1 已连接 (角色: {role1}, 来源: {((IPEndPoint)client1.Client.RemoteEndPoint!).Port})");

            // 接受第二个连接
            var client2 = await AcceptWithCancel(listener, ct);
            var stream2 = client2.GetStream();
            char role2 = (char)stream2.ReadByte();
            Console.WriteLine($"[{name}] 客户端 2 已连接 (角色: {role2}, 来源: {((IPEndPoint)client2.Client.RemoteEndPoint!).Port})");

            // 按角色分配
            if (role1 == 'U')
            {
                upperClient = client1;
                lowerClient = client2;
            }
            else
            {
                upperClient = client2;
                lowerClient = client1;
            }

            Console.WriteLine($"[{name}] ✓ 双端就绪，开始中继...");

            var upperStream = upperClient.GetStream();
            var lowerStream = lowerClient.GetStream();

            // 双向中继：两个方向同时转发
            var stats = new ChannelStats();

            var relayTask = Task.WhenAny(
                RelayAsync(name, upperStream, lowerStream, "U→L", stats, isUL: true, ct),
                RelayAsync(name, lowerStream, upperStream, "L→U", stats, isUL: false, ct)
            );

            // 定期打印统计
            var statsTask = PrintStatsAsync(name, stats, ct);

            await Task.WhenAny(relayTask, statsTask);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[{name}] 会话错误: {ex.Message}");
        }
        finally
        {
            upperClient?.Dispose();
            lowerClient?.Dispose();
            Console.WriteLine($"[{name}] 会话结束，等待新连接...\n");
        }
    }

    listener.Stop();
}

/// <summary>带取消支持的 AcceptTcpClient</summary>
static async Task<TcpClient> AcceptWithCancel(TcpListener listener, CancellationToken ct)
{
    using var reg = ct.Register(() => listener.Stop());
    try
    {
        return await listener.AcceptTcpClientAsync();
    }
    catch (ObjectDisposedException) when (ct.IsCancellationRequested)
    {
        throw new OperationCanceledException(ct);
    }
    catch (SocketException) when (ct.IsCancellationRequested)
    {
        throw new OperationCanceledException(ct);
    }
}

/// <summary>单方向数据中继</summary>
static async Task RelayAsync(string channel, NetworkStream from, NetworkStream to,
    string direction, ChannelStats stats, bool isUL, CancellationToken ct)
{
    byte[] buffer = new byte[65536];

    try
    {
        while (!ct.IsCancellationRequested)
        {
            int n = await from.ReadAsync(buffer, ct);
            if (n == 0)
            {
                Console.WriteLine($"[{channel}] {direction} 连接关闭");
                break;
            }

            await to.WriteAsync(buffer.AsMemory(0, n), ct);
            await to.FlushAsync(ct);

            if (isUL)
                Interlocked.Add(ref stats.BytesUL, n);
            else
                Interlocked.Add(ref stats.BytesLU, n);
        }
    }
    catch (IOException ex)
    {
        Console.WriteLine($"[{channel}] {direction} IO错误: {ex.Message}");
    }
}

/// <summary>定期打印中继统计信息</summary>
static async Task PrintStatsAsync(string channel, ChannelStats stats, CancellationToken ct)
{
    try
    {
        while (!ct.IsCancellationRequested)
        {
            await Task.Delay(5000, ct);
            long ul = Interlocked.Read(ref stats.BytesUL);
            long lu = Interlocked.Read(ref stats.BytesLU);
            Console.WriteLine($"[{channel}] 统计: U→L {FormatBytes(ul)}, L→U {FormatBytes(lu)}");
        }
    }
    catch (OperationCanceledException)
    {
        // 正常退出
    }
}

static string FormatBytes(long bytes)
{
    if (bytes < 1024) return $"{bytes} B";
    if (bytes < 1024 * 1024) return $"{bytes / 1024.0:F1} KB";
    return $"{bytes / (1024.0 * 1024.0):F1} MB";
}

/// <summary>通道统计计数器（避免 async 方法中使用 ref）</summary>
class ChannelStats
{
    public long BytesUL;
    public long BytesLU;
}
