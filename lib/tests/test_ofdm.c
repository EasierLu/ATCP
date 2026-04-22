#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "modulation/ofdm.h"
#include "modulation/qam.h"
#include "atcp/config.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (line %d): %s\n", __FILE__, __LINE__, msg); \
        failures++; \
    } else { \
        passes++; \
    } \
} while(0)

#define TEST_SUMMARY() do { \
    printf("\n=== %d passed, %d failed ===\n", passes, failures); \
    return failures > 0 ? 1 : 0; \
} while(0)

static float rand_float(unsigned int *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return ((float)(*seed & 0x7FFFFFFFu) / (float)0x7FFFFFFFu) * 2.0f - 1.0f;
}

int main(void)
{
    int passes = 0, failures = 0;

    atcp_config_t cfg = atcp_config_default();
    int n_subs = atcp_ofdm_num_subcarriers(&cfg);
    int sym_samples = atcp_ofdm_symbol_samples(&cfg);

    printf("OFDM config: n_fft=%d, cp_len=%d, n_subs=%d, sym_samples=%d\n",
           cfg.n_fft, cfg.cp_len, n_subs, sym_samples);

    /* Test 1: Modulate-demodulate roundtrip (no noise) */
    printf("--- Test 1: OFDM roundtrip ---\n");
    {
        atcp_complex_t *freq_in  = (atcp_complex_t *)malloc(sizeof(atcp_complex_t) * n_subs);
        atcp_complex_t *freq_out = (atcp_complex_t *)malloc(sizeof(atcp_complex_t) * n_subs);
        float *time_buf = (float *)malloc(sizeof(float) * sym_samples);
        int time_len = 0;
        int i;

        /* Generate random QPSK symbols as freq domain input */
        unsigned int seed = 77777u;
        for (i = 0; i < n_subs; i++) {
            /* Simple random QPSK: pick one of 4 constellation points */
            float v1 = rand_float(&seed) > 0 ? 1.0f : -1.0f;
            float v2 = rand_float(&seed) > 0 ? 1.0f : -1.0f;
            float norm = 1.0f / sqrtf(2.0f);
            freq_in[i].re = v1 * norm;
            freq_in[i].im = v2 * norm;
        }

        atcp_status_t s1 = atcp_ofdm_modulate(freq_in, n_subs, &cfg, time_buf, &time_len);
        TEST_ASSERT(s1 == ATCP_OK, "OFDM modulate should succeed");
        TEST_ASSERT(time_len == sym_samples, "time_len should match sym_samples");

        atcp_status_t s2 = atcp_ofdm_demodulate(time_buf, &cfg, freq_out, n_subs);
        TEST_ASSERT(s2 == ATCP_OK, "OFDM demodulate should succeed");

        for (i = 0; i < n_subs; i++) {
            TEST_ASSERT(fabsf(freq_out[i].re - freq_in[i].re) < 1e-3f, "OFDM roundtrip re");
            TEST_ASSERT(fabsf(freq_out[i].im - freq_in[i].im) < 1e-3f, "OFDM roundtrip im");
        }

        free(freq_in);
        free(freq_out);
        free(time_buf);
    }

    /* Test 2: Output length verification */
    printf("--- Test 2: Output length ---\n");
    {
        int expected_len = cfg.n_fft + cfg.cp_len;
        TEST_ASSERT(sym_samples == expected_len, "sym_samples == n_fft + cp_len");
    }

    /* Test 3: CP correctness — CP is copy of the tail of the data block */
    printf("--- Test 3: CP correctness ---\n");
    {
        atcp_complex_t *freq_in = (atcp_complex_t *)malloc(sizeof(atcp_complex_t) * n_subs);
        float *time_buf = (float *)malloc(sizeof(float) * sym_samples);
        int time_len = 0;
        int i;

        /* Fill with simple known symbols */
        for (i = 0; i < n_subs; i++) {
            freq_in[i].re = (float)(i + 1) / (float)n_subs;
            freq_in[i].im = 0.0f;
        }

        atcp_status_t s = atcp_ofdm_modulate(freq_in, n_subs, &cfg, time_buf, &time_len);
        TEST_ASSERT(s == ATCP_OK, "OFDM modulate for CP test should succeed");

        /* CP = time_buf[0 .. cp_len-1] should equal time_buf[n_fft .. n_fft+cp_len-1] */
        int cp_ok = 1;
        for (i = 0; i < cfg.cp_len; i++) {
            if (fabsf(time_buf[i] - time_buf[cfg.n_fft + i]) > 1e-6f) {
                cp_ok = 0;
                break;
            }
        }
        TEST_ASSERT(cp_ok, "CP should be copy of data block tail");

        free(freq_in);
        free(time_buf);
    }

    TEST_SUMMARY();
}
