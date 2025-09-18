#ifndef SC_WEBRTC_STREAMER_H
#define SC_WEBRTC_STREAMER_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "trait/frame_sink.h"
#include "util/thread.h"

/**
 * WebRTC视频推流器
 * 
 * 该模块实现了将scrcpy接收到的视频帧推送到WebRTC服务器的功能。
 * 它作为一个frame_sink，接收来自decoder的视频帧，并通过WebSocket
 * 连接将这些帧发送到指定的WebRTC信令服务器。
 */

struct sc_webrtc_streamer {
    struct sc_frame_sink frame_sink; // frame sink trait

    // WebRTC配置参数
    char *websocket_url;
    char *webrtc_signal_url;
    uint32_t user_id;

    // 网络连接
    int websocket_fd;
    bool connected;

    // 编码器相关
    AVCodecContext *encoder_ctx;
    AVFormatContext *format_ctx;
    AVStream *stream;
    AVPacket *packet;

    // 线程和同步
    sc_thread thread;
    sc_mutex mutex;
    sc_cond cond;
    
    // 状态管理
    bool stopped;
    bool initialized;
    bool header_sent;

    // 帧缓冲
    AVFrame *pending_frame;
    bool has_pending_frame;

    // 统计信息
    uint64_t frame_count;
    uint64_t bytes_sent;
};

/**
 * 初始化WebRTC推流器
 *
 * @param streamer WebRTC推流器实例
 * @param websocket_url WebSocket服务器URL
 * @param webrtc_signal_url WebRTC信令服务器URL  
 * @param user_id 用户ID
 * @return 成功返回true，失败返回false
 */
bool
sc_webrtc_streamer_init(struct sc_webrtc_streamer *streamer,
                        const char *websocket_url,
                        const char *webrtc_signal_url,
                        uint32_t user_id);

/**
 * 销毁WebRTC推流器
 *
 * @param streamer WebRTC推流器实例
 */
void
sc_webrtc_streamer_destroy(struct sc_webrtc_streamer *streamer);

/**
 * 启动WebRTC推流
 *
 * @param streamer WebRTC推流器实例
 * @return 成功返回true，失败返回false
 */
bool
sc_webrtc_streamer_start(struct sc_webrtc_streamer *streamer);

/**
 * 停止WebRTC推流
 *
 * @param streamer WebRTC推流器实例
 */
void
sc_webrtc_streamer_stop(struct sc_webrtc_streamer *streamer);

/**
 * 等待推流线程结束
 *
 * @param streamer WebRTC推流器实例
 */
void
sc_webrtc_streamer_join(struct sc_webrtc_streamer *streamer);

#endif