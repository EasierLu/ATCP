#include "qam.h"
#include <math.h>
#include <string.h>

/* ---------- 内部工具 ---------- */

/* 二进制转Gray码 */
static int bin_to_gray(int v) { return v ^ (v >> 1); }

/* Gray码转二进制 */
static int gray_to_bin(int g)
{
    int b = g;
    while (g >>= 1) b ^= g;
    return b;
}

/* 验证QAM阶数是否支持 */
static int qam_order_valid(int order)
{
    return (order == 4 || order == 16 || order == 64 || order == 256);
}

/* ---------- 公开接口 ---------- */

int atcp_qam_bits_per_symbol(int order)
{
    switch (order) {
        case   4: return 2;
        case  16: return 4;
        case  64: return 6;
        case 256: return 8;
        default:  return -1;
    }
}

float atcp_qam_norm_factor(int order)
{
    /* norm = sqrt(2*(M-1)/3) */
    return sqrtf(2.0f * (float)(order - 1) / 3.0f);
}

atcp_status_t atcp_qam_modulate(const uint8_t *bits, int n_bits, int order,
                            atcp_complex_t *symbols_out, int *n_symbols)
{
    if (!bits || !symbols_out || !n_symbols)
        return ATCP_ERR_INVALID_PARAM;
    if (!qam_order_valid(order))
        return ATCP_ERR_INVALID_PARAM;

    int bps = atcp_qam_bits_per_symbol(order);
    if (n_bits % bps != 0)
        return ATCP_ERR_INVALID_PARAM;

    int nsym = n_bits / bps;
    *n_symbols = nsym;

    int sqrt_m = (int)roundf(sqrtf((float)order));   /* 2,4,8,16 */
    int half_bps = bps / 2;                           /* I轴/Q轴各占的bit数 */
    float norm = atcp_qam_norm_factor(order);

    for (int s = 0; s < nsym; s++) {
        const uint8_t *bp = bits + s * bps;

        /* 高半部分bit -> I轴Gray值 */
        int i_val = 0;
        for (int b = 0; b < half_bps; b++)
            i_val = (i_val << 1) | bp[b];

        /* 低半部分bit -> Q轴Gray值 */
        int q_val = 0;
        for (int b = 0; b < half_bps; b++)
            q_val = (q_val << 1) | bp[half_bps + b];

        /* Gray码值 -> 星座坐标: gray_index g -> 映射到奇数序列
         * g -> bin_val = gray_to_bin(g)
         * coord = 2*bin_val - (sqrt_m - 1)  => {-(sqrt_m-1), ..., -1, +1, ..., +(sqrt_m-1)} */
        int i_bin = gray_to_bin(i_val);
        int q_bin = gray_to_bin(q_val);

        float i_coord = (float)(2 * i_bin - (sqrt_m - 1));
        float q_coord = (float)(2 * q_bin - (sqrt_m - 1));

        symbols_out[s].re = i_coord / norm;
        symbols_out[s].im = q_coord / norm;
    }

    return ATCP_OK;
}

atcp_status_t atcp_qam_demodulate(const atcp_complex_t *symbols, int n_symbols, int order,
                              uint8_t *bits_out, int *n_bits)
{
    if (!symbols || !bits_out || !n_bits)
        return ATCP_ERR_INVALID_PARAM;
    if (!qam_order_valid(order))
        return ATCP_ERR_INVALID_PARAM;

    int bps = atcp_qam_bits_per_symbol(order);
    *n_bits = n_symbols * bps;

    int sqrt_m = (int)roundf(sqrtf((float)order));
    int half_bps = bps / 2;
    float norm = atcp_qam_norm_factor(order);

    for (int s = 0; s < n_symbols; s++) {
        /* 还原到星座尺度 */
        float i_coord = symbols[s].re * norm;
        float q_coord = symbols[s].im * norm;

        /* 找最近奇数星座点: 值域 {-(sqrt_m-1), ..., -1, +1, ..., +(sqrt_m-1)}
         * bin_val = round((coord + (sqrt_m-1)) / 2)，钳位到[0, sqrt_m-1] */
        int i_bin = (int)roundf((i_coord + (float)(sqrt_m - 1)) / 2.0f);
        if (i_bin < 0) i_bin = 0;
        if (i_bin >= sqrt_m) i_bin = sqrt_m - 1;

        int q_bin = (int)roundf((q_coord + (float)(sqrt_m - 1)) / 2.0f);
        if (q_bin < 0) q_bin = 0;
        if (q_bin >= sqrt_m) q_bin = sqrt_m - 1;

        /* 二进制转Gray码 */
        int i_gray = bin_to_gray(i_bin);
        int q_gray = bin_to_gray(q_bin);

        /* 输出bit */
        uint8_t *bp = bits_out + s * bps;
        for (int b = half_bps - 1; b >= 0; b--) {
            bp[half_bps - 1 - b] = (uint8_t)((i_gray >> b) & 1);
        }
        for (int b = half_bps - 1; b >= 0; b--) {
            bp[half_bps + (half_bps - 1 - b)] = (uint8_t)((q_gray >> b) & 1);
        }
    }

    return ATCP_OK;
}
