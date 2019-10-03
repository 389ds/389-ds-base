#[no_mangle]
pub extern "C" fn do_nothing_rust() -> usize {
    0
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(2 + 2, 4);
    }
}
