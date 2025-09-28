pub mod models;

use haema_ff_sys::HMContext;
pub use models::{VideoCodec, AudioCodec, StreamType, SEGMENT_DURATION};

pub struct HMff(pub HMContext);

// Send and Sync are fulfilled by the Pool
unsafe impl Send for HMff {}
unsafe impl Sync for HMff {}

impl HMff {
    pub fn new() -> Self {
        HMff(HMContext::new())
    }

    pub fn context(&self) -> &HMContext {
        &self.0
    }
}
