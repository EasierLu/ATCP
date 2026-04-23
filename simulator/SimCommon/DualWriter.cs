/*
 * DualWriter.cs - 双重输出 TextWriter
 *
 * 将 Console.Out 同时输出到控制台和日志文件。
 * 同时通过重定向 C 运行时的 stderr 文件描述符，
 * 捕获 ATCP C 库的 fprintf(stderr, ...) 输出到同一日志文件。
 *
 * 用法：
 *   DualWriter.Init("upper");  // 生成 upper.log
 */

using System.Runtime.InteropServices;
using System.Text;

namespace SimCommon;

public class DualWriter : TextWriter
{
    private readonly TextWriter _console;
    private readonly StreamWriter _file;

    public override Encoding Encoding => _console.Encoding;

    private DualWriter(TextWriter console, StreamWriter file)
    {
        _console = console;
        _file = file;
    }

    /* Windows CRT 重定向：将 C 的 stderr 也写入日志文件 */
    [DllImport("ucrtbase.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    private static extern IntPtr freopen(string path, string mode, IntPtr stream);

    [DllImport("ucrtbase.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr __acrt_iob_func(int index);  // 0=stdin, 1=stdout, 2=stderr

    /// <summary>
    /// 初始化双重输出：
    /// 1. Console.Out 同时写控制台和日志文件
    /// 2. C 库的 stderr 重定向到同一日志文件
    /// </summary>
    /// <param name="name">日志文件名前缀，如 "upper" → upper.log</param>
    public static void Init(string name)
    {
        string logDir = AppContext.BaseDirectory;
        string logPath = Path.Combine(logDir, $"{name}.log");
        string nativeLogPath = Path.Combine(logDir, $"{name}_native.log");

        var fileWriter = new StreamWriter(logPath, append: false, Encoding.UTF8)
        {
            AutoFlush = true
        };

        // .NET Console 双重输出
        var dualOut = new DualWriter(Console.Out, fileWriter);
        Console.SetOut(dualOut);

        // C 库 stderr 重定向到单独的日志文件（避免文件句柄冲突）
        try
        {
            IntPtr stderrHandle = __acrt_iob_func(2);
            freopen(nativeLogPath, "w", stderrHandle);
            Console.WriteLine($"[Log] C 库调试日志: {nativeLogPath}");
        }
        catch
        {
            Console.WriteLine("[Log] 警告: 无法重定向 C stderr，ATCP 调试日志可能不在文件中");
        }

        Console.WriteLine($"[Log] .NET 日志文件: {logPath}");
    }

    public override void Write(char value)
    {
        _console.Write(value);
        _file.Write(value);
    }

    public override void Write(string? value)
    {
        _console.Write(value);
        _file.Write(value);
    }

    public override void WriteLine(string? value)
    {
        _console.WriteLine(value);
        _file.WriteLine(value);
    }

    public override void Flush()
    {
        _console.Flush();
        _file.Flush();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _file.Flush();
            _file.Dispose();
        }
        base.Dispose(disposing);
    }
}
