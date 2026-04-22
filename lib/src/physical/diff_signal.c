#include "diff_signal.h"
#include <stddef.h>

atcp_status_t atcp_diff_encode(const float *signal, int n, float *left_out, float *right_out)
{
    if (!signal || !left_out || !right_out || n <= 0)
        return ATCP_ERR_INVALID_PARAM;

    for (int i = 0; i < n; i++) {
        left_out[i]  =  signal[i];
        right_out[i] = -signal[i];
    }
    return ATCP_OK;
}

atcp_status_t atcp_diff_decode(const float *left, const float *right, int n, float *signal_out)
{
    if (!left || !right || !signal_out || n <= 0)
        return ATCP_ERR_INVALID_PARAM;

    for (int i = 0; i < n; i++) {
        signal_out[i] = (left[i] - right[i]) * 0.5f;
    }
    return ATCP_OK;
}

atcp_status_t atcp_diff_encode_interleaved(const float *signal, int n, float *interleaved_out)
{
    if (!signal || !interleaved_out || n <= 0)
        return ATCP_ERR_INVALID_PARAM;

    for (int i = 0; i < n; i++) {
        interleaved_out[2 * i]     =  signal[i];   /* L */
        interleaved_out[2 * i + 1] = -signal[i];   /* R */
    }
    return ATCP_OK;
}

atcp_status_t atcp_diff_decode_interleaved(const float *interleaved, int n, float *signal_out)
{
    if (!interleaved || !signal_out || n <= 0)
        return ATCP_ERR_INVALID_PARAM;

    for (int i = 0; i < n; i++) {
        float left  = interleaved[2 * i];
        float right = interleaved[2 * i + 1];
        signal_out[i] = (left - right) * 0.5f;
    }
    return ATCP_OK;
}
