#include "frame.h"
#include "../coding/crc32.h"
#include "../modulation/qam.h"
#include "../modulation/ofdm.h"
#include <string.h>

/* ---------- 帧头序列化（大端字节序） ---------- */

atcp_status_t atcp_frame_header_serialize(const atcp_frame_header_t *header, uint8_t *buf)
{
    if (!header || !buf)
        return ATCP_ERR_INVALID_PARAM;

    buf[0] = header->type;
    buf[1] = (uint8_t)(header->seq >> 8);
    buf[2] = (uint8_t)(header->seq & 0xFF);
    buf[3] = (uint8_t)(header->payload_len >> 8);
    buf[4] = (uint8_t)(header->payload_len & 0xFF);
    buf[5] = header->flags;
    buf[6] = (uint8_t)(header->crc >> 24);
    buf[7] = (uint8_t)(header->crc >> 16);
    buf[8] = (uint8_t)(header->crc >> 8);
    buf[9] = (uint8_t)(header->crc & 0xFF);

    return ATCP_OK;
}

atcp_status_t atcp_frame_header_deserialize(const uint8_t *buf, atcp_frame_header_t *header)
{
    if (!buf || !header)
        return ATCP_ERR_INVALID_PARAM;

    header->type        = buf[0];
    header->seq         = (uint16_t)((buf[1] << 8) | buf[2]);
    header->payload_len = (uint16_t)((buf[3] << 8) | buf[4]);
    header->flags       = buf[5];
    header->crc         = ((uint32_t)buf[6] << 24) |
                          ((uint32_t)buf[7] << 16) |
                          ((uint32_t)buf[8] << 8)  |
                          ((uint32_t)buf[9]);
    return ATCP_OK;
}

/* ---------- 帧构建 ---------- */

atcp_status_t atcp_frame_build_payload(atcp_frame_type_t type, uint16_t seq,
                                   const uint8_t *data, uint16_t data_len,
                                   uint8_t flags, uint8_t *payload_out,
                                   int payload_out_size, int *payload_len)
{
    if (!payload_out || !payload_len)
        return ATCP_ERR_INVALID_PARAM;
    if (data_len > 0 && !data)
        return ATCP_ERR_INVALID_PARAM;

    int required = ATCP_FRAME_HEADER_SIZE + (int)data_len;
    if (required > payload_out_size)
        return ATCP_ERR_BUFFER_FULL;

    atcp_frame_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type        = (uint8_t)type;
    hdr.seq         = seq;
    hdr.payload_len = data_len;
    hdr.flags       = flags;

    /* 先序列化帧头前6字节（不含CRC）到 payload_out */
    uint8_t hdr_buf[ATCP_FRAME_HEADER_SIZE];
    hdr.crc = 0;
    atcp_frame_header_serialize(&hdr, hdr_buf);

    /* CRC覆盖帧头前6字节 + 用户数据（增量计算） */
    uint32_t crc = atcp_crc32_update(0xFFFFFFFF, hdr_buf, 6);
    if (data && data_len > 0)
        crc = atcp_crc32_update(crc, data, data_len);
    crc ^= 0xFFFFFFFF;

    hdr.crc = crc;

    /* 序列化完整帧头 */
    atcp_frame_header_serialize(&hdr, payload_out);

    /* 拷贝用户数据 */
    if (data && data_len > 0)
        memcpy(payload_out + ATCP_FRAME_HEADER_SIZE, data, data_len);

    *payload_len = ATCP_FRAME_HEADER_SIZE + data_len;
    return ATCP_OK;
}

/* ---------- 帧解析 ---------- */

atcp_status_t atcp_frame_parse_payload(const uint8_t *payload, int payload_len,
                                   atcp_frame_header_t *header, uint8_t *data_out, int *data_len)
{
    if (!payload || !header || payload_len < ATCP_FRAME_HEADER_SIZE)
        return ATCP_ERR_INVALID_PARAM;

    atcp_frame_header_deserialize(payload, header);

    int user_len = header->payload_len;
    if (payload_len < ATCP_FRAME_HEADER_SIZE + user_len)
        return ATCP_ERR_INVALID_PARAM;

    /* 校验CRC：覆盖帧头前6字节 + 用户数据（增量计算） */
    uint32_t crc = atcp_crc32_update(0xFFFFFFFF, payload, 6);
    if (user_len > 0)
        crc = atcp_crc32_update(crc, payload + ATCP_FRAME_HEADER_SIZE, user_len);
    crc ^= 0xFFFFFFFF;

    if (crc != header->crc)
        return ATCP_ERR_CRC_FAIL;

    if (data_out && user_len > 0)
        memcpy(data_out, payload + ATCP_FRAME_HEADER_SIZE, user_len);
    if (data_len)
        *data_len = user_len;

    return ATCP_OK;
}

/* ---------- 辅助计算 ---------- */

int atcp_frame_calc_ofdm_symbols(int coded_bytes, const atcp_config_t *cfg)
{
    if (!cfg || coded_bytes <= 0)
        return 0;

    int n_subs = atcp_ofdm_num_subcarriers(cfg);
    int bits_per_sym = atcp_qam_bits_per_symbol(cfg->qam_order);
    if (n_subs <= 0 || bits_per_sym <= 0)
        return 0;

    int bytes_per_ofdm = n_subs * bits_per_sym / 8;
    if (bytes_per_ofdm <= 0)
        return 0;

    return (coded_bytes + bytes_per_ofdm - 1) / bytes_per_ofdm;
}

atcp_tx_mode_t atcp_frame_select_mode(int data_len)
{
    return (data_len > 1024) ? ATCP_MODE_STREAM : ATCP_MODE_STANDALONE;
}
