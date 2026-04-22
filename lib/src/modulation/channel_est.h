#ifndef ATCP_CHANNEL_EST_H
#define ATCP_CHANNEL_EST_H
#include "../../include/audiothief/types.h"

/* 信道估计：从接收的训练符号计算信道频率响应H(k)
 * rx_train: 接收端FFT后的训练符号频域数据（n_subs个）
 * tx_train: 已知发送端训练符号（n_subs个）
 * H_out: 信道响应输出（n_subs个） */
atcp_status_t atcp_channel_estimate(const atcp_complex_t *rx_train, const atcp_complex_t *tx_train,
                                int n_subs, atcp_complex_t *H_out);

/* 多训练符号平均信道估计 */
atcp_status_t atcp_channel_estimate_avg(const atcp_complex_t *rx_trains[], const atcp_complex_t *tx_train,
                                    int n_trains, int n_subs, atcp_complex_t *H_out);

/* ZF均衡：Y(k)/H(k) */
atcp_status_t atcp_channel_equalize(const atcp_complex_t *rx_data, const atcp_complex_t *H,
                                int n_subs, atcp_complex_t *eq_out);

/* 更新信道估计（用导频符号） */
atcp_status_t atcp_channel_update(atcp_complex_t *H, const atcp_complex_t *rx_pilot,
                              const atcp_complex_t *tx_pilot, int n_subs, float alpha);

#endif
