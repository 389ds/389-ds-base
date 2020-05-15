// This exposes C-FFI capable bindings for the concread concurrently readable cache.
use concread::arcache::{ARCache, ARCacheReadTxn, ARCacheWriteTxn};
use std::ffi::{CString, CStr};
use std::os::raw::c_char;
use std::borrow::Borrow;
use std::convert::TryInto;

pub struct ARCacheChar {
    inner: ARCache<CString, CString>,
}

pub struct ARCacheCharRead<'a> {
    inner: ARCacheReadTxn<'a, CString, CString>,
}

pub struct ARCacheCharWrite<'a> {
    inner: ARCacheWriteTxn<'a, CString, CString>,
}

#[no_mangle]
pub extern "C" fn cache_char_create(max: usize, read_max: usize) -> *mut ARCacheChar {
    let cache: Box<ARCacheChar> = Box::new(ARCacheChar { inner: ARCache::new_size(max, read_max) });
    Box::into_raw(cache)
}

#[no_mangle]
pub extern "C" fn cache_char_free(cache: *mut ARCacheChar) {
    // Should we be responsible to drain and free everything?
    debug_assert!(!cache.is_null());
    unsafe {
        Box::from_raw(cache);
    }
}

#[no_mangle]
pub extern "C" fn cache_char_stats(
    cache: *mut ARCacheChar,
    reader_hits: &mut u64,
    reader_includes: &mut u64,
    write_hits: &mut u64,
    write_inc_or_mod: &mut u64,
    shared_max: &mut u64,
    freq: &mut u64,
    recent: &mut u64,
    freq_evicts: &mut u64,
    recent_evicts: &mut u64,
    p_weight: &mut u64,
    all_seen_keys: &mut u64,
) {
    let cache_ref = unsafe {
        debug_assert!(!cache.is_null());
        &(*cache) as &ARCacheChar
    };
    let stat_rguard = cache_ref.inner.view_stats();
    let stats = stat_rguard.borrow();
    *reader_hits = stats.reader_hits.try_into().unwrap();
    *reader_includes = stats.reader_includes.try_into().unwrap();
    *write_hits = stats.write_hits.try_into().unwrap();
    *write_inc_or_mod = stats.write_inc_or_mod.try_into().unwrap();
    *shared_max = stats.shared_max.try_into().unwrap();
    *freq = stats.freq.try_into().unwrap();
    *recent = stats.recent.try_into().unwrap();
    *freq_evicts = stats.freq_evicts.try_into().unwrap();
    *recent_evicts = stats.recent_evicts.try_into().unwrap();
    *p_weight = stats.p_weight.try_into().unwrap();
    *all_seen_keys = stats.all_seen_keys.try_into().unwrap();
}


// start read
#[no_mangle]
pub extern "C" fn cache_char_read_begin(cache: *mut ARCacheChar) -> *mut ARCacheCharRead<'static> {
    let cache_ref = unsafe {
        debug_assert!(!cache.is_null());
        &(*cache) as &ARCacheChar
    };
    let read_txn = Box::new(ARCacheCharRead { inner: cache_ref.inner.read() });
    Box::into_raw(read_txn)
}

#[no_mangle]
pub extern "C" fn cache_char_read_complete(read_txn: *mut ARCacheCharRead) {
    debug_assert!(!read_txn.is_null());
    unsafe {
        Box::from_raw(read_txn);
    }
}

#[no_mangle]
pub extern "C" fn cache_char_read_get(read_txn: *mut ARCacheCharRead, key: *const c_char) -> *const c_char {
    let read_txn_ref = unsafe {
        debug_assert!(!read_txn.is_null());
        &mut (*read_txn) as &mut ARCacheCharRead
    };

    let key_ref = unsafe { CStr::from_ptr(key) };
    let key_dup = CString::from(key_ref);

    // Return a null pointer on miss.
    read_txn_ref.inner.get(&key_dup)
        .map(|v| v.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn cache_char_read_include(read_txn: *mut ARCacheCharRead, key: *const c_char, val: *const c_char) {
    let read_txn_ref = unsafe {
        debug_assert!(!read_txn.is_null());
        &mut (*read_txn) as &mut ARCacheCharRead
    };

    let key_ref = unsafe { CStr::from_ptr(key) };
    let key_dup = CString::from(key_ref);

    let val_ref = unsafe { CStr::from_ptr(val) };
    let val_dup = CString::from(val_ref);
    read_txn_ref.inner.insert(key_dup, val_dup);
}

#[no_mangle]
pub extern "C" fn cache_char_write_begin(cache: *mut ARCacheChar) -> *mut ARCacheCharWrite<'static> {
    let cache_ref = unsafe {
        debug_assert!(!cache.is_null());
        &(*cache) as &ARCacheChar
    };
    let write_txn = Box::new(ARCacheCharWrite { inner: cache_ref.inner.write() });
    Box::into_raw(write_txn)
}

#[no_mangle]
pub extern "C" fn cache_char_write_commit(write_txn: *mut ARCacheCharWrite) {
    debug_assert!(!write_txn.is_null());
    let wr = unsafe {
        Box::from_raw(write_txn)
    };
    (*wr).inner.commit();
}

#[no_mangle]
pub extern "C" fn cache_char_write_rollback(write_txn: *mut ARCacheCharWrite) {
    debug_assert!(!write_txn.is_null());
    unsafe {
        Box::from_raw(write_txn);
    }
}

#[no_mangle]
pub extern "C" fn cache_char_write_include(write_txn: *mut ARCacheCharWrite, key: *const c_char, val: *const c_char) {
    let write_txn_ref = unsafe {
        debug_assert!(!write_txn.is_null());
        &mut (*write_txn) as &mut ARCacheCharWrite
    };

    let key_ref = unsafe { CStr::from_ptr(key) };
    let key_dup = CString::from(key_ref);

    let val_ref = unsafe { CStr::from_ptr(val) };
    let val_dup = CString::from(val_ref);
    write_txn_ref.inner.insert(key_dup, val_dup);
}

#[cfg(test)]
mod tests {
    use crate::cache::*;

    #[test]
    fn test_cache_basic() {
        let cache_ptr = cache_char_create(1024, 8);
        let read_txn = cache_char_read_begin(cache_ptr);

        let k1 = CString::new("Hello").unwrap();
        let v1 = CString::new("Hello").unwrap();

        assert!(cache_char_read_get(read_txn, k1.as_ptr()).is_null());
        cache_char_read_include(read_txn, k1.as_ptr(), v1.as_ptr());
        assert!(!cache_char_read_get(read_txn, k1.as_ptr()).is_null());

        cache_char_read_complete(read_txn);
        cache_char_free(cache_ptr);
    }
}


