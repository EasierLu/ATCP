#ifndef ATCP_H
#define ATCP_H

#include "types.h"
#include "config.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 不透明实例类型 */
typedef struct atcp_instance atcp_instance_t;

/* === 生命周期 === */

/* 创建ATCP实例
 * config: 协议配置（NULL使用默认值）
 * platform: 平台回调接口（必须非NULL）
 * 返回: 实例指针，失败返回NULL */
ATCP_API atcp_instance_t *atcp_create(const atcp_config_t *config, const atcp_platform_t *platform);

/* 销毁实例并释放所有资源 */
ATCP_API void atcp_destroy(atcp_instance_t *inst);

/* === 连接管理 === */

/* 发起连接（主动端，执行握手）
 * 非阻塞：调用后通过atcp_tick()推进握手流程 */
ATCP_API atcp_status_t atcp_connect(atcp_instance_t *inst);

/* 等待连接（被动端）
 * 非阻塞：调用后通过atcp_tick()监听握手请求 */
ATCP_API atcp_status_t atcp_accept(atcp_instance_t *inst);

/* 断开连接 */
ATCP_API atcp_status_t atcp_disconnect(atcp_instance_t *inst);

/* === 数据传输 === */

/* 发送数据
 * 数据会被分块、RS编码、调制后通过音频输出
 * 非阻塞：数据入队后由atcp_tick()逐步发送 */
ATCP_API atcp_status_t atcp_send(atcp_instance_t *inst, const uint8_t *data, size_t len);

/* 接收数据
 * 从接收缓冲区读取已解码的数据
 * received: 实际读取的字节数 */
ATCP_API atcp_status_t atcp_recv(atcp_instance_t *inst, uint8_t *buf, size_t buf_len, size_t *received);

/* === 主循环 === */

/* 事件驱动tick，由调用方定期调用
 * 处理：音频读写、帧同步、解调、ACK/重传、心跳
 * 建议调用频率：与OFDM符号时长匹配（~10-15ms） */
ATCP_API atcp_status_t atcp_tick(atcp_instance_t *inst);

/* === 状态查询 === */

/* 获取当前连接状态 */
ATCP_API atcp_state_t atcp_get_state(const atcp_instance_t *inst);

/* 获取统计信息 */
ATCP_API atcp_stats_t atcp_get_stats(const atcp_instance_t *inst);

/* 获取音频缓冲区大小（float 个数）—— 平台层需保证缓冲区不小于此值 */
ATCP_API int atcp_get_audio_buf_size(const atcp_instance_t *inst);

#ifdef __cplusplus
}
#endif

#endif /* ATCP_H */
