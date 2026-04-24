#include "fft.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* 检查 n 是否为2的幂 */
static int is_power_of_two(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

/* 位反转置换（bit-reversal permutation） */
static void bit_reversal(atcp_complex_t *buf, int n)
{
    int i, j, k;
    j = 0;
    for (i = 1; i < n; i++) {
        k = n >> 1;
        while (j >= k) {
            j -= k;
            k >>= 1;
        }
        j += k;
        if (i < j) {
            atcp_complex_t tmp = buf[i];
            buf[i] = buf[j];
            buf[j] = tmp;
        }
    }
}

/* 核心 FFT（正向，原地 Cooley-Tukey radix-2 DIT） */
static void fft_core(atcp_complex_t *buf, int n)
{
    int len, i, j;

    bit_reversal(buf, n);

    for (len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        float angle = -2.0f * (float)M_PI / (float)len;
        float wn_re = cosf(angle);
        float wn_im = sinf(angle);
        for (i = 0; i < n; i += len) {
            float w_re = 1.0f, w_im = 0.0f;
            for (j = 0; j < half; j++) {
                atcp_complex_t *even = &buf[i + j];
                atcp_complex_t *odd  = &buf[i + j + half];
                float tre = odd->re * w_re - odd->im * w_im;
                float tim = odd->re * w_im + odd->im * w_re;
                odd->re = even->re - tre;
                odd->im = even->im - tim;
                even->re += tre;
                even->im += tim;
                float tmp = w_re * wn_re - w_im * wn_im;
                w_im = w_re * wn_im + w_im * wn_re;
                w_re = tmp;
            }
        }
    }
}

atcp_status_t atcp_fft(atcp_complex_t *buf, int n)
{
    if (!buf || n < 2 || !is_power_of_two(n))
        return ATCP_ERR_INVALID_PARAM;

    fft_core(buf, n);
    return ATCP_OK;
}

atcp_status_t atcp_ifft(atcp_complex_t *buf, int n)
{
    int i;
    float inv_n;

    if (!buf || n < 2 || !is_power_of_two(n))
        return ATCP_ERR_INVALID_PARAM;

    /* 共轭输入 */
    for (i = 0; i < n; i++)
        buf[i].im = -buf[i].im;

    /* 正向 FFT */
    fft_core(buf, n);

    /* 共轭输出并除以 N */
    inv_n = 1.0f / (float)n;
    for (i = 0; i < n; i++) {
        buf[i].re *= inv_n;
        buf[i].im = -buf[i].im * inv_n;
    }

    return ATCP_OK;
}
