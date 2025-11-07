import cockpit from "cockpit";
import React, { useState, useEffect } from 'react';
import {
	Button,
	Form,
	FormHelperText,
    FormSelect,
    FormSelectOption,
	Grid,
	GridItem,
	Modal,
	ModalVariant,
	TextInput,
	ValidatedOptions
} from '@patternfly/react-core';
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd, valid_dn } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

const _ = cockpit.gettext;

const DynamicLists = ({
    rows = [],
    serverId = "",
    savePluginHandler,
    pluginListHandler,
    addNotification,
    toggleLoadingHandler,
    attributes = [],
    objectClasses = [],
}) => {
    // Main plugin settings
    const [settings, setSettings] = React.useState({
        'objectclass': 'groupofurls',
        'URLAttr': 'memberurl',
        'listAttr': 'member',
        'sharedConfigEntry': ''});
    const [_settings, setOriginalSettings] = React.useState({
        'objectclass': 'groupofurls',
        'URLAttr': 'memberurl',
        'listAttr': 'member',
        'sharedConfigEntry': ''});
    const [newEntry, setNewEntry] = React.useState(false);
    // Main plugin state settings
    const [saveBtnDisabled, setSaveBtnDisabled] = React.useState(true);
    const [saving, setSaving] = React.useState(false);
    const [dnAttrs, setDnAttrs] = React.useState([]);
    const [urlAttrs, setUrlAttrs] = React.useState([]);
    // Shared config settings
    const [configSettings, setConfigSettings] = React.useState({
        'objectclass': 'groupofurls',
        'URLAttr': 'memberurl',
        'listAttr': 'member'});
    const [_configSettings, setOriginalConfigSettings] = React.useState({
        'objectclass': 'groupofurls',
        'URLAttr': 'memberurl',
        'listAttr': 'member'});
    const [configDN, setConfigDN] = React.useState('');
    const [_configDN, setOriginalConfigDN] = React.useState('');
    // Modal state settings
    const [configEntryModalShow, setConfigEntryModalShow] = React.useState(false);
    const [saveBtnDisabledModal, setSaveBtnDisabledModal] = React.useState(true);
    const [modalSpinning, setModalSpinning] = React.useState(false);
    const [modalChecked, setModalChecked] = React.useState(false);
    const [showConfirmDelete, setShowConfirmDelete] = React.useState(false);
    const [addSpinning, setAddSpinning] = React.useState(false);
    const [dnAttrsConfig, setDnAttrsConfig] = React.useState([]);
    const [urlAttrsConfig, setUrlAttrsConfig] = React.useState([]);

    const dn_syntax_oids = ["1.3.6.1.4.1.1466.115.121.1.34", "1.3.6.1.4.1.1466.115.121.1.12"];

    useEffect(() => {
        updateFields();
    }, [rows, serverId, attributes, objectClasses]);

    useEffect(() => {
        validateModal();
    }, [configSettings.objectclass, configSettings.URLAttr,
        configSettings.listAttr, configDN, _configDN]);

    useEffect(() => {
        validateConfig();
    }, [settings.objectclass, settings.URLAttr,
        settings.listAttr, settings.sharedConfigEntry]);

    const setSetting = (key, value) => {
        setSettings(prevSettings => ({
            ...prevSettings,
            [key]: value
        }));

    };
    const setOriginalSetting = (key, value) => {
        setOriginalSettings(prevSettings => ({
            ...prevSettings,
            [key]: value
        }));
    };

    const setConfigSetting = (key, value) => {
        setConfigSettings(prevConfigSettings => ({
            ...prevConfigSettings,
            [key]: value
        }));
    };

    const setOriginalConfigSetting = (key, value) => {
        setOriginalConfigSettings(prevConfigSettings => ({
            ...prevConfigSettings,
            [key]: value
        }));
    };

    const handleShowConfirmDelete = () => {
        setShowConfirmDelete(true);
        setModalChecked(false);
        setModalSpinning(false);
    };

    const closeConfirmDelete = () => {
        setShowConfirmDelete(false);
        setModalChecked(false);
        setModalSpinning(false);
    };

    const validateConfig = () => {
        let all_good = false;

        // Check for value differences to see if the save btn should be enabled
        const attrs = [
            'objectclass', 'URLAttr', 'listAttr', 'nsslapd-pluginConfigArea'
        ];
        for (const check_attr of attrs) {
            if (settings[check_attr] !== _settings[check_attr]) {
                all_good = true;
                break;
            }
        }

        setSaveBtnDisabled(!all_good);

        // Update the available attributes and objectclasses
        const dnAttrs = attributes.filter(attr =>
            (attr.syntax[0] === dn_syntax_oids[0] || attr.syntax[0] === dn_syntax_oids[1]) &&
            attr.name !== undefined &&
            attr.name[0].toLowerCase() !== settings.URLAttr.toLowerCase());
        const urlAttrs = attributes.filter(attr =>
            attr.name !== undefined &&
            attr.name[0].toLowerCase() !== settings.listAttr.toLowerCase());
        setDnAttrs(dnAttrs);
        setUrlAttrs(urlAttrs);
    };

    const validateModal = () => {
        let all_good = false;

        // Check for value differences to see if the save btn should be enabled
        const attrs = [
            'objectclass', 'URLAttr', 'listAttr',
        ];
        for (const check_attr of attrs) {
            if (configSettings[check_attr] !== _configSettings[check_attr]) {
                all_good = true;
                break;
            }
        }
        if (configDN !== _configDN) {
            all_good = true;
        }

        setSaveBtnDisabledModal(!all_good);

        // Update the available attributes and objectclasses
        const dnAttrsConfig = attributes.filter(attr =>
            (attr.syntax[0] === dn_syntax_oids[0] || attr.syntax[0] === dn_syntax_oids[1]) &&
            attr.name !== undefined &&
            attr.name[0].toLowerCase() !== configSettings.URLAttr.toLowerCase());
        const urlAttrsConfig = attributes.filter(attr =>
            attr.name !== undefined &&
            attr.name[0].toLowerCase() !== configSettings.listAttr.toLowerCase());
        setDnAttrsConfig(dnAttrsConfig);
        setUrlAttrsConfig(urlAttrsConfig);
    };

    const onChange = (e) => {
        // Generic handler for things that don't need validating
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        setModalChecked(value);
    };

    const handleSaveConfig = () => {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "plugin",
            "dynamic-lists",
            "set",
            "--objectclass",
            settings.objectclass || "delete",
            "--url-attr",
            settings.URLAttr || "delete",
            "--list-attr",
            settings.listAttr || "delete",
            "--config-entry",
            settings.sharedConfigEntry || "delete"
        ];

        setSaving(true);

        log_cmd(
            "handleSaveConfig",
            `Save Dynamic Lists Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("dynamicListsOperation", "Result", content);
                    addNotification(
                        "success",
                        _("Successfully updated Dynamic Lists Plugin")
                    );
                    setSaving(false);
                    setSaveBtnDisabled(true);
                    pluginListHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if ('info' in errMsg) {
                        errMsg = errMsg.desc + " " + errMsg.info;
                    } else {
                        errMsg = errMsg.desc;
                    }
                    addNotification(
                        "error", cockpit.format(_("Error during update - $0"), errMsg)
                    );
                    setSaving(false);
                    setSaveBtnDisabled(true);
                    pluginListHandler();
                });
    };

    const handleOpenModal = () => {
        if (!settings.sharedConfigEntry) {
            setNewEntry(true);
            setConfigEntryModalShow(true);
            setSaveBtnDisabledModal(true);
        } else {
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
                "plugin",
                "dynamic-lists",
                "config-entry",
                "show",
                settings.sharedConfigEntry
            ];

            log_cmd("handleOpenModal", "Fetch the shared config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const pluginRow = JSON.parse(content).attrs;
                        setConfigEntryModalShow(true);
                        setSaveBtnDisabledModal(true);
                        setNewEntry(false);
                        setConfigDN(settings.sharedConfigEntry);
                        setOriginalConfigDN(settings.sharedConfigEntry);
                        setConfigSetting('configObjectclass',
                            pluginRow["dynamiclistobjectclass"] === undefined
                                ? "GroupOfUrls"
                                : pluginRow["dynamiclistobjectclass"][0]
                        );
                        setOriginalConfigSetting('configObjectclass',
                            pluginRow["dynamiclistobjectclass"] === undefined
                                ? "GroupOfUrls"
                                : pluginRow["dynamiclistobjectclass"][0]
                        );
                        setConfigSetting('configURLAttr',
                            pluginRow["dynamiclisturlattr"] === undefined
                                ? "MemberUrl"
                                : pluginRow["dynamiclisturlattr"][0]
                        );
                        setOriginalConfigSetting('configURLAttr',
                            pluginRow["dynamiclisturlattr"] === undefined
                                ? "MemberUrl"
                                : pluginRow["dynamiclisturlattr"][0]
                        );
                        setConfigSetting('configListAttr',
                            pluginRow["dynamiclistattr"] === undefined
                                ? "member"
                                : pluginRow["dynamiclistattr"][0]
                        );
                        setOriginalConfigSetting('configListAttr',
                            pluginRow["dynamiclistattr"] === undefined
                                ? "member"
                                : pluginRow["dynamiclistattr"][0]
                        );

                    })
                    .fail(_ => {
                        setConfigEntryModalShow(false);
                        setNewEntry(true);
                        setConfigDN("");
                        setOriginalConfigDN("");
                        setConfigSetting('configObjectclass', "GroupOfUrls");
                        setOriginalConfigSetting('configObjectclass', "GroupOfUrls");
                        setConfigSetting('configURLAttr', "MemberUrl");
                        setOriginalConfigSetting('configURLAttr', "MemberUrl");
                        setConfigSetting('configListAttr', "member");
                        setOriginalConfigSetting('configListAttr', "member");
                    });
        }
    };

    const handleCloseModal = () => {
        setConfigEntryModalShow(false);
        setAddSpinning(false);
        setModalSpinning(false);
        setConfigDN("");
        setOriginalConfigDN("");
        setConfigSetting('configObjectclass', "GroupOfUrls");
        setOriginalConfigSetting('configObjectclass', "GroupOfUrls");
        setConfigSetting('configURLAttr', "MemberUrl");
        setOriginalConfigSetting('configURLAttr', "MemberUrl");
        setConfigSetting('configListAttr', "member");
        setOriginalConfigSetting('configListAttr', "member");
    };

    const cmdConfigOperation = (action) => {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "plugin",
            "dynamic-lists",
            "config-entry",
            action,
            configDN,
            "--objectclass",
            configSettings.objectclass || "delete",
            "--url-attr",
            configSettings.URLAttr || "delete",
            "--list-attr",
            configSettings.listAttr || "delete",
        ];

        setModalSpinning(true);

        log_cmd(
            "dynamicListsOperation",
            `Do the ${action} operation on the Dynamic Lists Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("dynamicListsOperation", "Result", content);
                    addNotification(
                        "success",
                        cockpit.format(_("Config entry $0 was successfully $1"),
                                       configDN, action + "ed")
                    );
                    pluginListHandler();
                    handleCloseModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry $0 operation $1: $2"),
                                       configDN, action , errMsg.desc)
                    );
                    pluginListHandler();
                    handleCloseModal();
                });
    };

    const deleteConfig = () => {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "plugin",
            "dynamic-lists",
            "config-entry",
            "delete",
            settings.sharedConfigEntry
        ];
        setModalSpinning(true);
        log_cmd("deleteConfig", "Delete the Dynamic Lists Plugin config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteConfig", "Result", content);
                    addNotification(
                        "success",
                        cockpit.format(_("Config entry $0 was successfully deleted"),
                                       settings.sharedConfigEntry)
                    );
                    pluginListHandler();
                    closeConfirmDelete();
                    handleCloseModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry removal operation - $0"),
                                       settings.sharedConfigEntry, errMsg.desc)
                    );
                    pluginListHandler();
                    closeConfirmDelete();
                    handleCloseModal();
                });
    };

    const handleAddConfig = () => {
        cmdConfigOperation("add");
    };

    const handleEditConfig = () => {
        cmdConfigOperation("set");
    };

    const updateFields = () => {
        if (rows.length > 0) {
            const pluginRow = rows.find(
                row => row.cn[0].toLowerCase() === "dynamic lists"
            );
            if (pluginRow === undefined) {
                // This shouln't happen, but if it does reset the fields
                setDnAttrs([]);
                setUrlAttrs([]);
                setDnAttrsConfig([]);
                setUrlAttrsConfig([]);
                setConfigDN("");
                setOriginalConfigDN("");
                setConfigSetting('configObjectclass', "GroupOfUrls");
                setOriginalConfigSetting('configObjectclass', "GroupOfUrls");
                setConfigSetting('configURLAttr', "MemberUrl");
                setOriginalConfigSetting('configURLAttr', "MemberUrl");
                setConfigSetting('configListAttr', "member");
                setOriginalConfigSetting('configListAttr', "member");
                return;
            }

            setSetting('objectclass',
                pluginRow["dynamiclistobjectclass"] === undefined
                    ? "GroupOfUrls"
                    : pluginRow["dynamiclistobjectclass"][0]
            );
            setOriginalSetting('objectclass', pluginRow["dynamiclistobjectclass"] === undefined
                ? "GroupOfUrls"
                : pluginRow["dynamiclistobjectclass"][0]);

            setSetting('URLAttr', pluginRow["dynamiclisturlattr"] === undefined
                ? "MemberUrl"
                : pluginRow["dynamiclisturlattr"][0]);
            setOriginalSetting('URLAttr', pluginRow["dynamiclisturlattr"] === undefined
                ? "MemberUrl"
                : pluginRow["dynamiclisturlattr"][0]);

            setSetting('listAttr', pluginRow["dynamiclistattr"] === undefined
                ? "member"
                : pluginRow["dynamiclistattr"][0]);
            setOriginalSetting('listAttr', pluginRow["dynamiclistattr"] === undefined
                ? "member"
                : pluginRow["dynamiclistattr"][0]);

            setSetting('sharedConfigEntry', pluginRow["nsslapd-pluginConfigArea"] === undefined
                ? ""
                : pluginRow["nsslapd-pluginConfigArea"][0]);
            setOriginalSetting('sharedConfigEntry', pluginRow["nsslapd-pluginConfigArea"] === undefined
                ? ""
                : pluginRow["nsslapd-pluginConfigArea"][0]);

            const dnAttrs = attributes.filter(attr =>
                (attr.syntax[0] === dn_syntax_oids[0] || attr.syntax[0] === dn_syntax_oids[1]) &&
                attr.name !== undefined &&
                attr.name[0].toLowerCase() !== settings.URLAttr.toLowerCase());
            const urlAttrs = attributes.filter(attr =>
                attr.name !== undefined &&
                attr.name[0].toLowerCase() !== settings.listAttr.toLowerCase());
            setDnAttrs(dnAttrs);
            setUrlAttrs(urlAttrs);
        }
    };

    return (
        <div className={saving || addSpinning || modalSpinning ? "ds-disabled" : ""}>
            <Modal
                variant={ModalVariant.medium}
                title={_("Manage Dynamic Lists Plugin Shared Config Entry")}
                isOpen={configEntryModalShow}
                aria-labelledby="ds-modal"
                onClose={handleCloseModal}
                actions={
                    !newEntry ? [
                        <Button
                            key="del"
                            variant="primary"
                            onClick={handleShowConfirmDelete}
                        >
                            {_("Delete Config")}
                        </Button>,
                        <Button
                            key="save"
                            variant="primary"
                            onClick={handleEditConfig}
                            isDisabled={saveBtnDisabledModal ||
                                        modalSpinning ||
                                        !valid_dn(configDN)}
                            isLoading={modalSpinning}
                            spinnerAriaValueText={modalSpinning ? _("Saving") : undefined}
                        >
                            {modalSpinning ? _("Saving ...") : _("Save Config")}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={handleCloseModal}>
                            {_("Cancel")}
                        </Button>
                    ] : [
                        <Button
                            key="add"
                            variant="primary"
                            onClick={handleAddConfig}
                            isDisabled={saveBtnDisabledModal ||
                                        modalSpinning ||
                                        !valid_dn(configDN)}
                            isLoading={modalSpinning}
                            spinnerAriaValueText={modalSpinning ? _("Adding") : undefined}
                        >
                            {modalSpinning ? _("Adding ...") : _("Add Config")}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={handleCloseModal}>
                            {_("Cancel")}
                        </Button>
                    ]
                }
            >
                <Form isHorizontal autoComplete="off">
                    <Grid className="ds-margin-top" title={_("The config entry full DN")}>
                        <GridItem span={3} className="ds-label">
                            {_("Config DN")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={configDN}
                                type="text"
                                id="sharedConfigEntry"
                                aria-describedby="horizontal-form-name-helper"
                                name="sharedConfigEntry"
                                onChange={(e, str) => { setConfigDN(str) }}
                                validated={valid_dn(configDN) ?
                                           ValidatedOptions.default :
                                           ValidatedOptions.error}
                                isDisabled={!newEntry}
                            />
                            {newEntry &&
                                <FormHelperText>
                                    {_("Value must be a valid DN")}
                                </FormHelperText>
                            }
                        </GridItem>
                    </Grid>
                    <Grid title={_("Specifies the objectclass that will trigger the dynamic list plugin (dynamicListObjectclass)")}>
                        <GridItem className="ds-label" span={3}>
                            {_("ObjectClass")}
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect
                                value={configSettings.objectclass.toLowerCase()}
                                onChange={(event, selection) => {
                                    setConfigSetting('objectclass', selection);
                                }}
                                aria-label="Dynamic List ObjectClass"
                                ouiaId="DynamicListObjectClassSelect"
                            >
                                {objectClasses.map((option, index) => (
                                    <FormSelectOption key={index} value={option.toLowerCase()} label={option} />
                                ))}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid title={_("Specifies the attribute in the entry for the LDAP URL of the dynamic list (dynamicListUrlAttr)")}>
                        <GridItem className="ds-label" span={3}>
                            {_("URL Attribute")}
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect
                                value={configSettings.URLAttr.toLowerCase()}
                                onChange={(event, selection) => {
                                    setConfigSetting('URLAttr', selection);
                                }}
                                aria-label="Dynamic List URL Attribute"
                                ouiaId="DynamicListURLAttributeSelect"
                            >
                                {urlAttrsConfig.map((option, index) => (
                                    <FormSelectOption key={index} value={option.name[0].toLowerCase()} label={option.name[0]} />
                                ))}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid title={_("Specifies attributes to check for and update (referint-membership-attr)")}>
                        <GridItem className="ds-label" span={3}>
                            {_("List Attribute")}
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect
                                value={configSettings.listAttr.toLowerCase()}
                                onChange={(event, selection) => {
                                    setConfigSetting('listAttr', selection);
                                }}
                                aria-label="Dynamic List Attribute"
                                ouiaId="DynamicListAttributeSelect"
                            >
                                {dnAttrsConfig.map((option, index) => (
                                    <FormSelectOption key={index} value={option.name[0].toLowerCase()} label={option.name[0]} />
                                ))}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>

            <PluginBasicConfig
                rows={rows}
                serverId={serverId}
                cn="dynamic lists"
                pluginName="Dynamic Lists"
                cmdName="dynamic-lists"
                savePluginHandler={savePluginHandler}
                pluginListHandler={pluginListHandler}
                addNotification={addNotification}
                toggleLoadingHandler={toggleLoadingHandler}
                saveBtnDisabled={saveBtnDisabled}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid title={_("Specifies objectclass that indicates a dynamic group (dynamicListObjectclass).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Objectclass")}
                        </GridItem>
                        <GridItem span={8}>
                            <FormSelect
                                value={settings.objectclass.toLowerCase()}
                                onChange={(event, selection) => {
                                    setSetting('objectclass', selection);
                                }}
                                aria-label="Dynamic List Objectclass"
                                ouiaId="DynamicListAttributeSelect"
                            >
                                {objectClasses.map((option, index) => (
                                    <FormSelectOption key={index} value={option.toLowerCase()} label={option} />
                                ))}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid title={_("Specifies attribute that contains the URL of the dynamic list (dynamicListUrlAttr).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("URL Attribute")}
                        </GridItem>
                        <GridItem span={8}>
                            <FormSelect
                                value={settings.URLAttr.toLowerCase()}
                                onChange={(event, selection) => {
                                    setSetting('URLAttr', selection);
                                }}
                                aria-label="Dynamic List URL Attribute"
                                ouiaId="DynamicListAttributeSelect"
                            >
                                {urlAttrs.map((option, index) => (
                                    <FormSelectOption key={index} value={option.name[0].toLowerCase()} label={option.name[0]} />
                                ))}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid title={_("Specifies attribute used to store the values of the dynamic list (dynamicListAttr).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("List Attribute")}
                        </GridItem>
                        <GridItem span={8}>
                            <FormSelect
                                value={settings.listAttr.toLowerCase()}
                                onChange={(event, selection) => {
                                    setSetting('listAttr', selection);
                                }}
                                aria-label="Dynamic List Attribute"
                                ouiaId="DynamicListAttributeSelect"
                            >
                                {dnAttrs.map((option, index) => (
                                    <FormSelectOption key={index} value={option.name[0].toLowerCase()} label={option.name[0]} />
                                ))}
                            </FormSelect>
                        </GridItem>
                    </Grid>

                    <Grid title={_("The value to set as shared config entry (nsslapd-pluginConfigArea)")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Shared Config Entry")}
                        </GridItem>
                        <GridItem className="ds-right-margin" span={9}>
                            {settings.sharedConfigEntry !== "" && (
                                <TextInput
                                    value={settings.sharedConfigEntry}
                                    type="text"
                                    id="memberOfConfigEntry"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="memberOfConfigEntry"
                                    readOnlyVariant={'plain'}
                                />
                            )}
                            {settings.sharedConfigEntry === "" && (
                                <Button
                                    variant="primary"
                                    onClick={handleOpenModal}
                                    size="sm"
                                >
                                    {_("Create Config")}
                                </Button>
                            )}
                        </GridItem>
                    </Grid>
                    {settings.sharedConfigEntry !== "" && (
                        <Grid>
                            <GridItem offset={3} span={3}>
                                <Button
                                    variant="primary"
                                    onClick={handleOpenModal}
                                    size="sm"
                                >
                                    {_("Manage Config")}
                                </Button>
                            </GridItem>
                        </Grid>
                    )}
                </Form>
                <Button
                    className="ds-margin-top-lg"
                    key="at"
                    isLoading={saving}
                    spinnerAriaValueText={saving ? _("Loading") : undefined}
                    variant="primary"
                    onClick={handleSaveConfig}
                    isDisabled={saveBtnDisabled || saving}
                >
                    {saving ? _("Saving ...") : _("Save")}
                </Button>
            </PluginBasicConfig>
            <DoubleConfirmModal
                showModal={showConfirmDelete}
                closeHandler={closeConfirmDelete}
                handleChange={onChange}
                actionHandler={deleteConfig}
                spinning={modalSpinning}
                item={settings.sharedConfigEntry}
                checked={modalChecked}
                mTitle={_("Delete Dynamic Lists Plugin Shared Config Entry")}
                mMsg={_("Are you sure you want to delete this config entry?")}
                mSpinningMsg={_("Deleting ...")}
                mBtnName={_("Delete")}
            />
        </div>
    );
}

export default DynamicLists;
