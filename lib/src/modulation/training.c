#include "training.h"
#include "ofdm.h"
#include "../common/prng.h"

/* 最大子载波数（栈上缓冲区） */
#define ATCP_TRAINING_MAX_SUBS 512

void atcp_training_generate(uint32_t seed, int n_subs, atcp_complex_t *symbols_out)
{
    if (!symbols_out || n_subs <= 0) return;

    atcp_prng_t prng;
    atcp_prng_seed(&prng, seed);

    for (int i = 0; i < n_subs; i++) {
        symbols_out[i].re = atcp_prng_bpsk(&prng);
        symbols_out[i].im = 0.0f;
    }
}

atcp_status_t atcp_training_generate_ofdm(const atcp_config_t *cfg,
                                      float *time_out, int *time_len)
{
    if (!cfg || !time_out || !time_len)
        return ATCP_ERR_INVALID_PARAM;

    int n_subs = atcp_ofdm_num_subcarriers(cfg);
    if (n_subs <= 0 || n_subs > ATCP_TRAINING_MAX_SUBS)
        return ATCP_ERR_INVALID_PARAM;

    /* 生成BPSK频域训练符号 */
    atcp_complex_t freq[ATCP_TRAINING_MAX_SUBS];
    atcp_training_generate(cfg->train_seed, n_subs, freq);

    /* 通过OFDM调制转为时域 */
    return atcp_ofdm_modulate(freq, n_subs, cfg, time_out, time_len);
}
