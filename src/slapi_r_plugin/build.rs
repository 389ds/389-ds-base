use std::env;

fn main() {
    if let Ok(lib_dir) = env::var("SLAPD_DYLIB_DIR") {
        println!("cargo:rustc-link-lib=dylib=slapd");
        println!("cargo:rustc-link-search=native={}", lib_dir);
        println!("cargo:rustc-link-search=native={}/.libs", lib_dir);
    }
}
