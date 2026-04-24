#include "handshake.h"
#include "frame.h"
#include <string.h>

/* 握手消息固定大小：14字节
 * sample_rate(4B) + qam_order(1B) + n_fft(2B) + cp_len(2B)
 * + sub_low(2B) + sub_high(2B) + rs_nsym(1B) */
#define ATCP_HS_MSG_SIZE 14

/* ---------- 内部辅助：参数序列化/反序列化 ---------- */

static void hs_params_serialize(const atcp_handshake_params_t *p, uint8_t *buf)
{
    buf[0] = (uint8_t)(p->sample_rate >> 24);
    buf[1] = (uint8_t)(p->sample_rate >> 16);
    buf[2] = (uint8_t)(p->sample_rate >> 8);
    buf[3] = (uint8_t)(p->sample_rate);
    buf[4] = (uint8_t)(p->qam_order);
    buf[5] = (uint8_t)(p->n_fft >> 8);
    buf[6] = (uint8_t)(p->n_fft);
    buf[7] = (uint8_t)(p->cp_len >> 8);
    buf[8] = (uint8_t)(p->cp_len);
    buf[9] = (uint8_t)(p->sub_low >> 8);
    buf[10] = (uint8_t)(p->sub_low);
    buf[11] = (uint8_t)(p->sub_high >> 8);
    buf[12] = (uint8_t)(p->sub_high);
    buf[13] = (uint8_t)(p->rs_nsym);
}

static void hs_params_deserialize(const uint8_t *buf, atcp_handshake_params_t *p)
{
    p->sample_rate = ((int)buf[0] << 24) | ((int)buf[1] << 16) |
                     ((int)buf[2] << 8)  | (int)buf[3];
    p->qam_order   = (int)buf[4];
    p->n_fft       = ((int)buf[5] << 8) | (int)buf[6];
    p->cp_len      = ((int)buf[7] << 8) | (int)buf[8];
    p->sub_low     = ((int)buf[9] << 8) | (int)buf[10];
    p->sub_high    = ((int)buf[11] << 8) | (int)buf[12];
    p->rs_nsym     = (int)buf[13];
}

/* 协商规则：取双方min */
static void hs_negotiate(const atcp_handshake_params_t *a,
                          const atcp_handshake_params_t *b,
                          atcp_handshake_params_t *out)
{
    out->sample_rate = (a->sample_rate < b->sample_rate) ? a->sample_rate : b->sample_rate;
    out->qam_order   = (a->qam_order < b->qam_order)     ? a->qam_order   : b->qam_order;
    out->n_fft       = (a->n_fft < b->n_fft)             ? a->n_fft       : b->n_fft;
    out->cp_len      = (a->cp_len < b->cp_len)           ? a->cp_len      : b->cp_len;
    out->sub_low     = (a->sub_low < b->sub_low)         ? a->sub_low     : b->sub_low;
    out->sub_high    = (a->sub_high < b->sub_high)       ? a->sub_high    : b->sub_high;
    out->rs_nsym     = (a->rs_nsym > b->rs_nsym)         ? a->rs_nsym     : b->rs_nsym;
}

/* ---------- 公开接口 ---------- */

void atcp_handshake_init(atcp_handshake_t *hs, const atcp_config_t *cfg)
{
    if (!hs || !cfg) return;
    memset(hs, 0, sizeof(*hs));
    hs->state = ATCP_HS_IDLE;
    hs->max_retries = 3;

    hs->local_caps.sample_rate = cfg->sample_rate;
    hs->local_caps.qam_order   = cfg->qam_order;
    hs->local_caps.n_fft       = cfg->n_fft;
    hs->local_caps.cp_len      = cfg->cp_len;
    hs->local_caps.sub_low     = cfg->sub_low;
    hs->local_caps.sub_high    = cfg->sub_high;
    hs->local_caps.rs_nsym     = cfg->rs_nsym;
}

atcp_status_t atcp_handshake_initiate(atcp_handshake_t *hs, uint8_t *msg_out, int *msg_len)
{
    if (!hs || !msg_out || !msg_len)
        return ATCP_ERR_INVALID_PARAM;

    hs_params_serialize(&hs->local_caps, msg_out);
    *msg_len = ATCP_HS_MSG_SIZE;
    hs->state = ATCP_HS_PHASE1_SENT;
    return ATCP_OK;
}

atcp_status_t atcp_handshake_process(atcp_handshake_t *hs,
                                 const uint8_t *msg_in, int msg_in_len,
                                 uint8_t *msg_out, int *msg_out_len)
{
    if (!hs || !msg_in || msg_in_len < ATCP_HS_MSG_SIZE || !msg_out || !msg_out_len)
        return ATCP_ERR_INVALID_PARAM;

    atcp_handshake_params_t incoming;
    hs_params_deserialize(msg_in, &incoming);

    switch (hs->state) {
    case ATCP_HS_IDLE:
        /* 响应方收到Phase1 */
        hs->remote_caps = incoming;
        hs_negotiate(&hs->local_caps, &hs->remote_caps, &hs->negotiated);
        hs_params_serialize(&hs->local_caps, msg_out);
        *msg_out_len = ATCP_HS_MSG_SIZE;
        hs->state = ATCP_HS_PHASE1_RECEIVED;
        break;

    case ATCP_HS_PHASE1_SENT:
        /* 发起方收到Phase1响应 */
        hs->remote_caps = incoming;
        hs_negotiate(&hs->local_caps, &hs->remote_caps, &hs->negotiated);
        hs_params_serialize(&hs->negotiated, msg_out);
        *msg_out_len = ATCP_HS_MSG_SIZE;
        hs->state = ATCP_HS_PHASE2_SENT;
        break;

    case ATCP_HS_PHASE1_RECEIVED:
        /* 响应方收到Phase2确认 */
        hs->negotiated = incoming;  /* 使用发起方确定的协商结果 */
        hs_params_serialize(&hs->negotiated, msg_out);
        *msg_out_len = ATCP_HS_MSG_SIZE;
        hs->state = ATCP_HS_PHASE2_RECEIVED;
        break;

    case ATCP_HS_PHASE2_SENT:
        /* 发起方收到Phase2确认回复，进入测试 */
        hs->state = ATCP_HS_PHASE3_TESTING;
        *msg_out_len = 0;
        break;

    case ATCP_HS_PHASE2_RECEIVED:
        /* 响应方也进入测试 */
        hs->state = ATCP_HS_PHASE3_TESTING;
        *msg_out_len = 0;
        break;

    case ATCP_HS_PHASE3_DONE:
        /* 收到Phase4确认 */
        hs_params_serialize(&hs->negotiated, msg_out);
        *msg_out_len = ATCP_HS_MSG_SIZE;
        hs->state = ATCP_HS_PHASE4_RECEIVED;
        break;

    case ATCP_HS_PHASE4_SENT:
        /* 收到Phase4回复，连接建立 */
        hs->state = ATCP_HS_CONNECTED;
        *msg_out_len = 0;
        break;

    case ATCP_HS_PHASE4_RECEIVED:
        /* 回复Phase4，连接建立 */
        hs->state = ATCP_HS_CONNECTED;
        *msg_out_len = 0;
        break;

    default:
        return ATCP_ERR_INVALID_PARAM;
    }

    return ATCP_OK;
}

atcp_status_t atcp_handshake_report_quality(atcp_handshake_t *hs, float ber, float loss_rate)
{
    if (!hs || hs->state != ATCP_HS_PHASE3_TESTING)
        return ATCP_ERR_INVALID_PARAM;

    hs->measured_ber = ber;
    hs->measured_loss_rate = loss_rate;

    /* 质量评估 */
    if (ber < 1e-4f && loss_rate <= 0.0f) {
        /* 优：直接通过 */
        hs->state = ATCP_HS_PHASE3_DONE;
    } else if (ber < 1e-2f && loss_rate < 0.05f) {
        /* 良：也通过 */
        hs->state = ATCP_HS_PHASE3_DONE;
    } else {
        /* 差：尝试降级 */
        if (atcp_handshake_downgrade(hs)) {
            hs->retry_count++;
            if (hs->retry_count > hs->max_retries) {
                hs->state = ATCP_HS_FAILED;
                return ATCP_ERR_HANDSHAKE_FAIL;
            }
            /* 继续测试 */
        } else {
            hs->state = ATCP_HS_FAILED;
            return ATCP_ERR_QUALITY_LOW;
        }
    }

    return ATCP_OK;
}

atcp_handshake_state_t atcp_handshake_get_state(const atcp_handshake_t *hs)
{
    if (!hs) return ATCP_HS_IDLE;
    return hs->state;
}

atcp_status_t atcp_handshake_get_config(const atcp_handshake_t *hs, atcp_config_t *cfg_out)
{
    if (!hs || !cfg_out)
        return ATCP_ERR_INVALID_PARAM;
    if (hs->state != ATCP_HS_CONNECTED)
        return ATCP_ERR_NOT_CONNECTED;

    *cfg_out = atcp_config_default();
    cfg_out->sample_rate = hs->negotiated.sample_rate;
    cfg_out->qam_order   = hs->negotiated.qam_order;
    cfg_out->n_fft       = hs->negotiated.n_fft;
    cfg_out->cp_len      = hs->negotiated.cp_len;
    cfg_out->sub_low     = hs->negotiated.sub_low;
    cfg_out->sub_high    = hs->negotiated.sub_high;
    cfg_out->rs_nsym     = hs->negotiated.rs_nsym;

    return ATCP_OK;
}

atcp_bool_t atcp_handshake_downgrade(atcp_handshake_t *hs)
{
    if (!hs) return ATCP_FALSE;

    int order = hs->negotiated.qam_order;
    if (order >= 256) {
        hs->negotiated.qam_order = 64;
        return ATCP_TRUE;
    } else if (order >= 64) {
        hs->negotiated.qam_order = 16;
        return ATCP_TRUE;
    } else if (order >= 16) {
        hs->negotiated.qam_order = 4;
        return ATCP_TRUE;
    } else {
        /* 已经是QPSK，增大RS冗余 */
        if (hs->negotiated.rs_nsym + 16 <= 128) {
            hs->negotiated.rs_nsym += 16;
            return ATCP_TRUE;
        }
        return ATCP_FALSE;
    }
}

/* ---------- 公开序列化/反序列化接口 ---------- */

int atcp_handshake_params_serialize(const atcp_handshake_params_t *p,
                                    uint8_t *buf)
{
    hs_params_serialize(p, buf);
    return ATCP_HS_MSG_SIZE;
}

atcp_status_t atcp_handshake_params_deserialize(const uint8_t *buf, int len,
                                                 atcp_handshake_params_t *p)
{
    if (!buf || !p || len < ATCP_HS_MSG_SIZE)
        return ATCP_ERR_INVALID_PARAM;
    hs_params_deserialize(buf, p);
    return ATCP_OK;
}

/* ---------- 握手自动推进 ---------- */

atcp_status_t atcp_handshake_tick(atcp_handshake_t *hs,
                                  uint8_t *out_payload, int *out_len,
                                  atcp_bool_t *state_changed)
{
    if (!hs || !out_payload || !out_len || !state_changed)
        return ATCP_ERR_INVALID_PARAM;

    *out_len = 0;
    *state_changed = ATCP_FALSE;

    atcp_handshake_state_t prev_state = hs->state;

    /* Phase3 自动推进: 报告完美链路质量（模拟器无噪声信道） */
    if (hs->state == ATCP_HS_PHASE3_TESTING) {
        atcp_handshake_report_quality(hs, 0.0f, 0.0f);
    }

    /* Phase3 通过后生成 Phase4 消息 */
    if (hs->state == ATCP_HS_PHASE3_DONE) {
        uint8_t p4_data[14];
        atcp_handshake_params_serialize(&hs->negotiated, p4_data);

        uint8_t p4_msg[256];
        int p4_len = 0;
        atcp_handshake_process(hs, p4_data, 14, p4_msg, &p4_len);

        if (p4_len > 0) {
            /* 构建帧payload */
            atcp_frame_build_payload(ATCP_FRAME_HANDSHAKE, 0,
                                   p4_msg, (uint16_t)p4_len, 0,
                                   out_payload, 300, out_len);
        }

        /* PHASE4_RECEIVED 需再推进一步到 CONNECTED */
        if (hs->state == ATCP_HS_PHASE4_RECEIVED) {
            uint8_t dummy_resp[256];
            int dummy_len = 0;
            atcp_handshake_process(hs, p4_data, 14, dummy_resp, &dummy_len);
        }
    }

    *state_changed = (hs->state != prev_state) ? ATCP_TRUE : ATCP_FALSE;
    return ATCP_OK;
}
