/*
 * AudioThief - 公开API实现（集成层）
 * 封装所有底层模块为统一接口
 */

#include "../include/audiothief/audiothief.h"
#include "../include/audiothief/types.h"
#include "../include/audiothief/config.h"
#include "../include/audiothief/platform.h"

/* 内部模块头文件 */
#include "modulation/ofdm.h"
#include "modulation/qam.h"
#include "modulation/training.h"
#include "modulation/frame_sync.h"
#include "modulation/channel_est.h"
#include "coding/reed_solomon.h"
#include "coding/crc32.h"
#include "physical/diff_signal.h"
#include "physical/agc.h"
#include "physical/cfo.h"
#include "physical/sfo.h"
#include "link/frame.h"
#include "link/handshake.h"
#include "link/heartbeat.h"
#include "link/arq.h"
#include "link/ack.h"
#include "common/ring_buffer.h"
#include "common/fft.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 * 内部实例结构
 * ================================================================ */

struct atcp_instance {
    /* 配置 */
    atcp_config_t config;
    atcp_platform_t platform;

    /* 连接状态 */
    atcp_state_t state;
    atcp_bool_t is_initiator;  /* 是否为主动连接方 */

    /* 各层状态 */
    atcp_rs_t rs;
    atcp_frame_sync_t frame_sync;
    atcp_agc_t agc;
    atcp_handshake_t handshake;
    atcp_heartbeatcp_t heartbeat;
    atcp_arq_sender_t arq_sender;
    atcp_arq_receiver_t arq_receiver;
    atcp_ack_dedup_t ack_dedup;

    /* 信道状态 */
    atcp_complex_t channel_h[256];  /* 信道估计（最大子载波数） */
    atcp_bool_t channel_valid;

    /* 发送缓冲区 */
    uint8_t tx_data_buf[4096];
    int tx_data_len;
    int tx_data_pos;
    uint16_t tx_seq;

    /* 接收缓冲区 */
    uint8_t rx_data_buf[4096];
    int rx_data_len;
    int rx_data_pos;

    /* 音频I/O缓冲区 */
    float audio_in_buf[2048];
    float audio_out_buf[2048];
    int audio_out_len;
    int audio_out_pos;

    /* 统计 */
    atcp_stats_t stats;
};

/* ================================================================
 * 内部辅助函数
 * ================================================================ */

/* 将帧payload编码并调制为音频采样，写入audio_out_buf */
static atcp_status_t inst_encode_and_modulate(atcp_instance_t *inst,
                                            const uint8_t *payload, int payload_len,
                                            atcp_bool_t use_qpsk)
{
    atcp_status_t rc;
    const atcp_config_t *cfg = &inst->config;
    int qam_order = use_qpsk ? 4 : cfg->qam_order;

    /* 1. RS编码 */
    uint8_t coded[512];
    int coded_len = 0;
    rc = atcp_rs_encode_blocks(&inst->rs, payload, payload_len, coded, &coded_len);
    if (rc != ATCP_OK) return rc;

    /* 2. 比特展开（字节 -> 比特） */
    uint8_t bits[4096];
    int n_bits = coded_len * 8;
    for (int i = 0; i < coded_len; i++) {
        for (int b = 7; b >= 0; b--) {
            bits[i * 8 + (7 - b)] = (coded[i] >> b) & 1;
        }
    }

    /* 3. QAM调制 */
    atcp_complex_t qam_symbols[2048];
    int n_qam = 0;
    rc = atcp_qam_modulate(bits, n_bits, qam_order, qam_symbols, &n_qam);
    if (rc != ATCP_OK) return rc;

    /* 4. 分配到OFDM符号并调制 */
    int n_subs = atcp_ofdm_num_subcarriers(cfg);
    int symbol_samples = atcp_ofdm_symbol_samples(cfg);
    int pos = 0;
    int audio_pos = 0;

    /* 先输出训练符号 */
    for (int t = 0; t < cfg->n_train; t++) {
        float train_time[1024];
        int train_len = 0;
        rc = atcp_training_generate_ofdm(cfg, train_time, &train_len);
        if (rc != ATCP_OK) return rc;

        /* 差分编码并写入输出缓冲区 */
        if (audio_pos + train_len * 2 > 2048) break;
        atcp_diff_encode_interleaved(train_time, train_len,
                                   &inst->audio_out_buf[audio_pos]);
        audio_pos += train_len * 2;
    }

    /* 数据OFDM符号 */
    while (pos < n_qam) {
        atcp_complex_t sym_buf[256];
        int chunk = (n_qam - pos < n_subs) ? (n_qam - pos) : n_subs;
        memcpy(sym_buf, &qam_symbols[pos], chunk * sizeof(atcp_complex_t));
        /* 零填充剩余子载波 */
        for (int i = chunk; i < n_subs; i++) {
            sym_buf[i].re = 0.0f;
            sym_buf[i].im = 0.0f;
        }

        float time_out[1024];
        int time_len = 0;
        rc = atcp_ofdm_modulate(sym_buf, n_subs, cfg, time_out, &time_len);
        if (rc != ATCP_OK) return rc;

        /* 差分编码 */
        if (audio_pos + time_len * 2 > 2048) break;
        atcp_diff_encode_interleaved(time_out, time_len,
                                   &inst->audio_out_buf[audio_pos]);
        audio_pos += time_len * 2;
        pos += n_subs;
    }

    inst->audio_out_len = audio_pos;
    inst->audio_out_pos = 0;
    return ATCP_OK;
}

/* 处理接收到的帧数据 */
static void inst_process_received_frame(atcp_instance_t *inst,
                                        const atcp_frame_header_t *header,
                                        const uint8_t *data, int data_len)
{
    uint32_t now = 0;
    if (inst->platform.get_time_ms) {
        now = inst->platform.get_time_ms(inst->platform.user_data);
    }

    switch (header->type) {
    case ATCP_FRAME_HANDSHAKE: {
        uint8_t resp[256];
        int resp_len = 0;
        atcp_status_t rc = atcp_handshake_process(&inst->handshake, data, data_len,
                                              resp, &resp_len);
        if (rc == ATCP_OK && resp_len > 0) {
            /* 构建帧并发送握手响应 */
            uint8_t payload[300];
            int payload_len = 0;
            atcp_frame_build_payload(ATCP_FRAME_HANDSHAKE, 0,
                                   resp, (uint16_t)resp_len, 0,
                                   payload, &payload_len);
            inst_encode_and_modulate(inst, payload, payload_len, ATCP_TRUE);
        }
        if (atcp_handshake_get_state(&inst->handshake) == ATCP_HS_CONNECTED) {
            inst->state = ATCP_STATE_CONNECTED;
            atcp_heartbeatcp_rx_update(&inst->heartbeat, now);
            /* 用协商结果更新配置 */
            atcp_handshake_get_config(&inst->handshake, &inst->config);
        }
        break;
    }

    case ATCP_FRAME_DATA: {
        atcp_heartbeatcp_rx_update(&inst->heartbeat, now);
        atcp_arq_receiver_process(&inst->arq_receiver, data, data_len, header->seq);

        /* 生成并发送ACK */
        uint16_t ack_base = 0;
        uint8_t bitmap = atcp_arq_receiver_generate_bitmap(&inst->arq_receiver, &ack_base);
        atcp_ack_data_t ack_data;
        ack_data.base_seq = ack_base;
        ack_data.bitmap = bitmap;
        uint8_t ack_buf[ATCP_ACK_DATA_SIZE];
        int ack_len = 0;
        atcp_ack_encode(&ack_data, ack_buf, &ack_len);

        uint8_t ack_payload[64];
        int ack_payload_len = 0;
        atcp_frame_build_payload(ATCP_FRAME_ACK, header->seq,
                               ack_buf, (uint16_t)ack_len, 0,
                               ack_payload, &ack_payload_len);
        inst_encode_and_modulate(inst, ack_payload, ack_payload_len, ATCP_TRUE);

        /* 尝试提取有序数据到接收缓冲区 */
        if (atcp_arq_receiver_has_complete(&inst->arq_receiver)) {
            uint8_t ordered[ATCP_ARQ_MAX_BLOCK_SIZE * ATCP_ARQ_MAX_WINDOW];
            int ordered_len = 0;
            if (atcp_arq_receiver_get_ordered(&inst->arq_receiver,
                                            ordered, &ordered_len) == ATCP_OK) {
                int space = (int)sizeof(inst->rx_data_buf) - inst->rx_data_len;
                int copy = (ordered_len < space) ? ordered_len : space;
                if (copy > 0) {
                    memcpy(&inst->rx_data_buf[inst->rx_data_len], ordered, copy);
                    inst->rx_data_len += copy;
                }
            }
        }
        inst->stats.frames_received++;
        break;
    }

    case ATCP_FRAME_ACK: {
        atcp_heartbeatcp_rx_update(&inst->heartbeat, now);
        atcp_ack_data_t ack_data;
        if (atcp_ack_decode(data, data_len, &ack_data) == ATCP_OK) {
            if (atcp_ack_dedup_check(&inst->ack_dedup, &ack_data)) {
                atcp_arq_sender_process_ack(&inst->arq_sender,
                                          ack_data.bitmap, ack_data.base_seq);
            }
        }
        break;
    }

    case ATCP_FRAME_HEARTBEAT:
        atcp_heartbeatcp_rx_update(&inst->heartbeat, now);
        break;

    case ATCP_FRAME_EOS:
        atcp_heartbeatcp_rx_update(&inst->heartbeat, now);
        /* 流结束标志 */
        break;

    default:
        break;
    }
}

/* ================================================================
 * 公开API实现
 * ================================================================ */

atcp_instance_t *atcp_create(const atcp_config_t *config, const atcp_platform_t *platform)
{
    if (!platform) return NULL;

    atcp_instance_t *inst = (atcp_instance_t *)malloc(sizeof(atcp_instance_t));
    if (!inst) return NULL;
    memset(inst, 0, sizeof(atcp_instance_t));

    /* 拷贝配置 */
    if (config) {
        inst->config = *config;
    } else {
        inst->config = atcp_config_default();
    }

    /* 拷贝平台接口 */
    inst->platform = *platform;

    /* 初始化各层 */
    atcp_rs_init(&inst->rs, inst->config.rs_nsym);
    atcp_frame_sync_init(&inst->frame_sync, &inst->config);
    atcp_agc_init(&inst->agc, 0.5f);
    atcp_heartbeatcp_init(&inst->heartbeat,
                      (uint32_t)inst->config.heartbeatcp_interval_ms,
                      (uint32_t)inst->config.heartbeatcp_timeout_ms);
    atcp_arq_sender_init(&inst->arq_sender, ATCP_ARQ_MAX_WINDOW,
                       (uint32_t)inst->config.ack_timeout_ms,
                       inst->config.max_ack_miss);
    atcp_arq_receiver_init(&inst->arq_receiver);
    atcp_ack_dedup_init(&inst->ack_dedup);
    atcp_crc32_init();

    inst->state = ATCP_STATE_IDLE;
    inst->channel_valid = ATCP_FALSE;
    inst->tx_seq = 0;

    return inst;
}

void atcp_destroy(atcp_instance_t *inst)
{
    if (!inst) return;
    atcp_frame_sync_free(&inst->frame_sync);
    free(inst);
}

atcp_status_t atcp_connect(atcp_instance_t *inst)
{
    if (!inst) return ATCP_ERR_INVALID_PARAM;
    if (inst->state != ATCP_STATE_IDLE) return ATCP_ERR_BUSY;

    inst->is_initiator = ATCP_TRUE;
    atcp_handshake_init(&inst->handshake, &inst->config);

    /* 生成Phase1消息 */
    uint8_t hs_msg[256];
    int hs_len = 0;
    atcp_status_t rc = atcp_handshake_initiate(&inst->handshake, hs_msg, &hs_len);
    if (rc != ATCP_OK) return rc;

    /* 构建握手帧 */
    uint8_t payload[300];
    int payload_len = 0;
    rc = atcp_frame_build_payload(ATCP_FRAME_HANDSHAKE, 0,
                                hs_msg, (uint16_t)hs_len, 0,
                                payload, &payload_len);
    if (rc != ATCP_OK) return rc;

    /* 编码调制（使用QPSK低速模式） */
    rc = inst_encode_and_modulate(inst, payload, payload_len, ATCP_TRUE);
    if (rc != ATCP_OK) return rc;

    inst->state = ATCP_STATE_CONNECTING;
    return ATCP_OK;
}

atcp_status_t atcp_accept(atcp_instance_t *inst)
{
    if (!inst) return ATCP_ERR_INVALID_PARAM;
    if (inst->state != ATCP_STATE_IDLE) return ATCP_ERR_BUSY;

    inst->is_initiator = ATCP_FALSE;
    atcp_handshake_init(&inst->handshake, &inst->config);
    inst->state = ATCP_STATE_CONNECTING;
    return ATCP_OK;
}

atcp_status_t atcp_disconnect(atcp_instance_t *inst)
{
    if (!inst) return ATCP_ERR_INVALID_PARAM;

    inst->state = ATCP_STATE_DISCONNECTING;

    /* 重置各层状态 */
    atcp_frame_sync_reset(&inst->frame_sync);
    atcp_agc_reset(&inst->agc);
    atcp_heartbeatcp_reset(&inst->heartbeat);
    atcp_arq_sender_reset(&inst->arq_sender);
    atcp_arq_receiver_reset(&inst->arq_receiver);
    atcp_ack_dedup_init(&inst->ack_dedup);

    inst->channel_valid = ATCP_FALSE;
    inst->tx_data_len = 0;
    inst->tx_data_pos = 0;
    inst->rx_data_len = 0;
    inst->rx_data_pos = 0;
    inst->audio_out_len = 0;
    inst->audio_out_pos = 0;

    inst->state = ATCP_STATE_DISCONNECTED;
    return ATCP_OK;
}

atcp_status_t atcp_send(atcp_instance_t *inst, const uint8_t *data, size_t len)
{
    if (!inst || !data || len == 0) return ATCP_ERR_INVALID_PARAM;
    if (inst->state != ATCP_STATE_CONNECTED) return ATCP_ERR_NOT_CONNECTED;
    if (len > sizeof(inst->tx_data_buf)) return ATCP_ERR_BUFFER_FULL;
    if (inst->tx_data_len > 0) return ATCP_ERR_BUSY;  /* 上次发送未完成 */

    memcpy(inst->tx_data_buf, data, len);
    inst->tx_data_len = (int)len;
    inst->tx_data_pos = 0;
    return ATCP_OK;
}

atcp_status_t atcp_recv(atcp_instance_t *inst, uint8_t *buf, size_t buf_len, size_t *received)
{
    if (!inst || !buf || !received) return ATCP_ERR_INVALID_PARAM;
    if (inst->state != ATCP_STATE_CONNECTED) return ATCP_ERR_NOT_CONNECTED;

    *received = 0;
    int avail = inst->rx_data_len - inst->rx_data_pos;
    if (avail <= 0) return ATCP_ERR_BUFFER_EMPTY;

    int copy = (avail < (int)buf_len) ? avail : (int)buf_len;
    memcpy(buf, &inst->rx_data_buf[inst->rx_data_pos], copy);
    inst->rx_data_pos += copy;
    *received = (size_t)copy;

    /* 如果全部读完，重置缓冲区 */
    if (inst->rx_data_pos >= inst->rx_data_len) {
        inst->rx_data_len = 0;
        inst->rx_data_pos = 0;
    }

    return ATCP_OK;
}

atcp_status_t atcp_tick(atcp_instance_t *inst)
{
    if (!inst) return ATCP_ERR_INVALID_PARAM;

    const atcp_config_t *cfg = &inst->config;
    uint32_t now = 0;
    if (inst->platform.get_time_ms) {
        now = inst->platform.get_time_ms(inst->platform.user_data);
    }

    /* === 1. 从平台读取音频采样 === */
    int symbol_samples = atcp_ofdm_symbol_samples(cfg);
    int read_n = (symbol_samples < 2048) ? symbol_samples : 2048;
    int got = 0;
    if (inst->platform.audio_read) {
        got = inst->platform.audio_read(inst->audio_in_buf, read_n, 1,
                                        inst->platform.user_data);
    }

    if (got > 0) {
        /* === 2. AGC处理 === */
        atcp_agc_process(&inst->agc, inst->audio_in_buf, got);

        /* === 3. 帧同步：逐采样送入 === */
        atcp_frame_sync_feed_batch(&inst->frame_sync, inst->audio_in_buf, got);

        /* === 4. 检测到帧 === */
        if (atcp_frame_sync_detected(&inst->frame_sync)) {
            int n_subs = atcp_ofdm_num_subcarriers(cfg);
            int local_offset = atcp_frame_sync_get_local_offset(&inst->frame_sync);

            /* 提取OFDM符号 -> 去CP -> FFT -> 信道估计/均衡 */
            /* 注: 帧同步后的采样从frame_sync内部获取偏移 */

            /* 训练符号信道估计 */
            atcp_complex_t train_ref[256];
            atcp_training_generate(cfg->train_seed, n_subs, train_ref);

            /* 解调训练符号以获取信道估计 */
            atcp_complex_t rx_train[256];
            if (local_offset + symbol_samples <= got) {
                atcp_ofdm_demodulate(&inst->audio_in_buf[local_offset], cfg, rx_train, n_subs);
                atcp_channel_estimate(rx_train, train_ref, n_subs, inst->channel_h);
                inst->channel_valid = ATCP_TRUE;
            }

            /* 解调数据符号（在训练符号之后） */
            int data_offset = local_offset + cfg->n_train * symbol_samples;
            if (data_offset + symbol_samples <= got) {
                atcp_complex_t rx_data[256];
                atcp_ofdm_demodulate(&inst->audio_in_buf[data_offset], cfg,
                                   rx_data, n_subs);

                /* ZF均衡 */
                atcp_complex_t eq_data[256];
                if (inst->channel_valid) {
                    atcp_channel_equalize(rx_data, inst->channel_h, n_subs, eq_data);
                } else {
                    memcpy(eq_data, rx_data, n_subs * sizeof(atcp_complex_t));
                }

                /* QAM解调 */
                uint8_t bits[2048];
                int n_bits = 0;
                int qam_order = cfg->qam_order;
                /* 握手阶段使用QPSK */
                if (inst->state == ATCP_STATE_CONNECTING) {
                    qam_order = 4;
                }
                atcp_qam_demodulate(eq_data, n_subs, qam_order, bits, &n_bits);

                /* 比特打包为字节 */
                int n_bytes = n_bits / 8;
                uint8_t coded_bytes[512];
                for (int i = 0; i < n_bytes; i++) {
                    coded_bytes[i] = 0;
                    for (int b = 0; b < 8; b++) {
                        coded_bytes[i] |= (bits[i * 8 + b] & 1) << (7 - b);
                    }
                }

                /* RS解码 */
                uint8_t decoded[512];
                int decoded_len = 0;
                atcp_status_t rc = atcp_rs_decode_blocks(&inst->rs, coded_bytes,
                                                     n_bytes, decoded, &decoded_len);
                if (rc == ATCP_OK && decoded_len >= ATCP_FRAME_HEADER_SIZE) {
                    /* 解析帧 */
                    atcp_frame_header_t header;
                    uint8_t frame_data[512];
                    int frame_data_len = 0;
                    if (atcp_frame_parse_payload(decoded, decoded_len,
                                               &header, frame_data,
                                               &frame_data_len) == ATCP_OK) {
                        inst_process_received_frame(inst, &header,
                                                    frame_data, frame_data_len);
                    }
                }
            }

            /* 重置帧同步，准备下一帧 */
            atcp_frame_sync_reset(&inst->frame_sync);
        }
    }

    /* === 5. 仅在CONNECTED状态下处理心跳和ARQ === */
    if (inst->state == ATCP_STATE_CONNECTED) {
        /* 心跳检查 */
        if (atcp_heartbeatcp_is_timeout(&inst->heartbeat, now)) {
            inst->state = ATCP_STATE_DISCONNECTED;
            return ATCP_ERR_TIMEOUT;
        }

        if (atcp_heartbeatcp_need_send(&inst->heartbeat, now)) {
            /* 发送心跳帧 */
            uint8_t hb_payload[64];
            int hb_len = 0;
            atcp_frame_build_payload(ATCP_FRAME_HEARTBEAT, 0, NULL, 0, 0,
                                   hb_payload, &hb_len);
            inst_encode_and_modulate(inst, hb_payload, hb_len, ATCP_TRUE);
            atcp_heartbeatcp_tx_update(&inst->heartbeat, now);
        }

        /* ARQ超时重传检查 */
        atcp_arq_block_t retx[ATCP_ARQ_MAX_WINDOW];
        int n_retx = atcp_arq_sender_check_timeout(&inst->arq_sender, now,
                                                  retx, ATCP_ARQ_MAX_WINDOW);
        for (int i = 0; i < n_retx; i++) {
            uint8_t payload[300];
            int payload_len = 0;
            atcp_frame_build_payload(ATCP_FRAME_DATA, retx[i].seq,
                                   retx[i].data, (uint16_t)retx[i].data_len, 0,
                                   payload, &payload_len);
            inst_encode_and_modulate(inst, payload, payload_len, ATCP_FALSE);
            inst->stats.retransmit_count++;
        }

        /* 发送待发数据 */
        if (inst->tx_data_len > 0 && inst->tx_data_pos < inst->tx_data_len
            && !atcp_arq_sender_window_full(&inst->arq_sender)) {
            int remaining = inst->tx_data_len - inst->tx_data_pos;
            int chunk = (remaining < ATCP_ARQ_MAX_BLOCK_SIZE)
                        ? remaining : ATCP_ARQ_MAX_BLOCK_SIZE;

            atcp_arq_sender_submit(&inst->arq_sender,
                                 &inst->tx_data_buf[inst->tx_data_pos],
                                 chunk, inst->tx_seq);

            uint8_t payload[300];
            int payload_len = 0;
            uint8_t flags = 0;
            if (inst->tx_data_pos + chunk >= inst->tx_data_len) {
                flags |= ATCP_FLAG_LAST_BLOCK;
            }
            atcp_frame_build_payload(ATCP_FRAME_DATA, inst->tx_seq,
                                   &inst->tx_data_buf[inst->tx_data_pos],
                                   (uint16_t)chunk, flags,
                                   payload, &payload_len);
            inst_encode_and_modulate(inst, payload, payload_len, ATCP_FALSE);

            uint32_t send_now = inst->platform.get_time_ms(inst->platform.user_data);
            atcp_arq_sender_mark_sent(&inst->arq_sender, inst->tx_seq, send_now);

            inst->tx_data_pos += chunk;
            inst->tx_seq++;
            inst->stats.frames_sent++;

            /* 如果所有数据已入队 */
            if (inst->tx_data_pos >= inst->tx_data_len) {
                inst->tx_data_len = 0;
                inst->tx_data_pos = 0;
            }
        }
    }

    /* === 6. 音频输出 === */
    if (inst->audio_out_len > 0 && inst->audio_out_pos < inst->audio_out_len) {
        int to_write = inst->audio_out_len - inst->audio_out_pos;
        if (inst->platform.audio_write) {
            int written = inst->platform.audio_write(
                &inst->audio_out_buf[inst->audio_out_pos],
                to_write / 2,  /* 交错格式，每通道采样数 = total/2 */
                2,             /* 双声道（差分） */
                inst->platform.user_data);
            if (written > 0) {
                inst->audio_out_pos += written * 2;
            }
        }
        /* 如果全部写完，重置 */
        if (inst->audio_out_pos >= inst->audio_out_len) {
            inst->audio_out_len = 0;
            inst->audio_out_pos = 0;
        }
    }

    return ATCP_OK;
}

atcp_state_t atcp_get_state(const atcp_instance_t *inst)
{
    if (!inst) return ATCP_STATE_DISCONNECTED;
    return inst->state;
}

atcp_stats_t atcp_get_stats(const atcp_instance_t *inst)
{
    atcp_stats_t empty;
    memset(&empty, 0, sizeof(empty));
    if (!inst) return empty;
    return inst->stats;
}
