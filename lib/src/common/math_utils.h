#ifndef ATCP_MATH_UTILS_H
#define ATCP_MATH_UTILS_H
#include "../../include/audiothief/types.h"

/* 复数运算 */
atcp_complex_t atcp_complex_mul(atcp_complex_t a, atcp_complex_t b);
atcp_complex_t atcp_complex_conj(atcp_complex_t a);
atcp_complex_t atcp_complex_add(atcp_complex_t a, atcp_complex_t b);
atcp_complex_t atcp_complex_sub(atcp_complex_t a, atcp_complex_t b);
float atcp_complex_abs(atcp_complex_t a);       /* 模 */
float atcp_complex_abs2(atcp_complex_t a);      /* 模的平方 */
float atcp_complex_arg(atcp_complex_t a);       /* 相角(rad) */
atcp_complex_t atcp_complex_from_polar(float mag, float phase);

/* 信号工具 */
float atcp_power_rms(const float *samples, int n);
void atcp_normalize(float *samples, int n, float target_amp);
float atcp_db_to_linear(float db);
float atcp_linear_to_db(float linear);

#endif
