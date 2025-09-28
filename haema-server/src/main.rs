use tower::ServiceBuilder;
use tower_http::cors::{Any, CorsLayer};

use haema_server::routes::{self, error_logging_middleware};
use haema_server::state::AppState;

#[tokio::main]
async fn main() {
    let cors = CorsLayer::new()
        .allow_origin(Any)
        .allow_headers(Any)
        .allow_methods(Any);
    let app_state = AppState::new();
    let app = routes::create_router()
        .with_state(app_state)
        .layer(ServiceBuilder::new().layer(axum::middleware::from_fn(error_logging_middleware)))
        .layer(cors);

    let listener = tokio::net::TcpListener::bind("0.0.0.0:4001").await.unwrap();

    axum::serve(listener, app).await.unwrap();
}

