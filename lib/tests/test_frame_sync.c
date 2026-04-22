#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "modulation/frame_sync.h"
#include "modulation/training.h"
#include "modulation/ofdm.h"
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

/* 简单伪随机 float [-1, 1] */
static float rand_float(unsigned int *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return ((float)(*seed & 0x7FFFFFFFu) / (float)0x7FFFFFFFu) * 2.0f - 1.0f;
}

/* 在指定偏移处注入训练符号并检测 */
static void test_sync_atcp_offset(int inject_offset, int *passes, int *failures)
{
    atcp_config_t cfg = atcp_config_default();
    int symbol_len = cfg.n_fft + cfg.cp_len;   /* 单个训练符号时域长度 */

    /* 生成训练OFDM时域符号 */
    float train_time[1024]; /* n_fft + cp_len = 544, 足够 */
    int train_len = 0;
    atcp_status_t s = atcp_training_generate_ofdm(&cfg, train_time, &train_len);
    TEST_ASSERT(s == ATCP_OK, "training generate should succeed");
    TEST_ASSERT(train_len == symbol_len, "training length should match symbol_len");

    /* 构造测试信号：[inject_offset个零] + [N_TRAIN个训练符号] + [500个随机数据] */
    int n_train = cfg.n_train;  /* 2 */
    int tail_len = 500;
    int total_len = inject_offset + n_train * train_len + tail_len;
    float *signal = (float *)calloc(total_len, sizeof(float));

    /* 注入训练符号 */
    int t;
    for (t = 0; t < n_train; t++) {
        memcpy(signal + inject_offset + t * train_len,
               train_time, train_len * sizeof(float));
    }

    /* 尾部填充随机数据 */
    unsigned int seed = 99999u;
    int i;
    for (i = inject_offset + n_train * train_len; i < total_len; i++) {
        signal[i] = rand_float(&seed) * 0.01f; /* 小幅噪声 */
    }

    /* 帧同步检测 */
    atcp_frame_sync_t sync;
    memset(&sync, 0, sizeof(sync));
    s = atcp_frame_sync_init(&sync, &cfg);
    TEST_ASSERT(s == ATCP_OK, "frame_sync_init should succeed");

    s = atcp_frame_sync_feed_batch(&sync, signal, total_len);

    TEST_ASSERT(atcp_frame_sync_detected(&sync), "frame should be detected");

    int detected_offset = atcp_frame_sync_get_offset(&sync);
    int diff = detected_offset - inject_offset;
    if (diff < 0) diff = -diff;
    char msg[128];
    sprintf(msg, "frame sync offset error=%d (expected ~%d, got %d)",
            diff, inject_offset, detected_offset);
    TEST_ASSERT(diff < 10, msg);

    atcp_frame_sync_free(&sync);
    free(signal);
}

int main(void)
{
    int passes = 0, failures = 0;

    /* Test 1: 基本检测 - 偏移1000处注入训练符号 */
    printf("--- Test 1: Basic frame sync detection at offset 1000 ---\n");
    test_sync_atcp_offset(1000, &passes, &failures);

    /* Test 2: 不同偏移 */
    printf("--- Test 2: Frame sync at offset 500 ---\n");
    test_sync_atcp_offset(500, &passes, &failures);

    printf("--- Test 2b: Frame sync at offset 2000 ---\n");
    test_sync_atcp_offset(2000, &passes, &failures);

    /* Test 3: 无训练符号 - 不应检测到帧 */
    printf("--- Test 3: No training symbols (noise only) ---\n");
    {
        atcp_config_t cfg = atcp_config_default();
        atcp_frame_sync_t sync;
        memset(&sync, 0, sizeof(sync));
        atcp_status_t s = atcp_frame_sync_init(&sync, &cfg);
        TEST_ASSERT(s == ATCP_OK, "frame_sync_init should succeed");

        /* 全噪声信号 */
        unsigned int seed = 54321u;
        int i;
        for (i = 0; i < 5000; i++) {
            float sample = rand_float(&seed) * 0.05f;
            atcp_frame_sync_feed(&sync, sample);
        }

        TEST_ASSERT(!atcp_frame_sync_detected(&sync), "no false positive on noise");

        atcp_frame_sync_free(&sync);
    }

    /* Test 4: 重置后重新检测 */
    printf("--- Test 4: Reset and re-detect ---\n");
    {
        atcp_config_t cfg = atcp_config_default();
        int symbol_len = cfg.n_fft + cfg.cp_len;
        int n_train = cfg.n_train;

        float train_time[1024];
        int train_len = 0;
        atcp_training_generate_ofdm(&cfg, train_time, &train_len);

        /* 第一次检测 */
        int inject1 = 800;
        int total1 = inject1 + n_train * train_len + 200;
        float *sig1 = (float *)calloc(total1, sizeof(float));
        int t;
        for (t = 0; t < n_train; t++) {
            memcpy(sig1 + inject1 + t * train_len,
                   train_time, train_len * sizeof(float));
        }

        atcp_frame_sync_t sync;
        memset(&sync, 0, sizeof(sync));
        atcp_frame_sync_init(&sync, &cfg);
        atcp_frame_sync_feed_batch(&sync, sig1, total1);
        TEST_ASSERT(atcp_frame_sync_detected(&sync), "first detection should succeed");

        /* 重置 */
        atcp_frame_sync_reset(&sync);
        TEST_ASSERT(!atcp_frame_sync_detected(&sync), "after reset, not detected");

        /* 第二次检测，不同偏移 */
        int inject2 = 600;
        int total2 = inject2 + n_train * train_len + 200;
        float *sig2 = (float *)calloc(total2, sizeof(float));
        for (t = 0; t < n_train; t++) {
            memcpy(sig2 + inject2 + t * train_len,
                   train_time, train_len * sizeof(float));
        }

        atcp_frame_sync_feed_batch(&sync, sig2, total2);
        TEST_ASSERT(atcp_frame_sync_detected(&sync), "re-detection after reset should succeed");

        atcp_frame_sync_free(&sync);
        free(sig1);
        free(sig2);
    }

    TEST_SUMMARY();
}
