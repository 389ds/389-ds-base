use crate::error::RPluginError;
use std::convert::TryFrom;
use std::os::raw::c_char;

pub const LDAP_SUCCESS: i32 = 0;
pub const PLUGIN_DEFAULT_PRECEDENCE: i32 = 50;

#[repr(i32)]
pub enum OpFlags {
    ByassReferrals = 0x0040_0000,
}

#[repr(i32)]
/// The set of possible function handles we can register via the pblock. These
/// values correspond to slapi-plugin.h.
pub enum PluginFnType {
    /// SLAPI_PLUGIN_DESTROY_FN
    Destroy = 11,
    /// SLAPI_PLUGIN_CLOSE_FN
    Close = 210,
    /// SLAPI_PLUGIN_START_FN
    Start = 212,
    /// SLAPI_PLUGIN_PRE_BIND_FN
    PreBind = 401,
    /// SLAPI_PLUGIN_PRE_UNBIND_FN
    PreUnbind = 402,
    /// SLAPI_PLUGIN_PRE_SEARCH_FN
    PreSearch = 403,
    /// SLAPI_PLUGIN_PRE_COMPARE_FN
    PreCompare = 404,
    /// SLAPI_PLUGIN_PRE_MODIFY_FN
    PreModify = 405,
    /// SLAPI_PLUGIN_PRE_MODRDN_FN
    PreModRDN = 406,
    /// SLAPI_PLUGIN_PRE_ADD_FN
    PreAdd = 407,
    /// SLAPI_PLUGIN_PRE_DELETE_FN
    PreDelete = 408,
    /// SLAPI_PLUGIN_PRE_ABANDON_FN
    PreAbandon = 409,
    /// SLAPI_PLUGIN_PRE_ENTRY_FN
    PreEntry = 410,
    /// SLAPI_PLUGIN_PRE_REFERRAL_FN
    PreReferal = 411,
    /// SLAPI_PLUGIN_PRE_RESULT_FN
    PreResult = 412,
    /// SLAPI_PLUGIN_PRE_EXTOP_FN
    PreExtop = 413,
    /// SLAPI_PLUGIN_BE_PRE_ADD_FN
    BeTxnPreAdd = 460,
    /// SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN
    BeTxnPreModify = 461,
    /// SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN
    BeTxnPreModRDN = 462,
    /// SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN
    BeTxnPreDelete = 463,
    /// SLAPI_PLUGIN_BE_TXN_PRE_DELETE_TOMBSTONE_FN
    BeTxnPreDeleteTombstone = 464,
    /// SLAPI_PLUGIN_POST_SEARCH_FN
    PostSearch = 503,
    /// SLAPI_PLUGIN_BE_POST_ADD_FN
    BeTxnPostAdd = 560,
    /// SLAPI_PLUGIN_BE_POST_MODIFY_FN
    BeTxnPostModify = 561,
    /// SLAPI_PLUGIN_BE_POST_MODRDN_FN
    BeTxnPostModRDN = 562,
    /// SLAPI_PLUGIN_BE_POST_DELETE_FN
    BeTxnPostDelete = 563,

    /// SLAPI_PLUGIN_MR_FILTER_CREATE_FN
    MRFilterCreate = 600,
    /// SLAPI_PLUGIN_MR_INDEXER_CREATE_FN
    MRIndexerCreate = 601,
    /// SLAPI_PLUGIN_MR_FILTER_AVA
    MRFilterAva = 618,
    /// SLAPI_PLUGIN_MR_FILTER_SUB
    MRFilterSub = 619,
    /// SLAPI_PLUGIN_MR_VALUES2KEYS
    MRValuesToKeys = 620,
    /// SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA
    MRAssertionToKeysAva = 621,
    /// SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB
    MRAssertionToKeysSub = 622,
    /// SLAPI_PLUGIN_MR_COMPARE
    MRCompare = 625,
    /// SLAPI_PLUGIN_MR_NORMALIZE
    MRNormalize = 626,

    /// SLAPI_PLUGIN_SYNTAX_FILTER_AVA
    SyntaxFilterAva = 700,
    /// SLAPI_PLUGIN_SYNTAX_FILTER_SUB
    SyntaxFilterSub = 701,
    /// SLAPI_PLUGIN_SYNTAX_VALUES2KEYS
    SyntaxValuesToKeys = 702,
    /// SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA
    SyntaxAssertion2KeysAva = 703,
    /// SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB
    SyntaxAssertion2KeysSub = 704,
    /// SLAPI_PLUGIN_SYNTAX_FLAGS
    SyntaxFlags = 707,
    /// SLAPI_PLUGIN_SYNTAX_COMPARE
    SyntaxCompare = 708,
    /// SLAPI_PLUGIN_SYNTAX_VALIDATE
    SyntaxValidate = 710,
    /// SLAPI_PLUGIN_SYNTAX_NORMALIZE
    SyntaxNormalize = 711,

    /// SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN
    PwdStorageEncrypt = 800,
    /// SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN
    PwdStorageCompare = 802,
}

static SV01: [u8; 3] = [b'0', b'1', b'\0'];
static SV02: [u8; 3] = [b'0', b'2', b'\0'];
static SV03: [u8; 3] = [b'0', b'3', b'\0'];

/// Corresponding plugin versions
pub enum PluginVersion {
    /// SLAPI_PLUGIN_VERSION_01
    V01,
    /// SLAPI_PLUGIN_VERSION_02
    V02,
    /// SLAPI_PLUGIN_VERSION_03
    V03,
}

impl PluginVersion {
    pub fn to_char_ptr(&self) -> *const c_char {
        match self {
            PluginVersion::V01 => &SV01 as *const _ as *const c_char,
            PluginVersion::V02 => &SV02 as *const _ as *const c_char,
            PluginVersion::V03 => &SV03 as *const _ as *const c_char,
        }
    }
}

static SMATCHINGRULE: [u8; 13] = [
    b'm', b'a', b't', b'c', b'h', b'i', b'n', b'g', b'r', b'u', b'l', b'e', b'\0',
];

pub enum PluginType {
    MatchingRule,
}

impl PluginType {
    pub fn to_char_ptr(&self) -> *const c_char {
        match self {
            PluginType::MatchingRule => &SMATCHINGRULE as *const _ as *const c_char,
        }
    }
}

#[repr(i32)]
/// data types that we can get or retrieve from the pblock. This is only
/// used internally.
pub(crate) enum PblockType {
    /// SLAPI_PLUGIN_PRIVATE
    _PrivateData = 4,
    /// SLAPI_PLUGIN_VERSION
    Version = 8,
    /// SLAPI_PLUGIN_DESCRIPTION
    _Description = 12,
    /// SLAPI_PLUGIN_IDENTITY
    Identity = 13,
    /// SLAPI_PLUGIN_INTOP_RESULT
    OpResult = 15,
    /// SLAPI_ADD_ENTRY
    AddEntry = 60,
    /// SLAPI_BACKEND
    Backend = 130,
    /// SLAPI_PLUGIN_MR_NAMES
    MRNames = 624,
    /// SLAPI_PLUGIN_SYNTAX_NAMES
    SyntaxNames = 705,
    /// SLAPI_PLUGIN_SYNTAX_OID
    SyntaxOid = 706,
    /// SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME
    PwdStorageSchemeName = 810,
}

/// See ./ldap/include/ldaprot.h
#[derive(PartialEq)]
pub enum FilterType {
    And = 0xa0,
    Or = 0xa1,
    Not = 0xa2,
    Equality = 0xa3,
    Substring = 0xa4,
    Ge = 0xa5,
    Le = 0xa6,
    Present = 0x87,
    Approx = 0xa8,
    Extended = 0xa9,
}

impl TryFrom<i32> for FilterType {
    type Error = RPluginError;

    fn try_from(value: i32) -> Result<Self, Self::Error> {
        match value {
            0xa0 => Ok(FilterType::And),
            0xa1 => Ok(FilterType::Or),
            0xa2 => Ok(FilterType::Not),
            0xa3 => Ok(FilterType::Equality),
            0xa4 => Ok(FilterType::Substring),
            0xa5 => Ok(FilterType::Ge),
            0xa6 => Ok(FilterType::Le),
            0x87 => Ok(FilterType::Present),
            0xa8 => Ok(FilterType::Approx),
            0xa9 => Ok(FilterType::Extended),
            _ => Err(RPluginError::FilterType),
        }
    }
}
