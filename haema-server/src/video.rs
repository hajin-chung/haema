use haema_ff_sys;
use regex::Regex;
use std::fmt;
use std::str::FromStr;
use tokio::task;

use crate::error::AppError;

pub struct VideoInfo {}

pub async fn get_video_info(video_id: &String) -> Result<VideoInfo, AppError> {
    Err(AppError::VideoNotFound(format!(
        "video with id {video_id} not found"
    )))
}

pub fn parse_segment_filename(segment_filename: &String) -> Result<usize, AppError> {
    let re = Regex::new(r"(\d+)\.ts$").unwrap();
    let caps = re
        .captures(&segment_filename)
        .ok_or(AppError::InvalidSegmentName)?;
    let idx: usize = caps[1].parse().map_err(|_| AppError::InvalidSegmentName)?;
    Ok(idx)
}

pub enum VideoCodec {
    AV1,
    H264,
    H265,
    None,
}

impl fmt::Display for VideoCodec {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            VideoCodec::AV1 => write!(f, "av1_qsv"),
            VideoCodec::H264 => write!(f, "h264_qsv"),
            VideoCodec::H265 => write!(f, "hevc_qsv"),
            VideoCodec::None => write!(f, "none"),
        }
    }
}

impl FromStr for VideoCodec {
    type Err = AppError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "av1" => Ok(VideoCodec::AV1),
            "h264" => Ok(VideoCodec::H264),
            "h265" => Ok(VideoCodec::H265),
            "none" => Ok(VideoCodec::None),
            _ => Err(AppError::InvalidCodec(s.into())),
        }
    }
}

pub enum AudioCodec {
    AAC,
    None,
}

impl fmt::Display for AudioCodec {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AudioCodec::AAC => write!(f, "AAC"),
            AudioCodec::None => write!(f, "None"),
        }
    }
}

impl FromStr for AudioCodec {
    type Err = AppError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "aac" => Ok(AudioCodec::AAC),
            "none" => Ok(AudioCodec::None),
            _ => Err(AppError::InvalidCodec(s.into())),
        }
    }
}

pub struct StreamType {
    pub resolution: String,
    pub video_codec: VideoCodec,
    pub audio_codec: AudioCodec,
}

impl FromStr for StreamType {
    type Err = AppError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let parts: Vec<&str> = s.split(",").collect();
        if parts.len() != 3 {
            return Err(AppError::InvalidStreamType(s.to_string()));
        }
        let resolution = parts[0].to_string();
        let video_codec: VideoCodec = parts[1].parse()?;
        let audio_codec: AudioCodec = parts[2].parse()?;
        Ok(StreamType {
            resolution,
            video_codec,
            audio_codec,
        })
    }
}

impl fmt::Display for StreamType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{},{},{}",
            self.resolution, self.video_codec, self.audio_codec
        )
    }
}

pub async fn get_video_duration(video_path: &str) -> Result<f64, AppError> {
    let video_path = video_path.to_owned();
    task::spawn_blocking(move || haema_ff_sys::get_video_duration(&video_path))
        .await
        .map_err(|err| AppError::Error(err.to_string()))
}

pub fn create_hls_media_playlist(video_duration: f64, segment_duration: f64) -> String {
    let mut durations: Vec<f64> = vec![];
    let mut cur: f64 = 0.0;

    while cur + segment_duration < video_duration {
        durations.push(segment_duration);
        cur += segment_duration;
    }
    durations.push(video_duration - cur);

    let target_duration: u32 = durations
        .iter()
        .max_by(|a, b| a.partial_cmp(b).unwrap())
        .copied()
        .unwrap_or(0.0)
        .ceil() as u32;

    let mut playlist = String::from("");
    playlist += "#EXTM3U\n";
    playlist += "#EXT-X-PLAYLIST-TYPE:VOD\n";
    playlist += format!("#EXT-X-TARGETDURATION:{}\n", target_duration).as_str();
    playlist += "#EXT-X-VERSION:4\n";
    playlist += "#EXT-X-MEDIA-SEQUENCE:0\n";
    durations.iter().enumerate().for_each(|(idx, duration)| {
        // playlist += "#EXT-X-DISCONTINUITY\n";
        playlist += format!("#EXTINF:{}\n", duration).as_str();
        playlist += format!("{}.ts\n", idx).as_str();
    });
    playlist += "#EXT-X-ENDLIST\n";
    playlist
}

pub async fn compute_video_segment(
    video_path: &str,
    stream_type: StreamType,
    video_duration: f64,
    segment_duration: f64,
    segment_idx: usize,
) -> Result<Vec<u8>, AppError> {
    let start: f64 = segment_duration * (segment_idx as f64);
    let duration: f64 = if start + segment_duration < video_duration {
        segment_duration
    } else {
        video_duration - start
    };
    let video_path = video_path.to_owned();

    eprintln!("{}", stream_type.video_codec.to_string());
    task::spawn_blocking(move || {
        haema_ff_sys::transcode_segment(
            &video_path,
            &stream_type.video_codec.to_string(),
            start,
            duration,
        )
    })
    .await
    .map_err(|e| AppError::Error(e.to_string()))?
    .map_err(|err| AppError::Error(format!("hm_transcode failed with code {err}")))
}

