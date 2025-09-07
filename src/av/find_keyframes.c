#include <libavutil/dict.h>
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/mem.h>
#include <libavutil/timestamp.h>

// Dumps key information about a single AVStream to stdout.
void dump_stream_info(const AVStream *stream) {
  if (!stream) {
    fprintf(stderr, "dump_stream_info: Provided stream is NULL\n");
    return;
  }

  const AVCodecParameters *codecpar = stream->codecpar;
  if (!codecpar) {
    fprintf(stderr, "dump_stream_info: Stream #%d has no codec parameters\n", stream->index);
    return;
  }

  // --- General Info ---
  fprintf(stderr, "Stream #%d:\n", stream->index);
  const char *codec_type = av_get_media_type_string(codecpar->codec_type);
  const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
  fprintf(stderr, "  Codec Type: %s\n", codec_type ? codec_type : "unknown");
  fprintf(stderr, "  Codec Name: %s\n", codec ? codec->long_name : "unknown");
  fprintf(stderr, "  Timebase: %d/%d\n", stream->time_base.num, stream->time_base.den);

  // --- Type-Specific Info ---
  if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    fprintf(stderr, "  Format: %s\n", av_get_pix_fmt_name(codecpar->format));
    fprintf(stderr, "  Dimensions: %dx%d\n", codecpar->width, codecpar->height);
    if (stream->avg_frame_rate.num != 0) {
      fprintf(stderr, "  Frame Rate: %.2f fps\n", av_q2d(stream->avg_frame_rate));
    }
  } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
    char ch_layout_str[128];
    av_channel_layout_describe(&codecpar->ch_layout, ch_layout_str, sizeof(ch_layout_str));
    fprintf(stderr, "  Format: %s\n", av_get_sample_fmt_name(codecpar->format));
    fprintf(stderr, "  Sample Rate: %d Hz\n", codecpar->sample_rate);
    fprintf(stderr, "  Channel Layout: %s\n", ch_layout_str);
  }

  // --- Metadata ---
  fprintf(stderr, "  Metadata:\n");
  const AVDictionaryEntry *tag = NULL;
  while ((tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
    fprintf(stderr, "    %s: %s\n", tag->key, tag->value);
  }
  fprintf(stderr, "\n");
}

void dump_program_info(const AVProgram *program) {
  if (!program) {
    fprintf(stderr, "dump_program_info: Provided program is NULL\n");
  }

  fprintf(stderr, "Program %d\n", program->id);
  const AVDictionaryEntry *tag = NULL;
  if (av_dict_count(program->metadata) > 0) {
    fprintf(stderr, "  Metadata:\n");
    while ((tag = av_dict_get(program->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
      fprintf(stderr, "    %s: %s\n", tag->key, tag->value);
    }
  }
}

char *limit(char *str, int limit) {
  str[limit] = '\0';
  return str;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("key: %d\tstream_index:%d\tpts_time:%s\tdts_time:%s\tduration_time:%s\n", 
           pkt->flags & AV_PKT_FLAG_KEY, 
           pkt->stream_index,
           limit(av_ts2timestr(pkt->pts, time_base), 6), 
           limit(av_ts2timestr(pkt->dts, time_base), 6), 
           limit(av_ts2timestr(pkt->duration, time_base), 6));
}


int main(int argc, char **argv) {
  AVFormatContext *ifmt_ctx = NULL;
  AVPacket *pkt = NULL;
  int ret, i;

  if (argc != 2) {
    fprintf(stderr, "usage: %s input\n", argv[0]);
    return 1;
  } 

  const char *in_filename = argv[1];

  pkt = av_packet_alloc();
  if (!pkt) {
    fprintf(stderr, "Could not allocate AVPacket\n");
    return 1;
  }

  if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
    fprintf(stderr, "Could not open input file '%s'\n", in_filename);
    goto end;
  }

  if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
    fprintf(stderr, "Failed to retrieve input stream information\n");
    goto end;
  }

  fprintf(stderr, "Input format\n");
  av_dump_format(ifmt_ctx, 0, in_filename, 0);

  fprintf(stderr, "input file has %d streams\n", ifmt_ctx->nb_streams);
  int video_stream_index = -1;
  for (i = 0; i < ifmt_ctx->nb_streams; i++) {
    AVStream *in_stream = ifmt_ctx->streams[i];
    if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
      video_stream_index = i;

    dump_stream_info(in_stream);
  }

  if (video_stream_index == -1) {
    fprintf(stderr, "input file has no video stream\n");
  }

  while (1) {
    ret = av_read_frame(ifmt_ctx, pkt);
    if (ret < 0)
      break;

    if (pkt->stream_index == video_stream_index)
      log_packet(ifmt_ctx, pkt);

    av_packet_unref(pkt);
  }

end:
  av_packet_free(&pkt);

  avformat_close_input(&ifmt_ctx);
}
