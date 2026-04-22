#ifndef ATCP_SFO_H
#define ATCP_SFO_H
#include "../../include/audiothief/types.h"

/* SFO估计：通过多个导频符号的相位漂移趋势
 * pilot_phases: 各导频符号上某个参考子载波的相位（弧度），长度n_pilots
 * symbol_indices: 对应导频符号的索引（第几个OFDM符号），长度n_pilots
 * 返回: SFO估计值（ppm） */
float atcp_sfo_estimate(const float *pilot_phases, const int *symbol_indices,
                      int n_pilots, int n_fft, int cp_len, int sample_rate);

/* SFO频域补偿：对单个OFDM符号的频域数据进行相位补偿
 * freq_symbols: n_subs个频域复数（原地修改）
 * sfo_ppm: SFO估计值
 * symbol_idx: 当前OFDM符号在流中的索引
 * sub_low: 最低子载波索引 */
atcp_status_t atcp_sfo_compensate(atcp_complex_t *freq_symbols, int n_subs,
                              float sfo_ppm, int symbol_idx,
                              int sub_low, int n_fft, int cp_len);

#endif
