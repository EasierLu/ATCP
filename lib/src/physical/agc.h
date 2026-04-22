#ifndef ATCP_AGC_H
#define ATCP_AGC_H
#include "../../include/atcp/types.h"

typedef struct {
    float target_amplitude;    /* 目标幅度（默认0.5） */
    float attack_coeff;        /* 攻击系数（快速响应，默认0.01） */
    float release_coeff;       /* 释放系数（慢速释放，默认0.001） */
    float current_gain;        /* 当前增益 */
    float peak_level;          /* 当前峰值追踪 */
} atcp_agc_t;

/* 初始化AGC */
void atcp_agc_init(atcp_agc_t *agc, float target_amplitude);

/* 处理采样（原地修改） */
atcp_status_t atcp_agc_process(atcp_agc_t *agc, float *samples, int n);

/* 获取当前增益 */
float atcp_agc_get_gain(const atcp_agc_t *agc);

/* 重置AGC状态 */
void atcp_agc_reset(atcp_agc_t *agc);

#endif
