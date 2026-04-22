#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "common/fft.h"
#include "common/math_utils.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

/* 简单伪随机 float [-1, 1] */
static float rand_float(unsigned int *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return ((float)(*seed & 0x7FFFFFFFu) / (float)0x7FFFFFFFu) * 2.0f - 1.0f;
}

static void test_fft_ifft_roundtrip(int N, int *passes, int *failures)
{
    atcp_complex_t *buf  = (atcp_complex_t *)malloc(sizeof(atcp_complex_t) * N);
    atcp_complex_t *orig = (atcp_complex_t *)malloc(sizeof(atcp_complex_t) * N);
    unsigned int seed = 12345u;
    int i;

    for (i = 0; i < N; i++) {
        buf[i].re = rand_float(&seed);
        buf[i].im = rand_float(&seed);
        orig[i] = buf[i];
    }

    atcp_status_t s1 = atcp_fft(buf, N);
    TEST_ASSERT(s1 == ATCP_OK, "FFT should succeed");

    atcp_status_t s2 = atcp_ifft(buf, N);
    TEST_ASSERT(s2 == ATCP_OK, "IFFT should succeed");

    for (i = 0; i < N; i++) {
        TEST_ASSERT(fabsf(buf[i].re - orig[i].re) < 1e-4f, "FFT/IFFT roundtrip re");
        TEST_ASSERT(fabsf(buf[i].im - orig[i].im) < 1e-4f, "FFT/IFFT roundtrip im");
    }

    free(buf);
    free(orig);
}

int main(void)
{
    int passes = 0, failures = 0;

    /* Test 1: FFT/IFFT roundtrip N=512 */
    printf("--- Test 1: FFT/IFFT roundtrip N=512 ---\n");
    test_fft_ifft_roundtrip(512, &passes, &failures);

    /* Test 2: Pure cosine signal */
    printf("--- Test 2: Pure cosine signal ---\n");
    {
        const int N = 256;
        const int k0 = 10;
        atcp_complex_t *buf = (atcp_complex_t *)malloc(sizeof(atcp_complex_t) * N);
        int i;

        for (i = 0; i < N; i++) {
            buf[i].re = cosf(2.0f * (float)M_PI * k0 * i / N);
            buf[i].im = 0.0f;
        }

        atcp_status_t s = atcp_fft(buf, N);
        TEST_ASSERT(s == ATCP_OK, "FFT of cosine should succeed");

        /* Find the max magnitude */
        float mag_k0 = atcp_complex_abs(buf[k0]);
        float mag_Nk0 = atcp_complex_abs(buf[N - k0]);

        /* k0 and N-k0 bins should have the largest magnitudes */
        int k0_is_max = 1;
        int Nk0_is_max = 1;
        for (i = 0; i < N; i++) {
            if (i == k0 || i == N - k0) continue;
            float m = atcp_complex_abs(buf[i]);
            if (m > mag_k0 * 0.1f) k0_is_max = 0;
            if (m > mag_Nk0 * 0.1f) Nk0_is_max = 0;
        }
        TEST_ASSERT(mag_k0 > 1.0f, "Bin k0 should have significant magnitude");
        TEST_ASSERT(mag_Nk0 > 1.0f, "Bin N-k0 should have significant magnitude");
        TEST_ASSERT(k0_is_max, "Other bins should be small compared to k0");
        TEST_ASSERT(Nk0_is_max, "Other bins should be small compared to N-k0");

        free(buf);
    }

    /* Test 3: Different N values */
    printf("--- Test 3: Different N values ---\n");
    {
        int sizes[] = {128, 256, 512, 1024};
        int s;
        for (s = 0; s < 4; s++) {
            printf("  N=%d\n", sizes[s]);
            test_fft_ifft_roundtrip(sizes[s], &passes, &failures);
        }
    }

    /* Test 4: Parameter validation */
    printf("--- Test 4: Parameter validation ---\n");
    {
        atcp_complex_t dummy[128];
        memset(dummy, 0, sizeof(dummy));

        atcp_status_t s1 = atcp_fft(dummy, 100); /* non-power-of-2 */
        TEST_ASSERT(s1 == ATCP_ERR_INVALID_PARAM, "N=100 should return INVALID_PARAM");

        atcp_status_t s2 = atcp_fft(dummy, 0);
        TEST_ASSERT(s2 == ATCP_ERR_INVALID_PARAM, "N=0 should return INVALID_PARAM");
    }

    TEST_SUMMARY();
}
