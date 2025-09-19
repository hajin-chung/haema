use std::ffi::CString;
use std::os::raw::{c_char, c_double, c_int};
use std::slice;

unsafe extern "C" {
    fn hm_fmp4_segment(
        in_filename: *const c_char,
        encoder_name: *const c_char,
        start: c_double,
        duration: c_double,
        output_buffer: *mut *mut u8,
        output_size: *mut c_int,
    ) -> c_int;

    fn hm_free_buffer(buffer: *mut u8);

    fn hm_probe(in_filename: *const c_char) -> c_double;
}

pub fn fmp4_segment(
    in_filename: &str,
    encoder_name: &str,
    start: f64,
    duration: f64,
) -> Result<Vec<u8>, i32> {
    let in_filename = CString::new(in_filename).unwrap();
    let encoder_name = CString::new(encoder_name).unwrap();
    let mut output_data: *mut u8 = std::ptr::null_mut();
    let mut output_size: i32 = 0;

    let ret = unsafe {
        hm_fmp4_segment(
            in_filename.as_ptr(),
            encoder_name.as_ptr(),
            start,
            duration,
            &mut output_data,
            &mut output_size,
        )
    };

    if ret < 0 {
        return Err(ret);
    }

    let slc = unsafe { slice::from_raw_parts(output_data, output_size as usize) };
    let data_vec = slc.to_vec();
    unsafe { hm_free_buffer(output_data) };

    Ok(data_vec)
}

pub fn get_video_duration(in_filename: &str) -> f64 {
    let in_filename = CString::new(in_filename).unwrap();
    unsafe { hm_probe(in_filename.as_ptr()) }
}

#[cfg(test)]
mod tests {
    use crate::fmp4_segment;
    use std::time::Instant;

    #[test]
    fn test_fmp4_segment() {
        let in_filename: &str = "/mnt/d/anime/01.mp4";
        let encoder_name: &str = "h264_qsv";
        let duration: f64 = 4.0;

        for i in 0..10 {
            let start_time = Instant::now();
            let buffer =
                fmp4_segment(in_filename, encoder_name, i as f64 * duration, duration).unwrap();
            println!("elapsed: {}ms", start_time.elapsed().as_millis());
            let filename = format!("segment{i}.m4s");
            std::fs::write(filename, buffer).unwrap();
        }
    }
}

