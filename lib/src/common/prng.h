#ifndef ATCP_PRNG_H
#define ATCP_PRNG_H
#include "../../include/atcp/types.h"

typedef struct {
    uint32_t state;
} atcp_prng_t;

void atcp_prng_seed(atcp_prng_t *prng, uint32_t seed);
uint32_t atcp_prng_next(atcp_prng_t *prng);
float atcp_prng_bpsk(atcp_prng_t *prng);  /* 返回 +1.0f 或 -1.0f */

#endif
