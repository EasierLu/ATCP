#ifndef ATCP_HANDSHAKE_H
#define ATCP_HANDSHAKE_H
#include "../../include/audiothief/types.h"
#include "../../include/audiothief/config.h"

/* 握手阶段 */
typedef enum {
    ATCP_HS_IDLE = 0,
    ATCP_HS_PHASE1_SENT,      /* 发起方已发Phase1 */
    ATCP_HS_PHASE1_RECEIVED,  /* 响应方已收Phase1 */
    ATCP_HS_PHASE2_SENT,
    ATCP_HS_PHASE2_RECEIVED,
    ATCP_HS_PHASE3_TESTING,   /* 链路质量测试中 */
    ATCP_HS_PHASE3_DONE,
    ATCP_HS_PHASE4_SENT,
    ATCP_HS_PHASE4_RECEIVED,
    ATCP_HS_CONNECTED,
    ATCP_HS_FAILED,
} atcp_handshake_state_t;

/* 握手协商参数 */
typedef struct {
    int sample_rate;
    int qam_order;
    int n_fft;
    int cp_len;
    int sub_low;
    int sub_high;
    int rs_nsym;
} atcp_handshake_params_t;

/* 握手上下文 */
typedef struct {
    atcp_handshake_state_t state;
    atcp_handshake_params_t local_caps;   /* 本地能力 */
    atcp_handshake_params_t remote_caps;  /* 对端能力 */
    atcp_handshake_params_t negotiated;   /* 协商结果 */
    int retry_count;
    int max_retries;
    float measured_ber;
    float measured_loss_rate;
} atcp_handshake_t;

/* 初始化握手上下文 */
void atcp_handshake_init(atcp_handshake_t *hs, const atcp_config_t *cfg);

/* 发起方：生成Phase1消息（QPSK低速）
 * msg_out: 输出协商参数字节流, msg_len: 输出长度 */
atcp_status_t atcp_handshake_initiate(atcp_handshake_t *hs, uint8_t *msg_out, int *msg_len);

/* 处理收到的握手消息，生成响应
 * msg_in: 收到的消息, msg_out: 响应消息 */
atcp_status_t atcp_handshake_process(atcp_handshake_t *hs,
                                 const uint8_t *msg_in, int msg_in_len,
                                 uint8_t *msg_out, int *msg_out_len);

/* 输入链路质量测试结果（Phase3） */
atcp_status_t atcp_handshake_report_quality(atcp_handshake_t *hs, float ber, float loss_rate);

/* 获取当前握手状态 */
atcp_handshake_state_t atcp_handshake_get_state(const atcp_handshake_t *hs);

/* 获取协商后的配置（仅在CONNECTED状态有效） */
atcp_status_t atcp_handshake_get_config(const atcp_handshake_t *hs, atcp_config_t *cfg_out);

/* 自动降级：将QAM阶数降一级 */
atcp_bool_t atcp_handshake_downgrade(atcp_handshake_t *hs);

#endif
