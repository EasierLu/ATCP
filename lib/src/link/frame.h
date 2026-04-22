#ifndef ATCP_FRAME_H
#define ATCP_FRAME_H
#include "../../include/atcp/types.h"
#include "../../include/atcp/config.h"

/* 帧头结构（10字节） */
typedef struct {
    uint8_t type;        /* atcp_frame_type_t */
    uint16_t seq;        /* 序列号 */
    uint16_t payload_len;/* 有效载荷长度 */
    uint8_t flags;       /* 标志位 */
    uint32_t crc;        /* CRC32校验 */
} atcp_frame_header_t;

#define ATCP_FRAME_HEADER_SIZE 10

/* 帧头标志位 */
#define ATCP_FLAG_STREAM_MODE  0x01  /* 连续流模式 */
#define ATCP_FLAG_LAST_BLOCK   0x02  /* 最后一个数据块 */
#define ATCP_FLAG_NACK_ONLY    0x04  /* NACK-only模式 */

/* 序列化帧头到字节数组（大端序） */
atcp_status_t atcp_frame_header_serialize(const atcp_frame_header_t *header, uint8_t *buf);

/* 从字节数组反序列化帧头 */
atcp_status_t atcp_frame_header_deserialize(const uint8_t *buf, atcp_frame_header_t *header);

/* 构建完整帧payload（帧头 + 用户数据），输出用于后续RS编码的字节流
 * payload_out = [header_bytes(10)] + [user_data(len)]
 * 返回总长度 */
atcp_status_t atcp_frame_build_payload(atcp_frame_type_t type, uint16_t seq,
                                   const uint8_t *data, uint16_t data_len,
                                   uint8_t flags, uint8_t *payload_out, int *payload_len);

/* 解析帧payload，提取帧头和用户数据 */
atcp_status_t atcp_frame_parse_payload(const uint8_t *payload, int payload_len,
                                   atcp_frame_header_t *header, uint8_t *data_out, int *data_len);

/* 计算数据在给定配置下需要多少个OFDM符号
 * 输入为RS编码后的总字节数 */
int atcp_frame_calc_ofdm_symbols(int coded_bytes, const atcp_config_t *cfg);

/* 判断应使用哪种传输模式 */
atcp_tx_mode_t atcp_frame_select_mode(int data_len);

#endif
