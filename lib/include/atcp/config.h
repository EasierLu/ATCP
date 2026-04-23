#ifndef ATCP_CONFIG_H
#define ATCP_CONFIG_H

#include "types.h"

/* 协议配置参数 (Section 8) */
typedef struct {
    /* OFDM 参数 */
    int n_fft;              /* FFT 点数，默认 512 */
    int cp_len;             /* 循环前缀长度，默认 32 */
    int sub_low;            /* 最低可用子载波索引，默认 2 */
    int sub_high;           /* 最高可用子载波索引，默认 200 */

    /* 训练序列 */
    int n_train;            /* 训练符号数，默认 2 */
    uint32_t train_seed;    /* 训练序列 PRNG 种子，默认 42 */

    /* 前导/尾导 */
    int lead_in;            /* 前导静音采样数，默认 1024 */
    int lead_out;           /* 尾导静音采样数，默认 1024 */

    /* 发射 */
    float amplitude;        /* 发射幅度，默认 0.85f */

    /* 纠错编码 */
    int rs_nsym;            /* RS 校验符号数，默认 48 */

    /* 调制 */
    int qam_order;          /* QAM 阶数，默认 16 */

    /* 采样率 */
    int sample_rate;        /* 采样率(Hz)，默认 44100 */

    /* 均衡 / 导频 */
    int window_size;        /* 信道均衡窗口大小，默认 4 */
    int pilot_interval;     /* 导频插入间隔(子载波数)，默认 10 */

    /* ACK / 重传 */
    int ack_timeout_ms;     /* ACK 超时(ms)，默认 500 */
    int max_ack_miss;       /* 最大连续 ACK 丢失次数，默认 3 */
    int ack_repeat;         /* ACK 帧重复发送次数，默认 2 */

    /* 心跳 */
    int heartbeatcp_interval_ms;  /* 心跳发送间隔(ms)，默认 3000 */
    int heartbeatcp_timeout_ms;   /* 心跳超时(ms)，默认 10000 */
} atcp_config_t;

/* 返回包含所有默认值的配置 */
static inline atcp_config_t atcp_config_default(void)
{
    atcp_config_t cfg;
    cfg.n_fft               = 512;
    cfg.cp_len              = 32;
    cfg.sub_low             = 2;
    cfg.sub_high            = 200;
    cfg.n_train             = 2;
    cfg.train_seed          = 42;
    cfg.lead_in             = 1024;
    cfg.lead_out            = 1024;
    cfg.amplitude           = 0.85f;
    cfg.rs_nsym             = 48;
    cfg.qam_order           = 16;
    cfg.sample_rate         = 44100;
    cfg.window_size         = 4;
    cfg.pilot_interval      = 10;
    cfg.ack_timeout_ms      = 500;
    cfg.max_ack_miss        = 3;
    cfg.ack_repeat          = 2;
    cfg.heartbeatcp_interval_ms = 3000;
    cfg.heartbeatcp_timeout_ms  = 10000;
    return cfg;
}

#endif /* ATCP_CONFIG_H */
