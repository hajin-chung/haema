/*
 * Haema Transcode
 * Copyright (c) 2025 Hajin Chung <hajinchung1@gmail.com>
 * Don't know what to say here
 * just do whatever you want with this code
 *
 * Haema Transcode is binary + library for correctly segmenting and transcoding
 * parts of a large video very fast.
 *
 * Currently utilizes intel's qsv hardware accelerated codecs.
 * timestamps of source video are preserved in segmented output.
 */

#include <libavutil/buffer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/packet.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>

#include "include/hm_util.h"

const int OUT_VIDEO_STREAM_INDEX = 0;
const int OUT_AUDIO_STREAM_INDEX = 1;

const char *SAMPLE_INIT_FILENAME = "segment.m4s";
const char *SAMPLE_SEGMENT_FILENAME = "segment.m4s";

int get_format(AVCodecContext *avctx, const enum AVPixelFormat *pix_fmts) {
    while (*pix_fmts != AV_PIX_FMT_NONE) {
        if (*pix_fmts == AV_PIX_FMT_QSV) {
            return AV_PIX_FMT_QSV;
        }

        pix_fmts++;
    }

    fprintf(stderr, "The QSV pixel format not offered in get_format()\n");

    return AV_PIX_FMT_NONE;
}

AVCodecContext *config_dec_ctx(AVStream *stream, AVFormatContext *ifmt_ctx,
                               AVBufferRef *hw_device_ctx) {
    int ret;
    const AVCodec *dec_codec = find_qsv_decoder(stream->codecpar->codec_id);
    if (!dec_codec) {
        fprintf(stderr, "Failed to find decoder\n");
        return NULL;
    }

    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec_codec);
    if (!dec_ctx) {
        fprintf(stderr, "Failed to allocate decoder context");
        return NULL;
    }

    ret = avcodec_parameters_to_context(dec_ctx, stream->codecpar);
    if (ret < 0) {
        fprintf(stderr,
                "Failed to copy decoder params to input decoder context");
        return NULL;
    }

    dec_ctx->pkt_timebase = stream->time_base;
    dec_ctx->framerate = av_guess_frame_rate(ifmt_ctx, stream, NULL);
    dec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    if (!dec_ctx->hw_device_ctx) {
        fprintf(stderr, "A hardware device reference create failed\n");
        return NULL;
    }
    dec_ctx->get_format = get_format;

    if ((ret = avcodec_open2(dec_ctx, dec_codec, NULL)) < 0) {
        fprintf(stderr, "Failed to open decoder\n");
        return NULL;
    }

    return dec_ctx;
}

int config_input(HaemaContext *hmctx) {
    int ret;
    hmctx->ifmt_ctx = NULL;
    if ((ret = avformat_open_input(&hmctx->ifmt_ctx, hmctx->in_filename, 0,
                                   0)) < 0) {
        fprintf(stderr, "Could not open input filename '%s'\n",
                hmctx->in_filename);
        return ret;
    }

    if ((ret = avformat_find_stream_info(hmctx->ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information\n");
        return ret;
    }

    if ((ret = av_find_best_stream(hmctx->ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1,
                                   NULL, 0)) < 0) {
        fprintf(stderr, "Cannot find a video stream in input file: %s\n",
                av_err2str(ret));
        return ret;
    }
    hmctx->in_video_stream_index = ret;
    hmctx->in_video_stream = hmctx->ifmt_ctx->streams[ret];

    if ((ret = av_find_best_stream(hmctx->ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1,
                                   NULL, 0)) < 0) {
        fprintf(stderr, "Cannot find a audio stream in input file: %s\n",
                av_err2str(ret));
        return ret;
    }
    hmctx->in_audio_stream_index = ret;
    hmctx->in_audio_stream = hmctx->ifmt_ctx->streams[ret];

    return 0;
}

int config_output(HaemaContext *hmctx, const char *encoder_name) {
    AVStream *out_video_stream, *out_audio_stream;
    int ret;

    const AVCodec *enc_codec = avcodec_find_encoder_by_name(encoder_name);
    if (!enc_codec) {
        fprintf(stderr, "Could not find encoder: %s\n", encoder_name);
        return -1;
    }
    hmctx->enc_ctx = avcodec_alloc_context3(enc_codec);
    if (hmctx->enc_ctx == NULL) {
        fprintf(stderr, "Failed to configure encoder context\n");
        return -1;
    }

    hmctx->ofmt_ctx = NULL;

    if ((ret = avformat_alloc_output_context2(&hmctx->ofmt_ctx, NULL, "mp4",
                                              NULL)) < 0 ||
        !hmctx->ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        return ret;
    }

    // config output video stream
    out_video_stream = avformat_new_stream(hmctx->ofmt_ctx, enc_codec);
    if (!out_video_stream) {
        fprintf(stderr, "Failed allocating output video stream\n");
        return -1;
    }
    hmctx->out_video_stream = out_video_stream;

    // config output audio stream
    out_audio_stream = avformat_new_stream(hmctx->ofmt_ctx, NULL);
    if (!out_audio_stream) {
        fprintf(stderr, "Failed allocating output video stream\n");
        return -1;
    }
    hmctx->out_audio_stream = out_audio_stream;

    ret = avcodec_parameters_copy(out_audio_stream->codecpar,
                                  hmctx->in_audio_stream->codecpar);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy audio stream codec params\n");
        return ret;
    }
    out_audio_stream->codecpar->codec_tag = 0;
    out_audio_stream->time_base = hmctx->in_audio_stream->time_base;

    if ((ret = avio_open_dyn_buf(&hmctx->ofmt_ctx->pb)) < 0) {
        fprintf(stderr, "Cannot open output file: %s\n", av_err2str(ret));
        return ret;
    }

    return 0;
}

int config_muxer(HaemaContext *hmctx, int is_init) {
    int ret;
    AVDictionary **muxer_opts = &hmctx->muxer_opts;
    *muxer_opts = NULL;
    const char *flags = is_init ? "dash+empty_moov+separate_moof"
                                : "dash+frag_keyframe+separate_moof";
    ret = av_dict_set(muxer_opts, "movflags", flags, 0);
    if (ret < 0) {
        fprintf(stderr,
                "Failed to set movflags in muxer_opts using av_dict_set\n");
        return ret;
    }
    return 0;
}

int config_enc(HaemaContext *hmctx) {
    AVCodecContext *enc_ctx = hmctx->enc_ctx;
    AVCodecContext *dec_ctx = hmctx->dec_ctx;
    int ret;

    enc_ctx->time_base = dec_ctx->pkt_timebase;
    enc_ctx->framerate = dec_ctx->framerate;
    enc_ctx->pix_fmt = AV_PIX_FMT_QSV;

    // TODO: variable out video dimensions
    enc_ctx->width = dec_ctx->width;
    enc_ctx->height = dec_ctx->height;

    // TODO: handle encoder options
    av_opt_set(enc_ctx->priv_data, "preset", "veryslow", 0);

    // Make boundaries land on IDR
    av_opt_set(enc_ctx->priv_data, "g", "60", 0);
    av_opt_set(enc_ctx->priv_data, "idr_interval", "0", 0);
    av_opt_set(enc_ctx->priv_data, "forced_idr", "1", 0);

    enc_ctx->hw_frames_ctx = av_buffer_ref(dec_ctx->hw_frames_ctx);
    if (!enc_ctx->hw_frames_ctx) {
        fprintf(stderr, "Failed to reference decoder context hw_frames_ctx\n");
        return -1;
    }

    if ((ret = avcodec_open2(enc_ctx, enc_ctx->codec, NULL)) < 0) {
        fprintf(stderr, "Failed to open encode codec: %s\n", av_err2str(ret));
        return ret;
    }

    hmctx->out_video_stream->time_base = enc_ctx->time_base;
    ret = avcodec_parameters_from_context(hmctx->out_video_stream->codecpar,
                                          enc_ctx);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy codec parameters to stream\n");
        return ret;
    }

    if ((ret = avformat_write_header(hmctx->ofmt_ctx, &hmctx->muxer_opts)) <
        0) {
        fprintf(stderr, "Error while writing stream header: %s\n",
                av_err2str(ret));
        return ret;
    }

    while (hmctx->audio_pktq->len) {
        AVPacket *pkt = packet_queue_pop(hmctx->audio_pktq);

        pkt->stream_index = OUT_AUDIO_STREAM_INDEX;
        pkt->pos = -1;
        av_packet_rescale_ts(pkt, hmctx->in_audio_stream->time_base,
                             hmctx->out_audio_stream->time_base);
        // TODO: log out audio packets

        ret = av_interleaved_write_frame(hmctx->ofmt_ctx, pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing audio packet\n");
            break;
        }
    }
    packet_queue_free(hmctx->audio_pktq);
    return 0;
}

int encode_write(HaemaContext *hmctx, AVPacket *pkt, AVFrame *frame) {
    AVCodecContext *enc_ctx = hmctx->enc_ctx;
    int ret = 0;

    av_packet_unref(pkt);

    if ((ret = avcodec_send_frame(enc_ctx, frame)) < 0) {
        fprintf(stderr, "Error during encoding: %s\n", av_err2str(ret));
        goto encode_write_end;
    }
    while (1) {
        if ((ret = avcodec_receive_packet(enc_ctx, pkt)))
            break;

        pkt->stream_index = OUT_VIDEO_STREAM_INDEX;
        // TODO: log output video packets
        av_packet_rescale_ts(pkt, hmctx->dec_ctx->pkt_timebase,
                             hmctx->out_video_stream->time_base);
        if ((ret = av_interleaved_write_frame(hmctx->ofmt_ctx, pkt)) < 0) {
            fprintf(stderr, "Error during writing data to output file: %s\n",
                    av_err2str(ret));
            return ret;
        }
    }

encode_write_end:
    if (ret == AVERROR_EOF)
        return 0;
    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}

int dec_enc(HaemaContext *hmctx, AVPacket *pkt, int64_t start_ts,
            int64_t end_ts) {
    AVCodecContext *enc_ctx = hmctx->enc_ctx;
    AVCodecContext *dec_ctx = hmctx->dec_ctx;
    AVFrame *frame;
    int ret = 0;

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error during decoding: %s\n", av_err2str(ret));
        return ret;
    }

    while (ret >= 0) {
        if (!(frame = av_frame_alloc())) {
            fprintf(stderr, "Failed allocating frame\n");
            return -1;
        }

        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding: %s\n", av_err2str(ret));
            av_frame_free(&frame);
            return ret;
        }

        if (!enc_ctx->hw_frames_ctx && (ret = config_enc(hmctx)) < 0) {
            fprintf(stderr, "Failed to configure encoder\n");
            goto dec_enc_end;
        }

        int64_t frame_ts =
            av_rescale_q(frame->pts, dec_ctx->pkt_timebase, AV_TIME_BASE_Q);

        if (frame_ts < start_ts || end_ts <= frame_ts) {
            // TODO: log video frame out of timestamp range
            goto dec_enc_end;
        }
        if ((ret = encode_write(hmctx, pkt, frame)) < 0)
            fprintf(stderr, "Error during encoding and writing\n");

    dec_enc_end:
        av_frame_free(&frame);
    }
    return ret;
}

/**
 * - seek to start and transcode duration length segment from file of
 * in_filename
 * - use encoder specified by encoder_name for video and copy audio
 * - output in mpegts format
 * - start and duration are in seconds
 * - returns -1 on error
 * - segment range is exactly [start_ts, end_ts)
 * - if segment index is zero output_buffer content is of init.mp4
 * - else it's <segment_idx>.m4s
 */
// TODO: add arguments for decoding and encoding
// TODO: factor out hw_device_ctx into a more long running context than this
// function
int hm_fmp4_segment(const char *in_filename, const char *encoder_name,
                    const double start, const double duration,
                    uint8_t **output_buffer, int *output_size) {
    int64_t start_ts = (int64_t)round(start * AV_TIME_BASE);
    int64_t end_ts = (int64_t)round((duration + start) * AV_TIME_BASE);
    HaemaContext *hmctx = malloc(sizeof(HaemaContext));
    AVPacket *pkt = NULL;
    AVBufferRef *hw_device_ctx = NULL;
    int ret;

    hmctx->in_filename = in_filename;

    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        return -1;
    }

    if ((ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_QSV,
                                      NULL, NULL, 0)) < 0) {
        fprintf(stderr, "Failed to create a QSV device. Error code: %s\n",
                av_err2str(ret));
        goto end;
    }
    hmctx->hw_device_ctx = hw_device_ctx;

    if ((ret = config_input(hmctx)) < 0) {
        fprintf(stderr, "Failed to config input '%s'\n", in_filename);
        goto end;
    }

    hmctx->dec_ctx = config_dec_ctx(hmctx->in_video_stream, hmctx->ifmt_ctx,
                                    hmctx->hw_device_ctx);
    if (hmctx->dec_ctx == NULL) {
        fprintf(stderr, "Failed to config decoder context for video stream\n");
        goto end;
    }

    if ((ret = config_output(hmctx, encoder_name)) < 0) {
        fprintf(stderr, "Failed to config output\n");
        goto end;
    }

    if ((ret = config_muxer(hmctx, 0)) < 0) {
        fprintf(stderr, "Failed to config muxer\n");
        goto end;
    }

    hmctx->audio_pktq = packet_queue_new();

    // adjust start timestamp with stream's start time stamp
    int64_t stream_start_ts =
        av_rescale_q(hmctx->in_video_stream->start_time,
                     hmctx->in_video_stream->time_base, AV_TIME_BASE_Q);

    end_ts += stream_start_ts;

    // seek based on video stream
    int64_t start_ts_vtb = av_rescale_q(start_ts, AV_TIME_BASE_Q,
                                        hmctx->in_video_stream->time_base);
    avformat_seek_file(hmctx->ifmt_ctx, hmctx->in_video_stream_index, INT64_MIN,
                       start_ts_vtb, start_ts_vtb, AVSEEK_FLAG_BACKWARD);

    avcodec_flush_buffers(hmctx->dec_ctx);
    start_ts += stream_start_ts;

    // TODO: log start and end timestamps
    int video_stream_end = 0, audio_stream_end = 0;
    while (ret >= 0 && !(video_stream_end && audio_stream_end)) {
        if ((ret = av_read_frame(hmctx->ifmt_ctx, pkt)) < 0)
            break;

        int64_t pkt_pts = av_rescale_q(
            pkt->pts, hmctx->ifmt_ctx->streams[pkt->stream_index]->time_base,
            av_get_time_base_q());
        // TODO: log input packets

        if (pkt->stream_index == hmctx->in_video_stream_index &&
            !video_stream_end) {
            if (pkt_pts >= end_ts && (pkt->flags & AV_PKT_FLAG_KEY)) {
                video_stream_end = 1;
                // TODO: log video stream ended
                goto cont_main_loop;
            }
            // decode packet then encode frame
            if ((ret = dec_enc(hmctx, pkt, start_ts, end_ts)) < 0) {
                fprintf(stderr, "Error on dec_enc %d\n", ret);
            }
            av_packet_unref(pkt);
        } else if (pkt->stream_index == hmctx->in_audio_stream_index &&
                   !audio_stream_end) {
            if (end_ts <= pkt_pts)
                audio_stream_end = 1;
            if (pkt_pts < start_ts || end_ts <= pkt_pts) {
                // TODO: log audio packet is not in timestamp range
                goto cont_main_loop;
            }

            if (!hmctx->enc_ctx->hw_frames_ctx) {
                packet_queue_push(hmctx->audio_pktq, pkt);
                // TODO: log encoder hw_frames_ctx not initialized yet
                goto cont_main_loop;
            }
            // copy audio codecs
            // continue;
            pkt->stream_index = OUT_AUDIO_STREAM_INDEX;
            pkt->pos = -1;
            av_packet_rescale_ts(pkt, hmctx->in_audio_stream->time_base,
                                 hmctx->out_audio_stream->time_base);
            // TODO: log output audio packet

            ret = av_interleaved_write_frame(hmctx->ofmt_ctx, pkt);
            if (ret < 0) {
                fprintf(stderr, "Error muxing audio packet\n");
                goto cont_main_loop;
            }
        }
    cont_main_loop:
        av_packet_unref(pkt);
    }

    // flush decoder
    av_packet_unref(pkt);
    if ((ret = dec_enc(hmctx, pkt, start_ts, end_ts)) < 0) {
        fprintf(stderr, "Failed to flush decoder %s\n", av_err2str(ret));
        goto end;
    }

    if ((ret = encode_write(hmctx, pkt, NULL)) < 0) {
        fprintf(stderr, "Failed to flush encoder %s\n", av_err2str(ret));
        goto end;
    }

    if ((ret = av_write_trailer(hmctx->ofmt_ctx)) < 0) {
        fprintf(stderr, "Failed to write trailer %s\n", av_err2str(ret));
        goto end;
    }

    *output_size = avio_close_dyn_buf(hmctx->ofmt_ctx->pb, output_buffer);
    hmctx->ofmt_ctx->pb = NULL;

    ret = 0;
end:
    avformat_close_input(&hmctx->ifmt_ctx);
    avformat_free_context(hmctx->ofmt_ctx);
    avcodec_free_context(&hmctx->dec_ctx);
    avcodec_free_context(&hmctx->enc_ctx);
    av_buffer_unref(&hw_device_ctx);
    av_packet_free(&pkt);
    free(hmctx);
    return ret;
}

void hm_free_buffer(uint8_t *buffer) { av_free(buffer); }

