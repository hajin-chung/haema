use crate::{
    domain::{HMff, StreamType},
    error::AppError,
    pool::PoolGuard,
};
use haema_ff_sys;
use regex::Regex;
use tokio::task;

pub fn parse_segment_filename(segment_filename: &String) -> Result<usize, AppError> {
    let re = Regex::new(r"(\d+)\.ts$").unwrap();
    let caps = re
        .captures(&segment_filename)
        .ok_or(AppError::InvalidSegmentName)?;
    let idx: usize = caps[1].parse().map_err(|_| AppError::InvalidSegmentName)?;
    Ok(idx)
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

pub fn get_video_duration(video_path: &str) -> Result<f64, AppError> {
    let video_path = video_path.to_owned();
    Ok(haema_ff_sys::get_video_duration(&video_path))
}

pub async fn compute_video_segment(
    hmff: PoolGuard<HMff>,
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

    task::spawn_blocking(move || {
        hmff.context().transcode_segment(
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

