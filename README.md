# ATCP — Audio Transmission Control Protocol

[![Language](https://img.shields.io/badge/language-C99-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake%203.10%2B-green.svg)]()
[![License](https://img.shields.io/badge/license-MIT-lightgrey.svg)](LICENSE)

[中文](README.md) | **English**

ATCP is a **pure C99, zero-dependency** audio-channel data communication protocol stack library. It establishes a bidirectional data link between a PC and an MCU over a standard 3.5 mm audio interface (Speaker Out / Mic In), enabling covert communication that bypasses the network layer.

```
┌─────────────────┐      3.5mm Audio Cable      ┌─────────────────┐
│    Host (PC)     │ ◄─────────────────────────► │   Device (MCU)   │
│                  │   Speaker Out ──► ADC In    │                  │
│                  │   Mic In     ◄── DAC Out    │                  │
└─────────────────┘                              └─────────────────┘
```

## Features

- **Zero Dependencies** — Pure C99; embeddable on any platform (bare-metal / RTOS / desktop)
- **Bidirectional** — Downlink: differential stereo OFDM + adaptive QAM (QPSK–256-QAM); Uplink: mono fixed QPSK
- **Dual Reliability** — Reed-Solomon FEC + sliding-window ARQ with selective retransmission
- **High Throughput** — ~90% effective data ratio in streaming mode; up to ~95 kbps downlink
- **Adaptive Handshake** — 4-phase negotiation of sample rate, QAM order, etc., with quality assessment and automatic fallback
- **Non-blocking API** — Event-driven design; all operations are asynchronous
- **Cross-platform** — Windows / Linux / STM32 / ESP32 and more

## Protocol Architecture

```
┌───────────────────────────────┐
│     Application Layer         │  User code (file transfer, command control, encryption, etc.)
├───────────────────────────────┤
│     Link Layer                │  Handshake, heartbeat, framing, sliding-window ARQ
├───────────────────────────────┤
│     Coding Layer              │  RS FEC encoding/decoding, CRC32 checksum
├───────────────────────────────┤
│     Modulation Layer          │  OFDM + M-QAM, frame sync, channel estimation
├───────────────────────────────┤
│     Physical Layer            │  Differential signaling, AGC, CFO/SFO compensation
└───────────────────────────────┘
```

## Building

### Prerequisites

- CMake >= 3.10
- C99-compatible compiler (MSVC / GCC / Clang)

### Compilation

```bash
# Shared library (default)
cmake -B build
cmake --build build --config Release

# Static library (recommended for embedded)
cmake -B build -DBUILD_SHARED_LIBS=OFF
cmake --build build --config Release
```

| CMake Option | Default | Description |
|---|---|---|
| `BUILD_SHARED_LIBS` | `ON` | `ON` = shared library, `OFF` = static library |
| `BUILD_TESTS` | `ON` | Whether to build unit tests |

> Static library users should define `ATCP_STATIC` in their compile options to suppress DLL export decorations.

## Quick Start

```c
#include <atcp/atcp.h>

/* 1. Implement platform callbacks */
int my_audio_write(const float *samples, int n, int ch, void *ud) { /* write to speaker */ return n; }
int my_audio_read(float *samples, int n, int ch, void *ud)        { /* read from mic */   return n; }
uint32_t my_get_time_ms(void *ud)                                  { return get_system_time_ms(); }

int main(void) {
    /* 2. Configure platform callbacks */
    atcp_platform_t platform = {0};
    platform.audio_write = my_audio_write;
    platform.audio_read  = my_audio_read;
    platform.get_time_ms = my_get_time_ms;

    /* 3. Create instance (NULL = use default config) */
    atcp_instance_t *inst = atcp_create(NULL, &platform);

    /* 4. Initiate connection */
    atcp_connect(inst);

    /* 5. Main loop (recommended 10-15 ms interval) */
    while (1) {
        atcp_tick(inst);

        if (atcp_get_state(inst) == ATCP_STATE_CONNECTED) {
            const uint8_t msg[] = "Hello MCU!";
            atcp_send(inst, msg, sizeof(msg));

            uint8_t buf[256];
            size_t received = 0;
            if (atcp_recv(inst, buf, sizeof(buf), &received) == ATCP_OK) {
                /* process received data */
            }
        }
    }

    atcp_destroy(inst);
    return 0;
}
```

## API Overview

| Function | Description |
|---|---|
| `atcp_create(config, platform)` | Create an instance; pass `NULL` for `config` to use defaults |
| `atcp_destroy(inst)` | Destroy instance and release all resources |
| `atcp_connect(inst)` | Initiate connection (active side), non-blocking |
| `atcp_accept(inst)` | Wait for connection (passive side), non-blocking |
| `atcp_disconnect(inst)` | Disconnect |
| `atcp_send(inst, data, len)` | Send data, non-blocking |
| `atcp_recv(inst, buf, len, &received)` | Receive data |
| `atcp_tick(inst)` | Drive the protocol stack (audio I/O, frame sync, demodulation, ARQ, heartbeat) |
| `atcp_get_state(inst)` | Get current connection state |
| `atcp_get_stats(inst)` | Get statistics (BER, SNR, throughput, etc.) |
| `atcp_get_audio_buf_size(inst)` | Get required audio buffer size (number of floats) |

## Project Structure

```
ATCP/
├── CMakeLists.txt                 # Top-level build config
├── lib/                           # Core C protocol stack library
│   ├── CMakeLists.txt             # Library build config
│   ├── USER_GUIDE.md              # User guide
│   ├── DEVELOPER_GUIDE.md         # Developer guide
│   ├── include/atcp/              # Public headers
│   │   ├── atcp.h                 #   Unified API entry point
│   │   ├── types.h                #   Type definitions, status codes
│   │   ├── config.h               #   Configuration struct & defaults
│   │   └── platform.h             #   Platform abstraction callbacks
│   ├── src/
│   │   ├── atcp.c                 # API implementation (integration layer)
│   │   ├── common/                # FFT, complex math, ring buffer, PRNG
│   │   ├── physical/              # Differential coding, AGC, CFO/SFO compensation
│   │   ├── modulation/            # OFDM, QAM, training sequences, frame sync, channel estimation
│   │   ├── coding/                # RS FEC, CRC32, GF(256)
│   │   └── link/                  # Framing, handshake, ARQ, ACK, heartbeat
│   └── tests/                     # Unit tests & integration tests
└── simulator/                     # C# three-node simulator
    ├── ATCP.Simulator.sln         # Visual Studio solution
    ├── SimCommon/                  # Common library (P/Invoke wrappers, TCP audio transport)
    ├── SimUpper/                   # Host simulator
    ├── SimMiddleware/              # Relay simulator (TCP proxy)
    └── SimLower/                   # Device simulator
```

## Platform Porting

Using ATCP requires implementing three callbacks in `atcp_platform_t`:

| Callback | Description |
|---|---|
| `audio_write` | Write audio samples to the output device (Speaker / DAC) |
| `audio_read` | Read audio samples from the input device (Mic / ADC) |
| `get_time_ms` | Return current system time in milliseconds |

| Platform | audio_write / audio_read | get_time_ms |
|---|---|---|
| Windows | WASAPI / PortAudio | `GetTickCount()` |
| Linux | ALSA / PulseAudio | `clock_gettime()` |
| STM32 | DMA + DAC/ADC | `HAL_GetTick()` |
| ESP32 | I2S Driver | `esp_timer_get_time() / 1000` |

## Simulator

ATCP includes a C#-based three-node simulation system that uses TCP to emulate the audio channel, allowing protocol stack validation without real hardware:

| Component | Description |
|---|---|
| **SimUpper** | Host simulator — initiates connection, sends/receives data |
| **SimMiddleware** | Relay simulator — TCP proxy forwarding audio streams between host and device |
| **SimLower** | Device simulator — accepts connection, echo communication |
| **SimCommon** | Common library — P/Invoke wrappers for the native ATCP DLL + TCP audio transport layer |

```bash
# Build with .NET CLI
dotnet build simulator/ATCP.Simulator.sln -c Release
```

> The simulator depends on the ATCP shared library (`atcp.dll`); build the core library first.

## Documentation

- [User Guide](lib/USER_GUIDE.md) — Build steps, full API reference, configuration parameters
- [Developer Guide](lib/DEVELOPER_GUIDE.md) — Architecture design, module details, coding standards, extension guide

## License

See the [LICENSE](LICENSE) file.
