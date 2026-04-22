#include "cfo.h"
#include "../common/math_utils.h"
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float atcp_cfo_estimate_frac(const float *rx_samples, int cp_len, int n_fft)
{
    if (!rx_samples || cp_len <= 0 || n_fft <= 0)
        return 0.0f;

    /*
     * 对于实数OFDM信号，CP自相关方法：
     * P = Σ_{k=0}^{cp_len-1} r(k) * r(k + n_fft)
     *
     * 由于CP是OFDM符号末尾的拷贝：r(k) ≈ r(k + n_fft)
     * 在无CFO时 P ≈ Σ r(k)^2（强正相关）
     * 有CFO时，相关值降低。
     *
     * 对实数信号我们无法直接提取相位角来获得CFO方向，
     * 但可以用延迟一个采样的自相关来近似解析信号：
     * P_re = Σ r(k)*r(k+N_FFT)
     * P_im = Σ r(k)*r(k+N_FFT+1)  （延迟1个采样近似正交分量）
     * CFO_frac ≈ atan2(P_im, P_re) / (2π)
     */
    float p_re = 0.0f;
    float p_im = 0.0f;

    for (int k = 0; k < cp_len; k++) {
        p_re += rx_samples[k] * rx_samples[k + n_fft];
        /* 使用延迟1采样近似Hilbert正交分量 */
        if (k + n_fft + 1 < cp_len + n_fft + 1)
            p_im += rx_samples[k] * rx_samples[k + n_fft + 1];
    }

    float cfo_frac = atan2f(p_im, p_re) / (2.0f * (float)M_PI);

    /* 限制范围 [-0.5, +0.5] */
    if (cfo_frac > 0.5f)  cfo_frac -= 1.0f;
    if (cfo_frac < -0.5f) cfo_frac += 1.0f;

    return cfo_frac;
}

int atcp_cfo_estimate_int(const atcp_complex_t *rx_train_freq1,
                        const atcp_complex_t *rx_train_freq2,
                        const atcp_complex_t *tx_train,
                        int n_subs, int sub_low)
{
    if (!rx_train_freq1 || !rx_train_freq2 || !tx_train || n_subs <= 0)
        return 0;

    /*
     * 整数CFO估计：尝试不同的整数偏移d，找到使相关最大的d
     * 对每个候选偏移d：
     *   C(d) = |Σ_{k} conj(tx[k]) * rx1[k+d]|^2
     * 搜索范围限制在 [-max_shift, +max_shift]
     */
    int max_shift = n_subs / 4;  /* 合理搜索范围 */
    if (max_shift < 1) max_shift = 1;

    float best_corr = -1.0f;
    int best_d = 0;

    for (int d = -max_shift; d <= max_shift; d++) {
        float corr_re = 0.0f;
        float corr_im = 0.0f;

        for (int k = 0; k < n_subs; k++) {
            int shifted_k = k + d;
            if (shifted_k < 0 || shifted_k >= n_subs)
                continue;

            /* conj(tx[k]) * rx1[shifted_k] */
            atcp_complex_t tx_conj = atcp_complex_conj(tx_train[k]);
            atcp_complex_t prod = atcp_complex_mul(tx_conj, rx_train_freq1[shifted_k]);
            corr_re += prod.re;
            corr_im += prod.im;
        }

        float corr_mag = corr_re * corr_re + corr_im * corr_im;
        if (corr_mag > best_corr) {
            best_corr = corr_mag;
            best_d = d;
        }
    }

    return best_d;
}

atcp_status_t atcp_cfo_compensate(float *samples, int n, float delta_f, int sample_rate)
{
    if (!samples || n <= 0 || sample_rate <= 0)
        return ATCP_ERR_INVALID_PARAM;

    /*
     * 频率搬移：samples[k] *= cos(2π·Δf·k / fs)
     * phase每步递增 2π·Δf / fs
     */
    float phase_inc = 2.0f * (float)M_PI * delta_f / (float)sample_rate;
    float phase = 0.0f;

    for (int k = 0; k < n; k++) {
        samples[k] *= cosf(phase);
        phase += phase_inc;

        /* 防止相位累积过大导致精度损失 */
        if (phase > 2.0f * (float)M_PI)
            phase -= 2.0f * (float)M_PI;
        else if (phase < -2.0f * (float)M_PI)
            phase += 2.0f * (float)M_PI;
    }

    return ATCP_OK;
}

float atcp_cfo_to_hz(float normalized_cfo, int n_fft, int sample_rate)
{
    if (n_fft <= 0) return 0.0f;
    return normalized_cfo * (float)sample_rate / (float)n_fft;
}
