#include "crc32.h"

/* 标准CRC32多项式（反射） */
#define CRC32_POLY 0xEDB88320

static uint32_t crc32_table[256];
static int crc32_initialized = 0;

void atcp_crc32_init(void)
{
    if (crc32_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ CRC32_POLY;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }

    crc32_initialized = 1;
}

uint32_t atcp_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

uint32_t atcp_crc32(const uint8_t *data, size_t len)
{
    return atcp_crc32_update(0xFFFFFFFF, data, len) ^ 0xFFFFFFFF;
}
