use crate::domain::SEGMENT_DURATION;
use crate::services::{
    compute_video_segment, create_hls_media_playlist, get_video_duration, parse_segment_filename,
};
use crate::state::AppState;
use crate::{domain::StreamType, error::AppError};
use axum::{
    Router,
    extract::{Path, State},
    http::{HeaderValue, header},
    response::{IntoResponse, Response},
    routing::get,
};

pub fn create_router() -> Router<AppState> {
    Router::new()
        .route(
            "/api/v1/video/{video_id}/master.m3u8",
            get(get_video_master_playlist),
        )
        .route(
            "/api/v1/video/{video_id}/{stream_type}/stream.m3u8",
            get(get_video_media_playlist),
        )
        .route(
            "/api/v1/video/{video_id}/{stream_type}/{segment_filename}",
            get(get_video_segment),
        )
}

pub async fn get_video_master_playlist(
    Path(video_id): Path<String>,
) -> Result<Response<String>, AppError> {
    // let _video_info = get_video_info(&video_id).await?;
    let res = Response::builder()
        .header(header::CONTENT_TYPE, "application/vnd.apple.mpegurl")
        .body(video_id)
        .unwrap();
    Ok(res)
}

pub async fn get_video_media_playlist(
    Path((_video_id, stream_type)): Path<(String, String)>,
    State(_state): State<AppState>,
) -> Result<Response<String>, AppError> {
    let _stream_type: StreamType = stream_type.parse()?;
    // let video_path = "/mnt/d/vod/25.08.12 뀨.mp4";
    let video_path = "/mnt/d/anime/01.mp4";

    // TODO: cache this result
    let video_duration = get_video_duration(video_path)?;

    // TODO: configurable segment duration
    let playlist = tokio::task::spawn_blocking(move || {
        create_hls_media_playlist(video_duration, SEGMENT_DURATION)
    })
    .await
    .map_err(|err| AppError::Error(err.to_string()))?;
    let res = Response::builder()
        .header(header::CONTENT_TYPE, "application/vnd.apple.mpegurl")
        .body(playlist)
        .unwrap();

    Ok(res)
}

pub async fn get_video_segment(
    Path((_video_id, stream_type, segment_filename)): Path<(String, String, String)>,
    State(state): State<AppState>,
) -> Result<Response, AppError> {
    let stream_type: StreamType = stream_type.parse()?;
    let segment_idx = parse_segment_filename(&segment_filename)?;
    // let video_path = "/mnt/d/vod/25.08.12 뀨.mp4";
    let video_path = "/mnt/d/anime/01.mp4";

    let video_duration = get_video_duration(video_path)?;

    let hmff = state.hmff_pool.get().await;
    let segment = compute_video_segment(
        hmff,
        video_path,
        stream_type,
        video_duration,
        SEGMENT_DURATION,
        segment_idx,
    )
    .await?;

    let mut res = segment.into_response();
    res.headers_mut()
        .insert(header::CONTENT_TYPE, HeaderValue::from_static("video/MP2T"));
    Ok(res)
}

