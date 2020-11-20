#[macro_export]
#[cfg(not(feature = "test_log_direct"))]
macro_rules! log_error {
    ($level:expr, $($arg:tt)*) => ({
        use std::fmt;
        match log_error(
            $level,
            format!("{}:{}", file!(), line!()),
            format!("{}\n", fmt::format(format_args!($($arg)*)))
        ) {
            Ok(_) => {},
            Err(e) => {
                eprintln!("A logging error occured {}, {} -> {:?}", file!(), line!(), e);
            }
        };
    })
}

#[macro_export]
#[cfg(feature = "test_log_direct")]
macro_rules! log_error {
    ($level:expr, $($arg:tt)*) => ({
        use std::fmt;
        eprintln!("{}:{} -> {}", file!(), line!(), fmt::format(format_args!($($arg)*)));
    })
}

#[macro_export]
macro_rules! slapi_r_plugin_hooks {
    ($mod_ident:ident, $hooks_ident:ident) => (
        paste::item! {
            use libc;
            use std::ffi::{CString, CStr};

            static mut [<$mod_ident _PLUGINID>]: *const libc::c_void = std::ptr::null();

            pub(crate) fn plugin_id() -> PluginIdRef {
                PluginIdRef {
                    raw_pid: unsafe { [<$mod_ident _PLUGINID>] }
                }
            }

            #[no_mangle]
            pub extern "C" fn [<$mod_ident _plugin_init>](raw_pb: *const libc::c_void) -> i32 {
                let mut pb = PblockRef::new(raw_pb);
                log_error!(ErrorLevel::Trace, "it's alive!\n");

                match pb.set_plugin_version(PluginVersion::V03) {
                    0 => {},
                    e => return e,
                };

                // Setup the plugin id.
                unsafe {
                    [<$mod_ident _PLUGINID>] = pb.get_plugin_identity();
                }

                if $hooks_ident::has_betxn_pre_modify() {
                    match pb.register_betxn_pre_modify_fn([<$mod_ident _plugin_betxn_pre_modify>]) {
                        0 => {},
                        e => return e,
                    };
                }

                if $hooks_ident::has_betxn_pre_add() {
                    match pb.register_betxn_pre_add_fn([<$mod_ident _plugin_betxn_pre_add>]) {
                        0 => {},
                        e => return e,
                    };
                }

                if $hooks_ident::has_pwd_storage() {
                    // SLAPI_PLUGIN_DESCRIPTION pbkdf2_sha256_pdesc
                    match pb.register_pwd_storage_encrypt_fn([<$mod_ident _plugin_pwd_storage_encrypt_fn>]) {
                        0 => {},
                        e => return e,
                    };
                    match pb.register_pwd_storage_compare_fn([<$mod_ident _plugin_pwd_storage_compare_fn>]) {
                        0 => {},
                        e => return e,
                    };
                    let scheme_name = CString::new($hooks_ident::pwd_scheme_name()).expect("invalid password scheme name");
                    // DS strdups this for us, so it owns it now.
                    match pb.register_pwd_storage_scheme_name(scheme_name.as_ptr()) {
                        0 => {},
                        e => return e,
                    };

                }

                // set the start fn
                match pb.register_start_fn([<$mod_ident _plugin_start>]) {
                    0 => {},
                    e => return e,
                };

                // set the close fn
                match pb.register_close_fn([<$mod_ident _plugin_close>]) {
                    0 => {},
                    e => return e,
                };

                0
            }

            pub extern "C" fn [<$mod_ident _plugin_start>](raw_pb: *const libc::c_void) -> i32 {
                let mut pb = PblockRef::new(raw_pb);

                if let Some(task_ident) = $hooks_ident::has_task_handler() {
                    match task_register_handler_fn(task_ident, [<$mod_ident _plugin_task_handler>], &mut pb) {
                        0 => {},
                        e => return e,
                    };
                };

                match $hooks_ident::start(&mut pb) {
                    Ok(()) => {
                        0
                    }
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "-> {:?}", e);
                        1
                    }
                }
            }

            pub extern "C" fn [<$mod_ident _plugin_close>](raw_pb: *const libc::c_void) -> i32 {
                let mut pb = PblockRef::new(raw_pb);

                if let Some(task_ident) = $hooks_ident::has_task_handler() {
                    match task_unregister_handler_fn(task_ident, [<$mod_ident _plugin_task_handler>]) {
                        0 => {},
                        e => return e,
                    };
                };

                match $hooks_ident::close(&mut pb) {
                    Ok(()) => {
                        0
                    }
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "-> {:?}", e);
                        1
                    }
                }
            }

            pub extern "C" fn [<$mod_ident _plugin_betxn_pre_modify>](raw_pb: *const libc::c_void) -> i32 {
                let mut pb = PblockRef::new(raw_pb);
                match $hooks_ident::betxn_pre_modify(&mut pb) {
                    Ok(()) => {
                        0
                    }
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "-> {:?}", e);
                        1
                    }
                }
            }

            pub extern "C" fn [<$mod_ident _plugin_betxn_pre_add>](raw_pb: *const libc::c_void) -> i32 {
                let mut pb = PblockRef::new(raw_pb);
                match $hooks_ident::betxn_pre_add(&mut pb) {
                    Ok(()) => {
                        0
                    }
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "-> {:?}", e);
                        1
                    }
                }
            }

            pub extern "C" fn [<$mod_ident _plugin_task_handler>](
                raw_pb: *const libc::c_void,
                raw_e_before: *const libc::c_void,
                _raw_e_after: *const libc::c_void,
                raw_returncode: *mut i32,
                _raw_returntext: *mut c_char,
                raw_arg: *const libc::c_void,
            ) -> i32 {
                let mut pb = PblockRef::new(raw_pb);

                let e_before = EntryRef::new(raw_e_before);
                // let e_after = EntryRef::new(raw_e_after);

                let task_data = match $hooks_ident::task_validate(
                    &e_before
                ) {
                    Ok(data) => data,
                    Err(retcode) => {
                        unsafe { *raw_returncode = retcode as i32 };
                        return DseCallbackStatus::Error as i32
                    }
                };

                let mut task = Task::new(&e_before, raw_arg);
                task.register_destructor_fn([<$mod_ident _plugin_task_destructor>]);

                // Setup the task thread and then run it. Remember, because Rust is
                // smarter about memory, the move statement here moves the task wrapper and
                // task_data to the thread, so they drop on thread close. No need for a
                // destructor beyond blocking on the thread to complete.
                std::thread::spawn(move || {
                    log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_task_thread => begin"));
                    // Indicate the task is begun
                    task.begin();
                    // Start a txn
                    let be: Option<BackendRef> = match $hooks_ident::task_be_dn_hint(&task_data)
                        .map(|be_dn| {
                            BackendRef::new(&be_dn)
                        })
                        .transpose() {
                            Ok(v) => v,
                            Err(_) => {
                                log_error!(ErrorLevel::Error, concat!(stringify!($mod_ident), "_plugin_task_thread => task error -> selected dn does not exist"));
                                task.error(PluginError::TxnFailure as i32);
                                return;
                            }
                        };
                    let be_txn: Option<BackendRefTxn> = match be {
                        Some(b) => {
                            match b.begin_txn() {
                                Ok(txn) => Some(txn),
                                Err(_) => {
                                    log_error!(ErrorLevel::Error, concat!(stringify!($mod_ident), "_plugin_task_thread => task error -> unable to begin txn"));
                                    task.error(PluginError::TxnFailure as i32);
                                    return;
                                }
                            }
                        }
                        None => None,
                    };

                    // Abort or commit the txn here.
                    match $hooks_ident::task_handler(&mut task, task_data) {
                        Ok(_data) => {
                            match be_txn {
                                Some(be_txn) => be_txn.commit(),
                                None => {}
                            };
                            // These will set the status, and guarantee the drop
                            task.success();
                        }
                        Err(e) => {
                            log_error!(ErrorLevel::Error, "{}_plugin_task_thread => task error -> {:?}", stringify!($mod_ident), e);
                            // These will set the status, and guarantee the drop
                            task.error(e as i32);
                            // On drop, be_txn implicitly aborts.
                        }
                    };

                    log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_task_thread <= complete"));
                });

                // Indicate that the thread started just fine.
                unsafe { *raw_returncode = LDAP_SUCCESS };
                DseCallbackStatus::Ok as i32
            }

            pub extern "C" fn [<$mod_ident _plugin_task_destructor>](
                raw_task: *const libc::c_void,
            ) {
                // Simply block until the task refcount drops to 0.
                let task = TaskRef::new(raw_task);
                task.block();
            }

            pub extern "C" fn [<$mod_ident _plugin_pwd_storage_encrypt_fn>](
                cleartext: *const c_char,
            ) -> *const c_char {
                let cleartext = unsafe { CStr::from_ptr(cleartext) };
                let clear_str = match cleartext.to_str() {
                    Ok(s) => s,
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "{}_plugin_pwd_storage_encrypt_fn => error -> {:?}", stringify!($mod_ident), e);
                        return std::ptr::null();
                    }
                };
                match $hooks_ident::pwd_storage_encrypt(clear_str)
                    // do some conversions.
                    .and_then(|s| {
                        // DS needs to own the returned value, so we have to alloc it for ds.
                        CString::new(s)
                            .map_err(|e| {
                                PluginError::GenericFailure
                            })
                            .map(|cs| {
                                unsafe { libc::strdup(cs.as_ptr()) }
                            })
                    })
                {
                    Ok(s_ptr) => {
                        s_ptr
                    }
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "{}_plugin_pwd_storage_encrypt_fn => error -> {:?}", stringify!($mod_ident), e);
                        std::ptr::null()
                    }
                }
            }

            pub extern "C" fn [<$mod_ident _plugin_pwd_storage_compare_fn>](
                cleartext: *const c_char,
                encrypted: *const c_char,
            ) -> i32 {
                // 1 is failure, 0 success.
                let cleartext = unsafe { CStr::from_ptr(cleartext) };
                let clear_str = match cleartext.to_str() {
                    Ok(s) => s,
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "{}_plugin_pwd_storage_compare_fn => error -> {:?}", stringify!($mod_ident), e);
                        return 1;
                    }
                };

                let encrypted = unsafe { CStr::from_ptr(encrypted) };
                let enc_str = match encrypted.to_str() {
                    Ok(s) => s,
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "{}_plugin_pwd_storage_compare_fn => error -> {:?}", stringify!($mod_ident), e);
                        return 1;
                    }
                };

                match $hooks_ident::pwd_storage_compare(clear_str, enc_str) {
                    Ok(r) => {
                        if r {
                            0
                        } else {
                            1
                        }
                    }
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "{}_plugin_pwd_storage_compare_fn => error -> {:?}", stringify!($mod_ident), e);
                        1
                    }
                }
            }

        } // end paste
    )
} // end macro

#[macro_export]
macro_rules! slapi_r_syntax_plugin_hooks {
    (
        $mod_ident:ident,
        $hooks_ident:ident
    ) => (
        paste::item! {
            use libc;
            use std::convert::TryFrom;
            use std::ffi::CString;

            #[no_mangle]
            pub extern "C" fn [<$mod_ident _plugin_init>](raw_pb: *const libc::c_void) -> i32 {
                let mut pb = PblockRef::new(raw_pb);
                log_error!(ErrorLevel::Trace, "slapi_r_syntax_plugin_hooks => begin");
                // Setup our plugin
                match pb.set_plugin_version(PluginVersion::V01) {
                    0 => {},
                    e => return e,
                };

                // Setup the names/oids that this plugin provides syntaxes for.
                // DS will clone these, so they can be ephemeral to this function.
                let name_vec = Charray::new($hooks_ident::attr_supported_names().as_slice()).expect("invalid supported names");
                match pb.register_syntax_names(name_vec.as_ptr()) {
                    0 => {},
                    e => return e,
                };

                let attr_oid = CString::new($hooks_ident::attr_oid()).expect("invalid attr oid");
                match pb.register_syntax_oid(attr_oid.as_ptr()) {
                    0 => {},
                    e => return e,
                };

                match pb.register_syntax_validate_fn([<$mod_ident _plugin_syntax_validate>]) {
                    0 => {},
                    e => return e,
                };

                // Now setup the MR's
                match register_plugin_ext(
                    PluginType::MatchingRule,
                    $hooks_ident::eq_mr_name(),
                    concat!(stringify!($mod_ident), "_plugin_eq_mr_init"),
                    [<$mod_ident _plugin_eq_mr_init>]
                ) {
                    0 => {},
                    e => return e,
                };

                if $hooks_ident::sub_mr_oid().is_some() {
                    match register_plugin_ext(
                        PluginType::MatchingRule,
                        $hooks_ident::sub_mr_name(),
                        concat!(stringify!($mod_ident), "_plugin_ord_mr_init"),
                        [<$mod_ident _plugin_ord_mr_init>]
                    ) {
                        0 => {},
                        e => return e,
                    };
                }

                if $hooks_ident::ord_mr_oid().is_some() {
                    match register_plugin_ext(
                        PluginType::MatchingRule,
                        $hooks_ident::ord_mr_name(),
                        concat!(stringify!($mod_ident), "_plugin_ord_mr_init"),
                        [<$mod_ident _plugin_ord_mr_init>]
                    ) {
                        0 => {},
                        e => return e,
                    };
                }

                log_error!(ErrorLevel::Trace, "slapi_r_syntax_plugin_hooks <= success");

                0
            }

            pub extern "C" fn [<$mod_ident _plugin_syntax_validate>](
                raw_berval: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_syntax_validate => begin"));

                let bval = BerValRef::new(raw_berval);

                match $hooks_ident::syntax_validate(&bval) {
                    Ok(()) => {
                        log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_syntax_validate <= success"));
                        LDAP_SUCCESS
                    }
                    Err(e) => {
                        log_error!(ErrorLevel::Warning,
                            "{}_plugin_syntax_validate error -> {:?}", stringify!($mod_ident), e
                        );
                        e as i32
                    }
                }
            }

            // All the MR types share this.
            pub extern "C" fn [<$mod_ident _plugin_mr_filter_ava>](
                raw_pb: *const libc::c_void,
                raw_bvfilter: *const libc::c_void,
                raw_bvals: *const libc::c_void,
                i_ftype: i32,
                _retval: *mut libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_mr_filter_ava => begin"));
                let mut pb = PblockRef::new(raw_pb);
                let bvfilter = BerValRef::new(raw_bvfilter);
                let bvals = ValueArrayRef::new(raw_bvals);
                let ftype = match FilterType::try_from(i_ftype) {
                    Ok(f) => f,
                    Err(e) => {
                        log_error!(ErrorLevel::Error, "{}_plugin_ord_mr_filter_ava Error -> {:?}",
                        stringify!($mod_ident), e);
                        return e as i32
                    }
                };

                let r: Result<bool, PluginError> = match ftype {
                    FilterType::And | FilterType::Or | FilterType::Not => {
                        Err(PluginError::InvalidFilter)
                    }
                    FilterType::Equality => {
                        $hooks_ident::filter_ava_eq(&mut pb, &bvfilter, &bvals)
                    }
                    FilterType::Substring => {
                        Err(PluginError::Unimplemented)
                    }
                    FilterType::Ge => {
                        $hooks_ident::filter_ava_ord(&mut pb, &bvfilter, &bvals)
                            .map(|o_ord| {
                                match o_ord {
                                    Some(Ordering::Greater) | Some(Ordering::Equal) => true,
                                    Some(Ordering::Less) | None => false,
                                }
                            })
                    }
                    FilterType::Le => {
                        $hooks_ident::filter_ava_ord(&mut pb, &bvfilter, &bvals)
                            .map(|o_ord| {
                                match o_ord {
                                    Some(Ordering::Less) | Some(Ordering::Equal) => true,
                                    Some(Ordering::Greater) | None => false,
                                }
                            })
                    }
                    FilterType::Present => {
                        Err(PluginError::Unimplemented)
                    }
                    FilterType::Approx => {
                        Err(PluginError::Unimplemented)
                    }
                    FilterType::Extended => {
                        Err(PluginError::Unimplemented)
                    }
                };

                match r {
                    Ok(b) => {
                        log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_mr_filter_ava <= success"));
                        // rust bool into i32 will become 0 false, 1 true. However, ds expects 0 true and 1 false for
                        // for the filter_ava match. So we flip the bool, and send it back.
                        (!b) as i32
                    }
                    Err(e) => {
                        log_error!(ErrorLevel::Warning,
                            "{}_plugin_mr_filter_ava error -> {:?}",
                            stringify!($mod_ident), e
                        );
                        e as i32
                    }
                }
            }


            // EQ MR plugin hooks
            #[no_mangle]
            pub extern "C" fn [<$mod_ident _plugin_eq_mr_init>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                let mut pb = PblockRef::new(raw_pb);
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_init => begin"));
                match pb.set_plugin_version(PluginVersion::V01) {
                    0 => {},
                    e => return e,
                };

                let name_vec = Charray::new($hooks_ident::eq_mr_supported_names().as_slice()).expect("invalid mr supported names");
                let name_ptr = name_vec.as_ptr();
                // SLAPI_PLUGIN_MR_NAMES
                match pb.register_mr_names(name_ptr) {
                    0 => {},
                    e => return e,
                };

                // description
                // SLAPI_PLUGIN_MR_FILTER_CREATE_FN
                match pb.register_mr_filter_create_fn([<$mod_ident _plugin_eq_mr_filter_create>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_INDEXER_CREATE_FN
                match pb.register_mr_indexer_create_fn([<$mod_ident _plugin_eq_mr_indexer_create>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_FILTER_AVA
                match pb.register_mr_filter_ava_fn([<$mod_ident _plugin_mr_filter_ava>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_FILTER_SUB
                match pb.register_mr_filter_sub_fn([<$mod_ident _plugin_eq_mr_filter_sub>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_VALUES2KEYS
                match pb.register_mr_values2keys_fn([<$mod_ident _plugin_eq_mr_filter_values2keys>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA
                match pb.register_mr_assertion2keys_ava_fn([<$mod_ident _plugin_eq_mr_filter_assertion2keys_ava>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB
                match pb.register_mr_assertion2keys_sub_fn([<$mod_ident _plugin_eq_mr_filter_assertion2keys_sub>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_COMPARE
                match pb.register_mr_compare_fn([<$mod_ident _plugin_eq_mr_filter_compare>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_NORMALIZE

                // Finaly, register the MR
                match unsafe { matchingrule_register($hooks_ident::eq_mr_oid(), $hooks_ident::eq_mr_name(), $hooks_ident::eq_mr_desc(), $hooks_ident::attr_oid(), &$hooks_ident::attr_compat_oids()) } {
                    0 => {},
                    e => return e,
                };

                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_init <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_eq_mr_filter_create>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_create => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_create <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_eq_mr_indexer_create>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_indexer_create => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_indexer_create <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_eq_mr_filter_sub>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_sub => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_sub <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_eq_mr_filter_values2keys>](
                raw_pb: *const libc::c_void,
                raw_vals: *const libc::c_void,
                raw_ivals: *mut libc::c_void,
                i_ftype: i32,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_values2keys => begin"));
                let mut pb = PblockRef::new(raw_pb);
                let vals = ValueArrayRef::new(raw_vals);
                let ftype = match FilterType::try_from(i_ftype) {
                    Ok(f) => f,
                    Err(e) => {
                        log_error!(ErrorLevel::Error,
                        "{}_plugin_eq_mr_filter_values2keys Error -> {:?}",
                        stringify!($mod_ident),
                        e);
                        return e as i32
                    }
                };

                if (ftype != FilterType::Equality && ftype != FilterType::Approx) {
                    log_error!(ErrorLevel::Error,
                        "{}_plugin_eq_mr_filter_values2keys Error -> Invalid Filter type",
                        stringify!($mod_ident),
                        );
                    return PluginError::InvalidFilter  as i32
                }

                let va = match $hooks_ident::eq_mr_filter_values2keys(&mut pb, &vals) {
                    Ok(va) => va,
                    Err(e) => {
                        log_error!(ErrorLevel::Error,
                        "{}_plugin_eq_mr_filter_values2keys Error -> {:?}",
                        stringify!($mod_ident),
                        e);
                        return e as i32
                    }
                };

                // Now, deconstruct the va, get the pointer, and put it into the ivals.
                unsafe {
                    let ivals_ptr: *mut *const libc::c_void = raw_ivals as *mut _;
                    (*ivals_ptr) = va.take_ownership() as *const libc::c_void;
                }

                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_values2keys <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_eq_mr_filter_assertion2keys_ava>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_assertion2keys_ava => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_assertion2keys_ava <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_eq_mr_filter_assertion2keys_sub>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_assertion2keys_sub => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_assertion2keys_sub <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_eq_mr_filter_names>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                // This is probably another char pointer.
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_eq_mr_filter_compare>](
                raw_va: *const libc::c_void,
                raw_vb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_compare => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_eq_mr_filter_compare <= success"));
                0
            }

            // SUB MR plugin hooks

            pub extern "C" fn [<$mod_ident _plugin_sub_mr_filter_create>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_create => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_create <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_sub_mr_indexer_create>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_indexer_create => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_indexer_create <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_sub_mr_filter_sub>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_sub => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_sub <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_sub_mr_filter_values2keys>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_values2keys => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_values2keys <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_sub_mr_filter_assertion2keys_ava>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_assertion2keys_ava => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_assertion2keys_ava <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_sub_mr_filter_assertion2keys_sub>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_assertion2keys_sub => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_assertion2keys_sub <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_sub_mr_filter_names>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                // Probably a char array
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_sub_mr_filter_compare>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_compare => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_sub_mr_filter_compare <= success"));
                0
            }

            // ORD MR plugin hooks
            #[no_mangle]
            pub extern "C" fn [<$mod_ident _plugin_ord_mr_init>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                let mut pb = PblockRef::new(raw_pb);
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_init => begin"));
                match pb.set_plugin_version(PluginVersion::V01) {
                    0 => {},
                    e => return e,
                };

                let name_vec = Charray::new($hooks_ident::ord_mr_supported_names().as_slice()).expect("invalid ord supported names");
                let name_ptr = name_vec.as_ptr();
                // SLAPI_PLUGIN_MR_NAMES
                match pb.register_mr_names(name_ptr) {
                    0 => {},
                    e => return e,
                };

                // description
                // SLAPI_PLUGIN_MR_FILTER_CREATE_FN
                match pb.register_mr_filter_create_fn([<$mod_ident _plugin_ord_mr_filter_create>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_INDEXER_CREATE_FN
                match pb.register_mr_indexer_create_fn([<$mod_ident _plugin_ord_mr_indexer_create>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_FILTER_AVA
                match pb.register_mr_filter_ava_fn([<$mod_ident _plugin_mr_filter_ava>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_FILTER_SUB
                match pb.register_mr_filter_sub_fn([<$mod_ident _plugin_ord_mr_filter_sub>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_VALUES2KEYS
                /*
                match pb.register_mr_values2keys_fn([<$mod_ident _plugin_ord_mr_filter_values2keys>]) {
                    0 => {},
                    e => return e,
                };
                */
                // SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA
                match pb.register_mr_assertion2keys_ava_fn([<$mod_ident _plugin_ord_mr_filter_assertion2keys_ava>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB
                match pb.register_mr_assertion2keys_sub_fn([<$mod_ident _plugin_ord_mr_filter_assertion2keys_sub>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_COMPARE
                match pb.register_mr_compare_fn([<$mod_ident _plugin_ord_mr_filter_compare>]) {
                    0 => {},
                    e => return e,
                };
                // SLAPI_PLUGIN_MR_NORMALIZE

                // Finaly, register the MR
                match unsafe { matchingrule_register($hooks_ident::ord_mr_oid().unwrap(), $hooks_ident::ord_mr_name(), $hooks_ident::ord_mr_desc(), $hooks_ident::attr_oid(), &$hooks_ident::attr_compat_oids()) } {
                    0 => {},
                    e => return e,
                };

                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_init <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_ord_mr_filter_create>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_create => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_create <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_ord_mr_indexer_create>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_indexer_create => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_indexer_create <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_ord_mr_filter_sub>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_sub => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_sub <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_ord_mr_filter_values2keys>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_values2keys => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_values2keys <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_ord_mr_filter_assertion2keys_ava>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_assertion2keys_ava => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_assertion2keys_ava <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_ord_mr_filter_assertion2keys_sub>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_assertion2keys_sub => begin"));
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_assertion2keys_sub <= success"));
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_ord_mr_filter_names>](
                raw_pb: *const libc::c_void,
            ) -> i32 {
                // probably char pointers
                0
            }

            pub extern "C" fn [<$mod_ident _plugin_ord_mr_filter_compare>](
                raw_va: *const libc::c_void,
                raw_vb: *const libc::c_void,
            ) -> i32 {
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_compare => begin"));
                let va = BerValRef::new(raw_va);
                let vb = BerValRef::new(raw_vb);
                let rc = match $hooks_ident::filter_compare(&va, &vb) {
                    Ordering::Less => -1,
                    Ordering::Equal => 0,
                    Ordering::Greater => 1,
                };
                log_error!(ErrorLevel::Trace, concat!(stringify!($mod_ident), "_plugin_ord_mr_filter_compare <= success"));
                rc
            }

        } // end paste
    )
} // end macro

#[macro_export]
macro_rules! slapi_r_search_callback_mapfn {
    (
        $mod_ident:ident,
        $cb_target_ident:ident,
        $cb_mod_ident:ident
    ) => {
        paste::item! {
            #[no_mangle]
            pub extern "C" fn [<$cb_target_ident>](
                raw_e: *const libc::c_void,
                raw_data: *const libc::c_void,
            ) -> i32 {
                let e = EntryRef::new(raw_e);
                let data_ptr = raw_data as *const _;
                let data = unsafe { &(*data_ptr) };
                match $cb_mod_ident(&e, data) {
                    Ok(_) => LDAPError::Success as i32,
                    Err(e) => e as i32,
                }
            }
        } // end paste
    };
} // end macro
