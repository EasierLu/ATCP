#include "ofdm.h"
#include "../common/fft.h"
#include <string.h>

/* 最大FFT点数（用于栈上缓冲区） */
#define ATCP_OFDM_MAX_FFT 1024

int atcp_ofdm_num_subcarriers(const atcp_config_t *cfg)
{
    if (!cfg) return 0;
    return cfg->sub_high - cfg->sub_low + 1;
}

int atcp_ofdm_symbol_samples(const atcp_config_t *cfg)
{
    if (!cfg) return 0;
    return cfg->n_fft + cfg->cp_len;
}

atcp_status_t atcp_ofdm_modulate(const atcp_complex_t *freq_symbols, int n_subs,
                             const atcp_config_t *cfg,
                             float *time_out, int *time_len)
{
    if (!freq_symbols || !cfg || !time_out || !time_len)
        return ATCP_ERR_INVALID_PARAM;

    int n_fft  = cfg->n_fft;
    int cp_len = cfg->cp_len;
    int sub_lo = cfg->sub_low;
    int sub_hi = cfg->sub_high;

    if (n_fft > ATCP_OFDM_MAX_FFT)
        return ATCP_ERR_INVALID_PARAM;
    if (n_subs != sub_hi - sub_lo + 1)
        return ATCP_ERR_INVALID_PARAM;

    /* 1. 创建FFT缓冲区并清零 */
    atcp_complex_t fft_buf[ATCP_OFDM_MAX_FFT];
    memset(fft_buf, 0, sizeof(atcp_complex_t) * (size_t)n_fft);

    /* 2. 映射数据子载波到 [sub_low..sub_high] */
    for (int i = 0; i < n_subs; i++) {
        fft_buf[sub_lo + i] = freq_symbols[i];
    }

    /* 3. Hermitian 对称: fft_buf[N_FFT - k] = conj(fft_buf[k]) */
    for (int k = sub_lo; k <= sub_hi; k++) {
        fft_buf[n_fft - k].re =  fft_buf[k].re;
        fft_buf[n_fft - k].im = -fft_buf[k].im;
    }

    /* 4. DC 和 Nyquist 置零 */
    fft_buf[0].re = 0.0f;
    fft_buf[0].im = 0.0f;
    fft_buf[n_fft / 2].re = 0.0f;
    fft_buf[n_fft / 2].im = 0.0f;

    /* 5. IFFT（原地） */
    atcp_status_t st = atcp_ifft(fft_buf, n_fft);
    if (st != ATCP_OK) return st;

    /* 6. 输出时域：CP + 主体 */
    /* CP: fft_buf[n_fft - cp_len .. n_fft - 1] 的实部 */
    for (int i = 0; i < cp_len; i++) {
        time_out[i] = fft_buf[n_fft - cp_len + i].re;
    }
    /* 主体: fft_buf[0..n_fft-1] 的实部 */
    for (int i = 0; i < n_fft; i++) {
        time_out[cp_len + i] = fft_buf[i].re;
    }

    *time_len = n_fft + cp_len;
    return ATCP_OK;
}

atcp_status_t atcp_ofdm_demodulate(const float *time_in, const atcp_config_t *cfg,
                               atcp_complex_t *freq_symbols_out, int n_subs)
{
    if (!time_in || !cfg || !freq_symbols_out)
        return ATCP_ERR_INVALID_PARAM;

    int n_fft  = cfg->n_fft;
    int cp_len = cfg->cp_len;
    int sub_lo = cfg->sub_low;
    int sub_hi = cfg->sub_high;

    if (n_fft > ATCP_OFDM_MAX_FFT)
        return ATCP_ERR_INVALID_PARAM;
    if (n_subs != sub_hi - sub_lo + 1)
        return ATCP_ERR_INVALID_PARAM;

    /* 1. 跳过CP，将实数采样转为复数 */
    atcp_complex_t fft_buf[ATCP_OFDM_MAX_FFT];
    const float *data = time_in + cp_len;
    for (int i = 0; i < n_fft; i++) {
        fft_buf[i].re = data[i];
        fft_buf[i].im = 0.0f;
    }

    /* 2. FFT */
    atcp_status_t st = atcp_fft(fft_buf, n_fft);
    if (st != ATCP_OK) return st;

    /* 3. 提取 [sub_low..sub_high] */
    for (int i = 0; i < n_subs; i++) {
        freq_symbols_out[i] = fft_buf[sub_lo + i];
    }

    return ATCP_OK;
}
