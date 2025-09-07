use regex::Regex;
use std::fmt;
use std::str::FromStr;
use tokio::process::Command;

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
            VideoCodec::AV1 => write!(f, "AV1"),
            VideoCodec::H264 => write!(f, "H264"),
            VideoCodec::H265 => write!(f, "H265"),
            VideoCodec::None => write!(f, "None"),
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

pub async fn get_video_keyframes(path: &str) -> Result<Vec<f32>, AppError> {
    // ffprobe -select_streams v:0 -show_entries packet=pts_time,flags -of csv=p=0 <path>
    let mut command = Command::new("ffprobe");
    command.args([
        "-select_streams",
        "v:0",
        "-show_entries",
        "packet=pts_time,flags",
        "-of",
        "csv=p=0",
        path,
    ]);

    println!(
        "command: {:?} {:?}",
        command.as_std().get_program(),
        command.as_std().get_args().collect::<Vec<_>>(),
    );

    let output = command.output().await.map_err(|err| {
        AppError::CommandFail(format!("get video keyframes using ffprobe failed: {err}"))
    })?;

    if !output.status.success() {
        return Err(AppError::CommandFail(format!(
            "get video keyframes using ffprobe status failed"
        )));
    }

    let stdout = String::from_utf8(output.stdout).map_err(|err| {
        AppError::CommandFail(format!(
            "get video keyframes using ffprobe failed to parse stdout: {err}"
        ))
    })?;

    let re = Regex::new(r"(?<timestamp>[\d\.]+),K").unwrap();
    let mut keyframes: Vec<f32> = re
        .captures_iter(&stdout)
        .map(|caps| {
            caps.name("timestamp")
                .unwrap()
                .as_str()
                .parse::<f32>()
                .unwrap()
            // TODO: proper error handling
            // .map_err(|err| {
            //     AppError::CommandFail(format!(
            //         "get video keyframes failed to parse keyframes to float: {err}"
            //     ))
            // })
        })
        .collect();

    // add last timestamp to better calculate durations of each segment
    let re = Regex::new(r"(?<timestamp>[\d\.]+),").unwrap();
    if let Some(last_line) = stdout.lines().last() {
        if let Some(caps) = re.captures(last_line) {
            let cap = caps.name("timestamp").unwrap().as_str().parse::<f32>();
            if let Ok(timestamp) = cap {
                keyframes.push(timestamp);
            }
        }
    }
    Ok(keyframes)
}

pub fn create_hls_media_playlist(keyframes: &Vec<f32>) -> String {
    let mut durations = vec![keyframes[0]];
    durations.extend::<Vec<f32>>(
        keyframes
            .windows(2)
            .map(|window| window[1] - window[0])
            .collect(),
    );
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
        playlist += "#EXT-X-DISCONTINUITY\n";
        playlist += format!("#EXTINF:{}\n", duration).as_str();
        playlist += format!("{}.ts\n", idx).as_str();
    });
    playlist += "#EXT-X-ENDLIST\n";
    playlist
}

pub async fn compute_video_segment(
    video_path: &str,
    keyframes: &Vec<f32>,
    segment_idx: usize,
) -> Result<Vec<u8>, AppError> {
    let start: f32 = if segment_idx == 0 {
        0.0
    } else {
        keyframes[segment_idx - 1]
    };
    let end: f32 = keyframes[segment_idx];
    let duration = end - start;

    let mut command = Command::new("./segment_transcode");
    command.args([
        video_path,
        "h264",
        &start.to_string(),
        &duration.to_string(),
    ]);

    if let Some(program) = command.as_std().get_program().to_str() {
        print!("command: {} ", program);
        command.as_std().get_args().for_each(|arg_option| {
            if let Some(arg) = arg_option.to_str() {
                print!("{} ", arg)
            }
        });
        println!("");
    }

    let output = command.output().await.map_err(|err| {
        AppError::CommandFail(format!("compute video segment using ffmpeg failed: {err}"))
    })?;

    if !output.status.success() {
        let len = output.stdout.len();
        eprintln!("{len}");
        let stderr = String::from_utf8(output.stderr)
            .map_err(|_err| AppError::Error("".to_string()))?;
        eprintln!("{}", stderr);
        return Err(AppError::CommandFail(format!(
            "compute video segment using ffmpeg status failed"
        )));
    }

    // let len = output.stdout.len();
    // eprintln!("{len}");
    // let stderr = String::from_utf8(output.stderr)
    //     .map_err(|_err| AppError::Error("".to_string()))?;
    // eprintln!("{}", stderr);


    Ok(output.stdout)
}
