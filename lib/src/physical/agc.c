#include "agc.h"
#include <math.h>
#include <stddef.h>

void atcp_agc_init(atcp_agc_t *agc, float target_amplitude)
{
    if (!agc) return;
    agc->target_amplitude = target_amplitude;
    agc->attack_coeff     = 0.01f;
    agc->release_coeff    = 0.001f;
    agc->current_gain     = 1.0f;
    agc->peak_level       = 0.0f;
}

atcp_status_t atcp_agc_process(atcp_agc_t *agc, float *samples, int n)
{
    if (!agc || !samples || n <= 0)
        return ATCP_ERR_INVALID_PARAM;

    for (int i = 0; i < n; i++) {
        float level = fabsf(samples[i]);

        /* 峰值追踪：快速攻击，慢速释放 */
        if (level > agc->peak_level)
            agc->peak_level += agc->attack_coeff * (level - agc->peak_level);
        else
            agc->peak_level += agc->release_coeff * (level - agc->peak_level);

        /* 计算目标增益（避免除零） */
        float gain = agc->target_amplitude / (agc->peak_level + 1e-10f);

        /* 限制增益范围 */
        if (gain < 0.1f)   gain = 0.1f;
        if (gain > 100.0f) gain = 100.0f;

        /* 平滑增益更新 */
        agc->current_gain += 0.01f * (gain - agc->current_gain);

        /* 应用增益 */
        samples[i] *= agc->current_gain;
    }
    return ATCP_OK;
}

float atcp_agc_get_gain(const atcp_agc_t *agc)
{
    if (!agc) return 1.0f;
    return agc->current_gain;
}

void atcp_agc_reset(atcp_agc_t *agc)
{
    if (!agc) return;
    float target = agc->target_amplitude;
    atcp_agc_init(agc, target);
}
