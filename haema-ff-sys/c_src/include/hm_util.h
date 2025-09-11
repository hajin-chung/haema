#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/buffer.h>
#include <libavutil/timestamp.h>

typedef struct PacketQueueNode {
    AVPacket *pkt;
    struct PacketQueueNode *next;
} PacketQueueNode;

typedef struct PacketQueue {
    PacketQueueNode *head;
    PacketQueueNode *tail;
    int len;
} PacketQueue;

static inline PacketQueue *packet_queue_new() {
    PacketQueue *pktq = (PacketQueue *)av_mallocz(sizeof(PacketQueue));
    pktq->len = 0; 
    pktq->head = pktq->tail = NULL;
    return pktq;
}

static inline int packet_queue_push(PacketQueue *pktq, AVPacket *pkt) {
    PacketQueueNode *new_node = (PacketQueueNode *)av_mallocz(sizeof(PacketQueueNode));
    new_node->pkt = av_packet_clone(pkt);
    new_node->next = NULL;

    if (pktq->len == 0) {
        pktq->head = pktq->tail = new_node;
    } else {
        pktq->tail->next = new_node;
        pktq->tail = new_node;
    }
    pktq->len++;

    return 0;
}

// ownership of AVPacket goes to you so you must av_packet_free returned packet
static inline AVPacket *packet_queue_pop(PacketQueue *pktq) {
    AVPacket *ret = pktq->head->pkt;
    PacketQueueNode *new_head = pktq->head->next;
    av_free(pktq->head);
    pktq->head = new_head;
    pktq->len--;
    if (pktq->len == 0) 
        pktq->tail = NULL;
    return ret;
}

static inline void packet_queue_free(PacketQueue *pktq) {
    while (pktq->len) {
        AVPacket *pkt = packet_queue_pop(pktq);
        av_packet_free(&pkt);
    }
    av_free(pktq);
}

typedef struct TranscodeContext {
    AVBufferRef *hw_device_ctx;

    const char *in_filename;
    AVFormatContext *ifmt_ctx;
    AVFormatContext *ofmt_ctx;

    // best video stream's index
    int in_video_stream_index;

    // best audio stream's index
    int in_audio_stream_index;

    AVStream *in_video_stream;
    AVStream *in_audio_stream;
    AVStream *out_video_stream;
    AVStream *out_audio_stream;

    PacketQueue *audio_pktq;

    // both decoder and encoder contexts are for video since audio is copied
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;

    AVCodec *video_enc_codec;
} TranscodeContext;

static inline char *limit(char *str, int limit) {
    // FIXME: check strlen for out of bounds error
    str[limit] = '\0';
    return str;
}

static inline void log_packet(AVPacket *pkt, AVStream *stream, const char *tag) {
    AVRational tb = stream->time_base;
    fprintf(stderr,
            "[%s] key: %d stream_index: %d pts_time: %s dts_time: %s "
            "duration_time: %s\n",
            tag, pkt->flags & AV_PKT_FLAG_KEY, pkt->stream_index,
            av_ts2timestr(pkt->pts, &tb),
            av_ts2timestr(pkt->dts, &tb),
            av_ts2timestr(pkt->duration, &tb));
}

static inline void log_frame(AVFrame *frame, AVStream *stream, const char *tag) {
    AVRational tb = stream->time_base;
    fprintf(stderr,
            "[%s] key: %d pts_time: %s dts_time: %s duration_time: %s\n",
            tag, frame->flags & AV_FRAME_FLAG_KEY,
            av_ts2timestr(frame->pts, &tb),
            av_ts2timestr(frame->pkt_dts, &tb),
            av_ts2timestr(frame->duration, &tb));
}

static inline void dump_transcode_context(TranscodeContext *tctx) {
    if (tctx->ifmt_ctx == NULL) {
        fprintf(stderr, "ifmt_ctx is NULL\n");
    } else {
        av_dump_format(tctx->ifmt_ctx, 0, tctx->in_filename, 0);
    }

    fprintf(stderr, "\tInput video stream index: %d\n",
            tctx->in_video_stream_index);
    fprintf(stderr, "\tInput video start time: %s\n",
            av_ts2timestr(tctx->in_video_stream->start_time,
                          &tctx->in_video_stream->time_base));
    fprintf(stderr, "\tInput audio stream index: %d\n",
            tctx->in_audio_stream_index);
    fprintf(stderr, "\tInput audio start time: %s\n",
            av_ts2timestr(tctx->in_audio_stream->start_time,
                          &tctx->in_audio_stream->time_base));
    fprintf(stderr, "\tInput audio stream time base: %d / %d\n",
            tctx->in_audio_stream->time_base.num,
            tctx->in_audio_stream->time_base.den);

    if (tctx->ofmt_ctx == NULL) {
        fprintf(stderr, "ofmt_ctx is NULL\n");
    } else {
        av_dump_format(tctx->ofmt_ctx, 0, NULL, 1);
    }
    fprintf(stderr, "\tOutput audio stream time base: %d / %d\n",
            tctx->out_audio_stream->time_base.num,
            tctx->out_audio_stream->time_base.den);
}

static inline const AVCodec *find_qsv_decoder(enum AVCodecID id) {
    switch (id) {
    case AV_CODEC_ID_H264:
        return avcodec_find_decoder_by_name("h264_qsv");
    case AV_CODEC_ID_HEVC:
        return avcodec_find_decoder_by_name("hevc_qsv");
    case AV_CODEC_ID_VP9:
        return avcodec_find_decoder_by_name("vp9_qsv");
    case AV_CODEC_ID_VP8:
        return avcodec_find_decoder_by_name("vp8_qsv");
    case AV_CODEC_ID_AV1:
        return avcodec_find_decoder_by_name("av1_qsv");
    case AV_CODEC_ID_MPEG2VIDEO:
        return avcodec_find_decoder_by_name("mpeg2_qsv");
    case AV_CODEC_ID_MJPEG:
        return avcodec_find_decoder_by_name("mjpeg_qsv");
    default:
        fprintf(stderr, "Codec is not supportted by qsv\n");
        return NULL;
    }
}

