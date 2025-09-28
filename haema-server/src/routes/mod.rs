use crate::{error::AppError, state::AppState};
use axum::{
    extract::Request, 
    http::header, 
    middleware::Next, 
    routing::get, 
    response::Response,
    Router
};

pub mod video_routes;

pub fn create_router() -> Router<AppState> {
    Router::new()
        .merge(video_routes::create_router())
        .route("/", get(root))
}

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

