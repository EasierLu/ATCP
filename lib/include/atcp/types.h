#ifndef ATCP_TYPES_H
#define ATCP_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* DLL导出宏 */
#if defined(_WIN32) || defined(_WIN64)
  #ifdef ATCP_BUILDING_DLL
    #define ATCP_API __declspec(dllexport)
  #else
    #define ATCP_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define ATCP_API __attribute__((visibility("default")))
#else
  #define ATCP_API
#endif

/* 静态库模式下清除导出宏 */
#ifdef ATCP_STATIC
  #undef ATCP_API
  #define ATCP_API
#endif

/* 布尔类型 */
typedef int atcp_bool_t;
#define ATCP_TRUE  1
#define ATCP_FALSE 0

/* 复数类型 */
typedef struct {
    float re;
    float im;
} atcp_complex_t;

/* 状态/错误码 */
typedef enum {
    ATCP_OK = 0,
    ATCP_ERR_INVALID_PARAM = -1,
    ATCP_ERR_NO_MEMORY = -2,
    ATCP_ERR_TIMEOUT = -3,
    ATCP_ERR_CRC_FAIL = -4,
    ATCP_ERR_RS_DECODE_FAIL = -5,
    ATCP_ERR_SYNC_FAIL = -6,
    ATCP_ERR_HANDSHAKE_FAIL = -7,
    ATCP_ERR_DISCONNECTED = -8,
    ATCP_ERR_BUSY = -9,
    ATCP_ERR_BUFFER_FULL = -10,
    ATCP_ERR_BUFFER_EMPTY = -11,
    ATCP_ERR_NOT_CONNECTED = -12,
    ATCP_ERR_QUALITY_LOW = -13,
} atcp_status_t;

/* 连接状态 */
typedef enum {
    ATCP_STATE_IDLE = 0,
    ATCP_STATE_CONNECTING,
    ATCP_STATE_CONNECTED,
    ATCP_STATE_DISCONNECTING,
    ATCP_STATE_DISCONNECTED,
} atcp_state_t;

/* 帧类型 */
typedef enum {
    ATCP_FRAME_DATA = 0x01,
    ATCP_FRAME_ACK = 0x02,
    ATCP_FRAME_HANDSHAKE = 0x03,
    ATCP_FRAME_HEARTBEAT = 0x04,
    ATCP_FRAME_EOS = 0x05,
} atcp_frame_type_t;

/* 传输模式 */
typedef enum {
    ATCP_MODE_STANDALONE = 0,   /* 模式A：独立帧 */
    ATCP_MODE_STREAM = 1,       /* 模式B：连续流 */
} atcp_tx_mode_t;

/* 统计信息 */
typedef struct {
    float ber;                /* 误码率 */
    float snr_db;             /* 信噪比(dB) */
    float throughput_bps;     /* 有效吞吐率(bps) */
    uint32_t frames_sent;
    uint32_t frames_received;
    uint32_t retransmit_count;
    uint32_t ack_miss_count;
} atcp_stats_t;

#endif /* ATCP_TYPES_H */
