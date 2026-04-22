#include "ack.h"
#include <string.h>

atcp_status_t atcp_ack_encode(const atcp_ack_data_t *ack, uint8_t *buf, int *len)
{
    if (!ack || !buf || !len)
        return ATCP_ERR_INVALID_PARAM;

    buf[0] = (uint8_t)(ack->base_seq >> 8);
    buf[1] = (uint8_t)(ack->base_seq & 0xFF);
    buf[2] = ack->bitmap;
    *len = ATCP_ACK_DATA_SIZE;

    return ATCP_OK;
}

atcp_status_t atcp_ack_decode(const uint8_t *buf, int len, atcp_ack_data_t *ack)
{
    if (!buf || !ack || len < ATCP_ACK_DATA_SIZE)
        return ATCP_ERR_INVALID_PARAM;

    ack->base_seq = (uint16_t)((buf[0] << 8) | buf[1]);
    ack->bitmap   = buf[2];

    return ATCP_OK;
}

void atcp_ack_dedup_init(atcp_ack_dedup_t *dedup)
{
    if (!dedup) return;
    memset(dedup, 0, sizeof(*dedup));
}

atcp_bool_t atcp_ack_dedup_check(atcp_ack_dedup_t *dedup, const atcp_ack_data_t *ack)
{
    if (!dedup || !ack)
        return ATCP_FALSE;

    if (ack->base_seq == dedup->last_base_seq &&
        ack->bitmap == dedup->last_bitmap) {
        dedup->dup_count++;
        return ATCP_FALSE;  /* 重复ACK */
    }

    dedup->last_base_seq = ack->base_seq;
    dedup->last_bitmap   = ack->bitmap;
    dedup->dup_count     = 0;
    return ATCP_TRUE;  /* 新ACK */
}

atcp_bool_t atcp_ack_should_use_nack_only(float ber)
{
    return (ber < 1e-4f) ? ATCP_TRUE : ATCP_FALSE;
}
