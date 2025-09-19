# Haema

simple self hosted streaming service in one binary and conf files

## features

1. dynamic hls(fmp4) packaging
    1. fake m3u8 based on keyframes of source video
    2. transcode or cut source video on demand
2. keep track of watch history
3. file structure as source of truth

## usage

```
haema
- p, port: port number
- h, host: host address
- t, target_path: path of videos to serve
- db: path of sqlite3 db file
- cache <true|false>: enable or disable cache
- cache-path: path to cache directory
- cache-limit: set cache limit
```

## Check list

- [ ] implement video streaming endpoints
    - [x] write test code for hm_transcode and check if output segments are aligned
    - [x] fix hm_transcode segment video stream timestamp alignment error
    - [x] output transcoded result to buffer and return that buffer currently it writes to stdout
    - [x] generate flame graph to analyze which part takes the most time
    - [x] rust ffi bindings for hm_transcode + project restructuring
    - [ ] create init.mp4
    - [ ] mux output into fmp4
    - [ ] pass encoder params to hm_transcode
        - [x] send encoder codec
        - [ ] send resolution
- [ ] implement metadata endpoints (db, video metadata, indexing ...etc)
- [ ] create docker image that builds ffmpeg with just the things hamea uses
- [ ] create benchmarks


## API

1. list shows (movie, series)
2. search show
3. get show data
4. play show
    - support multiple resolutions
5. save watch progress

```typescript
type Show = Movie | Series;
type ShowInfo = {
    id: string,
    title: string,
    thumbnail: string,
    banner: string,
};
type Series = {
    info: ShowInfo
    episodes: Episode[] 
};
type Episode = {
    id: string,
    index: string,
    title: string,
};
type Movie = {
    info: ShowInfo,
};
```

1. list & search show
    * GET /api/v1/shows -> Show[]
    * to search add query param: GET /api/v1/shows?search=query
2. get show info
    * GET /api/v1/shows/<show_id> -> Show
3. get HLS playlist
    * GET /api/v1/video/<video_id>/master.m3u8 -> master hls playlist
    * GET /api/v1/video/<video_id>/<resolution_codec>/stream.m3u8 -> media hls playlist
    * GET /api/v1/video/<video_id>/<resolution_codec>/<segment_idx>.ts -> video segement

# notes

## ffmpeg

### goals

1. ~~find I-frame timestamp or frame number quickly~~
2. ~~seek to i'th I-frame and transcode until (i+1)'th I-frame~~

I thought to have a smoothly playable and super fast streaming I had to cut 
streams based on I-frames since I-frames are fast to seek to. so naively thought 
I needed to get timestamps of all I-frames as fast as possible but thinking 
about how seeking works in ffmpeg I realized that you don't have to get the 
exact timestamp of all I-frames. you don't even need I-frame timestamps at all.

for example if I have a video of length 6 seconds I could just do this and when
1.ts request comes in I can just seek to 2.0 seconds and ffmpeg would 
automatically seek to the largest key frame smaller that or equal to 2.0 seconds
and start decode until we hit the 2.0 second timestamp then encode until 4.0 seconds.

```hls
#EXTINF:2.0
0.ts
#EXTINF:2.0
1.ts
#EXTINF:2.0
2.ts
```

====> double seeking

as long as keyframes of the videos are granularly distributed this would result in
very fast .ts file packaging. enabling on-demand streaming.

1. seek to largest keyframe that is smaller or equal than request segment.
2. decode until start of segment then start transcoding until end of segment.

### useful functions

almost like a reading list

1. retrieving I-frames
    `libavformat/avformat.h > av_read_frame`    
    `libavutil/frame.h > AVFrame.flags`
    `libavutil/frame.h > AV_FRAME_FLAG_KEY`

2. for fast seeking
    `libavformat/avformat.h > av_seek_frame`
    `libavformat/avformat.h > avformat_seek_file`
    `libavformat/avformat.h > AVSEEK_FLAG_FRAME`

### ffmpeg demux -> decode -> encode -> mux tutorial

**Flow Overview**

1. demux
    1. open input file
        `libavformat/avformat.h > avformat_open_input`
    2. retrieve input stream info
        `libavformat/avformat.h > avformat_find_stream_info`
    3. read frames from input file
        `libavformat/avformat.h > av_read_frame`
2. decode
    TODO
3. encode
    TODO
4. mux
    1. allocate output context
        `libavformat/avformat.h > avformat_alloc_output_context2`
    2. create streams
        `libavformat/avformat.h > avformat_new_stream`
    3. write header
        `libavformat/avformat.h > avformat_write_header`
    4. write frame
        `libavformat/avformat.h > av_interleaved_write_frame`
    5. write trailer
        `libavformat/avformat.h > av_write_trailer`

```bash
cd FFmpeg
./configure --enable-vaapi --enable-libvpl
make -j12
sudo make install
```

```bash
ffmpeg version n7.1.1-57-g1b48158a23 Copyright (c) 2000-2025 the FFmpeg developers
  built with gcc 13 (Ubuntu 13.3.0-6ubuntu2~24.04)
  configuration: --enable-vaapi --enable-libvpl
  libavutil      59. 39.100 / 59. 39.100
  libavcodec     61. 19.101 / 61. 19.101
  libavformat    61.  7.100 / 61.  7.100
  libavdevice    61.  3.100 / 61.  3.100
  libavfilter    10.  4.100 / 10.  4.100
  libswscale      8.  3.100 /  8.  3.100
  libswresample   5.  3.100 /  5.  3.100
```


```bash
sudo apt install pkg-config
sudo apt install libvpl-dev 
sudo apt install libva-dev
sudo apt install libva-drm2
sudo apt install libdrm-dev
sudo apt install libmfx-gen1
sudo apt install intel-media-va-driver-non-free
```

```bash
$ time ./bin/hm_transcode in/in.mp4 h264_qsv 0.00 4.00 > out/0.ts 2> out/0.log

real    0m1.066s
user    0m0.522s
sys     0m0.408s

$ time ffmpeg -ss 4 -i in/in.mp4 -t 4 -c:v h264_qsv -c:a copy -f mpegts out.ts > out/f0.ts 2> out/f0.log
real    0m1.742s
user    0m5.537s
sys     0m0.961s
```

fixed ffmpeg libavcodec/qsvdec thread leak caused by a refcount leak  
[pull request](https://code.ffmpeg.org/FFmpeg/FFmpeg/pulls/20532)

after some tests mpegts is bad. it doesn't support av1 (spec still in progress). switching to fmp4


```
  Program A: Generating init.mp4

  The job of this program is to run once per source video. It reads the source file's metadata and creates a single init.mp4 file. This file is small and contains the "recipe" that a video player needs to understand
  the stream (codecs, timescale, dimensions, etc.).

  The process is very similar to the previous example, but you stop after writing the header.

  Workflow:

   1. Open Input: Open your source MP4 file (avformat_open_input).
   2. Allocate Output Context: Allocate an AVFormatContext for the output. You don't need to specify a format; it will be deduced from the filename.
   1     avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, "init.mp4");
   3. Set Muxer Options: This is the critical step. You need to tell the mp4 muxer to generate a fragmented header. The easiest way is with the dash movflag, which enables all the necessary fragmentation options
      (empty_moov, frag_keyframe, etc.).

   1     // Set the fragmentation flags on the output context's private data
   2     av_opt_set(ofmt_ctx->priv_data, "movflags", "dash", 0);
   4. Copy Streams: Create new streams on the output context and copy the codec parameters from the input streams using avcodec_parameters_copy().
   5. Write Header: Open the output file (avio_open) and call avformat_write_header().

   1     // This is the call that actually creates and writes the init.mp4 file
   2     ret = avformat_write_header(ofmt_ctx, NULL);
   6. Stop: That's it. After avformat_write_header succeeds, the init.mp4 is complete. You can now call av_write_trailer() (which does nothing in this case but is good practice) and then clean up and close all your
      contexts. You do not write any frames.

  ---

  Program B: Generating a Media Segment (segmentN.m4s)

  This program would be called by your server on-demand, for example, when a user's browser requests /video/segment5.m4s. It needs to generate just that one piece of the video.

  Workflow:

   1. Get Request Parameters: Your program would need to know the source file, the start time of the segment, and the segment number. Let's say the request is for segment #5, which starts at 20.0 seconds and has a
      duration of 4.0 seconds.

   2. Open Input and Seek: Open the original source MP4 file. Use av_seek_frame() to jump directly to the start of the desired segment. It's best to seek to the keyframe at or just before your target start time.
   1     // Seek to the 20-second mark.
   2     av_seek_frame(ifmt_ctx, -1, 20 * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);

   3. Setup Output Context: This is very similar to Program A, but with two key differences.

   1     avformat_alloc_output_context2(&ofmt_ctx, NULL, "mp4", "segment5.m4s");
   2
   3     // 1. Set the EXACT SAME movflags as before
   4     av_opt_set(ofmt_ctx->priv_data, "movflags", "dash", 0);
   5
   6     // 2. CRITICAL: Set the fragment start number. This tells the muxer
   7     // to number this media segment correctly (e.g., as the 5th segment).
   8     av_opt_set_int(ofmt_ctx->priv_data, "frag_num", 5, 0);

   4. Copy Streams & Write Header: Copy the codec parameters just as before. When you call avformat_write_header(), the muxer knows not to write another moov atom because the empty_moov flag (part of dash) is set. It
      simply prepares to write the media fragment.

   5. Remuxing Loop: Now, you read packets from the seeked position in the input file and write them to the output.
       * Read frames with av_read_frame().
       * Keep track of the duration of the packets you've written.
       * Stop once you've written about 4 seconds of data and have ended on a keyframe.
       * Write each packet with av_interleaved_write_frame().

   6. Finalize Segment: Call av_write_trailer() to flush any buffered data and correctly finalize the .m4s segment file. Clean up and exit.

  This architecture is highly efficient because you can generate any segment of the video without having to process the file from the beginning every time.
```
