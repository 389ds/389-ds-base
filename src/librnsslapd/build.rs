extern crate cbindgen;

use std::env;

fn main() {
    if let Ok(crate_dir) = env::var("CARGO_MANIFEST_DIR") {
        if let Ok(out_dir) = env::var("SLAPD_HEADER_DIR") {
            cbindgen::Builder::new()
                .with_language(cbindgen::Language::C)
                .with_crate(crate_dir)
                .generate()
                .expect("Unable to generate bindings")
                .write_to_file(format!("{}/rust-nsslapd-private.h", out_dir));
        }
    }
}
