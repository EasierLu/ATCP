#ifndef ATCP_CFO_H
#define ATCP_CFO_H
#include "../../include/atcp/types.h"
#include "../../include/atcp/config.h"

/* Moose算法估计分数CFO
 * 利用OFDM符号的CP重复结构
 * rx_samples: 至少 n_fft + cp_len 个采样（一个完整OFDM符号）
 * 返回: 归一化频率偏移（-0.5 ~ +0.5，以子载波间距为单位） */
float atcp_cfo_estimate_frac(const float *rx_samples, int cp_len, int n_fft);

/* 整数CFO估计
 * 利用两个相邻训练符号的频域相位差
 * rx_train_freq1/2: 两个训练符号的FFT结果
 * tx_train: 已知训练符号频域
 * 返回: 整数子载波偏移 */
int atcp_cfo_estimate_int(const atcp_complex_t *rx_train_freq1,
                        const atcp_complex_t *rx_train_freq2,
                        const atcp_complex_t *tx_train,
                        int n_subs, int sub_low);

/* 时域CFO补偿
 * delta_f: 频率偏移(Hz)
 * sample_rate: 采样率(Hz)
 * 对实数信号: r_comp(n) = r(n) * cos(2π·Δf·n/fs) 取实部
 * 原地修改samples */
atcp_status_t atcp_cfo_compensate(float *samples, int n, float delta_f, int sample_rate);

/* CFO归一化值转Hz */
float atcp_cfo_to_hz(float normalized_cfo, int n_fft, int sample_rate);

#endif
