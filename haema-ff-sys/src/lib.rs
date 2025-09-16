use std::ffi::CString;
use std::os::raw::{c_char, c_double, c_int};
use std::slice;

unsafe extern "C" {
    fn hm_transcode_segment(
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

pub fn transcode_segment(
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
        hm_transcode_segment(
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
    use crate::*;
    use std::ffi::CString;

    #[test]
    fn it_works() {
        let in_filename = CString::new("/mnt/d/vod/25.08.12 뀨.mp4").unwrap();
        let encoder_name = CString::new("h264_qsv").unwrap();
        let start: f64 = 20.0;
        let duration: f64 = 4.0;
        let mut output_buffer: *mut u8 = std::ptr::null_mut();
        let mut output_size: i32 = 0;
        for i in 0..10 {
            unsafe {
                let result = hm_transcode_segment(
                    in_filename.as_ptr(),
                    encoder_name.as_ptr(),
                    start,
                    duration,
                    &mut output_buffer,
                    &mut output_size,
                );
                hm_free_buffer(output_buffer);
                assert_eq!(result, 0);
            }
        }
    }
}

