#ifndef ATCP_OFDM_H
#define ATCP_OFDM_H
#include "../../include/atcp/types.h"
#include "../../include/atcp/config.h"

/* 计算可用数据子载波数 */
int atcp_ofdm_num_subcarriers(const atcp_config_t *cfg);

/* 计算单个OFDM符号的时域采样数（含CP） */
int atcp_ofdm_symbol_samples(const atcp_config_t *cfg);

/* OFDM调制：频域符号 -> 时域采样
 * freq_symbols: n_subs个复数（数据子载波）
 * time_out: 输出时域实数采样，长度 = n_fft + cp_len
 * 内部处理：子载波映射到[sub_low,sub_high]，Hermitian对称，IFFT，加CP */
atcp_status_t atcp_ofdm_modulate(const atcp_complex_t *freq_symbols, int n_subs,
                             const atcp_config_t *cfg,
                             float *time_out, int *time_len);

/* OFDM解调：时域采样 -> 频域符号
 * time_in: n_fft + cp_len 个实数采样
 * freq_symbols_out: n_subs个复数输出
 * 内部处理：去CP，实数->复数，FFT，提取[sub_low,sub_high] */
atcp_status_t atcp_ofdm_demodulate(const float *time_in, const atcp_config_t *cfg,
                               atcp_complex_t *freq_symbols_out, int n_subs);

#endif
