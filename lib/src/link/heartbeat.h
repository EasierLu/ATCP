#ifndef ATCP_HEARTBEATCP_H
#define ATCP_HEARTBEATCP_H
#include "../../include/audiothief/types.h"

typedef struct {
    uint32_t last_rx_time_ms;    /* 上次收到任何帧的时间 */
    uint32_t last_tx_time_ms;    /* 上次发送心跳的时间 */
    uint32_t interval_ms;        /* 心跳发送间隔 */
    uint32_t timeout_ms;         /* 超时断开门限 */
} atcp_heartbeatcp_t;

void atcp_heartbeatcp_init(atcp_heartbeatcp_t *hb, uint32_t interval_ms, uint32_t timeout_ms);
void atcp_heartbeatcp_rx_update(atcp_heartbeatcp_t *hb, uint32_t current_time_ms);
void atcp_heartbeatcp_tx_update(atcp_heartbeatcp_t *hb, uint32_t current_time_ms);
atcp_bool_t atcp_heartbeatcp_need_send(const atcp_heartbeatcp_t *hb, uint32_t current_time_ms);
atcp_bool_t atcp_heartbeatcp_is_timeout(const atcp_heartbeatcp_t *hb, uint32_t current_time_ms);
void atcp_heartbeatcp_reset(atcp_heartbeatcp_t *hb);

#endif
