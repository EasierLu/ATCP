#include "channel_est.h"
#include <string.h>

/* 复数除法: a / b = a * conj(b) / |b|² */
static atcp_complex_t cdiv(atcp_complex_t a, atcp_complex_t b)
{
    float denom = b.re * b.re + b.im * b.im;
    atcp_complex_t r;
    if (denom < 1e-12f) {
        r.re = 0.0f;
        r.im = 0.0f;
    } else {
        r.re = (a.re * b.re + a.im * b.im) / denom;
        r.im = (a.im * b.re - a.re * b.im) / denom;
    }
    return r;
}

atcp_status_t atcp_channel_estimate(const atcp_complex_t *rx_train, const atcp_complex_t *tx_train,
                                int n_subs, atcp_complex_t *H_out)
{
    if (!rx_train || !tx_train || !H_out || n_subs <= 0)
        return ATCP_ERR_INVALID_PARAM;

    /* H(k) = Rx(k) / Tx(k)
     * 训练符号是BPSK(±1+0j)，Tx(k).im=0，所以除法简化为：
     * H(k).re = Rx(k).re * Tx(k).re  (因为 1/±1 = ±1)
     * H(k).im = Rx(k).im * Tx(k).re
     * 但为通用性仍使用复数除法 */
    for (int k = 0; k < n_subs; k++) {
        H_out[k] = cdiv(rx_train[k], tx_train[k]);
    }

    return ATCP_OK;
}

atcp_status_t atcp_channel_estimate_avg(const atcp_complex_t *rx_trains[], const atcp_complex_t *tx_train,
                                    int n_trains, int n_subs, atcp_complex_t *H_out)
{
    if (!rx_trains || !tx_train || !H_out || n_trains <= 0 || n_subs <= 0)
        return ATCP_ERR_INVALID_PARAM;

    /* 先清零累加器 */
    memset(H_out, 0, sizeof(atcp_complex_t) * (size_t)n_subs);

    float inv_n = 1.0f / (float)n_trains;

    for (int t = 0; t < n_trains; t++) {
        if (!rx_trains[t]) return ATCP_ERR_INVALID_PARAM;
        for (int k = 0; k < n_subs; k++) {
            atcp_complex_t h = cdiv(rx_trains[t][k], tx_train[k]);
            H_out[k].re += h.re;
            H_out[k].im += h.im;
        }
    }

    /* 求平均 */
    for (int k = 0; k < n_subs; k++) {
        H_out[k].re *= inv_n;
        H_out[k].im *= inv_n;
    }

    return ATCP_OK;
}

atcp_status_t atcp_channel_equalize(const atcp_complex_t *rx_data, const atcp_complex_t *H,
                                int n_subs, atcp_complex_t *eq_out)
{
    if (!rx_data || !H || !eq_out || n_subs <= 0)
        return ATCP_ERR_INVALID_PARAM;

    for (int k = 0; k < n_subs; k++) {
        float h_abs2 = H[k].re * H[k].re + H[k].im * H[k].im;
        if (h_abs2 < 1e-12f) {
            /* H(k) ≈ 0，该子载波不可靠，输出零 */
            eq_out[k].re = 0.0f;
            eq_out[k].im = 0.0f;
        } else {
            eq_out[k] = cdiv(rx_data[k], H[k]);
        }
    }

    return ATCP_OK;
}

atcp_status_t atcp_channel_update(atcp_complex_t *H, const atcp_complex_t *rx_pilot,
                              const atcp_complex_t *tx_pilot, int n_subs, float alpha)
{
    if (!H || !rx_pilot || !tx_pilot || n_subs <= 0)
        return ATCP_ERR_INVALID_PARAM;
    if (alpha < 0.0f || alpha > 1.0f)
        return ATCP_ERR_INVALID_PARAM;

    float beta = 1.0f - alpha;

    for (int k = 0; k < n_subs; k++) {
        atcp_complex_t h_new = cdiv(rx_pilot[k], tx_pilot[k]);
        H[k].re = alpha * h_new.re + beta * H[k].re;
        H[k].im = alpha * h_new.im + beta * H[k].im;
    }

    return ATCP_OK;
}
