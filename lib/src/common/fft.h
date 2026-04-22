#ifndef ATCP_FFT_H
#define ATCP_FFT_H
#include "../../include/audiothief/types.h"

/* 正向FFT（原地） */
atcp_status_t atcp_fft(atcp_complex_t *buf, int n);

/* 逆向FFT（原地，含1/N归一化） */
atcp_status_t atcp_ifft(atcp_complex_t *buf, int n);

#endif
