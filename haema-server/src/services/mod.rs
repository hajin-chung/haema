pub mod video_service;

pub use video_service::{
    get_video_duration, 
    compute_video_segment, 
    parse_segment_filename, 
    create_hls_media_playlist
};
