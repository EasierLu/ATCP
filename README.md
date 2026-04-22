# ATCP — Audio Transmission Control Protocol

[![Language](https://img.shields.io/badge/language-C99-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake%203.10%2B-green.svg)]()
[![License](https://img.shields.io/badge/license-MIT-lightgrey.svg)](LICENSE)

ATCP 是一个**纯 C99 实现、零外部依赖**的音频信道数据通信协议栈库。通过标准 3.5mm 音频接口（Speaker Out / Mic In），在 PC 与 MCU 之间建立双向数据通道，实现绕过网络层的隐蔽通信。

```
┌─────────────────┐      3.5mm Audio Cable      ┌─────────────────┐
│   上位机 (PC)    │ ◄─────────────────────────► │  下位机 (MCU)    │
│                 │   Speaker Out ──► ADC In    │                 │
│                 │   Mic In     ◄── DAC Out    │                 │
└─────────────────┘                             └─────────────────┘
```

## 特性

- **零依赖**：纯 C99，可嵌入任意平台（裸机 / RTOS / 桌面）
- **双向通信**：下行差分双声道 OFDM + 自适应 QAM（QPSK ~ 256-QAM），上行单声道固定 QPSK
- **双重可靠性**：Reed-Solomon 前向纠错 + 滑动窗口 ARQ 选择性重传
- **高吞吐**：连续流模式下有效数据占比 ~90%，下行最高 ~95 kbps
- **自适应握手**：四阶段协商采样率、QAM 阶数等参数，支持质量评估与自动降级
- **非阻塞 API**：事件驱动设计，所有操作异步处理
- **跨平台**：支持 Windows / Linux / STM32 / ESP32 等

## 协议架构

```
┌───────────────────────────────┐
│        应用层 (Application)    │  用户代码（文件传输、命令控制、加密等）
├───────────────────────────────┤
│        链路层 (Link)           │  握手、心跳、帧封装、滑动窗口 ARQ
├───────────────────────────────┤
│        编码层 (Coding)         │  RS FEC 编解码、CRC32 校验
├───────────────────────────────┤
│        调制层 (Modulation)     │  OFDM + M-QAM、帧同步、信道估计
├───────────────────────────────┤
│        物理层 (Physical)       │  差分信号、AGC、CFO/SFO 补偿
└───────────────────────────────┘
```

## 构建

### 环境要求

- CMake >= 3.10
- C99 兼容编译器（MSVC / GCC / Clang）

### 编译

```bash
# 动态库（默认）
cmake -B build
cmake --build build --config Release

# 静态库（推荐嵌入式场景）
cmake -B build -DBUILD_SHARED_LIBS=OFF
cmake --build build --config Release
```

| CMake 选项 | 默认值 | 说明 |
|---|---|---|
| `BUILD_SHARED_LIBS` | `ON` | `ON` = 动态库，`OFF` = 静态库 |
| `BUILD_TESTS` | `ON` | 是否编译单元测试 |

> 静态库用户请在编译选项中定义 `ATCP_STATIC` 以消除 DLL 导出装饰。

## 快速上手

```c
#include <atcp/atcp.h>

/* 1. 实现平台回调 */
int my_audio_write(const float *samples, int n, int ch, void *ud) { /* 写声卡 */ return n; }
int my_audio_read(float *samples, int n, int ch, void *ud)        { /* 读麦克风 */ return n; }
uint32_t my_get_time_ms(void *ud)                                 { return get_system_time_ms(); }

int main(void) {
    /* 2. 配置平台回调 */
    atcp_platform_t platform = {0};
    platform.audio_write = my_audio_write;
    platform.audio_read  = my_audio_read;
    platform.get_time_ms = my_get_time_ms;

    /* 3. 创建实例（NULL = 使用默认配置） */
    atcp_instance_t *inst = atcp_create(NULL, &platform);

    /* 4. 发起连接 */
    atcp_connect(inst);

    /* 5. 主循环（建议 10-15ms 周期） */
    while (1) {
        atcp_tick(inst);

        if (atcp_get_state(inst) == ATCP_STATE_CONNECTED) {
            const uint8_t msg[] = "Hello MCU!";
            atcp_send(inst, msg, sizeof(msg));

            uint8_t buf[256];
            size_t received = 0;
            if (atcp_recv(inst, buf, sizeof(buf), &received) == ATCP_OK) {
                /* 处理收到的数据 */
            }
        }
    }

    atcp_destroy(inst);
    return 0;
}
```

## API 概览

| 函数 | 说明 |
|---|---|
| `atcp_create(config, platform)` | 创建实例，`config` 传 `NULL` 使用默认值 |
| `atcp_destroy(inst)` | 销毁实例，释放所有资源 |
| `atcp_connect(inst)` | 发起连接（主动端），非阻塞 |
| `atcp_accept(inst)` | 等待连接（被动端），非阻塞 |
| `atcp_disconnect(inst)` | 断开连接 |
| `atcp_send(inst, data, len)` | 发送数据，非阻塞 |
| `atcp_recv(inst, buf, len, &received)` | 接收数据 |
| `atcp_tick(inst)` | 驱动协议栈（音频 I/O、帧同步、解调、ARQ、心跳） |
| `atcp_get_state(inst)` | 获取当前连接状态 |
| `atcp_get_stats(inst)` | 获取统计信息（BER、SNR、吞吐量等） |

## 项目结构

```
ATCP/
├── CMakeLists.txt                 # 顶层构建配置
├── ATCP.md                  # 系统设计文档（协议规格）
└── lib/
    ├── CMakeLists.txt             # 库构建配置
    ├── USER_GUIDE.md              # 用户指南
    ├── DEVELOPER_GUIDE.md         # 开发者指南
    ├── include/atcp/        # 公开头文件
    │   ├── atcp.h           #   统一 API 入口
    │   ├── types.h                #   类型定义、状态码
    │   ├── config.h               #   配置结构与默认值
    │   └── platform.h             #   平台抽象回调接口
    ├── src/
    │   ├── atcp.c           # API 实现（集成层）
    │   ├── common/                # FFT、复数运算、环形缓冲区、PRNG
    │   ├── physical/              # 差分编码、AGC、CFO/SFO 补偿
    │   ├── modulation/            # OFDM、QAM、训练序列、帧同步、信道估计
    │   ├── coding/                # RS 纠错、CRC32、GF(256)
    │   └── link/                  # 帧结构、握手、ARQ、ACK、心跳
    └── tests/                     # 单元测试与集成测试
```

## 平台适配

使用 ATCP 需要实现 `atcp_platform_t` 中的三个回调：

| 回调 | 说明 |
|---|---|
| `audio_write` | 将音频采样写入输出设备（Speaker / DAC） |
| `audio_read` | 从输入设备（Mic / ADC）读取音频采样 |
| `get_time_ms` | 返回当前系统时间（毫秒） |

| 平台 | audio_write / audio_read | get_time_ms |
|---|---|---|
| Windows | WASAPI / PortAudio | `GetTickCount()` |
| Linux | ALSA / PulseAudio | `clock_gettime()` |
| STM32 | DMA + DAC/ADC | `HAL_GetTick()` |
| ESP32 | I2S Driver | `esp_timer_get_time() / 1000` |

## 文档

- [系统设计文档](ATCP.md) — 协议规格、物理层设计、调制方案、链路层机制
- [用户指南](lib/USER_GUIDE.md) — 构建步骤、完整 API 参考、配置参数说明
- [开发者指南](lib/DEVELOPER_GUIDE.md) — 架构设计、模块详解、开发规范、扩展指南

## License

见 [LICENSE](LICENSE) 文件。
