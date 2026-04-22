#ifndef ATCP_ACK_H
#define ATCP_ACK_H
#include "../../include/audiothief/types.h"

/* ACK帧数据结构（紧凑格式）
 * 总共3字节：base_seq(2B) + bitmap(1B) */
#define ATCP_ACK_DATA_SIZE 3

typedef struct {
    uint16_t base_seq;
    uint8_t bitmap;       /* bit[i]=1 表示 base_seq+i 已收到 */
} atcp_ack_data_t;

/* 编码ACK数据到字节数组 */
atcp_status_t atcp_ack_encode(const atcp_ack_data_t *ack, uint8_t *buf, int *len);

/* 解码字节数组到ACK数据 */
atcp_status_t atcp_ack_decode(const uint8_t *buf, int len, atcp_ack_data_t *ack);

/* ACK去重器 */
typedef struct {
    uint16_t last_base_seq;
    uint8_t last_bitmap;
    int dup_count;
} atcp_ack_dedup_t;

void atcp_ack_dedup_init(atcp_ack_dedup_t *dedup);

/* 处理收到的ACK，返回ATCP_TRUE如果是新ACK，ATCP_FALSE如果是重复 */
atcp_bool_t atcp_ack_dedup_check(atcp_ack_dedup_t *dedup, const atcp_ack_data_t *ack);

/* NACK-only模式判断 */
atcp_bool_t atcp_ack_should_use_nack_only(float ber);

#endif
