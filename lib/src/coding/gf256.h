#ifndef ATCP_GF256_H
#define ATCP_GF256_H
#include <stdint.h>

/* 初始化GF(256)查找表，必须在使用前调用 */
void gf256_init(void);

/* 基本运算 */
uint8_t gf256_mul(uint8_t a, uint8_t b);
uint8_t gf256_div(uint8_t a, uint8_t b);  /* b不能为0 */
uint8_t gf256_pow(uint8_t a, int n);
uint8_t gf256_inv(uint8_t a);             /* 乘法逆元 */

/* 多项式运算（系数数组，poly[0]是最高次项） */
/* 多项式乘法: result = a * b, result_len = a_len + b_len - 1 */
void gf256_poly_mul(const uint8_t *a, int a_len, const uint8_t *b, int b_len,
                    uint8_t *result, int *result_len);

/* 多项式求值: poly(x) */
uint8_t gf256_poly_eval(const uint8_t *poly, int poly_len, uint8_t x);

/* 多项式除法/取模 */
void gf256_poly_divmod(const uint8_t *dividend, int div_len,
                       const uint8_t *divisor, int dvs_len,
                       uint8_t *remainder, int *rem_len);

#endif
