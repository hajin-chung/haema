use cc;
use pkg_config;

fn main() {
    let ffmpeg_libs = [
        "libavdevice",
        "libavformat",
        "libavfilter",
        "libavcodec",
        "libswresample",
        "libswscale",
        "libavutil",
    ];

    cc::Build::new()
        .file("c_src/hm_context.c")
        .file("c_src/hm_transcode.c")
        .file("c_src/hm_probe.c")
        .include("c_src/include")
        .flag("-Wall")
        .compile("hmff");

    for lib in ffmpeg_libs {
        pkg_config::Config::new().probe(lib).unwrap();
    }

    println!("cargo::rerun-if-changed=c_src/hm_context.c");
    println!("cargo::rerun-if-changed=c_src/hm_transcode.c");
    println!("cargo::rerun-if-changed=c_src/hm_probe.c");
    println!("cargo::rerun-if-changed=c_src/include/hm_util.h");
}

