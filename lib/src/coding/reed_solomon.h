#ifndef ATCP_REED_SOLOMON_H
#define ATCP_REED_SOLOMON_H
#include "../../include/audiothief/types.h"

/* RS编解码上下文 */
typedef struct {
    int nsym;                /* 冗余符号数 */
    uint8_t gen_poly[256];   /* 生成多项式系数 */
    int gen_poly_len;        /* 生成多项式长度 */
} atcp_rs_t;

/* 初始化RS上下文（计算生成多项式）*/
atcp_status_t atcp_rs_init(atcp_rs_t *rs, int nsym);

/* 编码单块：data_len <= 255-nsym，输出255字节码字 */
atcp_status_t atcp_rs_encode(const atcp_rs_t *rs, const uint8_t *data, int data_len,
                         uint8_t *output);

/* 解码单块：输入255字节码字，输出原始数据 */
atcp_status_t atcp_rs_decode(const atcp_rs_t *rs, const uint8_t *codeword,
                         uint8_t *output, int *output_len);

/* 分块编码：自动按(255-nsym)字节分块 */
atcp_status_t atcp_rs_encode_blocks(const atcp_rs_t *rs, const uint8_t *data, int data_len,
                                uint8_t *output, int *output_len);

/* 分块解码 */
atcp_status_t atcp_rs_decode_blocks(const atcp_rs_t *rs, const uint8_t *coded, int coded_len,
                                uint8_t *output, int *output_len);

#endif
