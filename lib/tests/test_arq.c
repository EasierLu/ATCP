#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "link/arq.h"
#include "link/ack.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (line %d): %s\n", __FILE__, __LINE__, msg); \
        failures++; \
    } else { \
        passes++; \
    } \
} while(0)

#define TEST_SUMMARY() do { \
    printf("\n=== %d passed, %d failed ===\n", passes, failures); \
    return failures > 0 ? 1 : 0; \
} while(0)

int main(void)
{
    int passes = 0, failures = 0;

    /* Test 1: 正常流程 - 发送4块，全部ACK，窗口滑动 */
    printf("--- Test 1: Normal flow ---\n");
    {
        atcp_arq_sender_t sender;
        atcp_arq_receiver_t receiver;
        atcp_arq_sender_init(&sender, 4, 500, 3);
        atcp_arq_receiver_init(&receiver);

        /* 提交4个块 */
        uint8_t data[4][16];
        int i;
        for (i = 0; i < 4; i++) {
            memset(data[i], (uint8_t)(i + 'A'), 16);
            atcp_status_t s = atcp_arq_sender_submit(&sender, data[i], 16, (uint16_t)i);
            TEST_ASSERT(s == ATCP_OK, "submit should succeed");
        }

        /* sender get_next 取出4个块 */
        atcp_arq_block_t block;
        for (i = 0; i < 4; i++) {
            atcp_status_t s = atcp_arq_sender_get_next(&sender, &block);
            TEST_ASSERT(s == ATCP_OK, "get_next should succeed");
            TEST_ASSERT(block.seq == (uint16_t)i, "seq should match");

            /* receiver 接收 */
            atcp_arq_receiver_process(&receiver, block.data, block.data_len, block.seq);
        }

        /* receiver 生成 bitmap */
        uint16_t base_seq;
        uint8_t bitmap = atcp_arq_receiver_generate_bitmap(&receiver, &base_seq);
        TEST_ASSERT(bitmap == 0x0F, "bitmap should be 0x0F (all 4 received)");
        TEST_ASSERT(base_seq == 0, "base_seq should be 0");

        /* sender 处理 ACK */
        atcp_status_t s = atcp_arq_sender_process_ack(&sender, bitmap, base_seq);
        TEST_ASSERT(s == ATCP_OK, "process_ack should succeed");
        TEST_ASSERT(sender.base_seq == 4, "base_seq should advance to 4");
    }

    /* Test 2: 选择性重传 - 跳过块2 */
    printf("--- Test 2: Selective retransmission ---\n");
    {
        atcp_arq_sender_t sender;
        atcp_arq_receiver_t receiver;
        atcp_arq_sender_init(&sender, 4, 500, 3);
        atcp_arq_receiver_init(&receiver);

        /* 提交4个块 */
        uint8_t data[4][16];
        int i;
        for (i = 0; i < 4; i++) {
            memset(data[i], (uint8_t)(i + 'A'), 16);
            atcp_arq_sender_submit(&sender, data[i], 16, (uint16_t)i);
        }

        /* 发送所有块 */
        atcp_arq_block_t blocks[4];
        for (i = 0; i < 4; i++) {
            atcp_arq_sender_get_next(&sender, &blocks[i]);
        }

        /* receiver只收到0,1,3 (跳过2) */
        atcp_arq_receiver_process(&receiver, blocks[0].data, blocks[0].data_len, blocks[0].seq);
        atcp_arq_receiver_process(&receiver, blocks[1].data, blocks[1].data_len, blocks[1].seq);
        atcp_arq_receiver_process(&receiver, blocks[3].data, blocks[3].data_len, blocks[3].seq);

        /* bitmap: bit0=1(seq0), bit1=1(seq1), bit2=0(seq2 missing), bit3=1(seq3) => 0x0B */
        uint16_t base_seq;
        uint8_t bitmap = atcp_arq_receiver_generate_bitmap(&receiver, &base_seq);
        TEST_ASSERT((bitmap & 0x01) != 0, "bit0 should be set (seq 0 received)");
        TEST_ASSERT((bitmap & 0x02) != 0, "bit1 should be set (seq 1 received)");
        TEST_ASSERT((bitmap & 0x04) == 0, "bit2 should be clear (seq 2 missing)");
        TEST_ASSERT((bitmap & 0x08) != 0, "bit3 should be set (seq 3 received)");

        /* sender 处理 ACK - 窗口不应滑过未确认的块2 */
        atcp_arq_sender_process_ack(&sender, bitmap, base_seq);
        TEST_ASSERT(sender.base_seq <= 2, "base_seq should not advance past unacked block 2");
    }

    /* Test 3: ACK丢失与窗口缩小 */
    printf("--- Test 3: Timeout and window shrink ---\n");
    {
        atcp_arq_sender_t sender;
        atcp_arq_sender_init(&sender, 4, 500, 3);

        /* 提交4个块 */
        uint8_t data[4][16];
        int i;
        for (i = 0; i < 4; i++) {
            memset(data[i], (uint8_t)(i + 'A'), 16);
            atcp_arq_sender_submit(&sender, data[i], 16, (uint16_t)i);
        }

        /* 发送所有块 (记录发送时间=0) */
        atcp_arq_block_t block;
        for (i = 0; i < 4; i++) {
            atcp_arq_sender_get_next(&sender, &block);
        }

        /* 模拟3次超时 */
        atcp_arq_block_t retx[ATCP_ARQ_MAX_WINDOW];
        int n_retx;

        n_retx = atcp_arq_sender_check_timeout(&sender, 500, retx, ATCP_ARQ_MAX_WINDOW);
        /* 第1次超时 */

        n_retx = atcp_arq_sender_check_timeout(&sender, 1000, retx, ATCP_ARQ_MAX_WINDOW);
        /* 第2次超时 */

        n_retx = atcp_arq_sender_check_timeout(&sender, 1500, retx, ATCP_ARQ_MAX_WINDOW);
        /* 第3次超时: 连续miss达到max_ack_miss=3, 窗口应缩小 */

        TEST_ASSERT(sender.window_size <= 2,
                    "window should shrink after max_ack_miss timeouts");
        TEST_ASSERT(sender.window_size >= 1,
                    "window should be at least 1");
    }

    /* Test 4: 窗口恢复 */
    printf("--- Test 4: Window recovery ---\n");
    {
        atcp_arq_sender_t sender;
        atcp_arq_sender_init(&sender, 4, 500, 3);

        /* 提交并发送块 */
        uint8_t data[4][16];
        int i;
        for (i = 0; i < 4; i++) {
            memset(data[i], (uint8_t)(i + 'A'), 16);
            atcp_arq_sender_submit(&sender, data[i], 16, (uint16_t)i);
        }
        atcp_arq_block_t block;
        for (i = 0; i < 4; i++) {
            atcp_arq_sender_get_next(&sender, &block);
        }

        /* 触发缩窗 */
        atcp_arq_block_t retx[ATCP_ARQ_MAX_WINDOW];
        atcp_arq_sender_check_timeout(&sender, 500, retx, ATCP_ARQ_MAX_WINDOW);
        atcp_arq_sender_check_timeout(&sender, 1000, retx, ATCP_ARQ_MAX_WINDOW);
        atcp_arq_sender_check_timeout(&sender, 1500, retx, ATCP_ARQ_MAX_WINDOW);

        int shrunk_size = sender.window_size;
        TEST_ASSERT(shrunk_size < 4, "window should have shrunk");

        /* 收到有效ACK -> 窗口应恢复 */
        atcp_arq_sender_process_ack(&sender, 0x0F, 0);
        TEST_ASSERT(sender.window_size == 4,
                    "window should recover to max after valid ACK");
    }

    /* Test 5: 按序交付 */
    printf("--- Test 5: Ordered delivery ---\n");
    {
        atcp_arq_receiver_t receiver;
        atcp_arq_receiver_init(&receiver);

        /* 乱序接收: seq=1, seq=0, seq=2 */
        uint8_t d0[4] = {0x10, 0x11, 0x12, 0x13};
        uint8_t d1[4] = {0x20, 0x21, 0x22, 0x23};
        uint8_t d2[4] = {0x30, 0x31, 0x32, 0x33};

        atcp_arq_receiver_process(&receiver, d1, 4, 1);  /* seq=1 先到 */
        /* expected_seq=0, 所以seq=1到了但不完整 */

        atcp_arq_receiver_process(&receiver, d0, 4, 0);  /* seq=0 到达 */
        /* 现在seq=0到了, has_complete应该为true */
        TEST_ASSERT(atcp_arq_receiver_has_complete(&receiver),
                    "should have complete data after seq 0 arrives");

        atcp_arq_receiver_process(&receiver, d2, 4, 2);  /* seq=2 到达 */

        /* 按序取出 */
        uint8_t out_buf[256];
        int out_len = 0;
        atcp_status_t s = atcp_arq_receiver_get_ordered(&receiver, out_buf, &out_len);
        TEST_ASSERT(s == ATCP_OK, "get_ordered should succeed");
        TEST_ASSERT(out_len == 12, "should output 12 bytes total (3 blocks x 4 bytes)");

        /* 验证顺序: d0, d1, d2 */
        TEST_ASSERT(memcmp(out_buf, d0, 4) == 0, "first block should be d0");
        TEST_ASSERT(memcmp(out_buf + 4, d1, 4) == 0, "second block should be d1");
        TEST_ASSERT(memcmp(out_buf + 8, d2, 4) == 0, "third block should be d2");
    }

    /* Test 6: ACK编解码 */
    printf("--- Test 6: ACK encode/decode ---\n");
    {
        atcp_ack_data_t ack_in = {0};
        ack_in.base_seq = 42;
        ack_in.bitmap = 0xAB;

        uint8_t buf[ATCP_ACK_DATA_SIZE];
        int len = 0;
        atcp_status_t s1 = atcp_ack_encode(&ack_in, buf, &len);
        TEST_ASSERT(s1 == ATCP_OK, "ack_encode should succeed");
        TEST_ASSERT(len == ATCP_ACK_DATA_SIZE, "ack data should be 3 bytes");

        atcp_ack_data_t ack_out = {0};
        atcp_status_t s2 = atcp_ack_decode(buf, len, &ack_out);
        TEST_ASSERT(s2 == ATCP_OK, "ack_decode should succeed");
        TEST_ASSERT(ack_out.base_seq == 42, "base_seq should roundtrip");
        TEST_ASSERT(ack_out.bitmap == 0xAB, "bitmap should roundtrip");
    }

    /* Test 7: ACK去重 */
    printf("--- Test 7: ACK dedup ---\n");
    {
        atcp_ack_dedup_t dedup;
        atcp_ack_dedup_init(&dedup);

        atcp_ack_data_t ack = {0};
        ack.base_seq = 10;
        ack.bitmap = 0x0F;

        atcp_bool_t is_new1 = atcp_ack_dedup_check(&dedup, &ack);
        TEST_ASSERT(is_new1 == ATCP_TRUE, "first ACK should be new");

        atcp_bool_t is_new2 = atcp_ack_dedup_check(&dedup, &ack);
        TEST_ASSERT(is_new2 == ATCP_FALSE, "duplicate ACK should be detected");

        ack.bitmap = 0x1F;  /* 不同bitmap */
        atcp_bool_t is_new3 = atcp_ack_dedup_check(&dedup, &ack);
        TEST_ASSERT(is_new3 == ATCP_TRUE, "different bitmap should be new");
    }

    TEST_SUMMARY();
}
