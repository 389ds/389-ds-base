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
import { DoubleConfirmModal } from "../notifications.jsx";

// Use default account policy name

class AccountPolicy extends React.Component {
    componentDidMount() {
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
        this.handleChange = this.handleChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.handleCheckboxChange = this.handleCheckboxChange.bind(this);
        this.openModal = this.openModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
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
        };

        // Always Record Login Attribute
        this.onRecordLoginSelect = (event, selection) => {
            if (selection == this.state.alwaysRecordLoginAttr) {
                this.onRecordLoginClear();
            } else {
                this.setState({
                    alwaysRecordLoginAttr: selection,
                    isRecordLoginOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.onRecordLoginToggle = isRecordLoginOpen => {
            this.setState({
                isRecordLoginOpen
            });
        };
        this.onRecordLoginClear = () => {
            this.setState({
                alwaysRecordLoginAttr: "",
                isRecordLoginOpen: false
            });
        };

        // Specific Attribute
        this.onSpecificAttrSelect = (event, selection) => {
            if (selection == this.state.specAttrName) {
                this.onSpecificAttrClear();
            } else {
                this.setState({
                    specAttrName: selection,
                    isSpecificAttrOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.onSpecificAttrToggle = isSpecificAttrOpen => {
            this.setState({
                isSpecificAttrOpen
            });
        };
        this.onSpecificAttrClear = () => {
            this.setState({
                specAttrName: [],
                isSpecificAttrOpen: false
            });
        };

        // State Attribute
        this.onStateAttrSelect = (event, selection) => {
            if (selection == this.state.stateAttrName) {
                this.onStateAttrClear();
            } else {
                this.setState({
                    stateAttrName: selection,
                    isStateAttrOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.onStateAttrToggle = isStateAttrOpen => {
            this.setState({
                isStateAttrOpen
            });
        };
        this.onStateAttrClear = () => {
            this.setState({
                stateAttrName: [],
                isStateAttrOpen: false
            });
        };

        // Alternative State Attribute
        this.onAlternativeStateSelect = (event, selection) => {
            if (selection == this.state.altStateAttrName) {
                this.onAlternativeStateClear();
            } else {
                this.setState({
                    altStateAttrName: selection,
                    isAltStateAttrOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.onAlternativeStateToggle = isAltStateAttrOpen => {
            this.setState({
                isAltStateAttrOpen
            });
        };
        this.onAlternativeStateClear = () => {
            this.setState({
                altStateAttrName: [],
                isAltStateAttrOpen: false
            });
        };

        // Limit Attribute
        this.onLimitAttrSelect = (event, selection) => {
            if (selection == this.state.limitAttrName) {
                this.onLimitAttrClear();
            } else {
                this.setState({
                    limitAttrName: selection,
                    isLimitAttrOpen: false
                }, () => { this.validateConfig() });
            }
        };
        this.onLimitAttrToggle = isLimitAttrOpen => {
            this.setState({
                isLimitAttrOpen
            });
        };
        this.onLimitAttrClear = () => {
            this.setState({
                limitAttrName: [],
                isLimitAttrOpen: false
            });
        };
    }

    showConfirmDelete() {
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
                    .fail(_ => {
                        this.props.addNotification(
                            "warning",
                            `Warning! Account Policy config entry "${this.state.configArea}" doesn't exist!`
                        );
                        this.setState({
                            sharedConfigExists: false
                        });
                    });
        }
    }

    openModal() {
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
                checkAllStateAttrs: "checkAllStateAttrs",
                _configDN: "",
                _altStateAttrName: "createTimestamp",
                _alwaysRecordLogin: false,
                _alwaysRecordLoginAttr: "lastLoginTime",
                _limitAttrName: "accountInactivityLimit",
                _specAttrName: "acctPolicySubentry",
                _stateAttrName: "lastLoginTime",
                _checkAllStateAttrs: "checkAllStateAttrs",
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

            log_cmd("openModal", "Fetch the Account Policy Plugin config entry", cmd);
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
                            _configDN: this.state.configArea,
                            altStateAttrName:
                            configEntry.altstateattrname === undefined
                                ? "createTimestamp"
                                : configEntry.altstateattrname[0],
                            alwaysRecordLogin: !(
                                configEntry.alwaysrecordlogin === undefined ||
                            configEntry.alwaysrecordlogin[0] == "no"
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
                            configEntry.checkallstateattrs[0] == "no"
                            ),
                            // original values
                            _altStateAttrName:
                            configEntry.altstateattrname === undefined
                                ? "createTimestamp"
                                : configEntry.altstateattrname[0],
                            _alwaysRecordLogin: !(
                                configEntry.alwaysrecordlogin === undefined ||
                            configEntry.alwaysrecordlogin[0] == "no"
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
                            configEntry.checkallstateattrs[0] == "no"
                            ),
                        });
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configDN: this.state.configArea,
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

    closeModal() {
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
            checkAllStateAttrs,
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
        if (altStateAttrName != "") {
            cmd = [...cmd, altStateAttrName];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--always-record-login-attr"];
        if (alwaysRecordLoginAttr != "") {
            cmd = [...cmd, alwaysRecordLoginAttr];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--limit-attr"];
        if (limitAttrName != "") {
            cmd = [...cmd, limitAttrName];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--spec-attr"];
        if (specAttrName != "") {
            cmd = [...cmd, specAttrName];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--state-attr"];
        if (stateAttrName != "") {
            cmd = [...cmd, stateAttrName];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        if (action == "add") {
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
                        `Config entry ${configDN} was successfully ${action}ed`
                    );
                    this.props.pluginListHandler();
                    this.closeModal();
                    if (action == "add") {
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
                        `Error during the config entry ${action} operation - ${errMsg.desc}`
                    );
                    this.props.pluginListHandler();
                    this.closeModal();
                    if (action == "add") {
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
                        `Config entry ${this.state.configDN} was successfully deleted`
                    );
                    this.props.pluginListHandler();
                    this.closeConfirmDelete();
                    this.closeModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry removal operation - ${errMsg.desc}`
                    );
                    this.props.pluginListHandler();
                    this.closeConfirmDelete();
                    this.closeModal();
                });
    }

    addConfig() {
        this.cmdOperation("add");
    }

    editConfig() {
        this.cmdOperation("set");
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
            if (this.state[attr] != "" && !valid_dn(this.state[attr])) {
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
                if (this.state[check_attr] != this.state['_' + check_attr]) {
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
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let saveBtnDisabled = true;
        const errObj = {};

        errObj[attr] = false;
        if (value != "") {
            if (!valid_dn(value)) {
                errObj[attr] = true;
            } else if (value != this.state['_' + attr]) {
                // New valid value, enable save button
                saveBtnDisabled = false;
            }
        } else if (value != this.state['_' + attr]) {
            // New valid value, enable save button
            saveBtnDisabled = false;
        }

        this.setState({
            [attr]: value,
            error: errObj,
            saveBtnDisabled: saveBtnDisabled
        });
    }

    handleModalChange(e) {
        this.setState({
            [e.target.id]: e.target.value,
        }, () => { this.validateConfig() });
    }

    handleChange(e) {
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
                configArea:
                    pluginRow.nsslapd_pluginconfigarea === undefined
                        ? ""
                        : pluginRow.nsslapd_pluginconfigarea[0]
            }, () => { this.sharedConfigExists() });
        }
    }

    saveConfig() {
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
            "saveConfig",
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
                        `Successfully updated Account Policy Plugin`
                    );
                    this.setState({
                        saving: false
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
                        "error", `Error during update - ${errMsg}`
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
        let saveBtnText = "Save Config";
        if (saving) {
            // Main plugin config
            saveBtnText = "Saving ...";
        }
        let modalButtons = [];
        if (!newEntry) {
            modalButtons = [
                <Button key="del" variant="primary" onClick={this.showConfirmDelete}>
                    Delete Config
                </Button>,
                <Button
                    key="save"
                    variant="primary"
                    onClick={this.editConfig}
                    isDisabled={saveBtnDisabledModal || savingModal}
                    isLoading={savingModal}
                    spinnerAriaValueText={savingModal ? "Saving" : undefined}
                    {...extraPrimaryProps}
                >
                    {savingModal ? "Saving ..." : "Save Config"}
                </Button>,
                <Button key="cancel" variant="link" onClick={this.closeModal}>
                    Cancel
                </Button>
            ];
        } else {
            modalButtons = [
                <Button
                    key="add"
                    variant="primary"
                    onClick={this.addConfig}
                    isDisabled={saveBtnDisabledModal || addingModal}
                    isLoading={addingModal}
                    spinnerAriaValueText={addingModal ? "Saving" : undefined}
                    {...extraPrimaryProps}
                >
                    {addingModal ? "Adding ..." : "Add Config"}
                </Button>,
                <Button key="cancel" variant="link" onClick={this.closeModal}>
                    Cancel
                </Button>
            ];
        }

        return (
            <div className={this.state.saving || this.state.savingModal || this.state.addingModal ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    title="Manage Account Policy Plugin Shared Config Entry"
                    isOpen={configEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeModal}
                    actions={modalButtons}
                >
                    <Form isHorizontal autoComplete="no">
                        <Grid title="DN of the config entry">
                            <GridItem span={4} className="ds-label">
                                Config DN
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
                                    isDisabled={newEntry}
                                />
                                <FormHelperText isError isHidden={!errorModal.configDN}>
                                    Value must be a valid DN
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid title="Specifies the attribute to store the time of the last successful login in this attribute in the users directory entry (alwaysRecordLoginAttr)">
                            <GridItem span={4} className="ds-label">
                                Always Record Login Attribute
                            </GridItem>
                            <GridItem span={4}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={this.onRecordLoginToggle}
                                    onSelect={this.onRecordLoginSelect}
                                    onClear={this.onRecordLoginClear}
                                    selections={alwaysRecordLoginAttr}
                                    isOpen={this.state.isRecordLoginOpen}
                                    aria-labelledby="typeAhead-record-login"
                                    placeholderText="Type an attribute name ..."
                                    noResultsFoundText="There are no matching entries"
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
                                    className="ds-left-margin ds-lower-field"
                                    isChecked={alwaysRecordLogin}
                                    title="Sets that every entry records its last login time (alwaysRecordLogin)"
                                    onChange={this.handleCheckboxChange}
                                    label="Always Record Login"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Specifies the attribute to identify which entries are account policy configuration entries (specAttrName)">
                            <GridItem span={4} className="ds-label">
                                Specific Attribute
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={this.onSpecificAttrToggle}
                                    onSelect={this.onSpecificAttrSelect}
                                    onClear={this.onSpecificAttrClear}
                                    selections={specAttrName}
                                    isOpen={this.state.isSpecificAttrOpen}
                                    aria-labelledby="typeAhead-specific-attr"
                                    placeholderText="Type an attribute name ..."
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid title="Specifies the attribute within the policy to use for the account inactivation limit (limitAttrName)">
                            <GridItem span={4} className="ds-label">
                                Limit Attribute
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={this.onLimitAttrToggle}
                                    onSelect={this.onLimitAttrSelect}
                                    onClear={this.onLimitAttrClear}
                                    selections={limitAttrName}
                                    isOpen={this.state.isLimitAttrOpen}
                                    aria-labelledby="typeAhead-limit-attr"
                                    placeholderText="Type an attribute name ..."
                                    noResultsFoundText="There are no matching entries"
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
                                State Attribute
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={this.onStateAttrToggle}
                                    onSelect={this.onStateAttrSelect}
                                    onClear={this.onStateAttrClear}
                                    selections={stateAttrName}
                                    isOpen={this.state.isStateAttrOpen}
                                    aria-labelledby="typeAhead-state-attr"
                                    placeholderText="Type an attribute name ..."
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid title="Provides a backup attribute to evaluate the expiration time if the main state attribute is not present (altStateAttrName)">
                            <GridItem span={4} className="ds-label">
                                Alternative State Attribute
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={this.onAlternativeStateToggle}
                                    onSelect={this.onAlternativeStateSelect}
                                    onClear={this.onAlternativeStateClear}
                                    selections={altStateAttrName}
                                    isOpen={this.state.isAltStateAttrOpen}
                                    aria-labelledby="typeAhead-alt-state-attr"
                                    placeholderText="Type an attribute name ..."
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid title="Check both the 'state attribute', and the 'alternate state attribute' regaredless if the main state attribute is present">
                            <GridItem span={4}>
                                <Checkbox
                                    id="checkAllStateAttrs"
                                    className="ds-left-margin"
                                    isChecked={checkAllStateAttrs}
                                    onChange={this.handleCheckboxChange}
                                    label="Check All State Attributes"
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
                        <Grid title="DN of the shared config entry (nsslapd-pluginConfigArea)">
                            <GridItem span={3} className="ds-label">
                                Shared Config Entry
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
                                    Value must be a valid DN
                                </FormHelperText>
                            </GridItem>
                            <GridItem span={2}>
                                <Button
                                    className="ds-left-margin"
                                    key="manage"
                                    variant="primary"
                                    onClick={this.openModal}
                                    isDisabled={!this.state.sharedConfigExists && saveBtnDisabled}
                                >
                                    {this.state.sharedConfigExists ? "Manage Config" : "Create Config"}
                                </Button>
                            </GridItem>
                        </Grid>
                    </Form>
                    <Button
                        className="ds-margin-top-lg"
                        key="at"
                        isLoading={saving}
                        spinnerAriaValueText={saving ? "Loading" : undefined}
                        variant="primary"
                        onClick={this.saveConfig}
                        {...extraPrimaryProps}
                        isDisabled={saveBtnDisabled || saving}
                    >
                        {saveBtnText}
                    </Button>
                </PluginBasicConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.handleChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.configDN}
                    checked={this.state.modalChecked}
                    mTitle="Delete Account Policy Config Entry"
                    mMsg="Are you sure you want to delete this config entry?"
                    mSpinningMsg="Deleting ..."
                    mBtnName="Delete"
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
