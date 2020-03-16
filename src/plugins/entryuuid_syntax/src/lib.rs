#[macro_use]
extern crate slapi_r_plugin;
use slapi_r_plugin::prelude::*;
use std::cmp::Ordering;
use std::convert::TryInto;
use uuid::Uuid;

struct EntryUuidSyntax;

// https://tools.ietf.org/html/rfc4530

slapi_r_syntax_plugin_hooks!(entryuuid_syntax, EntryUuidSyntax);

impl SlapiSyntaxPlugin1 for EntryUuidSyntax {
    fn attr_oid() -> &'static str {
        "1.3.6.1.1.16.1"
    }

    fn attr_compat_oids() -> Vec<&'static str> {
        Vec::new()
    }

    fn attr_supported_names() -> Vec<&'static str> {
        vec!["1.3.6.1.1.16.1", "UUID"]
    }

    fn syntax_validate(bval: &BerValRef) -> Result<(), PluginError> {
        let r: Result<Uuid, PluginError> = bval.try_into();
        r.map(|_| ())
    }

    fn eq_mr_oid() -> &'static str {
        "1.3.6.1.1.16.2"
    }

    fn eq_mr_name() -> &'static str {
        "UUIDMatch"
    }

    fn eq_mr_desc() -> &'static str {
        "UUIDMatch matching rule."
    }

    fn eq_mr_supported_names() -> Vec<&'static str> {
        vec!["1.3.6.1.1.16.2", "uuidMatch", "UUIDMatch"]
    }

    fn filter_ava_eq(
        _pb: &mut PblockRef,
        bval_filter: &BerValRef,
        vals: &ValueArrayRef,
    ) -> Result<bool, PluginError> {
        let u = match bval_filter.try_into() {
            Ok(u) => u,
            Err(_e) => return Ok(false),
        };

        let r = vals.iter().fold(false, |acc, va| {
            if acc {
                acc
            } else {
                // is u in va?
                log_error!(ErrorLevel::Trace, "filter_ava_eq debug -> {:?}", va);
                let res: Result<Uuid, PluginError> = (&*va).try_into();
                match res {
                    Ok(vu) => vu == u,
                    Err(_) => acc,
                }
            }
        });
        log_error!(ErrorLevel::Trace, "filter_ava_eq result -> {:?}", r);
        Ok(r)
    }

    fn eq_mr_filter_values2keys(
        _pb: &mut PblockRef,
        vals: &ValueArrayRef,
    ) -> Result<ValueArray, PluginError> {
        vals.iter()
            .map(|va| {
                let u: Uuid = (&*va).try_into()?;
                Ok(Value::from(&u))
            })
            .collect()
    }
}

impl SlapiSubMr for EntryUuidSyntax {}

impl SlapiOrdMr for EntryUuidSyntax {
    fn ord_mr_oid() -> Option<&'static str> {
        Some("1.3.6.1.1.16.3")
    }

    fn ord_mr_name() -> &'static str {
        "UUIDOrderingMatch"
    }

    fn ord_mr_desc() -> &'static str {
        "UUIDMatch matching rule."
    }

    fn ord_mr_supported_names() -> Vec<&'static str> {
        vec!["1.3.6.1.1.16.3", "uuidOrderingMatch", "UUIDOrderingMatch"]
    }

    fn filter_ava_ord(
        _pb: &mut PblockRef,
        bval_filter: &BerValRef,
        vals: &ValueArrayRef,
    ) -> Result<Option<Ordering>, PluginError> {
        let u: Uuid = match bval_filter.try_into() {
            Ok(u) => u,
            Err(_e) => return Ok(None),
        };

        let r = vals.iter().fold(None, |acc, va| {
            if acc.is_some() {
                acc
            } else {
                // is u in va?
                log_error!(ErrorLevel::Trace, "filter_ava_ord debug -> {:?}", va);
                let res: Result<Uuid, PluginError> = (&*va).try_into();
                match res {
                    Ok(vu) => {
                        // 1.partial_cmp(2) => ordering::less
                        vu.partial_cmp(&u)
                    }
                    Err(_) => acc,
                }
            }
        });
        log_error!(ErrorLevel::Trace, "filter_ava_ord result -> {:?}", r);
        Ok(r)
    }

    fn filter_compare(a: &BerValRef, b: &BerValRef) -> Ordering {
        let ua: Uuid = a.try_into().expect("An invalid value a was given!");
        let ub: Uuid = b.try_into().expect("An invalid value b was given!");
        ua.cmp(&ub)
    }
}

#[cfg(test)]
mod tests {}
