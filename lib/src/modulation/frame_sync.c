#include "frame_sync.h"
#include "training.h"
#include "ofdm.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------- 内部辅助 ---------- */

/* 环形缓冲区读取（允许负索引回绕） */
static float dbuf_read(const atcp_frame_sync_t *sync, int offset)
{
    int idx = (sync->delay_buf_pos + offset) % sync->delay_buf_size;
    if (idx < 0) idx += sync->delay_buf_size;
    return sync->delay_buf[idx];
}

/* ---------- 公开接口 ---------- */

atcp_status_t atcp_frame_sync_init(atcp_frame_sync_t *sync, const atcp_config_t *cfg)
{
    if (!sync || !cfg) return ATCP_ERR_INVALID_PARAM;

    memset(sync, 0, sizeof(*sync));

    /* 保存配置 */
    sync->n_fft      = cfg->n_fft;
    sync->cp_len     = cfg->cp_len;
    sync->n_train    = cfg->n_train;
    sync->sub_low    = cfg->sub_low;
    sync->sub_high   = cfg->sub_high;
    sync->train_seed = cfg->train_seed;
    sync->coarse_threshold = 0.8f;

    /* 延迟缓冲区大小 = n_fft + cp_len */
    sync->delay_buf_size = cfg->n_fft + cfg->cp_len;
    sync->delay_buf = (float *)malloc(sizeof(float) * (size_t)sync->delay_buf_size);
    if (!sync->delay_buf) return ATCP_ERR_NO_MEMORY;
    memset(sync->delay_buf, 0, sizeof(float) * (size_t)sync->delay_buf_size);

    /* 预生成训练符号时域波形 */
    sync->train_len = cfg->n_fft + cfg->cp_len;
    sync->train_time = (float *)malloc(sizeof(float) * (size_t)sync->train_len);
    if (!sync->train_time) {
        free(sync->delay_buf);
        sync->delay_buf = NULL;
        return ATCP_ERR_NO_MEMORY;
    }

    int tlen = 0;
    atcp_status_t st = atcp_training_generate_ofdm(cfg, sync->train_time, &tlen);
    if (st != ATCP_OK) {
        free(sync->delay_buf);
        free(sync->train_time);
        sync->delay_buf = NULL;
        sync->train_time = NULL;
        return st;
    }
    sync->train_len = tlen;

    sync->detected = ATCP_FALSE;
    sync->frame_offset = -1;
    sync->local_offset = 0;
    sync->sample_count = 0;
    sync->delay_buf_pos = 0;
    sync->p_re = 0.0f;
    sync->p_im = 0.0f;
    sync->r_val = 0.0f;

    return ATCP_OK;
}

void atcp_frame_sync_free(atcp_frame_sync_t *sync)
{
    if (!sync) return;
    if (sync->delay_buf) {
        free(sync->delay_buf);
        sync->delay_buf = NULL;
    }
    if (sync->train_time) {
        free(sync->train_time);
        sync->train_time = NULL;
    }
}

void atcp_frame_sync_reset(atcp_frame_sync_t *sync)
{
    if (!sync) return;
    sync->detected = ATCP_FALSE;
    sync->frame_offset = -1;
    sync->local_offset = 0;
    sync->sample_count = 0;
    sync->delay_buf_pos = 0;
    sync->p_re = 0.0f;
    sync->p_im = 0.0f;
    sync->r_val = 0.0f;
    if (sync->delay_buf)
        memset(sync->delay_buf, 0, sizeof(float) * (size_t)sync->delay_buf_size);
}

atcp_status_t atcp_frame_sync_feed(atcp_frame_sync_t *sync, float sample)
{
    if (!sync || !sync->delay_buf) return ATCP_ERR_INVALID_PARAM;
    if (sync->detected) return ATCP_OK; /* 已检测到，忽略后续 */

    int n_fft  = sync->n_fft;
    int cp_len = sync->cp_len;
    int buf_sz = sync->delay_buf_size; /* n_fft + cp_len */

    /* 写入新采样到环形缓冲区 */
    int pos = sync->delay_buf_pos;
    sync->delay_buf[pos] = sample;

    /* 推进环形缓冲区位置 */
    sync->delay_buf_pos = (pos + 1) % buf_sz;
    sync->sample_count++;

    /* 需要至少收集 buf_sz 个采样才开始检测 */
    if (sync->sample_count < buf_sz)
        return ATCP_OK;

    /* 直接从缓冲区计算 P(d) 和 R(d)
     * 缓冲区最老采样在 delay_buf_pos，对应 r(d)
     * P(d) = Σ_{k=0}^{cp_len-1} r(d+k) * r(d+k+n_fft)
     * R(d) = Σ_{k=0}^{cp_len-1} |r(d+k+n_fft)|²  (后半窗能量) */
    float p_val = 0.0f;
    float r_val = 0.0f;
    int base = sync->delay_buf_pos; /* 指向最老采样 r(d) */

    for (int k = 0; k < cp_len; k++) {
        int early_idx = (base + k) % buf_sz;
        int late_idx  = (base + k + n_fft) % buf_sz;
        float early = sync->delay_buf[early_idx];
        float late  = sync->delay_buf[late_idx];
        p_val += early * late;
        r_val += late * late;
    }

    /* 计算检测指标 M(d) = |P(d)|² / R(d)² */
    float p_abs2 = p_val * p_val;
    float r2 = r_val * r_val;
    if (r2 < 1e-12f) return ATCP_OK;

    float metric = p_abs2 / r2;

    /* 粗同步检测 */
    if (metric >= sync->coarse_threshold) {
        /* 候选粗位置 = sample_count - buf_sz (帧头候选) */
        int coarse_pos = sync->sample_count - buf_sz;

        /* 细同步：在候选位置 ±cp_len 范围搜索互相关峰值 */
        float best_corr = -1.0f;
        int best_tau = 0;
        int search_range = cp_len;
        int tlen = sync->train_len;

        for (int dt = -search_range; dt <= search_range; dt++) {
            float corr = 0.0f;

            for (int k = 0; k < tlen && k < buf_sz; k++) {
                int buf_offset = dt + k;
                if (buf_offset < 0 || buf_offset >= buf_sz) {
                    continue;
                }
                int ridx = (base + buf_offset) % buf_sz;
                if (ridx < 0) ridx += buf_sz;
                corr += sync->delay_buf[ridx] * sync->train_time[k];
            }

            float abs_corr = fabsf(corr);
            if (abs_corr > best_corr) {
                best_corr = abs_corr;
                best_tau = dt;
            }
        }

        sync->detected = ATCP_TRUE;
        sync->frame_offset = coarse_pos + best_tau;
    }

    return ATCP_OK;
}

atcp_status_t atcp_frame_sync_feed_batch(atcp_frame_sync_t *sync, const float *samples, int n)
{
    if (!sync || !samples) return ATCP_ERR_INVALID_PARAM;

    for (int i = 0; i < n; i++) {
        atcp_status_t st = atcp_frame_sync_feed(sync, samples[i]);
        if (st != ATCP_OK) return st;
        if (sync->detected) {
            sync->local_offset = i;
            break;
        }
    }
    return ATCP_OK;
}

atcp_bool_t atcp_frame_sync_detected(const atcp_frame_sync_t *sync)
{
    if (!sync) return ATCP_FALSE;
    return sync->detected;
}

int atcp_frame_sync_get_offset(const atcp_frame_sync_t *sync)
{
    if (!sync) return -1;
    return sync->frame_offset;
}

int atcp_frame_sync_get_local_offset(const atcp_frame_sync_t *sync)
{
    if (!sync) return 0;
    return sync->local_offset;
}
