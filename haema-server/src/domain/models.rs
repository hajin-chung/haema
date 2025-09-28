use std::{fmt, str::FromStr};

use crate::error::AppError;

pub const SEGMENT_DURATION: f64 = 4.0;

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
