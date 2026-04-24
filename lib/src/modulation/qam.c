#include "qam.h"
#include <math.h>
#include <string.h>

/* ---------- 预计算星座查找表 ---------- */

/* 正向坐标查找表：lut[gray_index] = 2 * gray_to_bin(gray_index) - (sqrt_m - 1)
 * 消除运行时的 Gray→Binary 转换和坐标计算 */

/* QPSK (order=4, sqrt_m=2): gray {0,1} → bin {0,1} → coord: 2*bin - 1 */
static const int8_t qam_coord_lut_2[2] = { -1, 1 };

/* 16-QAM (order=16, sqrt_m=4): gray {0,1,2,3} → bin {0,1,3,2} → coord: 2*bin - 3 */
static const int8_t qam_coord_lut_4[4] = { -3, -1, 3, 1 };

/* 64-QAM (order=64, sqrt_m=8): gray {0..7} → bin → coord: 2*bin - 7 */
static const int8_t qam_coord_lut_8[8] = { -7, -5, -1, -3, 7, 5, 1, 3 };

/* 256-QAM (order=256, sqrt_m=16): gray {0..15} → bin → coord: 2*bin - 15 */
static const int8_t qam_coord_lut_16[16] = {
    -15, -13, -9, -11, -1, -3, -7, -5,
     15,  13,  9,  11,  1,  3,  7,  5
};

/* 反向查找表: bin_to_gray LUT，用于解调时避免 bin_to_gray() 调用 */
static const uint8_t gray_lut_2[2] = { 0, 1 };
static const uint8_t gray_lut_4[4] = { 0, 1, 3, 2 };
static const uint8_t gray_lut_8[8] = { 0, 1, 3, 2, 6, 7, 5, 4 };
static const uint8_t gray_lut_16[16] = {
    0, 1, 3, 2, 6, 7, 5, 4, 12, 13, 15, 14, 10, 11, 9, 8
};

static const int8_t *get_coord_lut(int sqrt_m)
{
    switch (sqrt_m) {
        case  2: return qam_coord_lut_2;
        case  4: return qam_coord_lut_4;
        case  8: return qam_coord_lut_8;
        case 16: return qam_coord_lut_16;
        default: return NULL;
    }
}

static const uint8_t *get_gray_lut(int sqrt_m)
{
    switch (sqrt_m) {
        case  2: return gray_lut_2;
        case  4: return gray_lut_4;
        case  8: return gray_lut_8;
        case 16: return gray_lut_16;
        default: return NULL;
    }
}

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

    const int8_t *coord_lut = get_coord_lut(sqrt_m);

    for (int s = 0; s < nsym; s++) {
        const uint8_t *bp = bits + s * bps;

        /* 提取 I 轴 Gray 码索引 */
        int i_gray = 0;
        for (int b = 0; b < half_bps; b++)
            i_gray = (i_gray << 1) | bp[b];

        /* 提取 Q 轴 Gray 码索引 */
        int q_gray = 0;
        for (int b = 0; b < half_bps; b++)
            q_gray = (q_gray << 1) | bp[half_bps + b];

        /* 查表获取坐标（消除 gray_to_bin 和算术运算） */
        symbols_out[s].re = (float)coord_lut[i_gray] / norm;
        symbols_out[s].im = (float)coord_lut[q_gray] / norm;
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

        /* 查表获取Gray码（消除 bin_to_gray 调用） */
        const uint8_t *g_lut = get_gray_lut(sqrt_m);
        int i_gray = g_lut[i_bin];
        int q_gray = g_lut[q_bin];

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
