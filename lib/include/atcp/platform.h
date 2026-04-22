#ifndef ATCP_PLATFORM_H
#define ATCP_PLATFORM_H

#include "types.h"

/* 平台抽象接口 - 由调用方实现 */
typedef struct {
    /* 写音频采样到输出设备(Speaker/DAC)
     * samples: 交错格式采样数据
     * n_samples: 每通道采样数
     * n_channels: 通道数 (下行=2双声道, 上行=1单声道)
     * 返回: 实际写入的采样数，<0表示错误 */
    int (*audio_write)(const float *samples, int n_samples, int n_channels, void *user_data);
    
    /* 从输入设备(Mic/ADC)读取音频采样
     * samples: 输出缓冲区
     * n_samples: 请求的每通道采样数
     * n_channels: 通道数
     * 返回: 实际读取的采样数，<0表示错误 */
    int (*audio_read)(float *samples, int n_samples, int n_channels, void *user_data);
    
    /* 获取当前时间(毫秒) */
    uint32_t (*get_time_ms)(void *user_data);
    
    /* 用户自定义数据指针 */
    void *user_data;
} atcp_platform_t;

#endif /* ATCP_PLATFORM_H */
