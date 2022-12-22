This is the librslapd wrapper - it's a rust -> c bindgen stub. It does
not provide any logic, but exists to resolve linking issues that
exist between autotools and rust. For all the Rust logic, see ../slapd.
