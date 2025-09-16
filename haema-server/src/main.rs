use axum::{Router, routing::get};
use tower::ServiceBuilder;

pub mod cache;
pub mod error;
pub mod handlers;
pub mod state;
pub mod video;

use handlers::{
    error_logging_middleware, get_video_master_playlist, get_video_media_playlist,
    get_video_segment, root,
};
use tower_http::cors::{Any, CorsLayer};

#[tokio::main]
async fn main() {
    let cors = CorsLayer::new()
        .allow_origin(Any)
        .allow_headers(Any)
        .allow_methods(Any);
    let app_state = state::AppState::new();
    let app = Router::new()
        .route("/", get(root))
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
        .with_state(app_state)
        .layer(ServiceBuilder::new().layer(axum::middleware::from_fn(error_logging_middleware)))
        .layer(cors);

    let listener = tokio::net::TcpListener::bind("0.0.0.0:4001").await.unwrap();

    axum::serve(listener, app).await.unwrap();
}

