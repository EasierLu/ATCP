#include "math_utils.h"
#include <math.h>

/* ---- 复数运算 ---- */

atcp_complex_t atcp_complex_mul(atcp_complex_t a, atcp_complex_t b)
{
    atcp_complex_t r;
    r.re = a.re * b.re - a.im * b.im;
    r.im = a.re * b.im + a.im * b.re;
    return r;
}

atcp_complex_t atcp_complex_conj(atcp_complex_t a)
{
    atcp_complex_t r;
    r.re = a.re;
    r.im = -a.im;
    return r;
}

atcp_complex_t atcp_complex_add(atcp_complex_t a, atcp_complex_t b)
{
    atcp_complex_t r;
    r.re = a.re + b.re;
    r.im = a.im + b.im;
    return r;
}

atcp_complex_t atcp_complex_sub(atcp_complex_t a, atcp_complex_t b)
{
    atcp_complex_t r;
    r.re = a.re - b.re;
    r.im = a.im - b.im;
    return r;
}

float atcp_complex_abs(atcp_complex_t a)
{
    return sqrtf(a.re * a.re + a.im * a.im);
}

float atcp_complex_abs2(atcp_complex_t a)
{
    return a.re * a.re + a.im * a.im;
}

float atcp_complex_arg(atcp_complex_t a)
{
    return atan2f(a.im, a.re);
}

atcp_complex_t atcp_complex_from_polar(float mag, float phase)
{
    atcp_complex_t r;
    r.re = mag * cosf(phase);
    r.im = mag * sinf(phase);
    return r;
}

/* ---- 信号工具 ---- */

float atcp_power_rms(const float *samples, int n)
{
    float sum = 0.0f;
    int i;
    if (!samples || n <= 0) return 0.0f;
    for (i = 0; i < n; i++)
        sum += samples[i] * samples[i];
    return sqrtf(sum / (float)n);
}

void atcp_normalize(float *samples, int n, float target_amp)
{
    float peak = 0.0f;
    float scale;
    int i;
    if (!samples || n <= 0) return;
    for (i = 0; i < n; i++) {
        float v = samples[i] < 0 ? -samples[i] : samples[i];
        if (v > peak) peak = v;
    }
    if (peak < 1e-12f) return;  /* 避免除零 */
    scale = target_amp / peak;
    for (i = 0; i < n; i++)
        samples[i] *= scale;
}

float atcp_db_to_linear(float db)
{
    return powf(10.0f, db / 20.0f);
}

float atcp_linear_to_db(float linear)
{
    if (linear < 1e-12f) linear = 1e-12f;  /* 避免 log(0) */
    return 20.0f * log10f(linear);
}
