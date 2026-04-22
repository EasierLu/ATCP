#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "coding/reed_solomon.h"
#include "coding/gf256.h"

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

int main(void)
{
    int passes = 0, failures = 0;

    gf256_init();

    atcp_rs_t rs;
    atcp_status_t s = atcp_rs_init(&rs, 48);
    TEST_ASSERT(s == ATCP_OK, "RS init should succeed");

    const char *test_msg = "Hello, AudioThief RS Test!";
    int msg_len = (int)strlen(test_msg);

    /* Test 1: No errors */
    printf("--- Test 1: RS no-error decode ---\n");
    {
        uint8_t codeword[255];
        uint8_t decoded[255];
        int decoded_len = 0;

        s = atcp_rs_encode(&rs, (const uint8_t *)test_msg, msg_len, codeword);
        TEST_ASSERT(s == ATCP_OK, "RS encode should succeed");

        s = atcp_rs_decode(&rs, codeword, decoded, &decoded_len);
        TEST_ASSERT(s == ATCP_OK, "RS decode (no errors) should succeed");
        TEST_ASSERT(decoded_len == msg_len, "Decoded length should match");
        TEST_ASSERT(memcmp(decoded, test_msg, msg_len) == 0, "RS no-error decode");
    }

    /* Test 2: Correctable errors (inject t = nsym/2 = 24 errors) */
    printf("--- Test 2: RS correctable errors ---\n");
    {
        uint8_t codeword[255];
        uint8_t decoded[255];
        int decoded_len = 0;
        int t = rs.nsym / 2; /* 24 */

        s = atcp_rs_encode(&rs, (const uint8_t *)test_msg, msg_len, codeword);
        TEST_ASSERT(s == ATCP_OK, "RS encode should succeed");

        /* Inject t errors at pseudo-random positions */
        unsigned int seed = 42u;
        uint8_t used[255] = {0};
        int injected = 0;
        while (injected < t) {
            seed = seed * 1103515245u + 12345u;
            int pos = (int)(seed % 255u);
            if (!used[pos]) {
                used[pos] = 1;
                codeword[pos] ^= (uint8_t)((seed >> 8) & 0xFF) | 1; /* ensure non-zero flip */
                injected++;
            }
        }

        s = atcp_rs_decode(&rs, codeword, decoded, &decoded_len);
        TEST_ASSERT(s == ATCP_OK, "RS decode with t errors should succeed");
        TEST_ASSERT(decoded_len == msg_len, "Decoded length should match");
        TEST_ASSERT(memcmp(decoded, test_msg, msg_len) == 0, "RS correctable errors decode");
    }

    /* Test 3: Beyond correction capability (t+1 = 25 errors) */
    printf("--- Test 3: RS beyond correction ---\n");
    {
        uint8_t codeword[255];
        uint8_t decoded[255];
        int decoded_len = 0;
        int t_plus_1 = rs.nsym / 2 + 1; /* 25 */

        s = atcp_rs_encode(&rs, (const uint8_t *)test_msg, msg_len, codeword);
        TEST_ASSERT(s == ATCP_OK, "RS encode should succeed");

        unsigned int seed = 99u;
        uint8_t used[255] = {0};
        int injected = 0;
        while (injected < t_plus_1) {
            seed = seed * 1103515245u + 12345u;
            int pos = (int)(seed % 255u);
            if (!used[pos]) {
                used[pos] = 1;
                codeword[pos] ^= (uint8_t)((seed >> 8) & 0xFF) | 1;
                injected++;
            }
        }

        s = atcp_rs_decode(&rs, codeword, decoded, &decoded_len);
        TEST_ASSERT(s == ATCP_ERR_RS_DECODE_FAIL, "RS decode with t+1 errors should fail");
    }

    /* Test 4: Block encode/decode (300 bytes, needs 2 blocks) */
    printf("--- Test 4: RS block encode/decode ---\n");
    {
        int data_len = 300;
        uint8_t *data = (uint8_t *)malloc(data_len);
        int i;
        for (i = 0; i < data_len; i++) {
            data[i] = (uint8_t)(i & 0xFF);
        }

        /* max_data_per_block = 255 - 48 = 207, so 300 bytes -> 2 blocks */
        /* encoded size: 2 * 255 = 510 bytes max */
        uint8_t *coded = (uint8_t *)malloc(510);
        int coded_len = 0;

        s = atcp_rs_encode_blocks(&rs, data, data_len, coded, &coded_len);
        TEST_ASSERT(s == ATCP_OK, "RS block encode should succeed");
        TEST_ASSERT(coded_len == 2 * 255, "Coded length should be 2*255");

        uint8_t *decoded = (uint8_t *)malloc(data_len + 256);
        int decoded_len = 0;

        s = atcp_rs_decode_blocks(&rs, coded, coded_len, decoded, &decoded_len);
        TEST_ASSERT(s == ATCP_OK, "RS block decode should succeed");
        TEST_ASSERT(decoded_len == data_len, "Decoded length should match original");
        TEST_ASSERT(memcmp(decoded, data, data_len) == 0, "RS block roundtrip data");

        free(data);
        free(coded);
        free(decoded);
    }

    /* Test 5: Boundary cases */
    printf("--- Test 5: RS boundary cases ---\n");
    {
        /* 5a: Empty data (len=0) */
        {
            uint8_t codeword[255];
            s = atcp_rs_encode(&rs, NULL, 0, codeword);
            /* Might succeed with empty payload or return error — just don't crash */
            printf("  RS encode len=0: status=%d\n", s);
        }

        /* 5b: Exactly 206 bytes (single block max with length prefix) */
        {
            int data_len = 255 - 48 - 1; /* 206 */
            uint8_t *data = (uint8_t *)malloc(data_len);
            uint8_t codeword[255];
            uint8_t decoded[255];
            int decoded_len = 0;
            int i;

            for (i = 0; i < data_len; i++) data[i] = (uint8_t)(i & 0xFF);

            s = atcp_rs_encode(&rs, data, data_len, codeword);
            TEST_ASSERT(s == ATCP_OK, "RS encode 206 bytes should succeed");

            s = atcp_rs_decode(&rs, codeword, decoded, &decoded_len);
            TEST_ASSERT(s == ATCP_OK, "RS decode 206 bytes should succeed");
            TEST_ASSERT(decoded_len == data_len, "Decoded len should be 206");
            TEST_ASSERT(memcmp(decoded, data, data_len) == 0, "RS 206-byte roundtrip");

            free(data);
        }

        /* 5c: 208 bytes (just needs 2 blocks) */
        {
            int data_len = 208;
            uint8_t *data = (uint8_t *)malloc(data_len);
            uint8_t *coded = (uint8_t *)malloc(510);
            uint8_t *decoded = (uint8_t *)malloc(data_len + 256);
            int coded_len = 0, decoded_len = 0;
            int i;

            for (i = 0; i < data_len; i++) data[i] = (uint8_t)((i * 7) & 0xFF);

            s = atcp_rs_encode_blocks(&rs, data, data_len, coded, &coded_len);
            TEST_ASSERT(s == ATCP_OK, "RS block encode 208 bytes should succeed");
            TEST_ASSERT(coded_len == 2 * 255, "208 bytes should need 2 blocks");

            s = atcp_rs_decode_blocks(&rs, coded, coded_len, decoded, &decoded_len);
            TEST_ASSERT(s == ATCP_OK, "RS block decode 208 bytes should succeed");
            TEST_ASSERT(decoded_len == data_len, "Decoded len should be 208");
            TEST_ASSERT(memcmp(decoded, data, data_len) == 0, "RS 208-byte block roundtrip");

            free(data);
            free(coded);
            free(decoded);
        }
    }

    TEST_SUMMARY();
}
