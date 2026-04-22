#include "heartbeat.h"

void atcp_heartbeatcp_init(atcp_heartbeatcp_t *hb, uint32_t interval_ms, uint32_t timeout_ms)
{
    if (!hb) return;
    hb->last_rx_time_ms = 0;
    hb->last_tx_time_ms = 0;
    hb->interval_ms     = interval_ms;
    hb->timeout_ms      = timeout_ms;
}

void atcp_heartbeatcp_rx_update(atcp_heartbeatcp_t *hb, uint32_t current_time_ms)
{
    if (!hb) return;
    hb->last_rx_time_ms = current_time_ms;
}

void atcp_heartbeatcp_tx_update(atcp_heartbeatcp_t *hb, uint32_t current_time_ms)
{
    if (!hb) return;
    hb->last_tx_time_ms = current_time_ms;
}

atcp_bool_t atcp_heartbeatcp_need_send(const atcp_heartbeatcp_t *hb, uint32_t current_time_ms)
{
    if (!hb) return ATCP_FALSE;
    return (current_time_ms - hb->last_tx_time_ms >= hb->interval_ms) ? ATCP_TRUE : ATCP_FALSE;
}

atcp_bool_t atcp_heartbeatcp_is_timeout(const atcp_heartbeatcp_t *hb, uint32_t current_time_ms)
{
    if (!hb) return ATCP_FALSE;
    return (current_time_ms - hb->last_rx_time_ms >= hb->timeout_ms) ? ATCP_TRUE : ATCP_FALSE;
}

void atcp_heartbeatcp_reset(atcp_heartbeatcp_t *hb)
{
    if (!hb) return;
    hb->last_rx_time_ms = 0;
    hb->last_tx_time_ms = 0;
}
