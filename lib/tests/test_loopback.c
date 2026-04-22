#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "audiothief/config.h"
#include "audiothief/types.h"
#include "coding/reed_solomon.h"
#include "coding/crc32.h"
#include "modulation/qam.h"
#include "modulation/ofdm.h"
#include "modulation/training.h"
#include "modulation/channel_est.h"
#include "physical/diff_signal.h"
#include "common/math_utils.h"
#include "common/fft.h"

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

    printf("=== Full Loopback Test (no noise) ===\n");

    /* 1. 准备配置 */
    atcp_config_t cfg = atcp_config_default();
    int n_subs = cfg.sub_high - cfg.sub_low + 1;  /* 199 */
    int bps = atcp_qam_bits_per_symbol(cfg.qam_order);  /* 16-QAM -> 4 */
    int symbol_samples = cfg.n_fft + cfg.cp_len;  /* 544 */
    int bytes_per_symbol = n_subs * bps / 8;

    printf("  n_subs=%d, bps=%d, bytes_per_symbol=%d\n",
           n_subs, bps, bytes_per_symbol);

    /* 2. 原始数据 */
    const uint8_t original[] = "AudioThief Loopback Test - Hello World! 1234567890";
    int orig_len = (int)strlen((const char *)original);
    printf("  original data length = %d\n", orig_len);

    /* 3. CRC */
    atcp_crc32_init();
    uint32_t crc = atcp_crc32(original, orig_len);
    printf("  CRC32 = 0x%08X\n", crc);

    /* 4. RS编码 */
    atcp_rs_t rs;
    atcp_rs_init(&rs, cfg.rs_nsym);

    uint8_t coded[4096];
    int coded_len = 0;
    atcp_status_t s = atcp_rs_encode_blocks(&rs, original, orig_len, coded, &coded_len);
    TEST_ASSERT(s == ATCP_OK, "RS encode should succeed");
    printf("  RS coded length = %d\n", coded_len);

    /* 5. 字节->比特 (MSB first) */
    int n_bits_total = coded_len * 8;
    uint8_t *bits = (uint8_t *)malloc(n_bits_total);
    {
        int i, j;
        for (i = 0; i < coded_len; i++) {
            for (j = 0; j < 8; j++) {
                bits[i * 8 + j] = (coded[i] >> (7 - j)) & 1;
            }
        }
    }

    /* 需要padding到bps整数倍 */
    int bits_padded = n_bits_total;
    if (bits_padded % bps != 0) {
        bits_padded = ((bits_padded / bps) + 1) * bps;
    }
    uint8_t *bits_aligned = (uint8_t *)calloc(bits_padded, 1);
    memcpy(bits_aligned, bits, n_bits_total);
    /* 尾部已经是0 (calloc) */

    /* 6. QAM调制 */
    int n_qam_symbols = bits_padded / bps;
    atcp_complex_t *qam_symbols = (atcp_complex_t *)malloc(sizeof(atcp_complex_t) * n_qam_symbols);
    int n_sym_out = 0;
    s = atcp_qam_modulate(bits_aligned, bits_padded, cfg.qam_order, qam_symbols, &n_sym_out);
    TEST_ASSERT(s == ATCP_OK, "QAM modulate should succeed");
    TEST_ASSERT(n_sym_out == n_qam_symbols, "QAM symbol count should match");
    printf("  QAM symbols = %d\n", n_qam_symbols);

    /* 7. OFDM调制 - 按n_subs分组 */
    int n_ofdm_symbols = (n_qam_symbols + n_subs - 1) / n_subs;
    printf("  OFDM data symbols = %d\n", n_ofdm_symbols);

    /* 训练符号 */
    float *train_time = (float *)malloc(sizeof(float) * symbol_samples * cfg.n_train);
    int train_sym_len = 0;
    {
        float single_train[1024];
        int tl = 0;
        atcp_training_generate_ofdm(&cfg, single_train, &tl);
        train_sym_len = tl;
        int t;
        for (t = 0; t < cfg.n_train; t++) {
            memcpy(train_time + t * tl, single_train, tl * sizeof(float));
        }
    }

    /* 数据OFDM时域 */
    int total_data_samples = n_ofdm_symbols * symbol_samples;
    float *data_time = (float *)calloc(total_data_samples, sizeof(float));
    {
        /* 分组填充：不足n_subs的部分补零 */
        atcp_complex_t group[256]; /* n_subs最大200 */
        int sym_idx = 0;
        int ofdm_idx;
        for (ofdm_idx = 0; ofdm_idx < n_ofdm_symbols; ofdm_idx++) {
            int count = n_subs;
            if (sym_idx + count > n_qam_symbols)
                count = n_qam_symbols - sym_idx;

            memset(group, 0, sizeof(atcp_complex_t) * n_subs);
            if (count > 0)
                memcpy(group, qam_symbols + sym_idx, sizeof(atcp_complex_t) * count);

            int tl = 0;
            s = atcp_ofdm_modulate(group, n_subs, &cfg,
                                 data_time + ofdm_idx * symbol_samples, &tl);
            TEST_ASSERT(s == ATCP_OK, "OFDM modulate should succeed");
            sym_idx += count;
        }
    }

    /* 8. 组合信号: [训练符号] + [数据符号] */
    int total_train_samples = cfg.n_train * train_sym_len;
    int total_signal_len = total_train_samples + total_data_samples;
    float *tx_signal = (float *)malloc(sizeof(float) * total_signal_len);
    memcpy(tx_signal, train_time, sizeof(float) * total_train_samples);
    memcpy(tx_signal + total_train_samples, data_time, sizeof(float) * total_data_samples);

    /* 差分编码 */
    float *left  = (float *)malloc(sizeof(float) * total_signal_len);
    float *right = (float *)malloc(sizeof(float) * total_signal_len);
    s = atcp_diff_encode(tx_signal, total_signal_len, left, right);
    TEST_ASSERT(s == ATCP_OK, "diff encode should succeed");

    /* === 传输（直接内存，无噪声） === */

    /* 9. 差分解码 */
    float *rx_signal = (float *)malloc(sizeof(float) * total_signal_len);
    s = atcp_diff_decode(left, right, total_signal_len, rx_signal);
    TEST_ASSERT(s == ATCP_OK, "diff decode should succeed");

    /* 验证差分编解码往返 */
    {
        float max_err = 0.0f;
        int i;
        for (i = 0; i < total_signal_len; i++) {
            float err = fabsf(rx_signal[i] - tx_signal[i]);
            if (err > max_err) max_err = err;
        }
        printf("  diff encode/decode max error = %e\n", max_err);
        TEST_ASSERT(max_err < 1e-5f, "diff roundtrip should be lossless");
    }

    /* 10. 训练符号解调 -> 信道估计 */
    /* 用第一个训练符号做信道估计 */
    atcp_complex_t rx_train_freq[256];
    s = atcp_ofdm_demodulate(rx_signal, &cfg, rx_train_freq, n_subs);
    TEST_ASSERT(s == ATCP_OK, "OFDM demodulate training should succeed");

    atcp_complex_t tx_train_freq[256];
    atcp_training_generate(cfg.train_seed, n_subs, tx_train_freq);

    atcp_complex_t H[256];
    s = atcp_channel_estimate(rx_train_freq, tx_train_freq, n_subs, H);
    TEST_ASSERT(s == ATCP_OK, "channel estimate should succeed");

    /* 11. 数据OFDM解调 + 均衡 */
    atcp_complex_t *rx_qam = (atcp_complex_t *)malloc(sizeof(atcp_complex_t) * n_qam_symbols);
    int rx_qam_count = 0;
    {
        float *rx_data_start = rx_signal + total_train_samples;
        atcp_complex_t rx_freq[256];
        atcp_complex_t eq_freq[256];
        int ofdm_idx;
        for (ofdm_idx = 0; ofdm_idx < n_ofdm_symbols; ofdm_idx++) {
            s = atcp_ofdm_demodulate(rx_data_start + ofdm_idx * symbol_samples,
                                   &cfg, rx_freq, n_subs);
            TEST_ASSERT(s == ATCP_OK, "OFDM demodulate data should succeed");

            s = atcp_channel_equalize(rx_freq, H, n_subs, eq_freq);
            TEST_ASSERT(s == ATCP_OK, "channel equalize should succeed");

            int count = n_subs;
            if (rx_qam_count + count > n_qam_symbols)
                count = n_qam_symbols - rx_qam_count;
            if (count > 0)
                memcpy(rx_qam + rx_qam_count, eq_freq, sizeof(atcp_complex_t) * count);
            rx_qam_count += count;
        }
    }
    TEST_ASSERT(rx_qam_count == n_qam_symbols, "received QAM symbol count should match");

    /* 12. QAM解调 */
    uint8_t *rx_bits = (uint8_t *)malloc(bits_padded);
    int n_rx_bits = 0;
    s = atcp_qam_demodulate(rx_qam, n_qam_symbols, cfg.qam_order, rx_bits, &n_rx_bits);
    TEST_ASSERT(s == ATCP_OK, "QAM demodulate should succeed");
    TEST_ASSERT(n_rx_bits == bits_padded, "demodulated bit count should match");

    /* 13. 比特->字节 (MSB first) */
    int rx_coded_len = coded_len;
    uint8_t *rx_coded = (uint8_t *)calloc(rx_coded_len, 1);
    {
        int i, j;
        for (i = 0; i < rx_coded_len; i++) {
            uint8_t byte = 0;
            for (j = 0; j < 8; j++) {
                byte |= (rx_bits[i * 8 + j] & 1) << (7 - j);
            }
            rx_coded[i] = byte;
        }
    }

    /* 验证RS编码数据是否完整恢复 */
    TEST_ASSERT(memcmp(rx_coded, coded, coded_len) == 0,
                "coded data should match after modulation roundtrip");

    /* 14. RS解码 */
    uint8_t decoded[4096];
    int decoded_len = 0;
    s = atcp_rs_decode_blocks(&rs, rx_coded, rx_coded_len, decoded, &decoded_len);
    TEST_ASSERT(s == ATCP_OK, "RS decode should succeed");
    printf("  RS decoded length = %d\n", decoded_len);

    /* 15. CRC校验 */
    uint32_t rx_crc = atcp_crc32(decoded, decoded_len);
    printf("  RX CRC32 = 0x%08X\n", rx_crc);
    TEST_ASSERT(rx_crc == crc, "CRC match");

    /* 16. 数据比较 */
    TEST_ASSERT(decoded_len == orig_len, "length match");
    TEST_ASSERT(memcmp(decoded, original, orig_len) == 0, "data match");

    if (decoded_len == orig_len && memcmp(decoded, original, orig_len) == 0) {
        printf("  >> LOOPBACK SUCCESS: Data perfectly recovered! <<\n");
    }

    /* 清理 */
    free(bits);
    free(bits_aligned);
    free(qam_symbols);
    free(train_time);
    free(data_time);
    free(tx_signal);
    free(left);
    free(right);
    free(rx_signal);
    free(rx_qam);
    free(rx_bits);
    free(rx_coded);

    TEST_SUMMARY();
}
