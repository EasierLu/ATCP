#include "arq.h"
#include <string.h>

/* ========== 发送端 ========== */

void atcp_arq_sender_init(atcp_arq_sender_t *s, int window_size,
                        uint32_t ack_timeout_ms, int max_ack_miss)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (window_size > ATCP_ARQ_MAX_WINDOW)
        window_size = ATCP_ARQ_MAX_WINDOW;
    s->window_size     = window_size;
    s->max_window_size = window_size;
    s->ack_timeout_ms  = ack_timeout_ms;
    s->max_ack_miss    = max_ack_miss;
}

atcp_status_t atcp_arq_sender_submit(atcp_arq_sender_t *s, const uint8_t *data, int len, uint16_t seq)
{
    if (!s || !data || len <= 0 || len > ATCP_ARQ_MAX_BLOCK_SIZE)
        return ATCP_ERR_INVALID_PARAM;

    int idx = (seq - s->base_seq) & 0xFFFF;
    if (idx >= s->window_size)
        return ATCP_ERR_BUFFER_FULL;

    int slot = idx % ATCP_ARQ_MAX_WINDOW;
    memcpy(s->window[slot].data, data, len);
    s->window[slot].data_len = len;
    s->window[slot].seq      = seq;
    s->window[slot].valid    = ATCP_TRUE;
    s->send_time[slot]       = UINT32_MAX; /* 未发送 */

    return ATCP_OK;
}

atcp_bool_t atcp_arq_sender_window_full(const atcp_arq_sender_t *s)
{
    if (!s) return ATCP_TRUE;
    int pending = (s->next_seq - s->base_seq) & 0xFFFF;
    return (pending >= s->window_size) ? ATCP_TRUE : ATCP_FALSE;
}

atcp_status_t atcp_arq_sender_get_next(atcp_arq_sender_t *s, atcp_arq_block_t *block_out)
{
    if (!s || !block_out)
        return ATCP_ERR_INVALID_PARAM;

    int pending = (s->next_seq - s->base_seq) & 0xFFFF;
    if (pending >= s->window_size)
        return ATCP_ERR_BUFFER_EMPTY;

    int slot = s->next_seq % ATCP_ARQ_MAX_WINDOW;
    if (!s->window[slot].valid)
        return ATCP_ERR_BUFFER_EMPTY;

    *block_out = s->window[slot];
    s->send_time[slot] = 0; /* 标记已发送，时间起点0 */
    s->next_seq++;
    return ATCP_OK;
}

atcp_status_t atcp_arq_sender_process_ack(atcp_arq_sender_t *s, uint8_t bitmap, uint16_t ack_base_seq)
{
    if (!s)
        return ATCP_ERR_INVALID_PARAM;

    /* ACK base_seq 应该匹配发送端 base_seq */
    if (ack_base_seq != s->base_seq)
        return ATCP_OK; /* 过期ACK，忽略 */

    /* 收到有效ACK，重置计数并恢复窗口 */
    s->ack_miss_count = 0;
    s->window_size = s->max_window_size;

    /* 处理bitmap，清除已确认的块并滑动窗口 */
    /* 从最低位开始连续扫描，滑动base_seq */
    int advance = 0;
    for (int i = 0; i < 8; i++) {
        if (bitmap & (1 << i)) {
            int slot = (s->base_seq + i) % ATCP_ARQ_MAX_WINDOW;
            s->window[slot].valid = ATCP_FALSE;
            s->send_time[slot] = 0;
        }
    }

    /* 滑动窗口：连续已确认的块，不超过 next_seq */
    for (int i = 0; i < 8; i++) {
        if (s->base_seq >= s->next_seq)
            break; /* 不滑过未提交的位置 */
        int slot = s->base_seq % ATCP_ARQ_MAX_WINDOW;
        if (!s->window[slot].valid) {
            s->base_seq++;
            advance++;
        } else {
            break;
        }
    }

    (void)advance;
    return ATCP_OK;
}

int atcp_arq_sender_check_timeout(atcp_arq_sender_t *s, uint32_t current_time_ms,
                                atcp_arq_block_t *retx_out, int max_retx)
{
    if (!s || !retx_out || max_retx <= 0)
        return 0;

    int retx_count = 0;
    int in_window = (s->next_seq - s->base_seq) & 0xFFFF;
    if (in_window > s->window_size)
        in_window = s->window_size;

    for (int i = 0; i < in_window && retx_count < max_retx; i++) {
        int slot = (s->base_seq + i) % ATCP_ARQ_MAX_WINDOW;
        if (!s->window[slot].valid)
            continue;
        if (s->send_time[slot] == UINT32_MAX)
            continue; /* 未发送 */
        if (current_time_ms - s->send_time[slot] >= s->ack_timeout_ms) {
            retx_out[retx_count++] = s->window[slot];
            s->send_time[slot] = current_time_ms; /* 重置发送时间 */
            s->ack_miss_count++;
            if (s->ack_miss_count >= s->max_ack_miss) {
                int half = s->max_window_size / 2;
                s->window_size = (half > 1) ? half : 1;
            }
        }
    }

    return retx_count;
}

void atcp_arq_sender_reset(atcp_arq_sender_t *s)
{
    if (!s) return;
    int mws = s->max_window_size;
    uint32_t ato = s->ack_timeout_ms;
    int mam = s->max_ack_miss;
    memset(s, 0, sizeof(*s));
    s->max_window_size = mws;
    s->window_size     = mws;
    s->ack_timeout_ms  = ato;
    s->max_ack_miss    = mam;
}

void atcp_arq_sender_mark_sent(atcp_arq_sender_t *s, uint16_t seq, uint32_t now_ms) {
    if (!s) return;
    int idx = (seq - s->base_seq) & 0xFFFF;
    if (idx >= s->window_size) return;
    int slot = idx % ATCP_ARQ_MAX_WINDOW;
    s->send_time[slot] = now_ms;
}

/* ========== 接收端 ========== */

void atcp_arq_receiver_init(atcp_arq_receiver_t *r)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));
}

atcp_status_t atcp_arq_receiver_process(atcp_arq_receiver_t *r, const uint8_t *data, int len, uint16_t seq)
{
    if (!r || !data || len <= 0 || len > ATCP_ARQ_MAX_BLOCK_SIZE)
        return ATCP_ERR_INVALID_PARAM;

    int offset = (seq - r->expected_seq) & 0xFFFF;
    if (offset >= 8)
        return ATCP_ERR_INVALID_PARAM; /* 超出接收窗口 */

    int slot = seq % ATCP_ARQ_MAX_WINDOW;
    memcpy(r->buffer[slot].data, data, len);
    r->buffer[slot].data_len = len;
    r->buffer[slot].seq      = seq;
    r->buffer[slot].valid    = ATCP_TRUE;

    r->rx_bitmap |= (1 << offset);

    return ATCP_OK;
}

uint8_t atcp_arq_receiver_generate_bitmap(atcp_arq_receiver_t *r, uint16_t *base_seq_out)
{
    if (!r) return 0;
    if (base_seq_out)
        *base_seq_out = r->expected_seq;
    return r->rx_bitmap;
}

atcp_bool_t atcp_arq_receiver_has_complete(const atcp_arq_receiver_t *r)
{
    if (!r) return ATCP_FALSE;
    /* 至少bit0已收到 */
    return (r->rx_bitmap & 0x01) ? ATCP_TRUE : ATCP_FALSE;
}

atcp_status_t atcp_arq_receiver_get_ordered(atcp_arq_receiver_t *r, uint8_t *data_out, int *data_len)
{
    if (!r || !data_out || !data_len)
        return ATCP_ERR_INVALID_PARAM;

    int total = 0;
    int delivered = 0;

    /* 提取连续已收到的块 */
    while (r->rx_bitmap & 0x01) {
        int slot = r->expected_seq % ATCP_ARQ_MAX_WINDOW;
        if (!r->buffer[slot].valid)
            break;

        memcpy(data_out + total, r->buffer[slot].data, r->buffer[slot].data_len);
        total += r->buffer[slot].data_len;
        r->buffer[slot].valid = ATCP_FALSE;
        delivered++;

        r->expected_seq++;
        r->rx_bitmap >>= 1;
    }

    *data_len = total;
    return (delivered > 0) ? ATCP_OK : ATCP_ERR_BUFFER_EMPTY;
}

void atcp_arq_receiver_reset(atcp_arq_receiver_t *r)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));
}
