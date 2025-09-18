#include "webrtc_streamer.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>

#include "util/log.h"
#include "util/net.h"

/** Downcast frame_sink to sc_webrtc_streamer */
#define DOWNCAST(SINK) container_of(SINK, struct sc_webrtc_streamer, frame_sink)

// WebSocket协议相关常量
#define WS_MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_FRAME_OPCODE_TEXT 0x1
#define WS_FRAME_OPCODE_BINARY 0x2
#define WS_FRAME_OPCODE_CLOSE 0x8

// JSON消息模板
static const char *MESSAGE_TEMPLATE = 
    "{"
    "\"type\":\"video_frame\","
    "\"user_id\":%u,"
    "\"timestamp\":%lld,"
    "\"width\":%d,"
    "\"height\":%d,"
    "\"format\":\"%s\","
    "\"data_size\":%zu"
    "}";

static bool
send_websocket_message(struct sc_webrtc_streamer *streamer, 
                       const char *message, size_t length) {
    if (!streamer->connected || streamer->websocket_fd < 0) {
        return false;
    }

    // 构建WebSocket帧头
    uint8_t frame_header[10];
    size_t header_len = 0;
    
    // 第一个字节：FIN=1, RSV=000, Opcode=0001 (text frame)
    frame_header[0] = 0x81;
    
    // 负载长度
    if (length < 126) {
        frame_header[1] = (uint8_t)length;
        header_len = 2;
    } else if (length < 65536) {
        frame_header[1] = 126;
        frame_header[2] = (length >> 8) & 0xFF;
        frame_header[3] = length & 0xFF;
        header_len = 4;
    } else {
        frame_header[1] = 127;
        frame_header[2] = 0;
        frame_header[3] = 0;
        frame_header[4] = 0;
        frame_header[5] = 0;
        frame_header[6] = (length >> 24) & 0xFF;
        frame_header[7] = (length >> 16) & 0xFF;
        frame_header[8] = (length >> 8) & 0xFF;
        frame_header[9] = length & 0xFF;
        header_len = 10;
    }

    // 发送帧头
    ssize_t written = send(streamer->websocket_fd, frame_header, header_len, 0);
    if (written != (ssize_t)header_len) {
        LOGE("Failed to send WebSocket frame header");
        return false;
    }

    // 发送消息内容
    written = send(streamer->websocket_fd, message, length, 0);
    if (written != (ssize_t)length) {
        LOGE("Failed to send WebSocket message content");
        return false;
    }

    return true;
}

static bool
connect_websocket(struct sc_webrtc_streamer *streamer) {
    // 简化的WebSocket连接实现
    // 实际项目中需要完整的WebSocket握手协议
    
    LOGI("Connecting to WebSocket: %s", streamer->websocket_url);
    
    // 这里应该解析URL并建立TCP连接
    // 为了简化，我们假设连接成功
    streamer->websocket_fd = -1; // 实际需要创建socket连接
    streamer->connected = false;
    
    // TODO: 实现完整的WebSocket握手
    // 1. 创建TCP连接
    // 2. 发送HTTP升级请求
    // 3. 验证服务器响应
    
    LOGW("WebSocket connection not fully implemented yet");
    return false;
}

static bool
encode_and_send_frame(struct sc_webrtc_streamer *streamer, const AVFrame *frame) {
    if (!streamer->encoder_ctx) {
        LOGE("Encoder context not initialized");
        return false;
    }

    // 发送帧到编码器
    int ret = avcodec_send_frame(streamer->encoder_ctx, frame);
    if (ret < 0) {
        LOGE("Error sending frame to encoder: %d", ret);
        return false;
    }

    // 接收编码后的包
    AVPacket *packet = streamer->packet;
    while (ret >= 0) {
        ret = avcodec_receive_packet(streamer->encoder_ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            LOGE("Error encoding frame: %d", ret);
            return false;
        }

        // 创建JSON消息
        char message[1024];
        int64_t timestamp = av_gettime();
        
        int msg_len = snprintf(message, sizeof(message), MESSAGE_TEMPLATE,
                               streamer->user_id,
                               timestamp,
                               frame->width,
                               frame->height,
                               "h264", // 假设使用H264编码
                               (size_t)packet->size);

        if (msg_len > 0 && msg_len < (int)sizeof(message)) {
            // 发送JSON消息
            bool ok = send_websocket_message(streamer, message, msg_len);
            if (ok) {
                streamer->frame_count++;
                streamer->bytes_sent += packet->size;
                LOGV("Sent frame %llu, size: %d bytes", 
                     (unsigned long long)streamer->frame_count, packet->size);
            } else {
                LOGE("Failed to send WebSocket message");
                av_packet_unref(packet);
                return false;
            }
        }

        av_packet_unref(packet);
    }

    return true;
}

static int
run_webrtc_streamer(void *data) {
    struct sc_webrtc_streamer *streamer = data;

    LOGI("WebRTC streamer thread started");

    // 尝试连接WebSocket
    if (!connect_websocket(streamer)) {
        LOGE("Failed to connect to WebSocket server");
        return -1;
    }

    for (;;) {
        sc_mutex_lock(&streamer->mutex);

        // 等待新帧或停止信号
        while (!streamer->stopped && !streamer->has_pending_frame) {
            sc_cond_wait(&streamer->cond, &streamer->mutex);
        }

        if (streamer->stopped) {
            sc_mutex_unlock(&streamer->mutex);
            break;
        }

        // 获取待处理的帧
        AVFrame *frame = streamer->pending_frame;
        streamer->has_pending_frame = false;
        sc_mutex_unlock(&streamer->mutex);

        // 编码并发送帧
        bool ok = encode_and_send_frame(streamer, frame);
        if (!ok) {
            LOGE("Failed to encode and send frame");
            break;
        }
    }

    LOGI("WebRTC streamer thread ended");
    return 0;
}

static bool
sc_webrtc_streamer_frame_sink_open(struct sc_frame_sink *sink, 
                                   const AVCodecContext *ctx) {
    struct sc_webrtc_streamer *streamer = DOWNCAST(sink);
    
    LOGI("Opening WebRTC streamer");

    // 初始化编码器
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOGE("H264 encoder not found");
        return false;
    }

    streamer->encoder_ctx = avcodec_alloc_context3(codec);
    if (!streamer->encoder_ctx) {
        LOGE("Could not allocate encoder context");
        return false;
    }

    // 配置编码器参数
    streamer->encoder_ctx->width = ctx->width;
    streamer->encoder_ctx->height = ctx->height;
    streamer->encoder_ctx->time_base = (AVRational){1, 30}; // 30 FPS
    streamer->encoder_ctx->framerate = (AVRational){30, 1};
    streamer->encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    streamer->encoder_ctx->bit_rate = 2000000; // 2Mbps
    streamer->encoder_ctx->gop_size = 30;
    streamer->encoder_ctx->max_b_frames = 0;

    // 设置编码器选项
    av_opt_set(streamer->encoder_ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(streamer->encoder_ctx->priv_data, "tune", "zerolatency", 0);

    // 打开编码器
    int ret = avcodec_open2(streamer->encoder_ctx, codec, NULL);
    if (ret < 0) {
        LOGE("Could not open encoder: %d", ret);
        avcodec_free_context(&streamer->encoder_ctx);
        return false;
    }

    // 分配包
    streamer->packet = av_packet_alloc();
    if (!streamer->packet) {
        LOGE("Could not allocate packet");
        avcodec_free_context(&streamer->encoder_ctx);
        return false;
    }

    streamer->initialized = true;
    LOGI("WebRTC streamer opened successfully");
    return true;
}

static void
sc_webrtc_streamer_frame_sink_close(struct sc_frame_sink *sink) {
    struct sc_webrtc_streamer *streamer = DOWNCAST(sink);
    
    LOGI("Closing WebRTC streamer");

    streamer->initialized = false;

    if (streamer->encoder_ctx) {
        avcodec_free_context(&streamer->encoder_ctx);
    }

    if (streamer->packet) {
        av_packet_free(&streamer->packet);
    }

    if (streamer->websocket_fd >= 0) {
        close(streamer->websocket_fd);
        streamer->websocket_fd = -1;
    }

    streamer->connected = false;
}

static bool
sc_webrtc_streamer_frame_sink_push(struct sc_frame_sink *sink, 
                                   const AVFrame *frame) {
    struct sc_webrtc_streamer *streamer = DOWNCAST(sink);

    if (!streamer->initialized) {
        return false;
    }

    sc_mutex_lock(&streamer->mutex);

    // 如果已有待处理帧，跳过当前帧（避免积压）
    if (streamer->has_pending_frame) {
        sc_mutex_unlock(&streamer->mutex);
        return true; // 不返回false，避免停止整个流水线
    }

    // 复制帧数据
    if (!streamer->pending_frame) {
        streamer->pending_frame = av_frame_alloc();
        if (!streamer->pending_frame) {
            sc_mutex_unlock(&streamer->mutex);
            return false;
        }
    }

    int ret = av_frame_ref(streamer->pending_frame, frame);
    if (ret < 0) {
        sc_mutex_unlock(&streamer->mutex);
        return false;
    }

    streamer->has_pending_frame = true;
    sc_cond_signal(&streamer->cond);
    sc_mutex_unlock(&streamer->mutex);

    return true;
}

bool
sc_webrtc_streamer_init(struct sc_webrtc_streamer *streamer,
                        const char *websocket_url,
                        const char *webrtc_signal_url,
                        uint32_t user_id) {
    
    memset(streamer, 0, sizeof(*streamer));

    // 复制URL字符串
    streamer->websocket_url = strdup(websocket_url);
    if (!streamer->websocket_url) {
        LOGE("Could not duplicate websocket URL");
        return false;
    }

    streamer->webrtc_signal_url = strdup(webrtc_signal_url);
    if (!streamer->webrtc_signal_url) {
        LOGE("Could not duplicate WebRTC signal URL");
        free(streamer->websocket_url);
        return false;
    }

    streamer->user_id = user_id;
    streamer->websocket_fd = -1;
    streamer->connected = false;
    streamer->stopped = false;
    streamer->initialized = false;
    streamer->header_sent = false;
    streamer->has_pending_frame = false;
    streamer->frame_count = 0;
    streamer->bytes_sent = 0;

    // 初始化同步原语
    if (!sc_mutex_init(&streamer->mutex)) {
        LOGE("Could not initialize mutex");
        goto error;
    }

    if (!sc_cond_init(&streamer->cond)) {
        LOGE("Could not initialize condition");
        sc_mutex_destroy(&streamer->mutex);
        goto error;
    }

    // 设置frame sink操作
    static const struct sc_frame_sink_ops ops = {
        .open = sc_webrtc_streamer_frame_sink_open,
        .close = sc_webrtc_streamer_frame_sink_close,
        .push = sc_webrtc_streamer_frame_sink_push,
    };

    streamer->frame_sink.ops = &ops;

    LOGI("WebRTC streamer initialized for user %u", user_id);
    return true;

error:
    free(streamer->websocket_url);
    free(streamer->webrtc_signal_url);
    return false;
}

void
sc_webrtc_streamer_destroy(struct sc_webrtc_streamer *streamer) {
    if (streamer->pending_frame) {
        av_frame_free(&streamer->pending_frame);
    }

    sc_cond_destroy(&streamer->cond);
    sc_mutex_destroy(&streamer->mutex);

    free(streamer->websocket_url);
    free(streamer->webrtc_signal_url);

    LOGI("WebRTC streamer destroyed");
}

bool
sc_webrtc_streamer_start(struct sc_webrtc_streamer *streamer) {
    LOGI("Starting WebRTC streamer thread");

    bool ok = sc_thread_create(&streamer->thread, run_webrtc_streamer, 
                               "webrtc-streamer", streamer);
    if (!ok) {
        LOGE("Could not start WebRTC streamer thread");
        return false;
    }

    return true;
}

void
sc_webrtc_streamer_stop(struct sc_webrtc_streamer *streamer) {
    sc_mutex_lock(&streamer->mutex);
    streamer->stopped = true;
    sc_cond_signal(&streamer->cond);
    sc_mutex_unlock(&streamer->mutex);
}

void
sc_webrtc_streamer_join(struct sc_webrtc_streamer *streamer) {
    sc_thread_join(&streamer->thread, NULL);
}