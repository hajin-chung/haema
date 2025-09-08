#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "../include/hm_util.h"

const int OUT_VIDEO_STREAM_INDEX = 0;
const int OUT_AUDIO_STREAM_INDEX = 1;

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

int config_input(TranscodeContext *tctx) {
    int ret;
    tctx->ifmt_ctx = NULL;
    if ((ret = avformat_open_input(&tctx->ifmt_ctx, tctx->in_filename, 0, 0)) <
        0) {
        fprintf(stderr, "Could not open input filename '%s'\n",
                tctx->in_filename);
        return ret;
    }

    if ((ret = avformat_find_stream_info(tctx->ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information\n");
        return ret;
    }

    if ((ret = av_find_best_stream(tctx->ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1,
                                   NULL, 0)) < 0) {
        fprintf(stderr, "Cannot find a video stream in input file: %s\n",
                av_err2str(ret));
        return ret;
    }
    tctx->in_video_stream_index = ret;
    tctx->in_video_stream = tctx->ifmt_ctx->streams[ret];

    tctx->dec_ctx = config_dec_ctx(tctx->in_video_stream, tctx->ifmt_ctx,
                                   tctx->hw_device_ctx);
    if (tctx->dec_ctx == NULL) {
        fprintf(stderr, "Failed to config decoder context for video stream\n");
        return -1;
    }

    if ((ret = av_find_best_stream(tctx->ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1,
                                   NULL, 0)) < 0) {
        fprintf(stderr, "Cannot find a audio stream in input file: %s\n",
                av_err2str(ret));
        return ret;
    }
    tctx->in_audio_stream_index = ret;
    tctx->in_audio_stream = tctx->ifmt_ctx->streams[ret];

    return 0;
}

int config_output(TranscodeContext *tctx) {
    AVStream *out_video_stream, *out_audio_stream;
    int ret;

    tctx->ofmt_ctx = NULL;

    const AVOutputFormat *mpegts_ofmt =
        av_guess_format(NULL, NULL, "video/MP2T");
    if (mpegts_ofmt == NULL) {
        fprintf(stderr, "Failed guessing mpegts format\n");
        return -1;
    }

    if ((ret = avformat_alloc_output_context2(&tctx->ofmt_ctx, mpegts_ofmt,
                                              NULL, NULL)) < 0 ||
        !tctx->ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        return ret;
    }

    // TODO: implement variable codec
    const char *encoder_name = "h264_qsv";
    const AVCodec *enc_codec = avcodec_find_encoder_by_name(encoder_name);
    if (!enc_codec) {
        fprintf(stderr, "Could not find encoder: %s\n", encoder_name);
        return -1;
    }
    tctx->enc_ctx = avcodec_alloc_context3(enc_codec);
    if (tctx->enc_ctx == NULL) {
        fprintf(stderr, "Failed to configure encoder context\n");
        return -1;
    }

    // config output video stream
    out_video_stream = avformat_new_stream(tctx->ofmt_ctx, enc_codec);
    if (!out_video_stream) {
        fprintf(stderr, "Failed allocating output video stream\n");
        return -1;
    }
    tctx->out_video_stream = out_video_stream;

    // config output audio stream
    out_audio_stream = avformat_new_stream(tctx->ofmt_ctx, NULL);
    if (!out_audio_stream) {
        fprintf(stderr, "Failed allocating output video stream\n");
        return -1;
    }
    tctx->out_audio_stream = out_audio_stream;

    ret = avcodec_parameters_copy(out_audio_stream->codecpar,
                                  tctx->in_audio_stream->codecpar);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy audio stream codec params\n");
        return ret;
    }
    out_audio_stream->codecpar->codec_tag = 0;

    ret = avio_open(&tctx->ofmt_ctx->pb, "pipe:1", AVIO_FLAG_WRITE);
    if (ret < 0) {
        fprintf(stderr, "Cannot open output file: %s\n", av_err2str(ret));
        return ret;
    }

    return 0;
}

int encode_write(TranscodeContext *tctx, AVPacket *pkt, AVFrame *frame,
                 int do_write) {
    AVCodecContext *enc_ctx = tctx->enc_ctx;
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
        av_packet_rescale_ts(pkt, enc_ctx->time_base,
                             tctx->out_video_stream->time_base);
        log_packet(tctx, pkt, "out");
        if (!do_write) {
            fprintf(stderr, "don't write yet!\n");
            continue;
        }
        if ((ret = av_interleaved_write_frame(tctx->ofmt_ctx, pkt)) < 0) {
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

int dec_enc(TranscodeContext *tctx, AVPacket *pkt, int64_t start_ts,
            int64_t end_ts) {
    AVCodecContext *enc_ctx = tctx->enc_ctx;
    AVCodecContext *dec_ctx = tctx->dec_ctx;
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
            goto dec_enc_end;
        }

        if (!enc_ctx->hw_frames_ctx) {
            /* we need to ref hw_frames_ctx of decoder to initialize encoder's
               codec. Only after we get a decoded frame, can we obtain its
               hw_frames_ctx */
            enc_ctx->hw_frames_ctx = av_buffer_ref(dec_ctx->hw_frames_ctx);
            if (!enc_ctx->hw_frames_ctx) {
                fprintf(stderr,
                        "Failed to reference decoder context hw_frames_ctx\n");
                goto dec_enc_end;
            }

            enc_ctx->time_base = av_inv_q(dec_ctx->framerate);
            enc_ctx->pix_fmt = AV_PIX_FMT_QSV;

            // TODO: variable out video dimensions
            enc_ctx->width = dec_ctx->width;
            enc_ctx->height = dec_ctx->height;

            // TODO: handle encoder options

            if ((ret = avcodec_open2(enc_ctx, enc_ctx->codec, NULL)) < 0) {
                fprintf(stderr, "Failed to open encode codec: %s\n",
                        av_err2str(ret));
                goto dec_enc_end;
            }

            tctx->out_video_stream->time_base = enc_ctx->time_base;
            ret = avcodec_parameters_from_context(
                tctx->out_video_stream->codecpar, enc_ctx);
            if (ret < 0) {
                fprintf(stderr, "Failed to copy codec parameters to stream\n");
                goto dec_enc_end;
            }

            if ((ret = avformat_write_header(tctx->ofmt_ctx, NULL)) < 0) {
                fprintf(stderr, "Error while writing stream header: %s\n",
                        av_err2str(ret));
                goto dec_enc_end;
            }

            while (tctx->audio_pktq->len) {
                AVPacket *pkt = packet_queue_pop(tctx->audio_pktq);

                pkt->stream_index = OUT_AUDIO_STREAM_INDEX;
                av_packet_rescale_ts(pkt, tctx->in_audio_stream->time_base,
                                     tctx->out_audio_stream->time_base);
                pkt->pos = -1;
                log_packet(tctx, pkt, "out");

                ret = av_interleaved_write_frame(tctx->ofmt_ctx, pkt);
                if (ret < 0) {
                    fprintf(stderr, "Error muxing audio packet\n");
                    break;
                }
            }
            packet_queue_free(tctx->audio_pktq);

            // dump_transcode_context(tctx);
        }

        int64_t frame_ts =
            av_rescale_q(frame->pts, dec_ctx->pkt_timebase, AV_TIME_BASE_Q);

        int do_write = start_ts <= frame_ts && frame_ts < end_ts;
        frame->pts =
            av_rescale_q(frame->pts, dec_ctx->pkt_timebase, enc_ctx->time_base);
        if ((ret = encode_write(tctx, pkt, frame, do_write)) < 0)
            fprintf(stderr, "Error during encoding and writing\n");

        if (frame_ts < start_ts) {
            fprintf(stderr,
                    "Decoded frame timestamp %ld is smaller than start "
                    "timestamp %ld\n",
                    frame_ts, start_ts);
            goto dec_enc_end;
        }
        if (end_ts <= frame_ts) {
            fprintf(stderr,
                    "Decoded frame timestamp %ld is bigger than or equal to "
                    "end timestamp %ld\n",
                    frame_ts, end_ts);
            return 0;
        }

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
 */
// TODO: add arguments for decoding and encoding
// TODO: return pointer to buffer
int transcode_segment(const char *in_filename, const char *encoder_name,
                      const double start, const double duration) {
    int64_t start_ts = (int64_t)(start * AV_TIME_BASE);
    int64_t end_ts = start_ts + (int64_t)(duration * AV_TIME_BASE);
    TranscodeContext *tctx = malloc(sizeof(TranscodeContext));
    AVPacket *pkt = NULL;
    AVBufferRef *hw_device_ctx = NULL;
    int ret;

    tctx->in_filename = in_filename;

    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        return -1;
    }

    if ((ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_QSV,
                                      NULL, NULL, 0)) < 0) {
        fprintf(stderr, "Failed to create a QSV devoce. Error code: %s\n",
                av_err2str(ret));
        goto end;
    }
    tctx->hw_device_ctx = hw_device_ctx;

    if ((ret = config_input(tctx)) < 0) {
        fprintf(stderr, "Failed to config input '%s'\n", in_filename);
        goto end;
    }

    if ((ret = config_output(tctx)) < 0) {
        fprintf(stderr, "Failed to config output\n");
        goto end;
    }

    tctx->audio_pktq = packet_queue_new();

    dump_transcode_context(tctx);

    // // adjust start timestamp with stream's start time stamp
    int64_t stream_start_ts =
        av_rescale_q(tctx->in_audio_stream->start_time,
                     tctx->in_audio_stream->time_base, AV_TIME_BASE_Q);
    end_ts += stream_start_ts;

    // // seek to start_ts

    // av_seek_frame(tctx->ifmt_ctx, tctx->in_video_stream_index,
    //               av_rescale_q(start_ts,
    //                            AV_TIME_BASE_Q,
    //                            tctx->in_video_stream->time_base),
    //               AVSEEK_FLAG_BACKWARD);

    // av_seek_frame(tctx->ifmt_ctx, -1, start_ts, AVSEEK_FLAG_BACKWARD);

    // int64_t start_ts_atb = av_rescale_q(start_ts, AV_TIME_BASE_Q,
    //                                     tctx->in_audio_stream->time_base);
    // avformat_seek_file(tctx->ifmt_ctx, tctx->in_audio_stream_index,
    // INT64_MIN,
    //                    start_ts_atb, start_ts_atb, AVSEEK_FLAG_BACKWARD);

    int64_t start_ts_vtb = av_rescale_q(start_ts, AV_TIME_BASE_Q,
                                        tctx->in_video_stream->time_base);
    avformat_seek_file(tctx->ifmt_ctx, tctx->in_video_stream_index, INT64_MIN,
                       start_ts_vtb, start_ts_vtb, AVSEEK_FLAG_BACKWARD);

    // avformat_seek_file(tctx->ifmt_ctx, -1, INT64_MIN, start_ts, start_ts,
    //                    AVSEEK_FLAG_BACKWARD);
    start_ts += stream_start_ts;

    fprintf(stderr, "start: %ld\tend: %ld\n", start_ts, end_ts);
    int video_stream_end = 0, audio_stream_end = 0;
    while (ret >= 0 && !(video_stream_end && audio_stream_end)) {
        if ((ret = av_read_frame(tctx->ifmt_ctx, pkt)) < 0)
            break;

        int64_t pkt_pts = av_rescale_q(
            pkt->pts, tctx->ifmt_ctx->streams[pkt->stream_index]->time_base,
            av_get_time_base_q());
        log_packet(tctx, pkt, "in");

        if (pkt->stream_index == tctx->in_video_stream_index &&
            !video_stream_end) {
            if (pkt_pts > end_ts) {
                video_stream_end = 1;
                fprintf(stderr, "video stream end pkt_pts %ld > end_ts %ld\n",
                        pkt_pts, end_ts);
                goto cont_main_loop;
            }
            // decode packet then encode frame
            dec_enc(tctx, pkt, start_ts, end_ts);
            av_packet_unref(pkt);
        } else if (pkt->stream_index == tctx->in_audio_stream_index &&
                   !audio_stream_end) {
            if (pkt_pts >= end_ts) {
                audio_stream_end = 1;
                fprintf(stderr, "audio stream end pkt_pts %ld > end_ts %ld\n",
                        pkt_pts, end_ts);
                goto cont_main_loop;
            }
            if (pkt_pts < start_ts) {
                fprintf(stderr,
                        "Audio packet pts %ld is smaller than start "
                        "timestamp %ld\n",
                        pkt_pts, start_ts);
                goto cont_main_loop;
            }

            if (!tctx->enc_ctx->hw_frames_ctx) {
                packet_queue_push(tctx->audio_pktq, pkt);
                fprintf(stderr, "encoder hw_frames_ctx not initialized yet\n");
                goto cont_main_loop;
            }
            // copy audio codecs
            // continue;
            pkt->stream_index = OUT_AUDIO_STREAM_INDEX;
            av_packet_rescale_ts(pkt, tctx->in_audio_stream->time_base,
                                 tctx->out_audio_stream->time_base);
            pkt->pos = -1;
            log_packet(tctx, pkt, "out");

            ret = av_interleaved_write_frame(tctx->ofmt_ctx, pkt);
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
    if ((ret = dec_enc(tctx, pkt, start_ts, end_ts)) < 0) {
        fprintf(stderr, "Failed to flush decoder %s\n", av_err2str(ret));
        goto end;
    }

    if ((ret = encode_write(tctx, pkt, NULL, 1)) < 0) {
        fprintf(stderr, "Failed to flush encoder %s\n", av_err2str(ret));
        goto end;
    }

    if ((ret = av_write_trailer(tctx->ofmt_ctx)) < 0) {
        fprintf(stderr, "Failed to write trailer %s\n", av_err2str(ret));
        goto end;
    }

    ret = 0;
end:
    avformat_close_input(&tctx->ifmt_ctx);
    avformat_close_input(&tctx->ofmt_ctx);
    avcodec_free_context(&tctx->dec_ctx);
    avcodec_free_context(&tctx->enc_ctx);
    av_buffer_unref(&hw_device_ctx);
    av_packet_free(&pkt);
    return ret;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <input file> <encoder> <start> <duration>\n",
                argv[0]);
        return 1;
    }

    const char *in_filename = argv[1];
    const char *encoder_name = argv[2];
    const double start = atof(argv[3]);
    const double duration = atof(argv[4]);
    int ret = transcode_segment(in_filename, encoder_name, start, duration);

    if (ret < 0) {
        fprintf(stderr,
                "failed to transcode segment from %s starting at %lf for %lf "
                "seconds",
                in_filename, start, duration);
        return 0;
    }
}

