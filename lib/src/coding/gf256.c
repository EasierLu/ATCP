#include "gf256.h"
#include <string.h>

/* 本原多项式: x^8 + x^4 + x^3 + x^2 + 1 = 0x11D */
#define GF256_PRIM 0x11D

/* 查找表 */
static uint8_t gf256_exp[512]; /* 扩展防溢出 */
static uint8_t gf256_log[256];
static int gf256_initialized = 0;

void gf256_init(void)
{
    if (gf256_initialized) return;

    int val = 1;
    for (int i = 0; i < 255; i++) {
        gf256_exp[i] = (uint8_t)val;
        gf256_log[val] = (uint8_t)i;
        val <<= 1;
        if (val >= 256)
            val ^= GF256_PRIM;
    }
    /* exp[255] = exp[0] = 1, 周期为255 */
    for (int i = 255; i < 512; i++) {
        gf256_exp[i] = gf256_exp[i - 255];
    }
    gf256_log[0] = 0; /* 约定，log(0)未定义但置0 */

    gf256_initialized = 1;
}

uint8_t gf256_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf256_exp[gf256_log[a] + gf256_log[b]];
}

uint8_t gf256_div(uint8_t a, uint8_t b)
{
    if (a == 0) return 0;
    /* b不能为0，调用者保证 */
    return gf256_exp[(gf256_log[a] - gf256_log[b] + 255) % 255];
}

uint8_t gf256_pow(uint8_t a, int n)
{
    if (a == 0) return 0;
    n = ((n % 255) + 255) % 255;
    return gf256_exp[(gf256_log[a] * n) % 255];
}

uint8_t gf256_inv(uint8_t a)
{
    /* a不能为0 */
    return gf256_exp[255 - gf256_log[a]];
}

void gf256_poly_mul(const uint8_t *a, int a_len, const uint8_t *b, int b_len,
                    uint8_t *result, int *result_len)
{
    int rlen = a_len + b_len - 1;
    *result_len = rlen;
    memset(result, 0, (size_t)rlen);

    for (int i = 0; i < a_len; i++) {
        for (int j = 0; j < b_len; j++) {
            result[i + j] ^= gf256_mul(a[i], b[j]);
        }
    }
}

uint8_t gf256_poly_eval(const uint8_t *poly, int poly_len, uint8_t x)
{
    /* Horner法: poly[0]是最高次项 */
    uint8_t result = poly[0];
    for (int i = 1; i < poly_len; i++) {
        result = gf256_mul(result, x) ^ poly[i];
    }
    return result;
}

void gf256_poly_divmod(const uint8_t *dividend, int div_len,
                       const uint8_t *divisor, int dvs_len,
                       uint8_t *remainder, int *rem_len)
{
    /* 工作缓冲区 */
    uint8_t buf[512];
    memcpy(buf, dividend, (size_t)div_len);

    uint8_t lead = divisor[0];
    for (int i = 0; i <= div_len - dvs_len; i++) {
        if (buf[i] == 0) continue;
        uint8_t coeff = gf256_div(buf[i], lead);
        for (int j = 0; j < dvs_len; j++) {
            buf[i + j] ^= gf256_mul(divisor[j], coeff);
        }
    }

    /* 余数是buf末尾 dvs_len-1 个元素 */
    int rlen = dvs_len - 1;
    *rem_len = rlen;
    memcpy(remainder, buf + div_len - rlen, (size_t)rlen);
}
