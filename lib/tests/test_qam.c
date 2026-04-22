#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "modulation/qam.h"

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

/* 将整数val的低bps位写入bits数组 */
static void int_to_bits(int val, uint8_t *bits, int bps)
{
    int i;
    for (i = 0; i < bps; i++) {
        bits[i] = (val >> (bps - 1 - i)) & 1;
    }
}

int main(void)
{
    int passes = 0, failures = 0;

    /* Test: Modulate/demodulate roundtrip for each QAM order */
    {
        int orders[] = {4, 16, 64, 256};
        int oi;
        for (oi = 0; oi < 4; oi++) {
            int order = orders[oi];
            int bps = atcp_qam_bits_per_symbol(order);
            int n_patterns = 1 << bps;
            printf("--- QAM-%d roundtrip (%d patterns, %d bits/sym) ---\n",
                   order, n_patterns, bps);

            uint8_t bits_in[8];   /* max 8 bits for QAM-256 */
            uint8_t bits_out[8];
            atcp_complex_t sym;
            int n_sym, n_bits_out;
            int p;

            for (p = 0; p < n_patterns; p++) {
                int_to_bits(p, bits_in, bps);

                atcp_status_t s1 = atcp_qam_modulate(bits_in, bps, order, &sym, &n_sym);
                TEST_ASSERT(s1 == ATCP_OK, "QAM modulate should succeed");
                TEST_ASSERT(n_sym == 1, "Should produce 1 symbol");

                atcp_status_t s2 = atcp_qam_demodulate(&sym, 1, order, bits_out, &n_bits_out);
                TEST_ASSERT(s2 == ATCP_OK, "QAM demodulate should succeed");
                TEST_ASSERT(n_bits_out == bps, "Should output correct number of bits");

                int j;
                for (j = 0; j < bps; j++) {
                    TEST_ASSERT(bits_in[j] == bits_out[j], "Bit mismatch in roundtrip");
                }
            }
        }
    }

    /* Test: QPSK normalization — average power ~= 1.0 */
    printf("--- QPSK normalization ---\n");
    {
        int bps = atcp_qam_bits_per_symbol(4);
        int n_patterns = 1 << bps; /* 4 */
        float total_power = 0.0f;
        int p;

        for (p = 0; p < n_patterns; p++) {
            uint8_t bits[2];
            atcp_complex_t sym;
            int n_sym;
            int_to_bits(p, bits, bps);
            atcp_qam_modulate(bits, bps, 4, &sym, &n_sym);
            total_power += sym.re * sym.re + sym.im * sym.im;
        }
        float avg_power = total_power / (float)n_patterns;
        printf("  QPSK avg power = %f\n", avg_power);
        TEST_ASSERT(fabsf(avg_power - 1.0f) < 0.1f, "QPSK avg power should be ~1.0");
    }

    /* Test: Parameter validation */
    printf("--- QAM parameter validation ---\n");
    {
        uint8_t bits[8] = {0};
        atcp_complex_t sym;
        int n_sym;

        atcp_status_t s1 = atcp_qam_modulate(bits, 2, 3, &sym, &n_sym);
        TEST_ASSERT(s1 != ATCP_OK, "order=3 should return error");

        atcp_status_t s2 = atcp_qam_modulate(bits, 2, 512, &sym, &n_sym);
        TEST_ASSERT(s2 != ATCP_OK, "order=512 should return error");
    }

    TEST_SUMMARY();
}
