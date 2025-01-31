import cockpit from "cockpit";
import React from "react";
import {
	Button,
	Checkbox,
	Form,
	FormSelect,
	FormSelectOption,
	Grid,
	GridItem,
	Modal,
	ModalVariant,
	TextInput,
	ValidatedOptions
} from '@patternfly/react-core';
import {
	Select,
	SelectOption,
	SelectVariant
} from '@patternfly/react-core/deprecated';
import { PassthroughAuthConfigsTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

const _ = cockpit.gettext;

class PAMPassthroughAuthentication extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            pamConfigRows: [],
            tableKey: 1,
            error: {},
            modalSpinning: false,
            modalChecked: false,

            pamConfigName: "",
            pamExcludeSuffix: [],
            pamIncludeSuffix: [],
            pamMissingSuffix: "",
            pamFilter: "",
            pamIDAttr: "",
            pamIDMapMethod: "RDN",
            pamFallback: false,
            pamSecure: false,
            pamService: "ldapserver",
            _pamConfigName: "",
            _pamExcludeSuffix: [],
            _pamIncludeSuffix: [],
            _pamMissingSuffix: "",
            _pamFilter: "",
            _pamIDAttr: "",
            _pamIDMapMethod: "RDN",
            _pamFallback: false,
            _pamSecure: false,
            _pamService: "ldapserver",
            isExcludeOpen: false,
            excludeOptions: [],
            isIncludeOpen: false,
            includeOptions: [],
            showConfirmDeleteConfig: false,
            saveBtnDisabledPAM: true,
            savingPAM: false,
            newPAMConfigEntry: false,
            pamConfigEntryModalShow: false,
        };

        this.handleExcludeToggle = (_event, isExcludeOpen) => {
            this.setState({
                isExcludeOpen
            });
        };
        this.handleClearExcludeSelection = () => {
            this.setState({
                pamExcludeSuffix: [],
                isExcludeOpen: false
            }, () => { this.validatePAM() });
        };
        this.handleExcludeSelect = (event, selection) => {
            if (this.state.pamExcludeSuffix.includes(selection)) {
                this.setState(
                    prevState => ({
                        pamExcludeSuffix: prevState.pamExcludeSuffix.filter(item => item !== selection),
                        isExcludeOpen: false
                    }), () => { this.validatePAM() }
                );
            } else {
                this.setState(
                    prevState => ({
                        pamExcludeSuffix: [...prevState.pamExcludeSuffix, selection],
                        isExcludeOpen: false
                    }), () => { this.validatePAM() }
                );
            }
        };
        this.handleCreateExcludeOption = newValue => {
            if (!this.state.excludeOptions.includes(newValue)) {
                this.setState({
                    excludeOptions: [...this.state.excludeOptions, newValue],
                    isExcludeOpen: false
                }, () => { this.validatePAM() });
            }
        };

        this.handleIncludeToggle = (_event, isIncludeOpen) => {
            this.setState({
                isIncludeOpen
            });
        };
        this.handleClearIncludeSelection = () => {
            this.setState({
                pamIncludeSuffix: [],
                isIncludeOpen: false
            }, () => { this.validatePAM() });
        };
        this.handleIncludeSelect = (event, selection) => {
            if (this.state.pamIncludeSuffix.includes(selection)) {
                this.setState(
                    prevState => ({
                        pamIncludeSuffix: prevState.pamIncludeSuffix.filter(item => item !== selection),
                        isIncludeOpen: false
                    })
                );
            } else {
                this.setState(
                    prevState => ({
                        pamIncludeSuffix: [...prevState.pamIncludeSuffix, selection],
                        isIncludeOpen: false
                    })
                );
            }
        };
        this.handleCreateIncludeOption = newValue => {
            if (!this.state.includeOptions.includes(newValue)) {
                this.setState({
                    includeOptions: [...this.state.includeOptions, newValue],
                    isIncludeOpen: false
                });
            }
        };

        this.maxValue = 20000000;
        this.onMinusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validatePassthru() });
        };
        this.onConfigChange = (event, id, min) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > this.maxValue ? this.maxValue : newValue < min ? min : newValue
            }, () => { this.validatePassthru() });
        };
        this.onPlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validatePassthru() });
        };

        // This vastly improves rendering performance during handleChange()
        this.attrRows = this.props.attributes.map((attr) => (
            <FormSelectOption key={attr} value={attr} label={attr} />
        ));

        this.handlePAMChange = this.handlePAMChange.bind(this);
        this.onModalChange = this.onModalChange.bind(this);
        this.validatePAM = this.validatePAM.bind(this);
        this.loadPAMConfigs = this.loadPAMConfigs.bind(this);
        this.openPAMModal = this.openPAMModal.bind(this);
        this.handleClosePAMModal = this.handleClosePAMModal.bind(this);
        this.showEditPAMConfigModal = this.showEditPAMConfigModal.bind(this);
        this.handleShowAddPAMConfigModal = this.handleShowAddPAMConfigModal.bind(this);
        this.cmdPAMOperation = this.cmdPAMOperation.bind(this);
        this.deletePAMConfig = this.deletePAMConfig.bind(this);
        this.handleAddPAMConfig = this.handleAddPAMConfig.bind(this);
        this.handleEditPAMConfig = this.handleEditPAMConfig.bind(this);
        this.showConfirmDeleteConfig = this.showConfirmDeleteConfig.bind(this);
        this.closeConfirmDeleteConfig = this.closeConfirmDeleteConfig.bind(this);
    }

    componentDidMount() {
        this.loadPAMConfigs();
    }

    validatePAM() {
        const errObj = {};
        let all_good = true;

        const reqAttrs = ['pamConfigName', 'pamIDAttr'];
        const dnAttrLists = ['pamExcludeSuffix', 'pamExcludeSuffix'];

        // Check we have our required attributes set
        for (const attr of reqAttrs) {
            if (this.state[attr] === "") {
                errObj[attr] = true;
                all_good = false;
                break;
            }
        }

        // Check the DN's of our lists
        for (const attrList of dnAttrLists) {
            for (const dn of this.state[attrList]) {
                if (!valid_dn(dn)) {
                    errObj[attrList] = true;
                    all_good = false;
                    break;
                }
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrLists = ['pamExcludeSuffix', 'pamIncludeSuffix'];
            for (const check_attr of attrLists) {
                if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                    all_good = true;
                    break;
                }
            }

            const configAttrs = [
                'pamConfigName', 'pamFilter', 'pamMissingSuffix', 'pamIDMapMethod',
                'pamIDAttr', 'pamFallback', 'pamSecure', 'pamService'
            ];
            for (const check_attr of configAttrs) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabledPAM: !all_good,
            error: errObj
        });
    }

    handlePAMChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validatePAM() });
    }

    onModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    showConfirmDeleteConfig (name) {
        this.setState({
            showConfirmDeleteConfig: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: name
        });
    }

    closeConfirmDeleteConfig () {
        this.setState({
            showConfirmDeleteConfig: false,
            modalChecked: false,
            modalSpinning: false,
            deleteName: ""
        });
    }

    loadPAMConfigs() {
        this.setState({
            firstLoad: false
        });
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pam-pass-through-auth",
            "list",
        ];
        this.props.toggleLoadingHandler();
        log_cmd("loadPAMConfigs", "Get PAM Passthough Authentication Plugin Configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    const tableKey = this.state.tableKey + 1;
                    this.setState({
                        pamConfigRows: myObject.items.map(item => item.attrs),
                        tableKey,
                    });
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err !== 0) {
                        console.log("loadPAMConfigs failed", errMsg.desc);
                    }
                    this.props.toggleLoadingHandler();
                });
    }

    showEditPAMConfigModal(rowData) {
        this.openPAMModal(rowData);
    }

    handleShowAddPAMConfigModal() {
        this.openPAMModal();
    }

    openPAMModal(name) {
        if (!name) {
            this.setState({
                pamConfigEntryModalShow: true,
                newPAMConfigEntry: true,
                pamConfigName: "",
                pamExcludeSuffix: [],
                pamIncludeSuffix: [],
                pamMissingSuffix: "",
                pamFilter: "",
                pamIDAttr: "",
                pamIDMapMethod: "RDN",
                pamFallback: false,
                pamSecure: false,
                pamService: "ldapserver",
                saveBtnDisabledPAM: true,
            });
        } else {
            let pamExcludeSuffixList = [];
            let pamIncludeSuffixList = [];
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "pam-pass-through-auth",
                "config",
                name,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd(
                "openModal",
                "Fetch the PAM Passthough Authentication Plugin pamConfig entry",
                cmd
            );
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const pamConfigEntry = JSON.parse(content).attrs;
                        const tableKey = this.state.tableKey + 1;

                        if (pamConfigEntry.pamexcludesuffix !== undefined) {
                            for (const value of pamConfigEntry.pamexcludesuffix) {
                                pamExcludeSuffixList = [...pamExcludeSuffixList, value];
                            }
                        }
                        if (pamConfigEntry.pamincludesuffix !== undefined) {
                            for (const value of pamConfigEntry.pamincludesuffix) {
                                pamIncludeSuffixList = [...pamIncludeSuffixList, value];
                            }
                        }

                        this.setState({
                            tableKey,
                            saveBtnDisabledPAM: true,
                            pamConfigEntryModalShow: true,
                            newPAMConfigEntry: false,
                            pamExcludeSuffix: pamExcludeSuffixList,
                            pamIncludeSuffix: pamIncludeSuffixList,
                            pamIDAttr: pamConfigEntry.pamidattr[0],
                            pamConfigName:
                            pamConfigEntry.cn === undefined ? "" : pamConfigEntry.cn[0],
                            pamMissingSuffix:
                            pamConfigEntry.pammissingsuffix === undefined
                                ? ""
                                : pamConfigEntry.pammissingsuffix[0],
                            pamFilter:
                            pamConfigEntry.pamfilter === undefined
                                ? ""
                                : pamConfigEntry.pamfilter[0],
                            pamIDMapMethod:
                            pamConfigEntry.pamidmapmethod === undefined
                                ? "RDN"
                                : pamConfigEntry.pamidmapmethod[0],
                            pamFallback: !(
                                pamConfigEntry.pamfallback === undefined ||
                            pamConfigEntry.pamfallback[0] === "FALSE"
                            ),
                            pamSecure: !(
                                pamConfigEntry.pamsecure === undefined ||
                            pamConfigEntry.pamsecure[0] === "FALSE"
                            ),
                            pamService:
                            pamConfigEntry.pamservice === undefined
                                ? "ldapserver"
                                : pamConfigEntry.pamservice[0],
                            // Backup values
                            _pamExcludeSuffix: [...pamExcludeSuffixList],
                            _pamIncludeSuffix: [...pamIncludeSuffixList],
                            _pamIDAttr: pamConfigEntry.pamidattr[0],
                            _pamConfigName:
                            pamConfigEntry.cn === undefined ? "" : pamConfigEntry.cn[0],
                            _pamMissingSuffix:
                            pamConfigEntry.pammissingsuffix === undefined
                                ? ""
                                : pamConfigEntry.pammissingsuffix[0],
                            _pamFilter:
                            pamConfigEntry.pamfilter === undefined
                                ? ""
                                : pamConfigEntry.pamfilter[0],
                            _pamIDMapMethod:
                            pamConfigEntry.pamidmapmethod === undefined
                                ? "ldapserver"
                                : pamConfigEntry.pamidmapmethod[0],
                            _pamFallback: !(
                                pamConfigEntry.pamfallback === undefined ||
                            pamConfigEntry.pamfallback[0] === "FALSE"
                            ),
                            _pamSecure: !(
                                pamConfigEntry.pamsecure === undefined ||
                            pamConfigEntry.pamsecure[0] === "FALSE"
                            ),
                            _pamService:
                            pamConfigEntry.pamservice === undefined
                                ? "ldapserver"
                                : pamConfigEntry.pamservice[0],
                        });
                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            pamConfigEntryModalShow: true,
                            newPAMConfigEntry: true,
                            pamConfigName: "",
                            pamExcludeSuffix: [],
                            pamIncludeSuffix: [],
                            pamMissingSuffix: "",
                            pamFilter: "",
                            pamIDAttr: "",
                            pamIDMapMethod: "",
                            pamFallback: false,
                            pamSecure: false,
                            pamService: "",
                            saveBtnDisabledPAM: true,
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    handleClosePAMModal() {
        this.setState({
            pamConfigEntryModalShow: false,
            savingPAM: false,
        });
    }

    deletePAMConfig() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pam-pass-through-auth",
            "config",
            this.state.deleteName,
            "delete"
        ];

        this.setState({
            modalSpinning: true
        });

        this.props.toggleLoadingHandler();
        log_cmd(
            "deletePAMConfig",
            "Delete the PAM Passthough Authentication Plugin pamConfig entry",
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deletePAMConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        cockpit.format(_("PAMConfig entry $0 was successfully deleted"), this.state.deleteName)
                    );
                    this.loadPAMConfigs();
                    this.closeConfirmDeleteConfig();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the pamConfig entry removal operation - $0"), errMsg.desc)
                    );
                    this.loadPAMConfigs();
                    this.closeConfirmDeleteConfig();
                    this.props.toggleLoadingHandler();
                });
    }

    handleAddPAMConfig() {
        this.cmdPAMOperation("add");
    }

    handleEditPAMConfig() {
        this.cmdPAMOperation("set");
    }

    cmdPAMOperation(action) {
        // Save table here too
        const {
            pamConfigName,
            pamExcludeSuffix,
            pamIncludeSuffix,
            pamMissingSuffix,
            pamFilter,
            pamIDAttr,
            pamIDMapMethod,
            pamFallback,
            pamSecure,
            pamService
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pam-pass-through-auth",
            "config",
            pamConfigName,
            action,
            "--missing-suffix",
            pamMissingSuffix || action === "add" ? pamMissingSuffix : "delete",
            "--filter",
            pamFilter || action === "add" ? pamFilter : "delete",
            "--id_map_method",
            pamIDMapMethod || action === "add" ? pamIDMapMethod : "delete",
            "--fallback",
            pamFallback ? "TRUE" : "FALSE",
            "--secure",
            pamSecure ? "TRUE" : "FALSE",
            "--service",
            pamService || action === "add" ? pamService : "delete"
        ];

        cmd = [...cmd, "--exclude-suffix"];
        if (pamExcludeSuffix.length !== 0) {
            for (const value of pamExcludeSuffix) {
                cmd = [...cmd, value];
            }
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }
        cmd = [...cmd, "--include-suffix"];
        if (pamIncludeSuffix.length !== 0) {
            for (const value of pamIncludeSuffix) {
                cmd = [...cmd, value];
            }
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }
        cmd = [...cmd, "--id-attr"];
        if (pamIDAttr !== "") {
            cmd = [...cmd, pamIDAttr];
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        this.setState({
            savingPAM: true
        });
        log_cmd(
            "pamPassthroughAuthOperation",
            `Do the ${action} operation on the PAM Passthough Authentication Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("pamPassthroughAuthOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        cockpit.format(_("The $0 operation was successfully done on \"$1\" entry"), action, pamConfigName)
                    );
                    this.loadPAMConfigs();
                    this.handleClosePAMModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the pamConfig entry $0 operation - $1"), action, errMsg.desc)
                    );
                    this.loadPAMConfigs();
                    this.handleClosePAMModal();
                });
    }

    render() {
        const {
            pamConfigRows,
            pamConfigName,
            pamMissingSuffix,
            pamExcludeSuffix,
            pamIncludeSuffix,
            pamFilter,
            pamIDAttr,
            pamIDMapMethod,
            pamFallback,
            pamSecure,
            pamService,
            newPAMConfigEntry,
            pamConfigEntryModalShow,
            error,
            savingPAM,
        } = this.state;

        let saveBtnName = _("Save Config");
        const extraPrimaryProps = {};
        if (this.state.savingPAM) {
            saveBtnName = _("Saving Config ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        const title = cockpit.format(_("$0 PAM Passthough Auth Config Entry"), (newPAMConfigEntry ? _("Add") : _("Edit")));

        return (
            <div className={savingPAM ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    aria-labelledby="ds-modal"
                    title={title}
                    isOpen={pamConfigEntryModalShow}
                    onClose={this.handleClosePAMModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            onClick={newPAMConfigEntry ? this.handleAddPAMConfig : this.handleEditPAMConfig}
                            isDisabled={this.state.saveBtnDisabledPAM || this.state.savingPAM}
                            isLoading={this.state.savingPAM}
                            spinnerAriaValueText={this.state.savingPAM ? _("Saving") : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleClosePAMModal}>
                            {_("Cancel")}
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid className="ds-margin-top">
                            <GridItem className="ds-label" span={3}>
                                {_("Config Name")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={pamConfigName}
                                    type="text"
                                    id="pamConfigName"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="pamConfigName"
                                    onChange={(e, str) => {
                                        this.handlePAMChange(e);
                                    }}
                                    isDisabled={!newPAMConfigEntry}
                                    validated={error.pamConfigName ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem
                                className="ds-label"
                                span={3}
                                title={_("Specifies a suffix to exclude from PAM authentication (pamExcludeSuffix)")}
                            >
                                {_("Exclude Suffix")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    isCreatable
                                    onCreateOption={this.handleCreateExcludeOption}
                                    typeAheadAriaLabel="Add a suffix"
                                    onToggle={(event, isOpen) => this.handleExcludeToggle(event, isOpen)}
                                    onSelect={this.handleExcludeSelect}
                                    onClear={this.handleClearExcludeSelection}
                                    selections={pamExcludeSuffix}
                                    isOpen={this.state.isExcludeOpen}
                                    aria-labelledby="Add a suffix"
                                    placeholderText={_("Type a suffix DN ...")}
                                >
                                    {this.state.excludeOptions.map((suffix, index) => (
                                        <SelectOption
                                            key={index}
                                            value={suffix}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem
                                className="ds-label"
                                span={3}
                                title={_("Sets a suffix to include for PAM authentication (pamIncludeSuffix)")}
                            >
                                {_("Include Suffix")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    isCreatable
                                    onCreateOption={this.handleCreateIncludeOption}
                                    typeAheadAriaLabel="Add an include suffix"
                                    onToggle={(event, isOpen) => this.handleIncludeToggle(event, isOpen)}
                                    onSelect={this.handleIncludeSelect}
                                    onClear={this.handleClearIncludeSelection}
                                    selections={pamIncludeSuffix}
                                    isOpen={this.state.isIncludeOpen}
                                    aria-labelledby="Add an include suffix"
                                    placeholderText={_("Type a suffix DN ...")}
                                >
                                    {this.state.includeOptions.map((suffix, index) => (
                                        <SelectOption
                                            key={index}
                                            value={suffix}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={3} title={_("Contains the attribute name which is used to hold the PAM user ID (pamIDAttr)")}>
                                {_("ID Attribute")}
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="pamIDAttr"
                                    value={pamIDAttr}
                                    onChange={(event, value) => {
                                        this.handlePAMChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="no_setting2" value="" label={_("Choose an attribute...")} />
                                    {this.attrRows}
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Identifies how to handle missing include or exclude suffixes (pamMissingSuffix)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Missing Suffix")}
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="pamMissingSuffix"
                                    value={pamMissingSuffix}
                                    onChange={(event, value) => {
                                        this.handlePAMChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="ERROR" value="ERROR" label="ERROR" />
                                    <FormSelectOption key="ALLOW" value="ALLOW" label="ALLOW" />
                                    <FormSelectOption key="IGNORE" value="IGNORE" label="IGNORE" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Sets an LDAP filter to use to identify specific entries within the included suffixes for which to use PAM pass-through authentication (pamFilter)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Filter")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={pamFilter}
                                    type="text"
                                    id="pamFilter"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="pamFilter"
                                    onChange={(e, str) => {
                                        this.handlePAMChange(e);
                                    }}
                                    validated={error.pamFilter ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Gives the method to use to map the LDAP bind DN to a PAM identity (pamIDMapMethod)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Map Method")}
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="pamIDMapMethod"
                                    value={pamIDMapMethod}
                                    onChange={(event, value) => {
                                        this.handlePAMChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="RDN" value="RDN" label="RDN" />
                                    <FormSelectOption key="DN" value="DN" label="DN" />
                                    <FormSelectOption key="ENTRY" value="ENTRY" label="ENTRY" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Contains the service name to pass to PAM (pamService)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Service")}
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="pamService"
                                    value={pamService}
                                    onChange={(event, value) => {
                                        this.handlePAMChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="ldapserver" value="ldapserver" label="ldapserver" />
                                    <FormSelectOption key="system-auth" value="system-auth" label="system-auth (For AD)" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem span={3} className="ds-label">
                                {_("Fallback Auth Enabled")}
                            </GridItem>
                            <GridItem span={9}>
                                <Checkbox
                                    id="pamFallback"
                                    isChecked={pamFallback}
                                    onChange={(e, checked) => { this.handlePAMChange(e) }}
                                    title={_("Sets whether to fallback to regular LDAP authentication if PAM authentication fails (pamFallback)")}
                                />
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top">
                            <GridItem span={3} className="ds-label">
                                {_("Require Secure Connection")}
                            </GridItem>
                            <GridItem span={9}>
                                <Checkbox
                                    id="pamSecure"
                                    isChecked={pamSecure}
                                    onChange={(e, checked) => { this.handlePAMChange(e) }}
                                    title={_("Requires secure TLS connection for PAM authentication (pamSecure)")}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>

                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="PAM Pass Through Auth"
                    pluginName="PAM Pass Through Auth"
                    cmdName="pam-pass-through-auth"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <PassthroughAuthConfigsTable
                        rows={pamConfigRows}
                        key={this.state.tableKey}
                        editConfig={this.showEditPAMConfigModal}
                        deleteConfig={this.showConfirmDeleteConfig}
                    />
                    <Button
                        variant="primary"
                        onClick={this.handleShowAddPAMConfigModal}
                    >
                        {_("Add Config")}
                    </Button>
                </PluginBasicConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteConfig}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.onModalChange}
                    actionHandler={this.deletePAMConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete PAM Passthrough Configuration")}
                    mMsg={_("Are you sure you want to delete this configuration?")}
                    mSpinningMsg={_("Deleting Configuration...")}
                    mBtnName={_("Delete Configuration")}
                />
            </div>
        );
    }
}

PAMPassthroughAuthentication.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

PAMPassthroughAuthentication.defaultProps = {
    rows: [],
    serverId: "",
};

export default PAMPassthroughAuthentication;
