use std::sync::Arc;

use crate::error::AppError;
use crate::state::AppState;
use crate::video::{
    StreamType, compute_video_segment, create_hls_media_playlist, get_video_info,
    get_video_keyframes, parse_segment_filename,
};
use axum::extract::State;
use axum::http::HeaderValue;
use axum::{
    extract::{Path, Request},
    http::header,
    middleware::Next,
    response::{IntoResponse, Response},
};

pub async fn error_logging_middleware(req: Request, next: Next) -> Response {
    let response = next.run(req).await;

    if let Some(err) = response.extensions().get::<AppError>() {
        println!("{err}");
    }

    response
}

pub async fn root() -> Response<String> {
    let res = Response::builder()
        .header(header::CONTENT_TYPE, "text/html")
        .body("Welcome to Haema server".to_string())
        .unwrap();
    res
}

pub async fn get_video_master_playlist(
    Path(video_id): Path<String>,
) -> Result<Response<String>, AppError> {
    let _video_info = get_video_info(&video_id).await?;
    let res = Response::builder()
        .header(header::CONTENT_TYPE, "application/vnd.apple.mpegurl")
        .body(video_id)
        .unwrap();
    Ok(res)
}

async fn get_or_compute_keyframes(
    state: &AppState,
    video_path: &str,
) -> Result<Arc<Vec<f32>>, AppError> {
    if let Some(cached_arc) = state.keyframe_cache.get(&video_path.to_string()).await {
        return Ok(cached_arc.clone());
    }

    let keyframes = get_video_keyframes(video_path).await?;
    let keyframes_arc = Arc::new(keyframes);
    state
        .keyframe_cache
        .put(video_path.to_string(), keyframes_arc.clone())
        .await;
    Ok(keyframes_arc)
}

pub async fn get_video_media_playlist(
    Path((video_id, stream_type)): Path<(String, String)>,
    State(state): State<AppState>,
) -> Result<Response<String>, AppError> {
    // let _video_info = get_video_info(&video_id).await?;
    let stream_type: StreamType = stream_type.parse()?;
    let video_path = "/mnt/d/anime/dl/01.ts";

    let keyframes = get_or_compute_keyframes(&state, video_path).await?;
    let playlist = create_hls_media_playlist(&keyframes);
    let res = Response::builder()
        .header(header::CONTENT_TYPE, "application/vnd.apple.mpegurl")
        .body(playlist)
        .unwrap();

    Ok(res)
}

pub async fn get_video_segment(
    Path((video_id, stream_type, segment_filename)): Path<(String, String, String)>,
    State(state): State<AppState>,
) -> Result<Response, AppError> {
    let stream_type: StreamType = stream_type.parse()?;
    let segment_idx = parse_segment_filename(&segment_filename)?;
    let video_path = "/mnt/d/anime/dl/01.ts";

    let keyframes = get_or_compute_keyframes(&state, video_path).await?;
    let segment = compute_video_segment(video_path, &keyframes, segment_idx).await?;

    let mut res = segment.into_response();
    res.headers_mut()
        .insert(header::CONTENT_TYPE, HeaderValue::from_static("video/MP2T"));
    Ok(res)
}
