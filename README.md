# Haema

simple self hosted streaming service in one binary and conf files

## features

1. dynamic hls packaging
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
    - [ ] write test code for hm_transcode and check if output segments are aligned
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

