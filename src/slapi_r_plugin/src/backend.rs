use crate::dn::SdnRef;
use crate::pblock::Pblock;
// use std::ops::Deref;

extern "C" {
    fn slapi_back_transaction_begin(pb: *const libc::c_void) -> i32;
    fn slapi_back_transaction_commit(pb: *const libc::c_void);
    fn slapi_back_transaction_abort(pb: *const libc::c_void);
    fn slapi_be_select_exact(sdn: *const libc::c_void) -> *const libc::c_void;
}

pub struct BackendRef {
    raw_be: *const libc::c_void,
}

impl BackendRef {
    pub fn new(dn: &SdnRef) -> Result<Self, ()> {
        let raw_be = unsafe { slapi_be_select_exact(dn.as_ptr()) };
        if raw_be.is_null() {
            Err(())
        } else {
            Ok(BackendRef { raw_be })
        }
    }

    pub(crate) fn as_ptr(&self) -> *const libc::c_void {
        self.raw_be
    }

    pub fn begin_txn(self) -> Result<BackendRefTxn, ()> {
        let mut pb = Pblock::new();
        if pb.set_op_backend(&self) != 0 {
            return Err(());
        }
        let rc = unsafe { slapi_back_transaction_begin(pb.as_ptr()) };
        if rc != 0 {
            Err(())
        } else {
            Ok(BackendRefTxn {
                pb,
                _be: self,
                committed: false,
            })
        }
    }
}

pub struct BackendRefTxn {
    pb: Pblock,
    // Used to keep lifetimes in check.
    _be: BackendRef,
    committed: bool,
}

impl BackendRefTxn {
    pub fn commit(mut self) {
        self.committed = true;
        unsafe {
            slapi_back_transaction_commit(self.pb.as_ptr());
        }
    }
}

impl Drop for BackendRefTxn {
    fn drop(&mut self) {
        if self.committed == false {
            unsafe {
                slapi_back_transaction_abort(self.pb.as_ptr());
            }
        }
    }
}
