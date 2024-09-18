import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Checkbox,
    Form,
    FormHelperText,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Select,
    SelectVariant,
    SelectOption,
    TextInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd, valid_dn } from "../tools.jsx";
import {
    DoubleConfirmModal,
    WarningModal
} from "../notifications.jsx";

const _ = cockpit.gettext;

// Use default account policy name

class AccountPolicy extends React.Component {
    componentDidMount(prevProps) {
        this.updateFields();
    }

    componentDidUpdate(prevProps) {
        if (this.props.rows !== prevProps.rows) {
            this.updateFields();
        }
    }

    constructor(props) {
        super(props);

        this.updateFields = this.updateFields.bind(this);
        this.onChange = this.onChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.handleCheckboxChange = this.handleCheckboxChange.bind(this);
        this.handleOpenModal = this.handleOpenModal.bind(this);
        this.handleCloseModal = this.handleCloseModal.bind(this);
        this.handleAddConfig = this.handleAddConfig.bind(this);
        this.handleEditConfig = this.handleEditConfig.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.handleDeleteConfig = this.handleDeleteConfig.bind(this);
        this.closeWarningModal = this.closeWarningModal.bind(this);
        this.handleSaveConfig = this.handleSaveConfig.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.handleShowConfirmDelete = this.handleShowConfirmDelete.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
        this.sharedConfigExists = this.sharedConfigExists.bind(this);
        this.validateConfig = this.validateConfig.bind(this);

        this.state = {
            attributes: [],
            // Main config
            configArea: "",
            _configArea: "",
            error: [],
            saveBtnDisabled: true,
            saving: false,
            addingModal: false,
            savingModal: false,
            sharedConfigExists: false,
            // Modal Config
            configDN: "",
            altStateAttrName: "",
            alwaysRecordLogin: false,
            alwaysRecordLoginAttr: "",
            limitAttrName: "",
            specAttrName: "",
            stateAttrName: "",
            checkAllStateAttrs: false,
            _configDN: "",
            _altStateAttrName: [],
            _alwaysRecordLogin: false,
            _alwaysRecordLoginAttr: "",
            _limitAttrName: "",
            _specAttrName: "",
            _stateAttrName: "",
            _checkAllStateAttrs: false,
            errorModal: {},
            saveBtnDisabledModal: true,
            modalChecked: false,
            modalSpinning: false,
            configEntryModalShow: false,
            fixupModalShow: false,
            newEntry: false,
            isRecordLoginOpen: false,
            isSpecificAttrOpen: false,
            isStateAttrOpen: false,
            isAltStateAttrOpen: false,
            isLimitAttrOpen: false,
            showConfirmDelete: false,
            showWarningModal: false,
            warningMessage: "",
        };

        // Always Record Login Attribute
        this.handleRecordLoginSelect = (event, selection) => {
            if (selection === this.state.alwaysRecordLoginAttr) {
                this.handleRecordLoginClear();
            } else {
                this.setState({
                    alwaysRecordLoginAttr: selection,
                    isRecordLoginOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.handleRecordLoginToggle = isRecordLoginOpen => {
            this.setState({
                isRecordLoginOpen
            });
        };
        this.handleRecordLoginClear = () => {
            this.setState({
                alwaysRecordLoginAttr: "",
                isRecordLoginOpen: false
            });
        };

        // Specific Attribute
        this.handleSpecificAttrSelect = (event, selection) => {
            if (selection === this.state.specAttrName) {
                this.handleSpecificAttrClear();
            } else {
                this.setState({
                    specAttrName: selection,
                    isSpecificAttrOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.handleSpecificAttrToggle = isSpecificAttrOpen => {
            this.setState({
                isSpecificAttrOpen
            });
        };
        this.handleSpecificAttrClear = () => {
            this.setState({
                specAttrName: [],
                isSpecificAttrOpen: false
            });
        };

        // State Attribute
        this.handleStateAttrSelect = (event, selection) => {
            if (selection === this.state.stateAttrName) {
                this.handleStateAttrClear();
            } else {
                this.setState({
                    stateAttrName: selection,
                    isStateAttrOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.handleStateAttrToggle = isStateAttrOpen => {
            this.setState({
                isStateAttrOpen
            });
        };
        this.handleStateAttrClear = () => {
            this.setState({
                stateAttrName: [],
                isStateAttrOpen: false
            });
        };

        // Alternative State Attribute
        this.handleAlternativeStateSelect = (event, selection) => {
            if (selection === this.state.altStateAttrName) {
                this.handleAlternativeStateClear();
            } else {
                this.setState({
                    altStateAttrName: selection,
                    isAltStateAttrOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.handleAlternativeStateToggle = isAltStateAttrOpen => {
            this.setState({
                isAltStateAttrOpen
            });
        };
        this.handleAlternativeStateClear = () => {
            this.setState({
                altStateAttrName: [],
                isAltStateAttrOpen: false
            });
        };

        // Limit Attribute
        this.handleLimitAttrSelect = (event, selection) => {
            if (selection === this.state.limitAttrName) {
                this.handleLimitAttrClear();
            } else {
                this.setState({
                    limitAttrName: selection,
                    isLimitAttrOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.handleLimitAttrToggle = isLimitAttrOpen => {
            this.setState({
                isLimitAttrOpen
            });
        };
        this.handleLimitAttrClear = () => {
            this.setState({
                limitAttrName: [],
                isLimitAttrOpen: false
            });
        };
    }

    handleShowConfirmDelete() {
        this.setState({
            showConfirmDelete: true,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmDelete() {
        this.setState({
            showConfirmDelete: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    sharedConfigExists() {
        if (this.state.configArea) {
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "account-policy",
                "config-entry",
                "show",
                this.state.configArea
            ];
            log_cmd("sharedConfigExists", "Check if Account Policy config entry exists", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        this.setState({
                            sharedConfigExists: true
                        });
                    })
                    .fail(() => {
                        this.props.addNotification(
                            "warning",
                            cockpit.format(_("Warning! Account Policy config entry $0 doesn't exist!"), this.state.configArea)
                        );
                        this.setState({
                            sharedConfigExists: false
                        });
                    });
        }
    }

    handleOpenModal() {
        if (!this.state.configArea) {
            this.setState({
                configEntryModalShow: true,
                newEntry: true,
                configDN: "",
                altStateAttrName: "createTimestamp",
                alwaysRecordLogin: false,
                alwaysRecordLoginAttr: "lastLoginTime",
                limitAttrName: "accountInactivityLimit",
                specAttrName: "acctPolicySubentry",
                stateAttrName: "lastLoginTime",
                checkAllStateAttrs: false,
                _configDN: "",
                _altStateAttrName: "createTimestamp",
                _alwaysRecordLogin: false,
                _alwaysRecordLoginAttr: "lastLoginTime",
                _limitAttrName: "accountInactivityLimit",
                _specAttrName: "acctPolicySubentry",
                _stateAttrName: "lastLoginTime",
                _checkAllStateAttrs: false,
                savingModal: false,
                saveBtnDisabledModal: true,
            });
        } else {
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "account-policy",
                "config-entry",
                "show",
                this.state.configArea
            ];

            log_cmd("handleOpenModal", "Fetch the Account Policy Plugin config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const configEntry = JSON.parse(content).attrs;
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: false,
                            savingModal: false,
                            saveBtnDisabledModal: true,
                            configDN: this.state.configArea,
                            altStateAttrName:
                            configEntry.altstateattrname === undefined
                                ? "createTimestamp"
                                : configEntry.altstateattrname[0],
                            alwaysRecordLogin: !(
                                configEntry.alwaysrecordlogin === undefined ||
                            configEntry.alwaysrecordlogin[0] === "no"
                            ),
                            alwaysRecordLoginAttr:
                            configEntry.alwaysrecordloginattr === undefined
                                ? "lastLoginTime"
                                : configEntry.alwaysrecordloginattr[0],
                            limitAttrName:
                            configEntry.limitattrname === undefined
                                ? "accountInactivityLimit"
                                : configEntry.limitattrname[0],
                            specAttrName:
                            configEntry.specattrname === undefined
                                ? "acctPolicySubentry"
                                : configEntry.specattrname[0],
                            stateAttrName:
                            configEntry.stateattrname === undefined
                                ? "lastLoginTime"
                                : configEntry.stateattrname[0],
                            checkAllStateAttrs: !(
                                configEntry.checkallstateattrs === undefined ||
                            configEntry.checkallstateattrs[0] === "no"
                            ),
                            // original values
                            _altStateAttrName:
                            configEntry.altstateattrname === undefined
                                ? "createTimestamp"
                                : configEntry.altstateattrname[0],
                            _alwaysRecordLogin: !(
                                configEntry.alwaysrecordlogin === undefined ||
                            configEntry.alwaysrecordlogin[0] === "no"
                            ),
                            _alwaysRecordLoginAttr:
                            configEntry.alwaysrecordloginattr === undefined
                                ? "lastLoginTime"
                                : configEntry.alwaysrecordloginattr[0],
                            _limitAttrName:
                            configEntry.limitattrname === undefined
                                ? "accountInactivityLimit"
                                : configEntry.limitattrname[0],
                            _specAttrName:
                            configEntry.specattrname === undefined
                                ? "acctPolicySubentry"
                                : configEntry.specattrname[0],
                            _stateAttrName:
                            configEntry.stateattrname === undefined
                                ? "lastLoginTime"
                                : configEntry.stateattrname[0],
                            _checkAllStateAttrs: !(
                                configEntry.checkallstateattrs === undefined ||
                            configEntry.checkallstateattrs[0] === "no"
                            ),
                        });
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configDN: this.state.configArea,
                            _configDN: this.state.configArea,
                            altStateAttrName: "createTimestamp",
                            alwaysRecordLogin: false,
                            alwaysRecordLoginAttr: "lastLoginTime",
                            limitAttrName: "accountInactivityLimit",
                            specAttrName: "acctPolicySubentry",
                            stateAttrName: "lastLoginTime",
                            checkAllStateAttrs: false,
                            _altStateAttrName: "createTimestamp",
                            _alwaysRecordLogin: false,
                            _alwaysRecordLoginAttr: "lastLoginTime",
                            _limitAttrName: "accountInactivityLimit",
                            _specAttrName: "acctPolicySubentry",
                            _stateAttrName: "lastLoginTime",
                            _checkAllStateAttrs: false,
                            saveBtnDisabledModal: false, // We preset the form so it's ready to save
                        });
                    });
        }
    }

    handleCloseModal() {
        this.setState({ configEntryModalShow: false });
    }

    cmdOperation(action) {
        const {
            configDN,
            altStateAttrName,
            alwaysRecordLogin,
            alwaysRecordLoginAttr,
            limitAttrName,
            specAttrName,
            stateAttrName,
            checkAllStateAttrs
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "account-policy",
            "config-entry",
            action,
            configDN,
            "--always-record-login",
            alwaysRecordLogin ? "yes" : "no",
            "--check-all-state-attrs",
            checkAllStateAttrs ? "yes" : "no",
        ];

        cmd = [...cmd, "--alt-state-attr"];
        if (altStateAttrName !== "") {
            cmd = [...cmd, altStateAttrName];
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--always-record-login-attr"];
        if (alwaysRecordLoginAttr !== "") {
            cmd = [...cmd, alwaysRecordLoginAttr];
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--limit-attr"];
        if (limitAttrName !== "") {
            cmd = [...cmd, limitAttrName];
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--spec-attr"];
        if (specAttrName !== "") {
            cmd = [...cmd, specAttrName];
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--state-attr"];
        if (stateAttrName !== "") {
            cmd = [...cmd, stateAttrName];
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        if (action === "add") {
            this.setState({
                addingModal: true,
            });
        } else {
            this.setState({
                savingModal: true,
            });
        }
        log_cmd(
            "accountPolicyOperation",
            `Do the ${action} operation on the Account Policy Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("accountPolicyOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        cockpit.format(_("Config entry $0 was successfully $1"), configDN, action === "add" ? "added" : "set")
                    );
                    this.props.pluginListHandler();
                    this.handleCloseModal();
                    if (action === "add") {
                        this.setState({
                            addingModal: false,
                        });
                    } else {
                        this.setState({
                            savingModal: false,
                        });
                    }
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry $0 operation - $1"), action, errMsg.desc)
                    );
                    this.props.pluginListHandler();
                    this.handleCloseModal();
                    if (action === "add") {
                        this.setState({
                            addingModal: false,
                        });
                    } else {
                        this.setState({
                            savingModal: false,
                        });
                    }
                });
    }

    handleDeleteConfig() {
        const parentDN = "cn=Account Policy Plugin,cn=plugins,cn=config";
        if (this.state.configDN.toLowerCase().endsWith(parentDN.toLowerCase())) {
            this.setState({
                showWarningModal: true,
                warningMessage: _("Cannot delete this entry as it is a child of the Account Policy Plugin configuration."),
            });
        } else {
            this.handleShowConfirmDelete();
        }
    }

    closeWarningModal() {
        this.setState({ showWarningModal: false });
    }

    deleteConfig() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "account-policy",
            "config-entry",
            "delete",
            this.state.configDN
        ];

        this.setState({
            modalSpinning: true
        });
        log_cmd("deleteConfig", "Delete the Account Policy Plugin config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        cockpit.format(_("Config entry $0 was successfully deleted"), this.state.configDN)
                    );
                    this.props.pluginListHandler();
                    this.closeConfirmDelete();
                    this.handleCloseModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry removal operation - $0"), errMsg.desc)
                    );
                    this.props.pluginListHandler();
                    this.closeConfirmDelete();
                    this.handleCloseModal();
                });
    }

    handleAddConfig() {
        this.cmdOperation("add");
        if (!this.state.saveBtnDisabled && !this.state.saving) {
            this.handleSaveConfig();
        }
    }

    handleEditConfig() {
        this.cmdOperation("set");
        if (!this.state.saveBtnDisabled && !this.state.saving) {
            this.handleSaveConfig();
        }
    }

    handleCheckboxChange(checked, e) {
        this.setState({
            [e.target.id]: checked
        }, () => { this.validateConfig() });
    }

    validateConfig() {
        const errObj = {};
        let all_good = true;
        const dnAttrs = [
            'configDN'
        ];

        for (const attr of dnAttrs) {
            if (this.state[attr] !== "" && !valid_dn(this.state[attr])) {
                errObj[attr] = true;
                all_good = false;
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrs = [
                'configDN', 'altStateAttrName', 'alwaysRecordLogin',
                'alwaysRecordLoginAttr', 'limitAttrName', 'stateAttrName',
                'checkAllStateAttrs',
            ];
            for (const check_attr of attrs) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabledModal: !all_good,
            errorModal: errObj
        });
    }

    handleFieldChange(e) {
        const attr = e.target.id; // always configArea
        const value = e.target.value;
        let saveBtnDisabled = true;
        const errObj = {};

        errObj[attr] = false;
        if (value !== "") {
            if (!valid_dn(value)) {
                errObj[attr] = true;
            } else if (value !== this.state['_' + attr]) {
                // New valid value, enable save button
                saveBtnDisabled = false;
            }
        } else if (value !== this.state['_' + attr]) {
            // New valid value, enable save button
            saveBtnDisabled = false;
        }

        this.setState({
            [attr]: value,
            error: errObj,
            saveBtnDisabled
        });
    }

    handleModalChange(e) {
        this.setState({
            [e.target.id]: e.target.value,
        }, () => { this.validateConfig() });
    }

    onChange(e) {
        // Generic handler for things that don't need validating
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value,
        });
    }

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "Account Policy Plugin");
            this.setState({
                configDN:
                    pluginRow["nsslapd-pluginarg0"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginarg0"][0],
                configArea:
                    pluginRow["nsslapd-pluginarg0"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginarg0"][0],
                _configArea:
                    pluginRow["nsslapd-pluginarg0"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginarg0"][0],
            }, () => { this.sharedConfigExists() });
        }
    }

    handleSaveConfig() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "account-policy",
            "set",
            "--config-entry",
            this.state.configArea || "delete"
        ];

        this.setState({
            saving: true
        });

        log_cmd(
            "handleSaveConfig",
            `Save Account Policy Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        _("Successfully updated Account Policy Plugin")
                    );
                    this.setState({
                        saving: false,
                        saveBtnDisabled: true,
                        _configArea: this.state.configArea
                    });
                    this.props.pluginListHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if ('info' in errMsg) {
                        errMsg = errMsg.desc + " " + errMsg.info;
                    } else {
                        errMsg = errMsg.desc;
                    }
                    this.props.addNotification(
                        "error", cockpit.format(_("Error during update - $0"), errMsg)
                    );
                    this.setState({
                        saving: false
                    });
                    this.props.pluginListHandler();
                });
    }

    render() {
        const {
            configArea,
            configDN,
            altStateAttrName,
            alwaysRecordLogin,
            alwaysRecordLoginAttr,
            limitAttrName,
            specAttrName,
            stateAttrName,
            checkAllStateAttrs,
            newEntry,
            configEntryModalShow,
            error,
            errorModal,
            saveBtnDisabled,
            saveBtnDisabledModal,
            saving,
            savingModal,
            addingModal,
        } = this.state;

        const extraPrimaryProps = {};
        let saveBtnText = _("Save Config");
        if (saving) {
            // Main plugin config
            saveBtnText = _("Saving ...");
        }
        let modalButtons = [];
        if (!newEntry) {
            modalButtons = [
                <Button key="del" variant="primary" onClick={this.handleDeleteConfig}>
                    {_("Delete Config")}
                </Button>,
                <Button
                    key="save"
                    variant="primary"
                    onClick={this.handleEditConfig}
                    isDisabled={saveBtnDisabledModal || savingModal}
                    isLoading={savingModal}
                    spinnerAriaValueText={savingModal ? _("Saving") : undefined}
                    {...extraPrimaryProps}
                >
                    {savingModal ? _("Saving ...") : _("Save Config")}
                </Button>,
                <Button key="cancel" variant="link" onClick={this.handleCloseModal}>
                    {_("Cancel")}
                </Button>
            ];
        } else {
            modalButtons = [
                <Button
                    key="add"
                    variant="primary"
                    onClick={this.handleAddConfig}
                    isDisabled={saveBtnDisabledModal || addingModal}
                    isLoading={addingModal}
                    spinnerAriaValueText={addingModal ? _("Saving") : undefined}
                    {...extraPrimaryProps}
                >
                    {addingModal ? _("Adding ...") : _("Add Config")}
                </Button>,
                <Button key="cancel" variant="link" onClick={this.handleCloseModal}>
                    {_("Cancel")}
                </Button>
            ];
        }

        return (
            <div className={this.state.saving || this.state.savingModal || this.state.addingModal ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    title={_("Manage Account Policy Plugin Shared Config Entry")}
                    isOpen={configEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.handleCloseModal}
                    actions={modalButtons}
                >
                    <Form isHorizontal autoComplete="no">
                        <Grid title={_("DN of the config entry")}>
                            <GridItem span={4} className="ds-label">
                                {_("Config DN")}
                            </GridItem>
                            <GridItem span={8}>
                                <TextInput
                                    value={configDN}
                                    type="text"
                                    id="configDN"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="configDN"
                                    onChange={(str, e) => { this.handleModalChange(e) }}
                                    validated={errorModal.configDN ? ValidatedOptions.error : ValidatedOptions.default}
                                    isDisabled
                                />
                                <FormHelperText isError isHidden={!errorModal.configDN}>
                                    {_("Value must be a valid DN")}
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Specifies the attribute to store the time of the last successful login in this attribute in the users directory entry (alwaysRecordLoginAttr)")}>
                            <GridItem span={4} className="ds-label">
                                {_("Always Record Login Attribute")}
                            </GridItem>
                            <GridItem span={4}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel={_("Type an attribute name")}
                                    onToggle={this.handleRecordLoginToggle}
                                    onSelect={this.handleRecordLoginSelect}
                                    onClear={this.handleRecordLoginClear}
                                    selections={alwaysRecordLoginAttr}
                                    isOpen={this.state.isRecordLoginOpen}
                                    aria-labelledby="typeAhead-record-login"
                                    placeholderText={_("Type an attribute name ...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                >
                                    {this.props.attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                            <GridItem span={4}>
                                <Checkbox
                                    id="alwaysRecordLogin"
                                    className="ds-left-margin"
                                    isChecked={alwaysRecordLogin}
                                    title={_("Sets that every entry records its last login time (alwaysRecordLogin)")}
                                    onChange={this.handleCheckboxChange}
                                    label={_("Always Record Login")}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Specifies the attribute to identify which entries are account policy configuration entries (specAttrName)")}>
                            <GridItem span={4} className="ds-label">
                                {_("Specific Attribute")}
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel={_("Type an attribute name")}
                                    onToggle={this.handleSpecificAttrToggle}
                                    onSelect={this.handleSpecificAttrSelect}
                                    onClear={this.handleSpecificAttrClear}
                                    selections={specAttrName}
                                    isOpen={this.state.isSpecificAttrOpen}
                                    aria-labelledby="typeAhead-specific-attr"
                                    placeholderText={_("Type an attribute name ...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                >
                                    {this.props.attributes.map((attr) => (
                                        <SelectOption
                                            key={attr}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Specifies the attribute within the policy to use for the account inactivation limit (limitAttrName)")}>
                            <GridItem span={4} className="ds-label">
                                {_("Limit Attribute")}
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel={_("Type an attribute name")}
                                    onToggle={this.handleLimitAttrToggle}
                                    onSelect={this.handleLimitAttrSelect}
                                    onClear={this.handleLimitAttrClear}
                                    selections={limitAttrName}
                                    isOpen={this.state.isLimitAttrOpen}
                                    aria-labelledby="typeAhead-limit-attr"
                                    placeholderText={_("Type an attribute name ...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                >
                                    {this.props.attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title="Specifies the primary time attribute used to evaluate an account policy (stateAttrName)">
                            <GridItem span={4} className="ds-label">
                                {_("State Attribute")}
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel={_("Type an attribute name")}
                                    onToggle={this.handleStateAttrToggle}
                                    onSelect={this.handleStateAttrSelect}
                                    onClear={this.handleStateAttrClear}
                                    selections={stateAttrName}
                                    isOpen={this.state.isStateAttrOpen}
                                    aria-labelledby="typeAhead-state-attr"
                                    placeholderText={_("Type an attribute name ...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                >
                                    {this.props.attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Provides a backup attribute to evaluate the expiration time if the main state attribute is not present (altStateAttrName)")}>
                            <GridItem span={4} className="ds-label">
                                {_("Alternative State Attribute")}
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel={_("Type an attribute name")}
                                    onToggle={this.handleAlternativeStateToggle}
                                    onSelect={this.handleAlternativeStateSelect}
                                    onClear={this.handleAlternativeStateClear}
                                    selections={altStateAttrName}
                                    isOpen={this.state.isAltStateAttrOpen}
                                    aria-labelledby="typeAhead-alt-state-attr"
                                    placeholderText={_("Type an attribute name ...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                >
                                    {this.props.attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Check both the 'state attribute', and the 'alternate state attribute' regaredless if the main state attribute is present")}>
                            <GridItem span={4} className="ds-label">
                                {_("Check All State Attributes")}
                            </GridItem>
                            <GridItem span={8}>
                                <Checkbox
                                    id="checkAllStateAttrs"
                                    className="ds-left-margin"
                                    isChecked={checkAllStateAttrs}
                                    onChange={this.handleCheckboxChange}
                                    label={_("Check Both - State and Alternative State Attributes")}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>

                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Account Policy Plugin"
                    pluginName="Account Policy"
                    cmdName="account-policy"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title={_("DN of the shared config entry (nsslapd-pluginarg0)")}>
                            <GridItem span={3} className="ds-label">
                                {_("Shared Config Entry")}
                            </GridItem>
                            <GridItem span={7}>
                                <TextInput
                                    value={configArea}
                                    type="text"
                                    id="configArea"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="configArea"
                                    onChange={(str, e) => { this.handleFieldChange(e) }}
                                    validated={error.configArea ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                                <FormHelperText isError isHidden={!error.configArea}>
                                    {_("Value must be a valid DN")}
                                </FormHelperText>
                            </GridItem>
                            <GridItem span={2}>
                                <Button
                                    className="ds-left-margin"
                                    key="manage"
                                    variant="primary"
                                    onClick={this.handleOpenModal}
                                    isDisabled={error.configArea || !configArea}
                                >
                                    {this.state.sharedConfigExists ? _("Manage Config") : _("Create Config")}
                                </Button>
                            </GridItem>
                        </Grid>
                    </Form>
                    <Button
                        className="ds-margin-top-lg"
                        key="at"
                        isLoading={saving}
                        spinnerAriaValueText={saving ? _("Loading") : undefined}
                        variant="primary"
                        onClick={this.handleSaveConfig}
                        {...extraPrimaryProps}
                        isDisabled={saveBtnDisabled || saving}
                    >
                        {saveBtnText}
                    </Button>
                </PluginBasicConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.onChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.configDN}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete Account Policy Config Entry")}
                    mMsg={_("Are you sure you want to delete this config entry?")}
                    mSpinningMsg={_("Deleting ...")}
                    mBtnName={_("Delete")}
                />
                <WarningModal
                    showModal={this.state.showWarningModal}
                    closeHandler={this.closeWarningModal}
                    mTitle={_("Cannot Delete Entry")}
                    mMsg={this.state.warningMessage}
                />
            </div>
        );
    }
}

AccountPolicy.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

AccountPolicy.defaultProps = {
    rows: [],
    serverId: "",
};

export default AccountPolicy;
