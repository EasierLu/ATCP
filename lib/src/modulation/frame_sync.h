#ifndef ATCP_FRAME_SYNC_H
#define ATCP_FRAME_SYNC_H
#include "../../include/atcp/types.h"
#include "../../include/atcp/config.h"

/* 帧同步状态 */
typedef struct {
    /* 配置 */
    int n_fft;
    int cp_len;
    int n_train;
    int sub_low;
    int sub_high;
    uint32_t train_seed;

    /* 粗同步状态 */
    float *delay_buf;       /* 延迟缓冲区，大小 n_fft + cp_len */
    int delay_buf_size;
    int delay_buf_pos;
    float p_re, p_im;       /* P(d) 的实部虚部（增量更新） */
    float r_val;             /* R(d)（增量更新） */
    float coarse_threshold;  /* 粗同步门限，典型0.8 */

    /* 细同步状态 */
    float *train_time;       /* 预生成的训练符号时域波形 */
    int train_len;           /* 训练符号时域长度 */

    /* 检测结果 */
    atcp_bool_t detected;
    int frame_offset;        /* 检测到的帧起始位置（采样偏移） */
    int local_offset;        /* 在最近一次feed_batch中的局部偏移 */
    int sample_count;        /* 已输入的总采样数 */
} atcp_frame_sync_t;

/* 初始化（需要外部提供缓冲区内存或内部malloc） */
atcp_status_t atcp_frame_sync_init(atcp_frame_sync_t *sync, const atcp_config_t *cfg);

/* 释放内部资源 */
void atcp_frame_sync_free(atcp_frame_sync_t *sync);

/* 重置状态（保留配置） */
void atcp_frame_sync_reset(atcp_frame_sync_t *sync);

/* 逐采样输入 */
atcp_status_t atcp_frame_sync_feed(atcp_frame_sync_t *sync, float sample);

/* 批量输入 */
atcp_status_t atcp_frame_sync_feed_batch(atcp_frame_sync_t *sync, const float *samples, int n);

/* 查询是否检测到帧 */
atcp_bool_t atcp_frame_sync_detected(const atcp_frame_sync_t *sync);

/* 获取检测到的帧起始偏移 */
int atcp_frame_sync_get_offset(const atcp_frame_sync_t *sync);

/* 获取帧起始位置在最近一次 feed_batch 中的局部偏移
 * 仅在 atcp_frame_sync_detected() 返回 ATCP_TRUE 时有效 */
int atcp_frame_sync_get_local_offset(const atcp_frame_sync_t *sync);

#endif
