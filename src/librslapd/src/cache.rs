// This exposes C-FFI capable bindings for the concread concurrently readable cache.
use concread::arcache::stats::{ARCacheWriteStat, ReadCountStat};
use concread::arcache::{ARCache, ARCacheBuilder, ARCacheReadTxn, ARCacheWriteTxn};
use concread::cowcell::CowCell;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

#[derive(Clone, Debug, Default)]
struct CacheStats {
    reader_hits: u64,      // Hits from read transactions (main + local)
    reader_includes: u64,  // Number of includes from read transactions
    write_hits: u64,       // Hits from write transactions
    write_inc_or_mod: u64, // Number of includes/modifications from write transactions
    freq_evicts: u64,      // Number of evictions from frequent set
    recent_evicts: u64,    // Number of evictions from recent set
    p_weight: u64,         // Current cache weight between recent and frequent.
    shared_max: u64,       // Maximum number of items in the shared cache.
    freq: u64,             // Number of items in the frequent set at this point in time.
    recent: u64,           // Number of items in the recent set at this point in time.
    all_seen_keys: u64,    // Number of total keys seen through the cache's lifetime.
}

impl CacheStats {
    fn new() -> Self {
        CacheStats::default()
    }

    fn update_from_read_stat(&mut self, stat: ReadCountStat) {
        self.reader_hits += stat.main_hit + stat.local_hit;
        self.reader_includes += stat.include + stat.local_include;
    }

    fn update_from_write_stat(&mut self, stat: &FFIWriteStat) {
        self.write_hits += stat.read_hits;
        self.write_inc_or_mod += stat.includes + stat.modifications;
        self.freq_evicts += stat.freq_evictions;
        self.recent_evicts += stat.recent_evictions;
        self.p_weight = stat.p_weight;
        self.shared_max = stat.shared_max;
        self.freq = stat.freq;
        self.recent = stat.recent;
        self.all_seen_keys = stat.all_seen_keys;
    }
}

#[derive(Debug, Default)]
pub struct FFIWriteStat {
    pub read_ops: u64,
    pub read_hits: u64,
    pub p_weight: u64,
    pub shared_max: u64,
    pub freq: u64,
    pub recent: u64,
    pub all_seen_keys: u64,
    pub includes: u64,
    pub modifications: u64,
    pub freq_evictions: u64,
    pub recent_evictions: u64,
    pub ghost_freq_revives: u64,
    pub ghost_rec_revives: u64,
    pub haunted_includes: u64,
}

impl<K> ARCacheWriteStat<K> for FFIWriteStat {
    fn cache_clear(&mut self) {
        self.read_ops = 0;
        self.read_hits = 0;
    }

    fn cache_read(&mut self) {
        self.read_ops += 1;
    }

    fn cache_hit(&mut self) {
        self.read_hits += 1;
    }

    fn p_weight(&mut self, p: u64) {
        self.p_weight = p;
    }

    fn shared_max(&mut self, i: u64) {
        self.shared_max = i;
    }

    fn freq(&mut self, i: u64) {
        self.freq = i;
    }

    fn recent(&mut self, i: u64) {
        self.recent = i;
    }

    fn all_seen_keys(&mut self, i: u64) {
        self.all_seen_keys = i;
    }

    fn include(&mut self, _k: &K) {
        self.includes += 1;
    }

    fn include_haunted(&mut self, _k: &K) {
        self.haunted_includes += 1;
    }

    fn modify(&mut self, _k: &K) {
        self.modifications += 1;
    }

    fn ghost_frequent_revive(&mut self, _k: &K) {
        self.ghost_freq_revives += 1;
    }

    fn ghost_recent_revive(&mut self, _k: &K) {
        self.ghost_rec_revives += 1;
    }

    fn evict_from_recent(&mut self, _k: &K) {
        self.recent_evictions += 1;
    }

    fn evict_from_frequent(&mut self, _k: &K) {
        self.freq_evictions += 1;
    }
}

pub struct ARCacheChar {
    inner: ARCache<CString, CString>,
    stats: CowCell<CacheStats>,
}

pub struct ARCacheCharRead<'a> {
    inner: ARCacheReadTxn<'a, CString, CString, ReadCountStat>,
    cache: &'a ARCacheChar,
}

pub struct ARCacheCharWrite<'a> {
    inner: ARCacheWriteTxn<'a, CString, CString, FFIWriteStat>,
    cache: &'a ARCacheChar,
}

impl ARCacheChar {
    fn new(max: usize, read_max: usize) -> Option<Self> {
        ARCacheBuilder::new()
            .set_size(max, read_max)
            .set_reader_quiesce(false)
            .build()
            .map(|inner| Self {
                inner,
                stats: CowCell::new(CacheStats::new()),
            })
    }
}

#[no_mangle]
pub extern "C" fn cache_char_create(max: usize, read_max: usize) -> *mut ARCacheChar {
    if let Some(cache) = ARCacheChar::new(max, read_max) {
        Box::into_raw(Box::new(cache))
    } else {
        std::ptr::null_mut()
    }
}

#[no_mangle]
pub extern "C" fn cache_char_free(cache: *mut ARCacheChar) {
    debug_assert!(!cache.is_null());
    unsafe {
        drop(Box::from_raw(cache));
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
        &(*cache)
    };

    // Get stats snapshot
    let stats_read = cache_ref.stats.read();
    *reader_hits = stats_read.reader_hits;
    *reader_includes = stats_read.reader_includes;
    *write_hits = stats_read.write_hits;
    *write_inc_or_mod = stats_read.write_inc_or_mod;
    *freq_evicts = stats_read.freq_evicts;
    *recent_evicts = stats_read.recent_evicts;
    *p_weight = stats_read.p_weight;
    *shared_max = stats_read.shared_max;
    *freq = stats_read.freq;
    *recent = stats_read.recent;
    *all_seen_keys = stats_read.all_seen_keys;
}

// start read
#[no_mangle]
pub extern "C" fn cache_char_read_begin(cache: *mut ARCacheChar) -> *mut ARCacheCharRead<'static> {
    let cache_ref = unsafe {
        debug_assert!(!cache.is_null());
        &(*cache) as &ARCacheChar
    };
    let read_txn = Box::new(ARCacheCharRead {
        inner: cache_ref.inner.read_stats(ReadCountStat::default()),
        cache: cache_ref,
    });
    Box::into_raw(read_txn)
}

#[no_mangle]
pub extern "C" fn cache_char_read_complete(read_txn: *mut ARCacheCharRead) {
    debug_assert!(!read_txn.is_null());

    unsafe {
        let read_txn_box = Box::from_raw(read_txn);
        let read_stats = read_txn_box.inner.finish();
        let write_stats = read_txn_box
            .cache
            .inner
            .try_quiesce_stats(FFIWriteStat::default());

        // Update stats
        let mut stats_write = read_txn_box.cache.stats.write();
        stats_write.update_from_read_stat(read_stats);
        stats_write.update_from_write_stat(&write_stats);
        stats_write.commit();
    }
}

#[no_mangle]
pub extern "C" fn cache_char_read_get(
    read_txn: *mut ARCacheCharRead,
    key: *const c_char,
) -> *const c_char {
    let read_txn_ref = unsafe {
        debug_assert!(!read_txn.is_null());
        &mut (*read_txn) as &mut ARCacheCharRead
    };

    let key_ref = unsafe { CStr::from_ptr(key) };
    let key_dup = CString::from(key_ref);

    // Return a null pointer on miss.
    read_txn_ref
        .inner
        .get(&key_dup)
        .map(|v| v.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn cache_char_read_include(
    read_txn: *mut ARCacheCharRead,
    key: *const c_char,
    val: *const c_char,
) {
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
pub extern "C" fn cache_char_write_begin(
    cache: *mut ARCacheChar,
) -> *mut ARCacheCharWrite<'static> {
    let cache_ref = unsafe {
        debug_assert!(!cache.is_null());
        &(*cache) as &ARCacheChar
    };
    let write_txn = Box::new(ARCacheCharWrite {
        inner: cache_ref.inner.write_stats(FFIWriteStat::default()),
        cache: cache_ref,
    });
    Box::into_raw(write_txn)
}

#[no_mangle]
pub extern "C" fn cache_char_write_commit(write_txn: *mut ARCacheCharWrite) {
    debug_assert!(!write_txn.is_null());
    unsafe {
        let write_txn_box = Box::from_raw(write_txn);
        let current_stats = write_txn_box.inner.commit();

        let mut stats_write = write_txn_box.cache.stats.write();
        stats_write.update_from_write_stat(&current_stats);
        stats_write.commit();
    }
}

#[no_mangle]
pub extern "C" fn cache_char_write_rollback(write_txn: *mut ARCacheCharWrite) {
    debug_assert!(!write_txn.is_null());
    unsafe {
        drop(Box::from_raw(write_txn));
    }
}

#[no_mangle]
pub extern "C" fn cache_char_write_include(
    write_txn: *mut ARCacheCharWrite,
    key: *const c_char,
    val: *const c_char,
) {
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
    use super::*;

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

    #[test]
    fn test_cache_stats() {
        let cache = cache_char_create(100, 8);

        // Variables to store stats
        let mut reader_hits = 0;
        let mut reader_includes = 0;
        let mut write_hits = 0;
        let mut write_inc_or_mod = 0;
        let mut shared_max = 0;
        let mut freq = 0;
        let mut recent = 0;
        let mut freq_evicts = 0;
        let mut recent_evicts = 0;
        let mut p_weight = 0;
        let mut all_seen_keys = 0;

        // Do some operations
        let key = CString::new("stats_test").unwrap();
        let value = CString::new("value").unwrap();

        let write_txn = cache_char_write_begin(cache);
        cache_char_write_include(write_txn, key.as_ptr(), value.as_ptr());
        cache_char_write_commit(write_txn);

        let read_txn = cache_char_read_begin(cache);
        let _ = cache_char_read_get(read_txn, key.as_ptr());
        cache_char_read_complete(read_txn);

        // Get stats
        cache_char_stats(
            cache,
            &mut reader_hits,
            &mut reader_includes,
            &mut write_hits,
            &mut write_inc_or_mod,
            &mut shared_max,
            &mut freq,
            &mut recent,
            &mut freq_evicts,
            &mut recent_evicts,
            &mut p_weight,
            &mut all_seen_keys,
        );

        // Verify that stats were updated
        assert!(write_inc_or_mod > 0);
        assert!(all_seen_keys > 0);

        cache_char_free(cache);
    }

    #[test]
    fn test_cache_read_write_operations() {
        let cache = cache_char_create(100, 8);

        // Create test data
        let key = CString::new("test_key").unwrap();
        let value = CString::new("test_value").unwrap();

        // Test write operation
        let write_txn = cache_char_write_begin(cache);
        cache_char_write_include(write_txn, key.as_ptr(), value.as_ptr());
        cache_char_write_commit(write_txn);

        // Test read operation
        let read_txn = cache_char_read_begin(cache);
        let result = cache_char_read_get(read_txn, key.as_ptr());
        assert!(!result.is_null());

        // Verify the value
        let retrieved_value = unsafe { CStr::from_ptr(result) };
        assert_eq!(retrieved_value.to_bytes(), value.as_bytes());

        cache_char_read_complete(read_txn);
        cache_char_free(cache);
    }

    #[test]
    fn test_cache_miss() {
        let cache = cache_char_create(100, 8);
        let read_txn = cache_char_read_begin(cache);

        let missing_key = CString::new("nonexistent").unwrap();
        let result = cache_char_read_get(read_txn, missing_key.as_ptr());
        assert!(result.is_null());

        cache_char_read_complete(read_txn);
        cache_char_free(cache);
    }

    #[test]
    fn test_write_rollback() {
        let cache = cache_char_create(100, 8);

        let key = CString::new("rollback_test").unwrap();
        let value = CString::new("value").unwrap();

        // Start write transaction and rollback
        let write_txn = cache_char_write_begin(cache);
        cache_char_write_include(write_txn, key.as_ptr(), value.as_ptr());
        cache_char_write_rollback(write_txn);

        // Verify key doesn't exist
        let read_txn = cache_char_read_begin(cache);
        let result = cache_char_read_get(read_txn, key.as_ptr());
        assert!(result.is_null());

        cache_char_read_complete(read_txn);
        cache_char_free(cache);
    }
}
