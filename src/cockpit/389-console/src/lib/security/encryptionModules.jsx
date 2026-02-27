import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import { EncryptionModuleTable } from "./securityTables.jsx";

const _ = cockpit.gettext;

// Returns a normalized attribute value from dsconf JSON output.
function getModuleAttrValue(attrs, attrName, defaultValue = "") {
    const rawValue = attrs?.[attrName];
    if (Array.isArray(rawValue)) {
        return rawValue.length > 0 ? rawValue[0] : defaultValue;
    }
    if (rawValue === null || rawValue === undefined) {
        return defaultValue;
    }
    return rawValue;
}

// Maps module objects into the table row shape used by the UI table component.
function makeEncryptionModuleRows(modules) {
    return modules.map(module => ({
        name: module.name,
        certNickname: module.certNickname,
        activated: module.activated,
        token: module.token,
        serverCertExtractFile: module.serverCertExtractFile,
        serverKeyExtractFile: module.serverKeyExtractFile,
    }));
}

// Converts a raw encryption-module entry into normalized UI data.
function normalizeEncryptionModuleEntry(entry) {
    const attrs = entry?.attrs || entry || {};
    return {
        name: getModuleAttrValue(attrs, "cn", ""),
        certNickname: getModuleAttrValue(attrs, "nssslpersonalityssl", ""),
        activated: getModuleAttrValue(attrs, "nssslactivation", "off").toLowerCase(),
        token: getModuleAttrValue(attrs, "nsssltoken", "internal (software)"),
        serverCertExtractFile: getModuleAttrValue(attrs, "servercertextractfile", ""),
        serverKeyExtractFile: getModuleAttrValue(attrs, "serverkeyextractfile", ""),
        dn: entry?.dn || "",
    };
}

function EncryptionModules({ addNotification, serverId, serverCertNames }) {
    const [modules, setModules] = React.useState([]);
    const [rows, setRows] = React.useState([]);
    const [loadError, setLoadError] = React.useState("");
    const [partialDataCount, setPartialDataCount] = React.useState(0);
    const [certOptions, setCertOptions] = React.useState([]);
    const [actionInProgress, setActionInProgress] = React.useState(false);
    const [showToggleConfirm, setShowToggleConfirm] = React.useState(false);
    const [showDeleteConfirm, setShowDeleteConfirm] = React.useState(false);
    const [confirmChecked, setConfirmChecked] = React.useState(false);
    const [confirmSpinning, setConfirmSpinning] = React.useState(false);
    const [pendingModule, setPendingModule] = React.useState(null);
    const [pendingModuleActivate, setPendingModuleActivate] = React.useState(false);
    const [loading, setLoading] = React.useState(true);

    // Parse dsconf JSON errors and fallback safely for unexpected output.
    const parseCmdError = React.useCallback(err => {
        try {
            const errObj = JSON.parse(err);
            let msg = errObj.desc;
            if ("info" in errObj) {
                msg = `${errObj.desc} - ${errObj.info}`;
            }
            return msg;
        } catch (e) {
            return err?.toString() || _("Unexpected error");
        }
    }, []);

    // Build a consistent user-facing error string for module operations.
    const formatOpError = React.useCallback((opName, targetName, msg) => {
        // Localize the operation name so the full message is translated
        const opLabel = _(opName);
        const target = targetName ? cockpit.format(_(" for '$0'"), targetName) : "";
        return cockpit.format(_("Failed to $0$1 - $2"), opLabel, target, msg);
    }, []);

    // Mark cert nicknames already bound to a different module as non-selectable.
    const buildEncryptionModuleCertOptions = React.useCallback((moduleList, certNames) => {
        const usageByCert = {};
        for (const module of moduleList) {
            if (module.certNickname) {
                usageByCert[module.certNickname] = module.name;
            }
        }

        return (certNames || []).map(nickname => ({
            nickname,
            inUseByModule: usageByCert[nickname] || null,
            selectable: !usageByCert[nickname],
            isAdHoc: false,
        }));
    }, []);

    // Load module list and normalize it for table rendering and selectors.
    const loadEncryptionModules = React.useCallback(() => {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "security", "encryption-module", "list"
        ];
        log_cmd("loadEncryptionModules", "Load encryption modules", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const moduleResp = JSON.parse(content);
                    const moduleItems = moduleResp.items || [];
                    const normalizedModules = moduleItems.map(item => normalizeEncryptionModuleEntry(item));
                    const cleanModules = normalizedModules.filter(module => module.name !== "");
                    const partialCount = normalizedModules.length - cleanModules.length;
                    const tableRows = makeEncryptionModuleRows(cleanModules);
                    const moduleCertOptions = buildEncryptionModuleCertOptions(cleanModules, serverCertNames);
                    setModules(cleanModules);
                    setRows(tableRows);
                    setCertOptions(moduleCertOptions);
                    setLoadError("");
                    setPartialDataCount(partialCount);
                    setLoading(false);
                })
                .fail(err => {
                    const msg = parseCmdError(err);
                    setModules([]);
                    setRows([]);
                    setCertOptions([]);
                    setLoadError(msg);
                    setPartialDataCount(0);
                    setLoading(false);
                    addNotification(
                        "error",
                        cockpit.format(_("Error loading encryption modules - $0"), msg)
                    );
                });
    }, [addNotification, buildEncryptionModuleCertOptions, parseCmdError, serverCertNames, serverId]);

    // Load module data when the tab component is first rendered.
    React.useEffect(() => {
        loadEncryptionModules();
    }, [loadEncryptionModules]);

    // Recompute cert select options if certificate inventory changes.
    React.useEffect(() => {
        setCertOptions(buildEncryptionModuleCertOptions(modules, serverCertNames));
    }, [buildEncryptionModuleCertOptions, modules, serverCertNames]);

    // Execute a module mutation command with shared in-flight guards/notifications.
    const runEncryptionModuleOperation = React.useCallback(({ cmd, opName, successMsg, actionTarget: targetName = "" }) => {
        if (actionInProgress) {
            return Promise.reject(new Error("busy"));
        }
        log_cmd("runEncryptionModuleOperation", opName, cmd);
        setActionInProgress(true);

        return cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .then(() => {
                    addNotification("success", successMsg);
                    setActionInProgress(false);
                    loadEncryptionModules();
                })
                .catch(err => {
                    const msg = parseCmdError(err);
                    const formattedMsg = formatOpError(opName, targetName, msg);
                    addNotification("error", formattedMsg);
                    setActionInProgress(false);
                    throw err;
                });
    }, [actionInProgress, addNotification, formatOpError, loadEncryptionModules, parseCmdError]);

    // Build and execute dsconf add command for a new encryption module.
    const createEncryptionModule = React.useCallback((payload, onSuccessClose) => {
        if (actionInProgress) {
            return;
        }
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "security", "encryption-module", "add", payload.name,
            "--cert-nickname", payload.certNickname,
            "--token", payload.token,
        ];
        if (payload.activated) {
            cmd.push("--activated");
        }
        if (payload.serverCertExtractFile) {
            cmd.push("--server-cert-extract-file", payload.serverCertExtractFile);
        }
        if (payload.serverKeyExtractFile) {
            cmd.push("--server-key-extract-file", payload.serverKeyExtractFile);
        }

        runEncryptionModuleOperation({
            cmd,
            opName: "create encryption module",
            actionTarget: payload.name,
            successMsg: cockpit.format(_("Successfully created encryption module '$0'."), payload.name),
        }).then(() => {
            onSuccessClose();
        }).catch(() => {});
    }, [actionInProgress, runEncryptionModuleOperation, serverId]);

    // Build and execute dsconf edit command for an existing module.
    const editEncryptionModule = React.useCallback((moduleName, payload, onSuccessClose) => {
        if (actionInProgress) {
            return;
        }
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "security", "encryption-module", "edit", moduleName,
            "--cert-nickname", payload.certNickname,
            "--token", payload.token,
        ];

        if (payload.activated) {
            cmd.push("--activate");
        } else {
            cmd.push("--deactivate");
        }
        if (payload.serverCertExtractFile) {
            cmd.push("--server-cert-extract-file", payload.serverCertExtractFile);
        }
        if (payload.serverKeyExtractFile) {
            cmd.push("--server-key-extract-file", payload.serverKeyExtractFile);
        }

        runEncryptionModuleOperation({
            cmd,
            opName: "edit encryption module",
            actionTarget: moduleName,
            successMsg: cockpit.format(_("Successfully updated encryption module '$0'."), moduleName),
        }).then(() => {
            onSuccessClose();
        }).catch(() => {});
    }, [actionInProgress, runEncryptionModuleOperation, serverId]);

    // Open confirmation dialog for module activation state changes.
    const openToggleConfirm = React.useCallback((module, activate) => {
        setShowToggleConfirm(true);
        setConfirmChecked(false);
        setConfirmSpinning(false);
        setPendingModule(module);
        setPendingModuleActivate(activate);
    }, []);

    // Open confirmation dialog for module deletion.
    const openDeleteConfirm = React.useCallback(module => {
        setShowDeleteConfirm(true);
        setConfirmChecked(false);
        setConfirmSpinning(false);
        setPendingModule(module);
    }, []);

    // Close active confirmation dialog unless an action is still spinning.
    const closeConfirm = React.useCallback(() => {
        if (confirmSpinning) {
            return;
        }
        setShowToggleConfirm(false);
        setShowDeleteConfirm(false);
        setConfirmChecked(false);
        setConfirmSpinning(false);
        setPendingModule(null);
        setPendingModuleActivate(false);
    }, [confirmSpinning]);

    // Handle checkbox state in double-confirm modal.
    const onConfirmChange = React.useCallback(e => {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        setConfirmChecked(value);
    }, []);

    // Submit enable/disable after explicit user confirmation.
    const confirmToggle = React.useCallback(() => {
        if (!pendingModule || actionInProgress || confirmSpinning) {
            return;
        }

        const activate = pendingModuleActivate;
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "security", "encryption-module", "edit", pendingModule.name,
            activate ? "--activate" : "--deactivate",
        ];
        setConfirmSpinning(true);
        runEncryptionModuleOperation({
            cmd,
            opName: activate ? "enable encryption module" : "disable encryption module",
            actionTarget: pendingModule.name,
            successMsg: cockpit.format(
                activate
                    ? _("Successfully enabled encryption module '$0'.")
                    : _("Successfully disabled encryption module '$0'."),
                pendingModule.name
            ),
        }).then(() => {
            setShowToggleConfirm(false);
            setConfirmChecked(false);
            setConfirmSpinning(false);
            setPendingModule(null);
            setPendingModuleActivate(false);
        }).catch(() => {
            setConfirmSpinning(false);
        });
    }, [actionInProgress, confirmSpinning, pendingModule, pendingModuleActivate, runEncryptionModuleOperation, serverId]);

    // Submit delete after explicit user confirmation.
    const confirmDelete = React.useCallback(() => {
        if (!pendingModule || actionInProgress || confirmSpinning) {
            return;
        }

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "security", "encryption-module", "delete", pendingModule.name,
        ];
        setConfirmSpinning(true);
        runEncryptionModuleOperation({
            cmd,
            opName: "delete encryption module",
            actionTarget: pendingModule.name,
            successMsg: cockpit.format(_("Successfully deleted encryption module '$0'."), pendingModule.name),
        }).then(() => {
            setShowDeleteConfirm(false);
            setConfirmChecked(false);
            setConfirmSpinning(false);
            setPendingModule(null);
            setPendingModuleActivate(false);
        }).catch(() => {
            setConfirmSpinning(false);
        });
    }, [actionInProgress, confirmSpinning, pendingModule, runEncryptionModuleOperation, serverId]);

    // Render module table and confirmation dialogs owned by this container.
    return (
        <>
            <EncryptionModuleTable
                modules={modules}
                rows={rows}
                certOptions={certOptions}
                loading={loading}
                loadError={loadError}
                partialDataCount={partialDataCount}
                actionInProgress={actionInProgress}
                onCreateModule={createEncryptionModule}
                onEditModule={editEncryptionModule}
                onToggleModule={openToggleConfirm}
                onDeleteModule={openDeleteConfirm}
            />

            <DoubleConfirmModal
                showModal={showToggleConfirm}
                closeHandler={closeConfirm}
                handleChange={onConfirmChange}
                actionHandler={confirmToggle}
                spinning={confirmSpinning}
                item={pendingModule ? pendingModule.name : ""}
                checked={confirmChecked}
                mTitle={pendingModuleActivate ? _("Enable Encryption Module") : _("Disable Encryption Module")}
                mMsg={pendingModuleActivate
                    ? _("Are you sure you want to enable this encryption module?")
                    : _("Are you sure you want to disable this encryption module?")}
                mSpinningMsg={pendingModuleActivate ? _("Enabling ...") : _("Disabling ...")}
                mBtnName={pendingModuleActivate ? _("Enable") : _("Disable")}
            />

            <DoubleConfirmModal
                showModal={showDeleteConfirm}
                closeHandler={closeConfirm}
                handleChange={onConfirmChange}
                actionHandler={confirmDelete}
                spinning={confirmSpinning}
                item={pendingModule ? pendingModule.name : ""}
                checked={confirmChecked}
                mTitle={_("Delete Encryption Module")}
                mMsg={(pendingModule && pendingModule.activated === "on")
                    ? _("This encryption module is currently active. Deleting active modules is allowed but requires confirmation. Continue?")
                    : _("Are you sure you want to delete this encryption module?")}
                mSpinningMsg={_("Deleting ...")}
                mBtnName={_("Delete")}
            />
        </>
    );
}

EncryptionModules.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    serverCertNames: PropTypes.array,
};

export default EncryptionModules;
