#[macro_use]
extern crate slapi_r_plugin;
use slapi_r_plugin::prelude::*;
use std::convert::{TryFrom, TryInto};
use std::os::raw::c_char;
use uuid::Uuid;

#[derive(Debug)]
struct FixupData {
    basedn: Sdn,
    raw_filter: String,
}

struct EntryUuid;
/*
 *                    /---- plugin ident
 *                    |          /---- Struct name.
 *                    V          V
 */
slapi_r_plugin_hooks!(entryuuid, EntryUuid);

/*
 *                             /---- plugin ident
 *                             |          /---- cb ident
 *                             |          |                   /---- map function
 *                             V          V                   V
 */
slapi_r_search_callback_mapfn!(entryuuid, entryuuid_fixup_cb, entryuuid_fixup_mapfn);

fn assign_uuid(e: &mut EntryRef) {
    let sdn = e.get_sdnref();

    // 🚧 safety barrier 🚧
    if e.contains_attr("entryUUID") {
        log_error!(
            ErrorLevel::Plugin,
            "assign_uuid -> entryUUID exists, skipping dn {}",
            sdn.to_dn_string()
        );
        return;
    }

    // We could consider making these lazy static.
    let config_sdn = Sdn::try_from("cn=config").expect("Invalid static dn");
    let schema_sdn = Sdn::try_from("cn=schema").expect("Invalid static dn");

    if sdn.is_below_suffix(&*config_sdn) || sdn.is_below_suffix(&*schema_sdn) {
        // We don't need to assign to these suffixes.
        log_error!(
            ErrorLevel::Plugin,
            "assign_uuid -> not assigning to {:?} as part of system suffix",
            sdn.to_dn_string()
        );
        return;
    }

    // Generate a new Uuid.
    let u: Uuid = Uuid::new_v4();
    log_error!(
        ErrorLevel::Plugin,
        "assign_uuid -> assigning {:?} to dn {}",
        u,
        sdn.to_dn_string()
    );

    let uuid_value = Value::from(&u);

    // Add it to the entry
    e.add_value("entryUUID", &uuid_value);
}

impl SlapiPlugin3 for EntryUuid {
    // Indicate we have pre add
    fn has_betxn_pre_add() -> bool {
        true
    }

    fn betxn_pre_add(pb: &mut PblockRef) -> Result<(), PluginError> {
        if pb.get_is_replicated_operation() {
            log_error!(
                ErrorLevel::Plugin,
                "betxn_pre_add -> replicated operation, will not change"
            );
            return Ok(());
        }

        log_error!(ErrorLevel::Plugin, "betxn_pre_add -> start");

        let mut e = pb.get_op_add_entryref().map_err(|_| PluginError::Pblock)?;
        assign_uuid(&mut e);

        Ok(())
    }

    fn has_task_handler() -> Option<&'static str> {
        Some("entryuuid task")
    }

    type TaskData = FixupData;

    fn task_validate(e: &EntryRef) -> Result<Self::TaskData, LDAPError> {
        // Does the entry have what we need?
        let basedn: Sdn = match e.get_attr("basedn") {
            Some(values) => values
                .first()
                .ok_or_else(|| {
                    log_error!(
                        ErrorLevel::Plugin,
                        "task_validate basedn error -> empty value array?"
                    );
                    LDAPError::Operation
                })?
                .as_ref()
                .try_into()
                .map_err(|e| {
                    log_error!(ErrorLevel::Plugin, "task_validate basedn error -> {:?}", e);
                    LDAPError::Operation
                })?,
            None => return Err(LDAPError::ObjectClassViolation),
        };

        let raw_filter: String = match e.get_attr("filter") {
            Some(values) => values
                .first()
                .ok_or_else(|| {
                    log_error!(
                        ErrorLevel::Plugin,
                        "task_validate filter error -> empty value array?"
                    );
                    LDAPError::Operation
                })?
                .as_ref()
                .try_into()
                .map_err(|e| {
                    log_error!(ErrorLevel::Plugin, "task_validate filter error -> {:?}", e);
                    LDAPError::Operation
                })?,
            None => {
                // Give a default filter.
                "(objectClass=*)".to_string()
            }
        };

        // Error if the first filter is empty?

        // Now, to make things faster, we wrap the filter in a exclude term.

        // 2021 - #4877 because we allow entryuuid to be strings, on import these may
        // be invalid. As a result, we DO need to allow the fixup to check the entryuuid
        // value is correct, so we can not exclude these during the search.
        /*
        let raw_filter = if !raw_filter.starts_with('(') && !raw_filter.ends_with('(') {
            format!("(&({})(!(entryuuid=*)))", raw_filter)
        } else {
            format!("(&{}(!(entryuuid=*)))", raw_filter)
        };
        */

        Ok(FixupData { basedn, raw_filter })
    }

    fn task_be_dn_hint(data: &Self::TaskData) -> Option<Sdn> {
        Some(data.basedn.clone())
    }

    fn task_handler(_task: &Task, data: Self::TaskData) -> Result<Self::TaskData, PluginError> {
        log_error!(
            ErrorLevel::Plugin,
            "task_handler -> start thread with -> {:?}",
            data
        );

        let search = Search::new_map_entry(
            &(*data.basedn),
            SearchScope::Subtree,
            &data.raw_filter,
            plugin_id(),
            &(),
            entryuuid_fixup_cb,
        )
        .map_err(|e| {
            log_error!(
                ErrorLevel::Error,
                "task_handler -> Unable to construct search -> {:?}",
                e
            );
            e
        })?;

        match search.execute() {
            Ok(_) => {
                log_error!(ErrorLevel::Info, "task_handler -> fixup complete, success!");
                Ok(data)
            }
            Err(e) => {
                // log, and return
                log_error!(
                    ErrorLevel::Error,
                    "task_handler -> fixup complete, failed -> {:?}",
                    e
                );
                Err(PluginError::GenericFailure)
            }
        }
    }

    fn start(_pb: &mut PblockRef) -> Result<(), PluginError> {
        log_error!(ErrorLevel::Plugin, "plugin start");
        Ok(())
    }

    fn close(_pb: &mut PblockRef) -> Result<(), PluginError> {
        log_error!(ErrorLevel::Plugin, "plugin close");
        Ok(())
    }
}

pub fn entryuuid_fixup_mapfn(e: &EntryRef, _data: &()) -> Result<(), PluginError> {
    /* Supply a modification to the entry. */
    let sdn = e.get_sdnref();

    /* Check that entryuuid doesn't already exist, and is valid */
    if let Some(valueset) = e.get_attr("entryUUID") {
        if valueset.iter().all(|v| {
            let u: Result<Uuid, _> = (&v).try_into();
            u.is_ok()
        }) {
            // All values were valid uuid, move on!
            log_error!(
                ErrorLevel::Plugin,
                "skipping fixup for -> {}",
                sdn.to_dn_string()
            );
            return Ok(());
        }
    }

    // Setup the modifications
    let mut mods = SlapiMods::new();

    let u: Uuid = Uuid::new_v4();
    let uuid_value = Value::from(&u);
    let values: ValueArray = std::iter::once(uuid_value).collect();
    mods.append(ModType::Replace, "entryUUID", values);

    /* */
    let lmod = Modify::new(&sdn, mods, plugin_id())?;

    match lmod.execute() {
        Ok(_) => {
            log_error!(ErrorLevel::Plugin, "fixed-up -> {}", sdn.to_dn_string());
            Ok(())
        }
        Err(e) => {
            log_error!(
                ErrorLevel::Error,
                "entryuuid_fixup_mapfn -> fixup failed -> {} {:?}",
                sdn.to_dn_string(),
                e
            );
            Err(PluginError::GenericFailure)
        }
    }
}

#[cfg(test)]
mod tests {}
