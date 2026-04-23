# ATCP 用户指南

## 1. 简介

ATCP 是一个纯 C 语言实现的音频信道数据通信协议栈库。它通过标准 3.5mm 音频接口（Speaker Out / Mic In）在 PC 与 MCU 之间建立双向数据通道，实现绕过网络层的隐蔽通信。

**核心特性：**

- 零外部依赖，纯 C99，可嵌入任意平台
- 支持编译为静态库或动态库（DLL/SO）
- 下行差分双声道 OFDM + 自适应 QAM（QPSK ~ 256-QAM）
- 上行单声道固定 QPSK
- Reed-Solomon 前向纠错 + 滑动窗口 ARQ 双重可靠性保障
- 非阻塞事件驱动 API，适配裸机/RTOS/桌面环境

## 2. 构建

### 2.1 环境要求

- CMake >= 3.10
- C99 兼容编译器（MSVC / GCC / Clang）

### 2.2 编译步骤

**静态库（推荐嵌入式场景）：**

```bash
cmake -B build -DBUILD_SHARED_LIBS=OFF
cmake --build build  --config Release
```

**动态库（DLL/SO）：**

```bash
cmake -B build -DBUILD_SHARED_LIBS=ON
cmake --build build  --config Release
```

静态库模式下，建议在你的项目中定义 `ATCP_STATIC` 宏以消除导出符号装饰。

### 2.3 集成到你的项目

**CMake `add_subdirectory` 方式：**

```cmake
add_subdirectory(path/to/ATCP/lib)
target_link_libraries(your_app PRIVATE atcp)
```

**手动链接：**

- 头文件目录：`lib/include`
- 库文件：`atcp.lib` / `libatcp.a` / `atcp.dll`

## 3. 快速上手

### 3.1 最小使用示例

```c
#include <atcp/atcp.h>

/* ---- 1. 实现平台回调 ---- */

int my_audio_write(const float *samples, int n_samples, int n_channels, void *ud) {
    /* 将采样写入声卡输出 / DAC */
    return n_samples; /* 返回实际写入数 */
}

int my_audio_read(float *samples, int n_samples, int n_channels, void *ud) {
    /* 从麦克风输入 / ADC 读取采样 */
    return n_samples; /* 返回实际读取数 */
}

uint32_t my_get_time_ms(void *ud) {
    /* 返回当前毫秒级时间戳 */
    return get_system_time_ms();
}

/* ---- 2. 创建实例并通信 ---- */

int main(void) {
    /* 配置平台回调 */
    atcp_platform_t platform = {0};
    platform.audio_write = my_audio_write;
    platform.audio_read  = my_audio_read;
    platform.get_time_ms = my_get_time_ms;
    platform.user_data   = NULL;

    /* 创建实例（NULL 使用默认配置） */
    atcp_instance_t *inst = atcp_create(NULL, &platform);

    /* 发起连接（主动端） */
    atcp_connect(inst);

    /* 主循环 */
    while (1) {
        atcp_tick(inst);  /* 驱动协议栈，建议 10-15ms 调用一次 */

        if (atcp_get_state(inst) == ATCP_STATE_CONNECTED) {
            /* 发送数据 */
            const uint8_t msg[] = "Hello MCU!";
            atcp_send(inst, msg, sizeof(msg));

            /* 接收数据 */
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

### 3.2 被动端（MCU 侧）

被动端使用 `atcp_accept()` 替代 `atcp_connect()`：

```c
atcp_instance_t *inst = atcp_create(NULL, &platform);
atcp_accept(inst);  /* 监听握手请求 */

while (1) {
    atcp_tick(inst);
    /* ... 与主动端相同的收发逻辑 ... */
}
```

## 4. API 参考

### 4.1 生命周期管理

| 函数 | 说明 |
|------|------|
| `atcp_create(config, platform)` | 创建实例。`config` 传 NULL 使用默认值，`platform` 必须非 NULL |
| `atcp_destroy(inst)` | 销毁实例，释放所有资源 |

### 4.2 连接管理

| 函数 | 说明 |
|------|------|
| `atcp_connect(inst)` | 发起连接（主动端），非阻塞 |
| `atcp_accept(inst)` | 等待连接（被动端），非阻塞 |
| `atcp_disconnect(inst)` | 断开连接，重置所有内部状态 |

### 4.3 数据传输

| 函数 | 说明 |
|------|------|
| `atcp_send(inst, data, len)` | 发送数据，非阻塞，数据入队后由 `atcp_tick()` 逐步发送 |
| `atcp_recv(inst, buf, buf_len, &received)` | 从接收缓冲区读取已解码数据 |

**注意事项：**
- `atcp_send()` 单次最大发送 4096 字节
- 上次 `atcp_send()` 未完成时再次调用会返回 `ATCP_ERR_BUSY`
- `atcp_recv()` 无可读数据时返回 `ATCP_ERR_BUFFER_EMPTY`

### 4.4 主循环

| 函数 | 说明 |
|------|------|
| `atcp_tick(inst)` | 驱动协议栈的核心函数，处理音频 I/O、帧同步、解调、ACK/重传、心跳 |

`atcp_tick()` 建议以 10~15ms 周期调用，与 OFDM 符号时长（~12.3ms @ 44.1kHz）匹配。

### 4.5 状态查询

| 函数 | 说明 |
|------|------|
| `atcp_get_state(inst)` | 获取当前连接状态 |
| `atcp_get_stats(inst)` | 获取统计信息（BER、SNR、吞吐量、帧计数等） |
| `atcp_get_audio_buf_size(inst)` | 获取音频缓冲区所需大小（float 个数），平台层需保证缓冲区不小于此值 |

## 5. 配置参数

通过 `atcp_config_t` 结构体自定义协议行为：

```c
atcp_config_t cfg = atcp_config_default();  /* 获取默认配置 */
cfg.qam_order = 64;                     /* 改为 64-QAM */
cfg.rs_nsym = 32;                       /* 减少 RS 冗余，提高速率 */
atcp_instance_t *inst = atcp_create(&cfg, &platform);
```

### 5.1 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `n_fft` | 512 | FFT 点数 |
| `cp_len` | 32 | 循环前缀长度（采样数）|
| `sub_low` | 2 | 最低可用子载波索引 |
| `sub_high` | 200 | 最高可用子载波索引 |
| `n_train` | 2 | 训练符号数 |
| `train_seed` | 42 | 训练序列 PRNG 种子 |
| `lead_in` | 1024 | 前导静音（采样数），约 23ms @ 44.1kHz |
| `lead_out` | 1024 | 尾导静音（采样数）|
| `amplitude` | 0.85 | 发射幅度上限 |
| `rs_nsym` | 48 | RS 冗余符号数（纠错能力 = nsym/2 字节/块）|
| `qam_order` | 16 | QAM 阶数（4/16/64/256）|
| `sample_rate` | 44100 | 采样率（Hz）|
| `window_size` | 4 | ARQ 滑动窗口大小 |
| `pilot_interval` | 10 | 导频插入间隔 |
| `ack_timeout_ms` | 500 | ACK 超时重传门限（ms）|
| `max_ack_miss` | 3 | 连续 ACK 超时达此值后缩小窗口 |
| `ack_repeat` | 2 | ACK 帧重复发送次数 |
| `heartbeatcp_interval_ms` | 3000 | 心跳发送间隔（ms）|
| `heartbeatcp_timeout_ms` | 10000 | 心跳超时断开门限（ms）|

### 5.2 QAM 阶数选择

| QAM | bits/符号 | 所需 SNR | 适用场景 |
|-----|-----------|----------|----------|
| 4 (QPSK) | 2 | ~10 dB | 恶劣信道、握手、控制命令 |
| 16 | 4 | ~17 dB | **默认**，平衡速率与可靠性 |
| 64 | 6 | ~23 dB | 良好信道，文件传输 |
| 256 | 8 | ~30 dB | 优质短线缆，最大吞吐 |

## 6. 平台回调接口

使用 ATCP 必须实现 `atcp_platform_t` 中的三个回调：

### 6.1 audio_write

```c
int (*audio_write)(const float *samples, int n_samples, int n_channels, void *user_data);
```

将音频采样写入输出设备（Speaker / DAC）。

- `samples`：交错格式浮点数据（双声道时为 `[L0, R0, L1, R1, ...]`）
- `n_samples`：**每通道**采样数
- `n_channels`：通道数，下行固定为 2（差分双声道）
- 返回值：实际写入的每通道采样数，`< 0` 表示错误

### 6.2 audio_read

```c
int (*audio_read)(float *samples, int n_samples, int n_channels, void *user_data);
```

从输入设备（Mic / ADC）读取音频采样。

- `n_samples`：请求的每通道采样数
- `n_channels`：通道数
- 返回值：实际读取的每通道采样数，`< 0` 表示错误

### 6.3 get_time_ms

```c
uint32_t (*get_time_ms)(void *user_data);
```

返回当前系统时间（毫秒），用于心跳检测和 ARQ 超时计算。

**平台实现示例：**

| 平台 | audio_write / audio_read | get_time_ms |
|------|--------------------------|-------------|
| Windows | WASAPI / PortAudio | `GetTickCount()` |
| Linux | ALSA / PulseAudio | `clock_gettime()` |
| STM32 | DMA + DAC/ADC | `HAL_GetTick()` |
| ESP32 | I2S Driver | `esp_timer_get_time() / 1000` |

## 7. 状态码与错误处理

| 状态码 | 值 | 说明 |
|--------|-----|------|
| `ATCP_OK` | 0 | 成功 |
| `ATCP_ERR_INVALID_PARAM` | -1 | 参数无效 |
| `ATCP_ERR_NO_MEMORY` | -2 | 内存分配失败 |
| `ATCP_ERR_TIMEOUT` | -3 | 操作超时 |
| `ATCP_ERR_CRC_FAIL` | -4 | CRC 校验失败 |
| `ATCP_ERR_RS_DECODE_FAIL` | -5 | RS 解码失败（错误超过纠错能力）|
| `ATCP_ERR_SYNC_FAIL` | -6 | 帧同步失败 |
| `ATCP_ERR_HANDSHAKE_FAIL` | -7 | 握手失败 |
| `ATCP_ERR_DISCONNECTED` | -8 | 连接已断开 |
| `ATCP_ERR_BUSY` | -9 | 忙（上次操作未完成）|
| `ATCP_ERR_BUFFER_FULL` | -10 | 发送缓冲区满 |
| `ATCP_ERR_BUFFER_EMPTY` | -11 | 接收缓冲区空 |
| `ATCP_ERR_NOT_CONNECTED` | -12 | 未连接状态下调用收发 |
| `ATCP_ERR_QUALITY_LOW` | -13 | 链路质量过低 |

## 8. 连接状态机

```
  atcp_create()
      │
      ▼
   ┌──────┐  atcp_connect()   ┌────────────┐
   │ IDLE │───────────────►│ CONNECTING  │
   │      │  atcp_accept()    │ (握手中)    │
   └──────┘                 └─────┬──────┘
                                  │ 握手成功
                                  ▼
                           ┌────────────┐
                           │ CONNECTED  │◄──── 正常通信
                           └─────┬──────┘
                                 │ 超时/主动断开
                                 ▼
                         ┌───────────────┐
                         │ DISCONNECTED  │
                         └───────────────┘
```

## 9. 高级用法：直接使用子层 API

除了统一的高层 API，ATCP 也暴露了各协议层的接口，供高级用户按需组合使用。

通过包含对应的层级头文件即可访问：

```c
#include <atcp/coding.h>      /* RS 编解码、CRC32 */
#include <atcp/modulation.h>  /* QAM、OFDM、帧同步、信道估计 */
#include <atcp/physical.h>    /* 差分编解码、AGC、CFO/SFO */
#include <atcp/link.h>        /* 帧结构、握手、ARQ、ACK、心跳 */
```

**示例 —— 单独使用 RS 编解码：**

```c
#include <atcp/coding.h>

atcp_rs_t rs;
atcp_rs_init(&rs, 48);  /* 48 冗余符号，纠错 24 字节/块 */

/* 编码 */
uint8_t coded[4096];
int coded_len = 0;
atcp_rs_encode_blocks(&rs, data, data_len, coded, &coded_len);

/* 解码（自动纠错） */
uint8_t decoded[4096];
int decoded_len = 0;
atcp_rs_decode_blocks(&rs, coded, coded_len, decoded, &decoded_len);
```

**示例 —— 单独使用 QAM + OFDM：**

```c
#include <atcp/modulation.h>
#include <atcp/config.h>

atcp_config_t cfg = atcp_config_default();

/* QAM 调制 */
atcp_complex_t symbols[256];
int n_sym = 0;
atcp_qam_modulate(bits, n_bits, 16, symbols, &n_sym);

/* OFDM 调制 */
float time_samples[1024];
int time_len = 0;
atcp_ofdm_modulate(symbols, n_subs, &cfg, time_samples, &time_len);
```

## 10. 吞吐量参考

以默认参数（44.1kHz 采样率，N_FFT=512）估算：

| 模式 | QAM | 净吞吐量 |
|------|-----|----------|
| 独立帧 | QPSK | ~13 kbps |
| 独立帧 | 16-QAM | ~26 kbps |
| 连续流 | 16-QAM | ~47 kbps |
| 连续流 | 64-QAM | ~71 kbps |
| 连续流 | 256-QAM | ~95 kbps |

**典型传输时间（连续流 + 16-QAM）：**

| 数据量 | 时间 |
|--------|------|
| 1 KB | ~0.2s |
| 10 KB | ~1.7s |
| 100 KB | ~17s |
| 1 MB | ~3 min |

## 11. 常见问题

**Q: 支持哪些平台？**
A: 任何支持 C99 编译器的平台。已在 Windows (MSVC)、Linux (GCC)、STM32 (ARM GCC)、ESP32 (Xtensa GCC) 上验证构建。

**Q: 是否需要外部依赖？**
A: 不需要。ATCP 零外部依赖，仅使用 C 标准库和 `<math.h>`。Unix/Linux 需链接 `-lm`。

**Q: 如何提高传输速率？**
A: 提高 QAM 阶数（需信道 SNR 支持）、提高采样率（需双方声卡支持）、或减少 RS 冗余（`rs_nsym`，会降低纠错能力）。

**Q: 握手阶段为什么总是用 QPSK？**
A: 握手阶段尚未协商信道参数，使用最低阶调制（QPSK）确保在任何信道条件下都能成功建立连接。

**Q: `atcp_tick()` 调用频率不规律会怎样？**
A: 可能导致音频缓冲区溢出/欠载。建议使用定时器确保 10~15ms 的稳定调用周期。

**Q: 如何处理连接断开？**
A: 监测 `atcp_get_state()` 返回 `ATCP_STATE_DISCONNECTED`，然后重新调用 `atcp_connect()` / `atcp_accept()` 建立连接。
