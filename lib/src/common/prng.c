#include "prng.h"

void atcp_prng_seed(atcp_prng_t *prng, uint32_t seed)
{
    if (!prng) return;
    prng->state = seed ? seed : 1u;  /* 避免零状态 */
}

uint32_t atcp_prng_next(atcp_prng_t *prng)
{
    uint32_t s = prng->state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    prng->state = s;
    return s;
}

float atcp_prng_bpsk(atcp_prng_t *prng)
{
    return (atcp_prng_next(prng) & 1u) ? 1.0f : -1.0f;
}
