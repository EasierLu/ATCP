#ifndef ATCP_DIFF_SIGNAL_H
#define ATCP_DIFF_SIGNAL_H
#include "../../include/audiothief/types.h"

/* 差分编码：单信号 -> 左右声道
 * signal[n] -> left[n] = +signal[n], right[n] = -signal[n] */
atcp_status_t atcp_diff_encode(const float *signal, int n, float *left_out, float *right_out);

/* 差分解码：左右声道 -> 单信号
 * signal_out[n] = (left[n] - right[n]) / 2.0 */
atcp_status_t atcp_diff_decode(const float *left, const float *right, int n, float *signal_out);

/* 交错格式差分编码：signal -> interleaved[L0,R0,L1,R1,...] */
atcp_status_t atcp_diff_encode_interleaved(const float *signal, int n, float *interleaved_out);

/* 交错格式差分解码：interleaved[L0,R0,L1,R1,...] -> signal */
atcp_status_t atcp_diff_decode_interleaved(const float *interleaved, int n, float *signal_out);

#endif
