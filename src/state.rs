use std::sync::Arc;

use crate::cache::Cache;

#[derive(Clone)]
pub struct AppState {
    pub keyframe_cache: Arc<Cache<Vec<f32>>>,
}

impl AppState {
    pub fn new() -> Self {
        Self {
            keyframe_cache: Arc::new(Cache::new()),
        }
    }
}
