use std::sync::Arc;

use crate::{domain::HMff, pool::Pool};

#[derive(Clone)]
pub struct AppState {
    pub hmff_pool: Arc<Pool<HMff>>,
}

impl AppState {
    pub fn new() -> Self {
        // TODO: get number of cpus
        let hmff_pool = Arc::new(Pool::new(HMff::new, 10));

        Self { hmff_pool }
    }
}

