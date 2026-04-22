#ifndef ATCP_TRAINING_H
#define ATCP_TRAINING_H
#include "../../include/atcp/types.h"
#include "../../include/atcp/config.h"

/* 生成BPSK训练/导频频域符号
 * seed: PRNG种子（默认42）
 * n_subs: 子载波数
 * symbols_out: n_subs个复数输出（实部±1，虚部0） */
void atcp_training_generate(uint32_t seed, int n_subs, atcp_complex_t *symbols_out);

/* 生成完整的训练OFDM时域符号（含CP）
 * time_out长度 = n_fft + cp_len */
atcp_status_t atcp_training_generate_ofdm(const atcp_config_t *cfg,
                                      float *time_out, int *time_len);

#endif
