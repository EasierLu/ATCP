#ifndef ATCP_QAM_H
#define ATCP_QAM_H
#include "../../include/atcp/types.h"

/* 返回每符号比特数：4->2, 16->4, 64->6, 256->8 */
int atcp_qam_bits_per_symbol(int order);

/* QAM调制：bits -> 复数符号（Gray码映射，归一化功率=1）
 * n_bits必须是bits_per_symbol的整数倍
 * symbols_out大小 = n_bits / bits_per_symbol */
atcp_status_t atcp_qam_modulate(const uint8_t *bits, int n_bits, int order,
                            atcp_complex_t *symbols_out, int *n_symbols);

/* QAM解调：复数符号 -> bits（硬判决）
 * bits_out大小 = n_symbols * bits_per_symbol */
atcp_status_t atcp_qam_demodulate(const atcp_complex_t *symbols, int n_symbols, int order,
                              uint8_t *bits_out, int *n_bits);

/* 获取QAM归一化因子 */
float atcp_qam_norm_factor(int order);

#endif
