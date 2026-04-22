# AudioThief 开发者指南

## 1. 项目概览

AudioThief 是一个五层协议栈的纯 C99 类库，实现基于音频接口的隐蔽数据通信。本文档面向希望理解内部实现、修改或扩展该库的开发者。

## 2. 目录结构

```
AudioThief/
├── CMakeLists.txt              # 顶层构建配置
├── AudioThief.md               # 系统设计文档（协议规格）
└── lib/
    ├── CMakeLists.txt           # 库构建配置
    ├── USER_GUIDE.md            # 用户指南
    ├── DEVELOPER_GUIDE.md       # 本文档
    ├── include/audiothief/      # 公开头文件
    │   ├── audiothief.h         #   统一 API 入口
    │   ├── types.h              #   类型定义、状态码、DLL 导出宏
    │   ├── config.h             #   配置结构与默认值
    │   ├── platform.h           #   平台抽象回调接口
    │   ├── physical.h           #   物理层聚合头文件
    │   ├── modulation.h         #   调制层聚合头文件
    │   ├── coding.h             #   编码层聚合头文件
    │   └── link.h               #   链路层聚合头文件
    ├── src/
    │   ├── audiothief.c         # API 实现（集成层）
    │   ├── common/              # 通用工具
    │   │   ├── fft.c/h          #   FFT / IFFT
    │   │   ├── math_utils.c/h   #   复数运算、信号工具
    │   │   ├── ring_buffer.c/h  #   环形缓冲区
    │   │   └── prng.c/h         #   伪随机数生成器
    │   ├── physical/            # 物理层
    │   │   ├── diff_signal.c/h  #   差分编解码
    │   │   ├── agc.c/h          #   自动增益控制
    │   │   ├── cfo.c/h          #   载波频率偏移补偿
    │   │   └── sfo.c/h          #   采样频率偏移补偿
    │   ├── modulation/          # 调制层
    │   │   ├── ofdm.c/h         #   OFDM 调制/解调
    │   │   ├── qam.c/h          #   M-QAM 调制/解调
    │   │   ├── training.c/h     #   训练序列生成
    │   │   ├── frame_sync.c/h   #   两级帧同步
    │   │   └── channel_est.c/h  #   信道估计与均衡
    │   ├── coding/              # 编码层
    │   │   ├── reed_solomon.c/h #   RS 前向纠错
    │   │   ├── crc32.c/h        #   CRC32 校验
    │   │   └── gf256.c/h        #   GF(256) 有限域算术
    │   └── link/                # 链路层
    │       ├── frame.c/h        #   帧结构序列化
    │       ├── handshake.c/h    #   四阶段握手协商
    │       ├── arq.c/h          #   滑动窗口 ARQ
    │       ├── ack.c/h          #   ACK 编解码与去重
    │       └── heartbeat.c/h    #   心跳保活
    └── tests/
        ├── CMakeLists.txt       # 测试构建配置
        ├── test_fft.c           # FFT 单元测试
        ├── test_qam.c           # QAM 单元测试
        ├── test_ofdm.c          # OFDM 单元测试
        ├── test_rs.c            # RS 编解码测试
        ├── test_crc.c           # CRC32 测试
        ├── test_frame_sync.c    # 帧同步测试
        ├── test_arq.c           # ARQ 链路层测试
        └── test_loopback.c      # 端到端集成测试
```

## 3. 架构设计

### 3.1 五层协议栈

```
┌─────────────────────────────────────────────┐
│            应用层 (Application)              │  用户代码，不在库内
├─────────────────────────────────────────────┤
│              API 集成层                      │  audiothief.c
│   atcp_create / atcp_tick / atcp_send / atcp_recv   │  封装所有子层为统一接口
├─────────────────────────────────────────────┤
│            链路层 (Link)                     │  frame, handshake, arq, ack, heartbeat
│   帧封装 → 握手协商 → ARQ可靠传输 → 心跳    │
├─────────────────────────────────────────────┤
│            编码层 (Coding)                   │  reed_solomon, crc32, gf256
│   RS FEC 编解码 → CRC32 完整性校验          │
├─────────────────────────────────────────────┤
│            调制层 (Modulation)               │  ofdm, qam, training, frame_sync, channel_est
│   QAM映射 → OFDM多载波 → 帧同步 → 信道均衡 │
├─────────────────────────────────────────────┤
│            物理层 (Physical)                 │  diff_signal, agc, cfo, sfo
│   差分编码 → AGC → CFO/SFO补偿             │
├─────────────────────────────────────────────┤
│         通用工具 (Common)                    │  fft, math_utils, ring_buffer, prng
└─────────────────────────────────────────────┘
```

### 3.2 层间依赖规则

- **严格上层依赖下层**：链路层可调用编码层，编码层可调用调制层，但反向禁止
- **Common 为最底层**：所有层均可调用 common 中的工具函数
- **API 层为唯一入口**：`audiothief.c` 是唯一串联所有层的文件

### 3.3 核心实例结构

`atcp_instance`（定义在 `audiothief.c` 中，对外不透明）封装了全部运行时状态：

```c
struct atcp_instance {
    atcp_config_t config;           // 协议配置
    atcp_platform_t platform;       // 平台回调

    atcp_state_t state;             // 连接状态机
    atcp_bool_t is_initiator;       // 是否为主动连接方

    // 各层上下文
    atcp_rs_t rs;                   // RS 编解码器
    atcp_frame_sync_t frame_sync;   // 帧同步器
    atcp_agc_t agc;                 // 自动增益控制
    atcp_handshake_t handshake;     // 握手状态机
    atcp_heartbeatcp_t heartbeat;     // 心跳管理
    atcp_arq_sender_t arq_sender;   // ARQ 发送端
    atcp_arq_receiver_t arq_receiver; // ARQ 接收端
    atcp_ack_dedup_t ack_dedup;     // ACK 去重器

    // 信道状态
    atcp_complex_t channel_h[256];  // 信道频率响应
    atcp_bool_t channel_valid;

    // 收发缓冲区
    uint8_t tx_data_buf[4096];    // 发送缓冲
    uint8_t rx_data_buf[4096];    // 接收缓冲
    float audio_in_buf[2048];     // 音频输入缓冲
    float audio_out_buf[2048];    // 音频输出缓冲

    atcp_stats_t stats;             // 运行统计
};
```

## 4. 数据流

### 4.1 发送端处理链

```
用户数据
  │
  ▼
atcp_send()  ─────  数据入队到 tx_data_buf
  │
  ▼  (atcp_tick 驱动)
帧构建  ─────────  atcp_frame_build_payload() 添加帧头(10B) + CRC32
  │
  ▼
RS 编码  ─────────  atcp_rs_encode_blocks() 分块编码, 每块 255-nsym → 255 字节
  │
  ▼
比特展开  ────────  每字节 MSB-first 拆为 8 bit
  │
  ▼
QAM 调制  ────────  atcp_qam_modulate() Gray码映射, 功率归一化
  │
  ▼
OFDM 调制  ───────  atcp_ofdm_modulate() 子载波映射 → Hermitian对称 → IFFT → 加CP
  │
  ▼
差分编码  ────────  atcp_diff_encode_interleaved() L=+signal, R=-signal
  │
  ▼
平台输出  ────────  platform.audio_write() 写入声卡
```

### 4.2 接收端处理链

```
平台输入  ────────  platform.audio_read() 从麦克风读取
  │
  ▼
AGC  ─────────────  atcp_agc_process() 幅度归一化
  │
  ▼
帧同步  ──────────  atcp_frame_sync_feed_batch()
  │                 ├─ 粗同步: CP 延迟自相关 |P(d)|²/R(d)
  │                 └─ 细同步: 训练序列互相关精确定位
  ▼
信道估计  ────────  训练符号 → FFT → atcp_channel_estimate() 得到 H(k)
  │
  ▼
OFDM 解调  ───────  atcp_ofdm_demodulate() 去CP → FFT → 提取子载波
  │
  ▼
ZF 均衡  ─────────  atcp_channel_equalize() Y(k)/H(k)
  │
  ▼
QAM 解调  ────────  atcp_qam_demodulate() 硬判决
  │
  ▼
比特打包  ────────  每 8 bit MSB-first 合并为字节
  │
  ▼
RS 解码  ─────────  atcp_rs_decode_blocks() 纠错 + 恢复
  │
  ▼
帧解析  ──────────  atcp_frame_parse_payload() 提取帧头和用户数据
  │
  ▼
协议处理  ────────  inst_process_received_frame() 按帧类型分发
  │
  ├─ DATA  ──→  ARQ 接收缓冲 → 生成 ACK → 按序交付到 rx_data_buf
  ├─ ACK   ──→  去重 → 更新 ARQ 发送窗口
  ├─ HS    ──→  握手状态机推进
  └─ HB    ──→  心跳时间戳更新
```

### 4.3 atcp_tick() 处理流程

`atcp_tick()` 是协议栈的核心驱动函数，每次调用执行 6 个阶段：

1. **音频输入**：从平台读取一个 OFDM 符号长度的采样
2. **AGC + 帧同步**：信号归一化，然后送入帧同步器
3. **信道估计**：检测到帧后，从训练符号计算信道响应
4. **解调解码**：数据 OFDM 符号 → 均衡 → QAM 解调 → RS 解码 → 帧解析
5. **链路层协议**：心跳检查、ARQ 超时重传、新数据发送
6. **音频输出**：将编码后的音频采样写入平台输出

## 5. 各模块详解

### 5.1 Common — 通用工具

#### FFT (`fft.c/h`)

- Cooley-Tukey 基-2 原地 FFT/IFFT
- 要求 N 为 2 的幂次（128/256/512/1024）
- IFFT 包含 1/N 归一化

#### 复数运算 (`math_utils.c/h`)

提供完整的复数算术：乘法、共轭、加减、模、相角、极坐标转换，以及信号级工具（RMS 功率、归一化、dB 转换）。

#### 环形缓冲区 (`ring_buffer.c/h`)

`float` 类型的无锁环形缓冲区，用于音频流的读写缓冲。支持 `write`、`read`、`peek`（不消费的预览读取）。

#### PRNG (`prng.c/h`)

LCG 伪随机数生成器，用于生成收发双方一致的 BPSK 训练序列（+1.0 / -1.0）。

### 5.2 Physical — 物理层

#### 差分编码 (`diff_signal.c/h`)

下行信道使用差分双声道：`L = +signal, R = -signal`。接收端 `(L - R) / 2 = signal`，抑制共模噪声。

提供四种变体：
- `atcp_diff_encode` / `atcp_diff_decode`：分离左右声道缓冲区
- `atcp_diff_encode_interleaved` / `atcp_diff_decode_interleaved`：交错格式 `[L0,R0,L1,R1,...]`

#### AGC (`agc.c/h`)

自动增益控制，追踪信号峰值并动态调整增益：
- `attack_coeff`（0.01）：快速响应上升沿
- `release_coeff`（0.001）：缓慢释放避免过度压缩
- 目标幅度默认 0.5，确保 ADC 不饱和

#### CFO 补偿 (`cfo.c/h`)

载波频率偏移估计与补偿：
- **分数 CFO**：Moose 算法，利用 CP 的重复结构
- **整数 CFO**：相邻训练符号的频域相位差
- **时域补偿**：`r(n) × cos(2π·Δf·n/fs)`

#### SFO 补偿 (`sfo.c/h`)

采样频率偏移估计与补偿：
- 通过多个导频符号的参考子载波相位漂移趋势估计 SFO（ppm）
- 频域线性相位补偿

### 5.3 Modulation — 调制层

#### OFDM (`ofdm.c/h`)

正交频分复用调制/解调：
- **调制**：子载波映射到 `[sub_low, sub_high]` → Hermitian 对称 → IFFT → 加 CP
- **解调**：去 CP → 实数转复数 → FFT → 提取数据子载波
- DC (bin 0) 和 Nyquist (bin N/2) 强制置零

关键参数计算：
- 子载波数：`n_subs = sub_high - sub_low + 1 = 199`（默认）
- 符号采样数：`n_fft + cp_len = 544`（默认）
- 符号时长：`544 / 44100 ≈ 12.3ms`

#### QAM (`qam.c/h`)

M-QAM 星座映射，支持 4/16/64/256 阶：
- Gray 码 I/Q 双轴映射
- 归一化因子 `norm = sqrt(2(M-1)/3)`，确保平均符号功率 = 1
- 解调使用硬判决（最近邻搜索）

#### 训练序列 (`training.c/h`)

- `atcp_training_generate()`：生成 BPSK 频域训练符号（实部 ±1，虚部 0）
- `atcp_training_generate_ofdm()`：生成完整时域训练符号（含 OFDM 调制和 CP）
- 使用固定种子 PRNG，保证收发双方序列一致

#### 帧同步 (`frame_sync.c/h`)

两级帧同步策略：

**第一级 — CP 延迟自相关（粗同步）：**
```
P(d) = Σ r*(d+k) · r(d+k+N_FFT), k = 0..CP_LEN-1
```
- 滑动窗口增量更新，每采样 O(1) 计算量
- 当 `|P(d)|² / R(d)` 超过门限（默认 0.8）时触发

**第二级 — 训练序列互相关（细同步）：**
- 在粗同步候选位置 ±CP_LEN 范围内做局部互相关
- 精确定位帧头

内部状态通过 `atcp_frame_sync_t` 结构维护，支持 `feed`（逐采样）和 `feed_batch`（批量）两种输入模式。

#### 信道估计 (`channel_est.c/h`)

- **信道估计**：`H(k) = Y_train(k) / X_train(k)`
- **多符号平均**：`atcp_channel_estimate_avg()` 对多个训练符号取平均
- **ZF 均衡**：`X̂(k) = Y(k) / H(k)`
- **导频更新**：`atcp_channel_update()` 使用遗忘因子 α 平滑更新

### 5.4 Coding — 编码层

#### Reed-Solomon (`reed_solomon.c/h`)

RS(255, 255-nsym) 编解码：
- **编码**：利用 GF(256) 生成多项式进行系统编码
- **解码**：Berlekamp-Massey + Chien Search + Forney 算法
- **分块**：`atcp_rs_encode_blocks()` / `atcp_rs_decode_blocks()` 自动将长数据按 `255 - nsym` 字节分块
- 默认 nsym=48，单块纠错能力 24 字节

#### CRC32 (`crc32.c/h`)

- 查找表加速的 CRC32 计算
- 支持一次性 `atcp_crc32()` 和增量 `atcp_crc32_update()` 两种模式
- `atcp_crc32_init()` 需在首次使用前调用一次

#### GF(256) (`gf256.c/h`)

有限域 GF(256) 算术，RS 编解码的数学基础：
- 基本运算：乘法、除法、幂运算、求逆
- 多项式运算：乘法、求值、除法取余
- 使用查找表加速

### 5.5 Link — 链路层

#### 帧结构 (`frame.c/h`)

帧头固定 10 字节（大端序）：

```
Offset  Size  Field
0       1     type         帧类型 (DATA/ACK/HANDSHAKE/HEARTBEAT/EOS)
1       2     seq          序列号
3       2     payload_len  有效载荷长度
5       1     flags        标志位 (STREAM_MODE/LAST_BLOCK/NACK_ONLY)
6       4     crc          CRC32 校验
```

#### 握手协商 (`handshake.c/h`)

四阶段握手状态机：

```
IDLE → PHASE1 → PHASE2 → PHASE3(测试) → PHASE4 → CONNECTED
                                  │
                                  └─ 质量不达标 → 降级QAM → 回到 PHASE2
```

- Phase 1：QPSK 低速交换能力参数
- Phase 2：切换到协商参数验证
- Phase 3：链路质量测试（BER < 10⁻⁴ 优，< 10⁻² 良，否则降级）
- Phase 4：最终确认

支持自动降级：`atcp_handshake_downgrade()` 将 QAM 阶数降一级。

#### ARQ (`arq.c/h`)

滑动窗口 + 选择性重传：

**发送端 (`atcp_arq_sender_t`)：**
- `submit()`：提交数据块到发送窗口
- `get_next()`：取出下一个待发送块
- `mark_sent()`：记录发送时间戳
- `process_ack()`：处理 ACK，滑动窗口
- `check_timeout()`：检查超时，返回需要重传的块列表

**接收端 (`atcp_arq_receiver_t`)：**
- `process()`：接收数据块（支持乱序）
- `generate_bitmap()`：生成 8-bit ACK 位图
- `has_complete()`：检查是否有连续完整数据
- `get_ordered()`：按序取出数据

**窗口缩放机制：**
- 连续 `max_ack_miss` 次 ACK 超时 → 窗口缩小为 `window_size / 2`（最小 1）
- 收到有效 ACK → 窗口立即恢复为 `max_window_size`

#### ACK (`ack.c/h`)

紧凑 ACK 格式（仅 3 字节）：
- `base_seq`（2B）+ `bitmap`（1B）
- bitmap 的 bit[i] 表示 base_seq + i 是否已收到
- 单个 ACK 可确认最多 8 个连续块

包含去重器 `atcp_ack_dedup_t`，防止重复 ACK 导致窗口异常滑动。

#### 心跳 (`heartbeat.c/h`)

连接保活机制：
- `need_send()`：距上次发送超过 `interval_ms` 时返回 true
- `is_timeout()`：距上次接收超过 `timeout_ms` 时返回 true
- 任何有效帧接收都更新 `last_rx_time_ms`

## 6. 构建系统

### 6.1 CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_SHARED_LIBS` | ON | ON=动态库，OFF=静态库 |
| `BUILD_TESTS` | ON | 是否编译测试用例 |

### 6.2 源文件收集

```cmake
file(GLOB_RECURSE ATCP_SOURCES "src/*.c")  # 自动收集所有 .c 文件
```

添加新源文件时无需修改 CMakeLists.txt，只需放入对应 `src/` 子目录即可。

### 6.3 DLL 导出

- 构建动态库时自动定义 `ATCP_BUILDING_DLL` 宏
- `ATCP_API` 宏在 Windows 上展开为 `__declspec(dllexport/import)`
- 静态库用户应定义 `ATCP_STATIC` 消除导出装饰

### 6.4 平台差异

- **Unix/Linux**：需链接 `libm`（`-lm`），测试中已自动处理
- **Windows**：MSVC 自带数学库，无需额外链接

## 7. 测试

### 7.1 测试列表

| 测试文件 | 覆盖模块 | 测试内容 |
|----------|----------|----------|
| `test_fft` | FFT | 往返精度、余弦信号检测、多 N 值、参数校验 |
| `test_qam` | QAM | 全星座点往返(4/16/64/256)、功率归一化、无效参数 |
| `test_ofdm` | OFDM | 无噪声往返、输出长度、CP 正确性 |
| `test_rs` | RS | 无错解码、可纠错、超限检测、分块编解码、边界用例 |
| `test_crc` | CRC32 | 已知向量、空数据、增量计算、全字节覆盖 |
| `test_frame_sync` | 帧同步 | 多偏移检测、噪声免疫(无误报)、重置后重检测 |
| `test_arq` | ARQ+ACK | 正常流、选择性重传、超时缩窗、窗口恢复、按序交付、ACK 编解码/去重 |
| `test_loopback` | 全栈 | 端到端环回：RS→QAM→OFDM→差分→信道估计→解调→RS→数据比对 |

### 7.2 编译与运行

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build

# 运行单个测试
./build/lib/tests/test_fft

# 运行所有测试 (需顶层 CMakeLists.txt 添加 enable_testing())
cd build && ctest --output-on-failure
```

### 7.3 测试框架

所有测试共享统一的宏框架（目前在每个文件中重复定义）：

```c
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d): %s\n", __FILE__, __LINE__, msg); failures++; } \
    else { passes++; } \
} while(0)

#define TEST_SUMMARY() do { \
    printf("\n=== %d passed, %d failed ===\n", passes, failures); \
    return failures > 0 ? 1 : 0; \
} while(0)
```

### 7.4 添加新测试

1. 在 `lib/tests/` 下创建 `test_xxx.c`
2. 在 `lib/tests/CMakeLists.txt` 的 `TEST_SOURCES` 列表中添加 `test_xxx`
3. 编译后自动生成可执行文件并注册到 CTest

## 8. 开发规范

### 8.1 命名约定

| 类别 | 规则 | 示例 |
|------|------|------|
| 公开函数 | `atcp_` 前缀 + 模块名 + 动词 | `atcp_qam_modulate()` |
| 公开类型 | `atcp_` 前缀 + `_t` 后缀 | `atcp_config_t` |
| 公开枚举值 | `ATCP_` 全大写 | `ATCP_OK`, `ATCP_STATE_CONNECTED` |
| 公开宏 | `ATCP_` 全大写 | `ATCP_ARQ_MAX_WINDOW` |
| 内部函数 | `模块名_` 前缀或 `static` | `gf256_mul()` |
| 文件名 | 小写 + 下划线 | `frame_sync.c` |

### 8.2 代码风格

- **标准**：C99（不使用 C11+ 特性）
- **缩进**：4 空格
- **花括号**：Allman 或 K&R 均可（保持文件内一致）
- **变量声明**：C89 兼容风格（块首声明），循环变量例外
- **注释语言**：中文或英文均可，保持模块内一致

### 8.3 错误处理

- 所有可失败的函数返回 `atcp_status_t`
- 成功返回 `ATCP_OK`（0），失败返回负值错误码
- 入口处先做参数校验，失败立即返回
- 不使用异常（纯 C）

### 8.4 内存管理

- 库内部使用 `malloc` / `free`
- `atcp_create()` 分配实例，`atcp_destroy()` 释放
- `atcp_frame_sync_init()` 内部分配缓冲区，`atcp_frame_sync_free()` 释放
- 其他模块使用栈上固定大小数组，避免动态分配

## 9. 扩展指南

### 9.1 添加新的 QAM 阶数

1. 在 `qam.c` 的 `atcp_qam_bits_per_symbol()` 中添加新阶数映射
2. 在 `atcp_qam_modulate()` / `atcp_qam_demodulate()` 中添加星座映射逻辑
3. 更新 `atcp_qam_norm_factor()` 的归一化因子
4. 在 `test_qam.c` 中添加新阶数的往返测试

### 9.2 添加新的帧类型

1. 在 `types.h` 的 `atcp_frame_type_t` 中添加新类型
2. 在 `audiothief.c` 的 `inst_process_received_frame()` 中添加 `case` 分支
3. 编写对应的帧构建和解析逻辑

### 9.3 替换 FFT 实现

FFT 接口隔离在 `fft.h` 中，仅暴露两个函数：

```c
atcp_status_t atcp_fft(atcp_complex_t *buf, int n);
atcp_status_t atcp_ifft(atcp_complex_t *buf, int n);
```

替换时只需提供新的 `fft.c` 实现（如 CMSIS-DSP 的 `arm_cfft_f32`），保持接口不变即可。

### 9.4 移植到新平台

1. 实现 `atcp_platform_t` 的三个回调函数
2. 如果目标平台不支持 `malloc`，需修改 `atcp_create()` 和 `atcp_frame_sync_init()` 为静态分配
3. 确保 `<math.h>` 可用（`sinf`, `cosf`, `sqrtf`, `atan2f`, `fabsf`）

## 10. 已知限制

1. **单帧缓冲**：`atcp_tick()` 每次只处理一个 OFDM 符号长度的输入，连续流模式下可能需要更高频率的调用
2. **固定缓冲区大小**：发送/接收缓冲区固定 4096 字节，大数据需分批 `atcp_send()`
3. **音频输出缓冲区**：`audio_out_buf` 固定 2048 float，限制了单帧可输出的最大采样数
4. **内部头文件路径**：子层头文件使用 `../../include` 相对路径引用公开头文件，影响 `cmake install` 场景
5. **单线程设计**：所有 API 必须在同一线程调用，不支持并发访问
