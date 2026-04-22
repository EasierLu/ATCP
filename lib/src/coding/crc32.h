#ifndef ATCP_CRC32_H
#define ATCP_CRC32_H
#include <stdint.h>
#include <stddef.h>

/* 初始化CRC32查找表 */
void atcp_crc32_init(void);

/* 计算CRC32（一次性） */
uint32_t atcp_crc32(const uint8_t *data, size_t len);

/* 增量计算CRC32 */
uint32_t atcp_crc32_update(uint32_t crc, const uint8_t *data, size_t len);

#endif
