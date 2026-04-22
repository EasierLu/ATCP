#include "sfo.h"
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float atcp_sfo_estimate(const float *pilot_phases, const int *symbol_indices,
                      int n_pilots, int n_fft, int cp_len, int sample_rate)
{
    if (!pilot_phases || !symbol_indices || n_pilots < 2 || n_fft <= 0)
        return 0.0f;

    /*
     * 最小二乘线性拟合: phase = a * symbol_index + b
     * a = (n*Σ(x*y) - Σx*Σy) / (n*Σ(x²) - (Σx)²)
     * 其中 x = symbol_indices, y = pilot_phases
     */
    float sum_x  = 0.0f;
    float sum_y  = 0.0f;
    float sum_xy = 0.0f;
    float sum_x2 = 0.0f;
    int n = n_pilots;

    for (int i = 0; i < n; i++) {
        float x = (float)symbol_indices[i];
        float y = pilot_phases[i];
        sum_x  += x;
        sum_y  += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }

    float denom = (float)n * sum_x2 - sum_x * sum_x;
    if (fabsf(denom) < 1e-12f)
        return 0.0f;

    float slope_a = ((float)n * sum_xy - sum_x * sum_y) / denom;

    /*
     * 斜率 a 是每OFDM符号的相位漂移（弧度/符号）
     * SFO_ppm = a * N_FFT / (2π * (N_FFT + CP_LEN)) * 1e6
     */
    float symbol_dur = (float)(n_fft + cp_len);
    float sfo_ppm = slope_a * (float)n_fft / (2.0f * (float)M_PI * symbol_dur) * 1e6f;

    return sfo_ppm;
}

atcp_status_t atcp_sfo_compensate(atcp_complex_t *freq_symbols, int n_subs,
                              float sfo_ppm, int symbol_idx,
                              int sub_low, int n_fft, int cp_len)
{
    if (!freq_symbols || n_subs <= 0 || n_fft <= 0)
        return ATCP_ERR_INVALID_PARAM;

    /*
     * 每个子载波 k 的补偿相位：
     * θ(k) = -2π * (sub_low + k) * sfo_ppm * 1e-6 * symbol_idx * (N_FFT+CP_LEN) / N_FFT
     */
    float symbol_dur_ratio = (float)(n_fft + cp_len) / (float)n_fft;
    float base = -2.0f * (float)M_PI * sfo_ppm * 1e-6f * (float)symbol_idx * symbol_dur_ratio;

    for (int k = 0; k < n_subs; k++) {
        float theta = base * (float)(sub_low + k);
        float cos_t = cosf(theta);
        float sin_t = sinf(theta);

        /* freq_symbols[k] *= exp(j*θ) = (re + j*im) * (cos + j*sin) */
        float re = freq_symbols[k].re;
        float im = freq_symbols[k].im;
        freq_symbols[k].re = re * cos_t - im * sin_t;
        freq_symbols[k].im = re * sin_t + im * cos_t;
    }

    return ATCP_OK;
}
