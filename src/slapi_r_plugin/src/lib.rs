#![deny(warnings)]
// #[macro_use]
// extern crate lazy_static;

#[macro_use]
pub mod macros;
pub mod backend;
pub mod ber;
pub mod charray;
mod constants;
pub mod dn;
pub mod entry;
pub mod error;
pub mod log;
pub mod modify;
pub mod pblock;
pub mod plugin;
pub mod search;
pub mod syntax_plugin;
pub mod task;
pub mod value;

pub mod prelude {
    pub use crate::backend::{BackendRef, BackendRefTxn};
    pub use crate::ber::BerValRef;
    pub use crate::charray::Charray;
    pub use crate::constants::{FilterType, PluginFnType, PluginType, PluginVersion, LDAP_SUCCESS};
    pub use crate::dn::{Sdn, SdnRef};
    pub use crate::entry::EntryRef;
    pub use crate::error::{DseCallbackStatus, LDAPError, PluginError, RPluginError};
    pub use crate::log::{log_error, ErrorLevel};
    pub use crate::modify::{ModType, Modify, SlapiMods};
    pub use crate::pblock::{Pblock, PblockRef};
    pub use crate::plugin::{register_plugin_ext, PluginIdRef, SlapiPlugin3};
    pub use crate::search::{Search, SearchScope};
    pub use crate::syntax_plugin::{
        matchingrule_register, SlapiOrdMr, SlapiSubMr, SlapiSyntaxPlugin1,
    };
    pub use crate::task::{task_register_handler_fn, task_unregister_handler_fn, Task, TaskRef};
    pub use crate::value::{Value, ValueArray, ValueArrayRef, ValueRef};
}
