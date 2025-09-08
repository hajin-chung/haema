/*
 * Haema Probe
 * Copyright (c) 2025 Hajin Chung <hajinchung1@gmail.com>
 * Don't know what to say here
 * just do whatever you want with this code
 *
 * Haema Probe is binary + library for correctly getting total duration of best
 * video stream.
 */
#include <stdio.h>

#include "../include/hm_util.h"

double hm_probe(const char *in_filename) {
    AVFormatContext *ifmt_ctx = NULL;
    AVStream *vs = NULL;
    int vs_idx;
    int ret;

    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input file '%s'\n", in_filename);
        return 0;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information\n");
        return 0;
    }
    
    if ((ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0) {
        fprintf(stderr, "Could not find a video stream in input file '%s'\n", in_filename);
        return 0;
    }
    vs_idx = ret;
    vs = ifmt_ctx->streams[vs_idx];
    
    int64_t duration_vstb = vs->duration;
    int64_t duration_ts = av_rescale_q(duration_vstb, vs->time_base, AV_TIME_BASE_Q);
    double duration_s = (double)duration_ts / AV_TIME_BASE;

    return duration_s;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <input file>\n", argv[0]);
        return 1;
    } 

    const char *in_filename = argv[1];
    float ret = hm_probe(in_filename);
    printf("%lf\n", ret);
    return 0;
}
