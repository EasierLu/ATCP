#ifndef ATCP_ARQ_H
#define ATCP_ARQ_H
#include "../../include/atcp/types.h"

#define ATCP_ARQ_MAX_WINDOW 16
#define ATCP_ARQ_MAX_BLOCK_SIZE 256

/* 数据块 */
typedef struct {
    uint8_t data[ATCP_ARQ_MAX_BLOCK_SIZE];
    int data_len;
    uint16_t seq;
    atcp_bool_t valid;
} atcp_arq_block_t;

/* 发送端ARQ */
typedef struct {
    atcp_arq_block_t window[ATCP_ARQ_MAX_WINDOW];
    int window_size;           /* 当前窗口大小 */
    int max_window_size;       /* 配置的最大窗口大小 */
    uint16_t base_seq;         /* 窗口起始序号 */
    uint16_t next_seq;         /* 下一个要发送的序号 */
    uint32_t send_time[ATCP_ARQ_MAX_WINDOW];  /* 各块的发送时间 */
    int ack_miss_count;        /* 连续ACK超时计数 */
    uint32_t ack_timeout_ms;
    int max_ack_miss;
} atcp_arq_sender_t;

/* 接收端ARQ */
typedef struct {
    atcp_arq_block_t buffer[ATCP_ARQ_MAX_WINDOW];
    uint16_t expected_seq;     /* 期望的下一个序号 */
    uint8_t rx_bitmap;         /* 接收位图（8位对应8个块） */
} atcp_arq_receiver_t;

/* 发送端 */
void atcp_arq_sender_init(atcp_arq_sender_t *s, int window_size,
                        uint32_t ack_timeout_ms, int max_ack_miss);
atcp_status_t atcp_arq_sender_submit(atcp_arq_sender_t *s, const uint8_t *data, int len, uint16_t seq);
atcp_bool_t atcp_arq_sender_window_full(const atcp_arq_sender_t *s);
atcp_status_t atcp_arq_sender_get_next(atcp_arq_sender_t *s, atcp_arq_block_t *block_out);
atcp_status_t atcp_arq_sender_process_ack(atcp_arq_sender_t *s, uint8_t bitmap, uint16_t ack_base_seq);
int atcp_arq_sender_check_timeout(atcp_arq_sender_t *s, uint32_t current_time_ms,
                                atcp_arq_block_t *retx_out, int max_retx);
void atcp_arq_sender_reset(atcp_arq_sender_t *s);
/* 标记某个块已发送，记录发送时间 */
void atcp_arq_sender_mark_sent(atcp_arq_sender_t *s, uint16_t seq, uint32_t now_ms);

/* 接收端 */
void atcp_arq_receiver_init(atcp_arq_receiver_t *r);
atcp_status_t atcp_arq_receiver_process(atcp_arq_receiver_t *r, const uint8_t *data, int len, uint16_t seq);
uint8_t atcp_arq_receiver_generate_bitmap(atcp_arq_receiver_t *r, uint16_t *base_seq_out);
atcp_bool_t atcp_arq_receiver_has_complete(const atcp_arq_receiver_t *r);
atcp_status_t atcp_arq_receiver_get_ordered(atcp_arq_receiver_t *r, uint8_t *data_out, int *data_len);
void atcp_arq_receiver_reset(atcp_arq_receiver_t *r);

#endif
