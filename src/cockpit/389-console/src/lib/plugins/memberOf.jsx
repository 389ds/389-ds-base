import cockpit from "cockpit";
import React from "react";
import {
	Button,
	Checkbox,
	Form,
	FormHelperText,
	FormSelect,
	FormSelectOption,
	Grid,
	GridItem,
	Modal,
	ModalVariant,
	TextInput,
	Text,
	TextContent,
	TextVariants,
	ValidatedOptions
} from '@patternfly/react-core';
import {
	Select,
	SelectVariant,
	SelectOption
} from '@patternfly/react-core/deprecated';
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";
import {
    WrenchIcon,
} from '@patternfly/react-icons';

const _ = cockpit.gettext;

class MemberOf extends React.Component {
    componentDidMount(prevProps) {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.updateFields();
            }
        }
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
        this.handleOpenModal = this.handleOpenModal.bind(this);
        this.handleCloseModal = this.handleCloseModal.bind(this);
        this.handleAddConfig = this.handleAddConfig.bind(this);
        this.handleEditConfig = this.handleEditConfig.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.handleRunFixup = this.handleRunFixup.bind(this);
        this.handleToggleFixupModal = this.handleToggleFixupModal.bind(this);
        this.handleSaveConfig = this.handleSaveConfig.bind(this);
        this.handleShowConfirmDelete = this.handleShowConfirmDelete.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
        this.validateConfig = this.validateConfig.bind(this);
        this.validateModal = this.validateModal.bind(this);

        this.state = {
            firstLoad: true,
            error: {},
            errorModal: {},
            saveBtnDisabled: true,
            saveBtnDisabledModal: true,
            showConfirmDelete: false,
            modalCheck: false,
            modalSpinning: false,
            memberOfAttr: "",
            memberOfGroupAttr: [],
            memberOfEntryScope: [],
            memberOfEntryScopeOptions: [],
            memberOfEntryScopeExcludeSubtree: [],
            memberOfEntryScopeExcludeOptions: [],
            memberOfAutoAddOC: "",
            memberOfAllBackends: false,
            memberOfSkipNested: false,
            memberOfConfigEntry: "",
            configEntryModalShow: false,
            fixupModalShow: false,
            isSubtreeScopeOpen: false,
            isExcludeScopeOpen: false,

            configDN: "",
            configAttr: "",
            configGroupAttr: [],
            configEntryScope: [],
            configEntryScopeOptions: [],
            configEntryScopeExcludeSubtree: [],
            configEntryScopeExcludeOptions: [],
            configAutoAddOC: "",
            configAllBackends: false,
            configSkipNested: false,
            isConfigSubtreeScopeOpen: false,
            isConfigExcludeScopeOpen: false,
            newEntry: true,

            fixupDN: "",
            fixupFilter: "",

            isConfigAttrOpen: false,
            isConfigGroupAttrOpen: false,
            isConfigAutoAddOCOpen: false,
            isMemberOfAttrOpen: false,
            isMemberOfGroupAttrOpen: false,
            isMemberOfAutoAddOCOpen: false,
        };

        // Config Attribute
        this.handleConfigAttrSelect = (event, selection) => {
            if (selection === this.state.configAttr) {
                this.handleConfigAttrClear();
            } else {
                this.setState({
                    configAttr: selection,
                    isConfigAttrOpen: false
                }, () => { this.validateModal() });
            }
        };
        this.handleConfigAttrToggle = (_event, isConfigAttrOpen) => {
            this.setState({
                isConfigAttrOpen
            });
        };
        this.handleConfigAttrClear = () => {
            this.setState({
                configAttr: "",
                isConfigAttrOpen: false
            }, () => { this.validateModal() });
        };

        // Config Group Attribute
        this.handleConfigGroupAttrSelect = (event, selection) => {
            if (this.state.configGroupAttr.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        configGroupAttr: prevState.configGroupAttr.filter((item) => item !== selection),
                        isConfigGroupAttrOpen: false
                    }), () => { this.validateModal() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        configGroupAttr: [...prevState.configGroupAttr, selection],
                        isConfigGroupAttrOpen: false
                    }), () => { this.validateModal() }
                );
            }
        };
        this.handleConfigGroupAttrToggle = (_event, isConfigGroupAttrOpen) => {
            this.setState({
                isConfigGroupAttrOpen
            });
        };
        this.handleConfigGroupAttrClear = () => {
            this.setState({
                configGroupAttr: [],
                isConfigGroupAttrOpen: false
            }, () => { this.validateModal() });
        };

        // MemberOf Attribute
        this.handleMemberOfAttrSelect = (event, selection) => {
            if (selection === this.state.configAttr) {
                this.handleMemberOfAttrClear();
            } else {
                this.setState({
                    memberOfAttr: selection,
                    isMemberOfAttrOpen: false
                }, () => { this.validateModal() });
            }
        };
        this.handleMemberOfAttrToggle = (_event, isMemberOfAttrOpen) => {
            this.setState({
                isMemberOfAttrOpen
            });
        };
        this.handleMemberOfAttrClear = () => {
            this.setState({
                memberOfAttr: [],
                isMemberOfAttrOpen: false
            }, () => { this.validateConfig() });
        };

        // MemberOf Group Attribute
        this.handleMemberOfGroupAttrSelect = (event, selection) => {
            if (this.state.memberOfGroupAttr.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        memberOfGroupAttr: prevState.memberOfGroupAttr.filter((item) => item !== selection),
                        isMemberOfGroupAttrOpen: false
                    }), () => { this.validateConfig() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        memberOfGroupAttr: [...prevState.memberOfGroupAttr, selection],
                        isMemberOfGroupAttrOpen: false
                    }), () => { this.validateConfig() }
                );
            }
        };
        this.handleMemberOfGroupAttrToggle = (_event, isMemberOfGroupAttrOpen) => {
            this.setState({
                isMemberOfGroupAttrOpen
            });
        };
        this.handleMemberOfGroupAttrClear = () => {
            this.setState({
                memberOfGroupAttr: [],
                isMemberOfGroupAttrOpen: false
            }, () => { this.validateConfig() });
        };

        // Handle scope subtree
        this.handleSubtreeScopeSelect = (event, selection) => {
            if (this.state.memberOfEntryScope.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        memberOfEntryScope: prevState.memberOfEntryScope.filter((item) => item !== selection),
                        isSubtreeScopeOpen: false
                    }), () => { this.validateConfig() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        memberOfEntryScope: [...prevState.memberOfEntryScope, selection],
                        isSubtreeScopeOpen: false
                    }), () => { this.validateConfig() }
                );
            }
        };
        this.handleSubtreeScopeToggle = (_event, isSubtreeScopeOpen) => {
            this.setState({
                isSubtreeScopeOpen
            }, () => { this.validateConfig() });
        };
        this.handleSubtreeScopeClear = () => {
            this.setState({
                memberOfEntryScope: [],
                isSubtreeScopeOpen: false
            }, () => { this.validateConfig() });
        };
        this.handleSubtreeScopeCreateOption = newValue => {
            if (!this.state.memberOfEntryScopeOptions.includes(newValue)) {
                this.setState({
                    memberOfEntryScopeOptions: [...this.state.memberOfEntryScopeOptions, newValue],
                    isSubtreeScopeOpen: false
                }, () => { this.validateConfig() });
            }
        };

        // Handle Exclude Scope subtree
        this.handleExcludeScopeSelect = (event, selection) => {
            if (this.state.memberOfEntryScopeExcludeSubtree.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        memberOfEntryScopeExcludeSubtree: prevState.memberOfEntryScopeExcludeSubtree.filter((item) => item !== selection),
                        isExcludeScopeOpen: false
                    }), () => { this.validateConfig() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        memberOfEntryScopeExcludeSubtree: [...prevState.memberOfEntryScopeExcludeSubtree, selection],
                        isExcludeScopeOpen: false
                    }), () => { this.validateConfig() }
                );
            }
        };
        this.handleExcludeScopeToggle = (_event, isExcludeScopeOpen) => {
            this.setState({
                isExcludeScopeOpen
            }, () => { this.validateConfig() });
        };
        this.handleExcludeScopeClear = () => {
            this.setState({
                memberOfEntryScopeExcludeSubtree: [],
                isExcludeScopeOpen: false
            }, () => { this.validateConfig() });
        };
        this.handleExcludeCreateOption = newValue => {
            if (!this.state.memberOfEntryScopeOptions.includes(newValue)) {
                this.setState({
                    memberOfEntryScopeExcludeOptions: [...this.state.memberOfEntryScopeExcludeOptions, newValue],
                    isExcludeScopeOpen: false
                }, () => { this.validateConfig() });
            }
        };

        // Modal scope and exclude Scope
        // Handle scope subtree
        this.handleConfigScopeSelect = (event, selection) => {
            if (this.state.configEntryScope.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        configEntryScope: prevState.configEntryScope.filter((item) => item !== selection),
                        isConfigSubtreeScopeOpen: false
                    }), () => { this.validateModal() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        configEntryScope: [...prevState.configEntryScope, selection],
                        isConfigSubtreeScopeOpen: false
                    }), () => { this.validateModal() }
                );
            }
        };
        this.handleConfigScopeToggle = (_event, isConfigSubtreeScopeOpen) => {
            this.setState({
                isConfigSubtreeScopeOpen
            }, () => { this.validateModal() });
        };
        this.handleConfigScopeClear = () => {
            this.setState({
                configEntryScope: [],
                isConfigSubtreeScopeOpen: false
            }, () => { this.validateModal() });
        };
        this.handleConfigCreateOption = newValue => {
            if (!this.state.configEntryScopeOptions.includes(newValue)) {
                this.setState({
                    configEntryScopeOptions: [...this.state.configEntryScopeOptions, newValue],
                    isConfigSubtreeScopeOpen: false
                }, () => { this.validateModal() });
            }
        };

        // Handle Exclude Scope subtree
        this.handleConfigExcludeScopeSelect = (event, selection) => {
            if (this.state.configEntryScopeExcludeSubtree.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        configEntryScopeExcludeSubtree: prevState.configEntryScopeExcludeSubtree.filter((item) => item !== selection),
                        isConfigExcludeScopeOpen: false
                    }), () => { this.validateModal() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        configEntryScopeExcludeSubtree: [...prevState.configEntryScopeExcludeSubtree, selection],
                        isConfigExcludeScopeOpen: false
                    }), () => { this.validateModal() }
                );
            }
        };
        this.handleConfigExcludeScopeToggle = (_event, isConfigExcludeScopeOpen) => {
            this.setState({
                isConfigExcludeScopeOpen
            }, () => { this.validateModal() });
        };
        this.handleConfigExcludeScopeClear = () => {
            this.setState({
                configEntryScopeExcludeSubtree: [],
                isConfigExcludeScopeOpen: false
            }, () => { this.validateModal() });
        };
        this.handleConfigExcludeCreateOption = newValue => {
            if (!this.state.configEntryScopeExcludeOptions.includes(newValue)) {
                this.setState({
                    configEntryScopeExcludeOptions: [...this.state.configEntryScopeExcludeOptions, newValue],
                    isConfigExcludeScopeOpen: false
                }, () => { this.validateModal() });
            }
        };
    }

    handleToggleFixupModal() {
        this.setState(prevState => ({
            fixupModalShow: !prevState.fixupModalShow,
            fixupDN: "",
            fixupFilter: "",
            savingModal: false,
        }));
    }

    validateConfig() {
        const errObj = {};
        let all_good = true;

        const reqAttrs = [
            'memberOfAttr'
        ];

        const reqLists = [
            'memberOfGroupAttr', 'memberOfEntryScope',
        ];

        const dnAttrs = [
            'memberOfConfigEntry'
        ];

        const dnLists = [
            'memberOfEntryScopeExcludeSubtree', 'memberOfEntryScope'
        ];

        // Check required attributes
        for (const attr of reqAttrs) {
            if (this.state[attr] === "") {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check required Lists are not empty
        for (const attr of reqLists) {
            if (this.state[attr].length === 0) {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check DN attrs
        for (const attr of dnAttrs) {
            if (this.state[attr] !== "" && !valid_dn(this.state[attr])) {
                errObj[attr] = true;
                all_good = false;
            }
        }

        // Validate the subtree lists
        for (const dn_list of dnLists) {
            for (const dn of this.state[dn_list]) {
                if (!valid_dn(dn)) {
                    errObj[dn_list] = true;
                    all_good = false;
                }
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;

            const attrLists = [
                'memberOfEntryScope',
                'memberOfEntryScopeExcludeSubtree', 'memberOfGroupAttr',
            ];
            for (const check_attr of attrLists) {
                if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                    all_good = true;
                    break;
                }
            }

            const configAttrs = [
                'memberOfAllBackends', 'memberOfSkipNested',
                'memberOfConfigEntry', 'memberOfAttr', 'memberOfAutoAddOC'
            ];
            for (const check_attr of configAttrs) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabled: !all_good,
            error: errObj
        });
    }

    validateModal() {
        const errObj = {};
        let all_good = true;

        const reqAttrs = [
            'configDN', 'configAttr'
        ];

        const reqLists = [
            'configEntryScope', 'configGroupAttr'
        ];

        const dnAttrs = [
            'configDN'
        ];

        const dnLists = [
            'configEntryScope', 'configEntryScopeExcludeSubtree'
        ];

        // Check required attributes
        for (const attr of reqAttrs) {
            if (this.state[attr] === "") {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check required Lists are not empty
        for (const attr of reqLists) {
            if (this.state[attr].length === 0) {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check DN attrs
        for (const attr of dnAttrs) {
            if (this.state[attr] !== "" && !valid_dn(this.state[attr])) {
                errObj[attr] = true;
                all_good = false;
                break;
            }
        }

        // Validate the subtree lists
        for (const dn_list of dnLists) {
            for (const dn of this.state[dn_list]) {
                if (!valid_dn(dn)) {
                    errObj[dn_list] = true;
                    all_good = false;
                }
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrLists = [
                'configEntryScope', 'configEntryScopeExcludeSubtree',
                'configGroupAttr'
            ];
            for (const check_attr of attrLists) {
                if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                    all_good = true;
                    break;
                }
            }

            const configAttrs = [
                'configDN', 'configAttr', 'configAutoAddOC',
                'configAllBackends', 'configSkipNested'
            ];
            for (const check_attr of configAttrs) {
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
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validateConfig() });
    }

    handleModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validateModal() });
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

    onChange(e) {
        // Generic handler for things that don't need validating
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value,
        });
    }

    toggleFixupModal() {
        this.setState(prevState => ({
            fixupModalShow: !prevState.fixupModalShow,
            fixupDN: "",
            fixupFilter: ""
        }));
    }

    handleRunFixup() {
        if (!this.state.fixupDN) {
            this.props.addNotification("warning", _("Fixup DN is required."));
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "memberof",
                "fixup",
                this.state.fixupDN
            ];

            if (this.state.fixupFilter) {
                cmd = [...cmd, "--filter", this.state.fixupFilter];
            }

            this.props.toggleLoadingHandler();
            log_cmd("handleRunFixup", "Run fixup MemberOf Plugin ", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        this.props.addNotification(
                            "success",
                            cockpit.format(_("Fixup task for $0 was successfull"), this.state.fixupDN)
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            fixupModalShow: false
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Fixup task for $0 has failed $1"), this.state.fixupDN, errMsg.desc)
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            fixupModalShow: false
                        });
                    });
        }
    }

    handleOpenModal() {
        if (!this.state.memberOfConfigEntry) {
            this.setState({
                configEntryModalShow: true,
                newEntry: true,
                configDN: "",
                configAttr: "",
                configGroupAttr: [],
                configEntryScope: [],
                configEntryScopeExcludeSubtree: [],
                configAutoAddOC: "",
                configAllBackends: false,
                configSkipNested: false,
                saveBtnDisabledModal: true,
            });
        } else {
            let configScopeList = [];
            let configExcludeScopeList = [];
            let configGroupAttrObjectList = [];
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "memberof",
                "config-entry",
                "show",
                this.state.memberOfConfigEntry
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openMemberOfModal", "Fetch the MemberOf Plugin config entry", cmd);
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
                            saveBtnDisabledModal: true,
                            configDN: this.state.memberOfConfigEntry,
                            configAttr:
                            configEntry.memberofattr === undefined
                                ? ""
                                : configEntry.memberofattr[0],
                            configAutoAddOC:
                            configEntry.memberofautoaddoc === undefined
                                ? ""
                                : configEntry.memberofautoaddoc[0],
                            configAllBackends: !(
                                configEntry.memberofallbackends === undefined ||
                            configEntry.memberofallbackends[0] === "off"
                            ),
                            configSkipNested: !(
                                configEntry.memberofskipnested === undefined ||
                            configEntry.memberofskipnested[0] === "off"
                            ),
                            configConfigEntry:
                            configEntry["nsslapd-pluginConfigArea"] === undefined
                                ? ""
                                : configEntry["nsslapd-pluginConfigArea"][0],
                        });

                        if (configEntry.memberofgroupattr === undefined) {
                            this.setState({ configGroupAttr: [] });
                        } else {
                            for (const value of configEntry.memberofgroupattr) {
                                configGroupAttrObjectList = [...configGroupAttrObjectList, value];
                            }
                            this.setState({
                                configGroupAttr: configGroupAttrObjectList
                            });
                        }
                        if (configEntry.memberofentryscope === undefined) {
                            this.setState({ configEntryScope: [] });
                        } else {
                            for (const value of configEntry.memberofentryscope) {
                                configScopeList = [...configScopeList, value];
                            }
                            this.setState({
                                configEntryScope: configScopeList
                            });
                        }
                        if (configEntry.memberofentryscopeexcludesubtree === undefined) {
                            this.setState({ configEntryScopeExcludeSubtreeScope: [] });
                        } else {
                            for (const value of configEntry.memberofentryscopeexcludesubtree) {
                                configExcludeScopeList = [...configExcludeScopeList, value];
                            }
                            this.setState({
                                configEntryScopeExcludeSubtreeScope: configExcludeScopeList
                            });
                        }
                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configDN: this.state.memberOfConfigEntry,
                            configAttr: [],
                            configGroupAttr: [],
                            configEntryScope: [],
                            configEntryScopeExcludeSubtree: [],
                            configAutoAddOC: "",
                            configAllBackends: false,
                            configSkipNested: false
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    handleCloseModal() {
        this.setState({ configEntryModalShow: false });
    }

    cmdOperation(action) {
        const {
            configDN,
            configAttr,
            configGroupAttr,
            configEntryScope,
            configEntryScopeExcludeSubtree,
            configAutoAddOC,
            configAllBackends,
            configSkipNested
        } = this.state;

        if (configAttr.length === 0 || configGroupAttr.length === 0) {
            this.props.addNotification(
                "warning",
                _("Config Attribute and Group Attribute are required.")
            );
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "memberof",
                "config-entry",
                action,
                configDN,
                "--attr",
                configAttr || action === "add" ? configAttr : "delete",
                "--allbackends",
                configAllBackends ? "on" : "off",
                "--skipnested",
                configSkipNested ? "on" : "off",
                "--autoaddoc",
                configAutoAddOC || action === "add" ? configAutoAddOC : "delete",
            ];

            // Delete attributes if the user set an empty value to the field
            cmd = [...cmd, "--scope"];
            if (configEntryScope.length !== 0) {
                for (const value of configEntryScope) {
                    cmd = [...cmd, value];
                }
            } else {
                cmd = [...cmd, "delete"];
            }
            cmd = [...cmd, "--exclude"];
            if (configEntryScopeExcludeSubtree.length !== 0) {
                for (const value of configEntryScopeExcludeSubtree) {
                    cmd = [...cmd, value];
                }
            } else {
                cmd = [...cmd, "delete"];
            }
            cmd = [...cmd, "--groupattr"];
            if (configGroupAttr.length !== 0) {
                for (const value of configGroupAttr) {
                    cmd = [...cmd, value];
                }
            } else {
                cmd = [...cmd, "delete"];
            }

            this.setState({
                savingModal: true,
            });

            log_cmd("memberOfOperation", `Do the ${action} operation on the MemberOf Plugin`, cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        console.info("memberOfOperation", "Result", content);
                        const value = action === "set" ? "edit" : "add";
                        this.props.addNotification(
                            "success",
                            cockpit.format(_("Config entry $0 was successfully $1ed"), configDN, value)
                        );
                        this.props.pluginListHandler();
                        this.handleCloseModal();
                        this.setState({
                            savingModal: false,
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error during the config entry $0 operation - $1"), action, errMsg.desc)
                        );
                        this.props.pluginListHandler();
                        this.handleCloseModal();
                        this.setState({
                            savingModal: true,
                        });
                    });
        }
    }

    deleteConfig() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "memberof",
            "config-entry",
            "delete",
            this.state.configDN
        ];

        this.setState({
            modalSpinning: true,
        });
        log_cmd("deleteConfig", "Delete the MemberOf Plugin config entry", cmd);
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
                    this.handleCloseModal();
                    this.setState({
                        modalSpinning: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry removal operation - $0"), errMsg.desc)
                    );
                    this.props.pluginListHandler();
                    this.handleCloseModal();
                    this.setState({
                        modalSpinning: false,
                    });
                });
    }

    handleAddConfig() {
        this.cmdOperation("add");
    }

    handleEditConfig() {
        this.cmdOperation("set");
    }

    updateFields() {
        let memberOfGroupAttrObjectList = [];
        let memberOfEntryScopeList = [];
        let getSchemamemberOfExcludeScopeList = [];

        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "MemberOf Plugin");

            this.setState({
                memberOfAttr:
                    pluginRow.memberofattr === undefined
                        ? ""
                        : pluginRow.memberofattr[0],
                memberOfAutoAddOC:
                    pluginRow.memberofautoaddoc === undefined
                        ? ""
                        : pluginRow.memberofautoaddoc[0],
                memberOfAllBackends: !(
                    pluginRow.memberofallbackends === undefined ||
                    pluginRow.memberofallbackends[0] === "off"
                ),
                memberOfSkipNested: !(
                    pluginRow.memberofskipnested === undefined ||
                    pluginRow.memberofskipnested[0] === "off"
                ),
                memberOfConfigEntry:
                    pluginRow["nsslapd-pluginConfigArea"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginConfigArea"][0],
                _memberOfAttr:
                    pluginRow.memberofattr === undefined
                        ? ""
                        : pluginRow.memberofattr[0],
                _memberOfAutoAddOC:
                    pluginRow.memberofautoaddoc === undefined
                        ? ""
                        : pluginRow.memberofautoaddoc[0],
                _memberOfAllBackends: !(
                    pluginRow.memberofallbackends === undefined ||
                    pluginRow.memberofallbackends[0] === "off"
                ),
                _memberOfSkipNested: !(
                    pluginRow.memberofskipnested === undefined ||
                    pluginRow.memberofskipnested[0] === "off"
                ),
                _memberOfConfigEntry:
                    pluginRow["nsslapd-pluginConfigArea"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginConfigArea"][0],
            });
            if (pluginRow.memberofgroupattr === undefined) {
                this.setState({ memberOfGroupAttr: [], _memberOfGroupAttr: [] });
            } else {
                for (const value of pluginRow.memberofgroupattr) {
                    memberOfGroupAttrObjectList = [...memberOfGroupAttrObjectList, value];
                }
                this.setState({
                    memberOfGroupAttr: memberOfGroupAttrObjectList,
                    _memberOfGroupAttr: [...memberOfGroupAttrObjectList],
                });
            }
            if (pluginRow.memberofentryscope === undefined) {
                this.setState({ memberOfEntryScope: [], _memberOfEntryScope: [] });
            } else {
                for (const value of pluginRow.memberofentryscope) {
                    memberOfEntryScopeList = [...memberOfEntryScopeList, value];
                }
                this.setState({
                    memberOfEntryScope: memberOfEntryScopeList,
                    _memberOfEntryScope: [...memberOfEntryScopeList],
                });
            }
            if (pluginRow.memberofentryscopeexcludesubtree === undefined) {
                this.setState({ memberOfEntryScopeExcludeSubtree: [], _memberOfEntryScopeExcludeSubtree: [] });
            } else {
                for (const value of pluginRow.memberofentryscopeexcludesubtree) {
                    getSchemamemberOfExcludeScopeList = [...getSchemamemberOfExcludeScopeList, value];
                }
                this.setState({
                    memberOfEntryScopeExcludeSubtree: getSchemamemberOfExcludeScopeList,
                    _memberOfEntryScopeExcludeSubtree: [...getSchemamemberOfExcludeScopeList]
                });
            }
        }
        this.setState({
            firstLoad: false
        });
    }

    handleSaveConfig() {
        const {
            memberOfAttr,
            memberOfGroupAttr,
            memberOfEntryScope,
            memberOfEntryScopeExcludeSubtree,
            memberOfAutoAddOC,
            memberOfAllBackends,
            memberOfSkipNested,
            memberOfConfigEntry,
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "memberof",
            "set",
            "--attr",
            memberOfAttr || "delete",
            "--config-entry",
            memberOfConfigEntry || "delete",
            "--allbackends",
            memberOfAllBackends ? "on" : "off",
            "--skipnested",
            memberOfSkipNested ? "on" : "off",
            "--autoaddoc",
            memberOfAutoAddOC || "delete",
        ];

        // Delete attributes if the user set an empty value to the field
        cmd = [...cmd, "--scope"];
        if (memberOfEntryScope.length !== 0) {
            for (const value of memberOfEntryScope) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--exclude"];
        if (memberOfEntryScopeExcludeSubtree.length !== 0) {
            for (const value of memberOfEntryScopeExcludeSubtree) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--groupattr"];
        if (memberOfGroupAttr.length !== 0) {
            for (const value of memberOfGroupAttr) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        this.setState({
            saving: true
        });
        log_cmd("handleSaveConfig", `Save MemberOf Plugin`, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        _("Successfully updated MemberOf Plugin")
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
            memberOfAttr,
            memberOfGroupAttr,
            memberOfEntryScope,
            memberOfEntryScopeExcludeSubtree,
            memberOfAutoAddOC,
            memberOfAllBackends,
            memberOfSkipNested,
            memberOfConfigEntry,
            configDN,
            configEntryModalShow,
            configAttr,
            configGroupAttr,
            configEntryScope,
            configEntryScopeExcludeSubtree,
            configAutoAddOC,
            configAllBackends,
            configSkipNested,
            newEntry,
            fixupModalShow,
            fixupDN,
            fixupFilter,
            error,
            errorModal,
            saving,
            savingModal,
            saveBtnDisabled,
            saveBtnDisabledModal,
            isSubtreeScopeOpen,
            isExcludeScopeOpen,
            isConfigExcludeScopeOpen,
            isConfigSubtreeScopeOpen,

        } = this.state;

        let saveBtnName = _("Save Config");
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = _("Saving Config ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        let modalButtons = [];
        if (!newEntry) {
            modalButtons = [
                <Button key="del" variant="primary" onClick={this.handleShowConfirmDelete}>
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
                    isDisabled={saveBtnDisabledModal || savingModal}
                    isLoading={savingModal}
                    spinnerAriaValueText={savingModal ? _("Saving") : undefined}
                    {...extraPrimaryProps}
                >
                    {savingModal ? _("Adding ...") : _("Add Config")}
                </Button>,
                <Button key="cancel" variant="link" onClick={this.handleCloseModal}>
                    {_("Cancel")}
                </Button>
            ];
        }

        return (
            <div>
                <Modal
                    variant={ModalVariant.small}
                    aria-labelledby="ds-modal"
                    title={_("MemberOf Plugin FixupTask")}
                    isOpen={fixupModalShow}
                    onClose={this.handleToggleFixupModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            onClick={this.handleRunFixup}
                            isDisabled={!valid_dn(fixupDN)}
                        >
                            {_("Run")}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleToggleFixupModal}>
                            {_("Cancel")}
                        </Button>
                    ]}
                >
                    <Grid>
                        <GridItem span={12}>
                            <Form isHorizontal autoComplete="off">
                                <TextContent>
                                    <Text className="ds-margin-top" component={TextVariants.h4}>
                                        {_("This task only needs to be run after enabling the plugin for the first time, or if the plugin configuration has changed in a way that will impact the group memberships.")}
                                    </Text>
                                </TextContent>
                                <Grid className="ds-margin-top" title={_("Base DN that contains entries to fix up.")}>
                                    <GridItem className="ds-label" span={3}>
                                        {_("Subtree DN")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={fixupDN}
                                            type="text"
                                            id="fixupDN"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="fixupDN"
                                            onChange={(e, str) => { this.handleFieldChange(e) }}
                                            validated={!valid_dn(fixupDN) ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText  >
                                            {_("Value must be a valid DN")}
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-bottom" title={_("Optional. Filter for finding entries to fix up. For example:  (uid=*).  If omitted, all entries with objectclass 'inetuser', 'inetadmin', or 'nsmemberof' under the specified subtree DN will have their memberOf attribute regenerated.")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Search Filter")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={fixupFilter}
                                            type="text"
                                            id="fixupFilter"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="fixupFilter"
                                            onChange={(e, str) => { this.handleFieldChange(e) }}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                        </GridItem>
                    </Grid>
                </Modal>
                <Modal
                    variant={ModalVariant.medium}
                    aria-labelledby="ds-modal"
                    title={_("Manage MemberOf Plugin Shared Config Entry")}
                    isOpen={configEntryModalShow}
                    onClose={this.handleCloseModal}
                    actions={modalButtons}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid className="ds-margin-top" title={_("The config entry full DN")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Config DN")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={configDN}
                                    type="text"
                                    id="configDN"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="configDN"
                                    onChange={(e, str) => { this.handleModalChange(e) }}
                                    validated={errorModal.configDN ? ValidatedOptions.error : ValidatedOptions.default}
                                    isDisabled={newEntry}
                                />
                                <FormHelperText  >
                                    {_("Value must be a valid DN")}
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Specifies the attribute in the user entry for the Directory Server to manage to reflect group membership (memberOfAttr)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Membership Attribute")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a member attribute"
                                    onToggle={(event, isOpen) => this.handleConfigAttrToggle(event, isOpen)}
                                    onSelect={this.handleConfigAttrSelect}
                                    onClear={this.handleConfigAttrClear}
                                    selections={configAttr}
                                    isOpen={this.state.isConfigAttrOpen}
                                    aria-labelledby="typeAhead-config-attr"
                                    placeholderText={_("Type a member attribute...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    validated={errorModal.configAttr ? "error" : "default"}
                                >
                                    {["memberOf"].map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top" title={_("Specifies the attribute in the group entry to use to identify the DNs of group members (memberOfGroupAttr)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Group Attribute")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a member group attribute"
                                    onToggle={(event, isOpen) => this.handleConfigGroupAttrToggle(event, isOpen)}
                                    onSelect={this.handleConfigGroupAttrSelect}
                                    onClear={this.handleConfigGroupAttrClear}
                                    selections={configGroupAttr}
                                    isOpen={this.state.isConfigGroupAttrOpen}
                                    aria-labelledby="typeAhead-config-group-attr"
                                    placeholderText={_("Type a member group attribute...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    validated={errorModal.configGroupAttr ? "error" : "default"}
                                >
                                    {["member", "memberCertificate", "uniqueMember"].map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top" title={_("Specifies backends or multiple-nested suffixes for the MemberOf plug-in to work on (memberOfEntryScope)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Subtree Scope")}
                            </GridItem>
                            <GridItem span={6}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a subtree DN"
                                    onToggle={(event, isOpen) => this.handleConfigScopeToggle(event, isOpen)}
                                    onSelect={this.handleConfigScopeSelect}
                                    onClear={this.handleConfigScopeClear}
                                    selections={configEntryScope}
                                    isOpen={isConfigSubtreeScopeOpen}
                                    aria-labelledby="typeAhead-subtrees"
                                    placeholderText={_("Type a subtree DN...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                    onCreateOption={this.handleConfigCreateOption}
                                    validated={errorModal.configEntryScope ? "error" : "default"}
                                >
                                    {[""].map((dn, index) => (
                                        <SelectOption
                                            key={index}
                                            value={dn}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText  >
                                    {_("Values must be valid DN's")}
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Checkbox
                                    id="configAllBackends"
                                    isChecked={configAllBackends}
                                    onChange={(e, checked) => { this.handleModalChange(e) }}
                                    title={_("Specifies whether to search the local suffix for user entries on all available suffixes (memberOfAllBackends)")}
                                    label={_("All Backends")}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Specifies backends or multiple-nested suffixes for the MemberOf plug-in to exclude (memberOfEntryScopeExcludeSubtree)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Exclude Subtree")}
                            </GridItem>
                            <GridItem span={6}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a subtree DN"
                                    onToggle={(event, isOpen) => this.handleConfigExcludeScopeToggle(event, isOpen)}
                                    onSelect={this.handleConfigExcludeScopeSelect}
                                    onClear={this.handleConfigExcludeScopeClear}
                                    selections={configEntryScopeExcludeSubtree}
                                    isOpen={isConfigExcludeScopeOpen}
                                    aria-labelledby="typeAhead-subtrees"
                                    placeholderText={_("Type a subtree DN...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                    onCreateOption={this.handleConfigExcludeCreateOption}
                                    validated={errorModal.configEntryScopeExcludeSubtree ? "error" : "default"}
                                >
                                    {[""].map((dn, index) => (
                                        <SelectOption
                                            key={index}
                                            value={dn}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText  >
                                    {_("Values must be valid DN's")}
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Checkbox
                                    id="configSkipNested"
                                    isChecked={configSkipNested}
                                    onChange={(e, checked) => { this.handleModalChange(e) }}
                                    title={_("Specifies wherher to skip nested groups or not (memberOfSkipNested)")}
                                    label={_("Skip Nested")}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("If an entry does not have an object class that allows the memberOf attribute then the memberOf plugin will automatically add the object class listed in the memberOfAutoAddOC parameter")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Auto Add OC")}
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="configAutoAddOC"
                                    value={configAutoAddOC}
                                    onChange={(event, value) => {
                                        this.handleFieldChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="no_setting2" value="" label="-" />
                                    {this.props.objectClasses.map((attr, index) => (
                                        <FormSelectOption key={attr} value={attr} label={attr} />
                                    ))}
                                </FormSelect>
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>

                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="MemberOf Plugin"
                    pluginName="MemberOf"
                    cmdName="memberof"
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title={_("Specifies the attribute in the user entry for the Directory Server to manage to reflect group membership (memberOfAttr)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Membership Attribute")}
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a member attribute"
                                    onToggle={(event, isOpen) => this.handleMemberOfAttrToggle(event, isOpen)}
                                    onSelect={this.handleMemberOfAttrSelect}
                                    onClear={this.handleMemberOfAttrClear}
                                    selections={memberOfAttr}
                                    isOpen={this.state.isMemberOfAttrOpen}
                                    aria-labelledby="typeAhead-memberof-attr"
                                    placeholderText={_("Type a member attribute...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    validated={error.memberOfAttr ? "error" : "default"}
                                >
                                    {["memberOf"].map((attr) => (
                                        <SelectOption
                                            key={attr}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top" title={_("Specifies the attribute in the group entry to use to identify the DNs of group members (memberOfGroupAttr)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Group Attribute")}
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a member group attribute"
                                    onToggle={(event, isOpen) => this.handleMemberOfGroupAttrToggle(event, isOpen)}
                                    onSelect={this.handleMemberOfGroupAttrSelect}
                                    onClear={this.handleMemberOfGroupAttrClear}
                                    selections={memberOfGroupAttr}
                                    isOpen={this.state.isMemberOfGroupAttrOpen}
                                    aria-labelledby="typeAhead-memberof-group-attr"
                                    placeholderText={_("Type a member group attribute...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    validated={error.memberOfGroupAttr ? "error" : "default"}
                                >
                                    {["member", "memberCertificate", "uniqueMember"].map((attr) => (
                                        <SelectOption
                                            key={attr}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top" title={_("Specifies backends or multiple-nested suffixes for the MemberOf plug-in to work on (memberOfEntryScope)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Subtree Scope")}
                            </GridItem>
                            <GridItem span={6}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a subtree DN"
                                    onToggle={(event, isOpen) => this.handleSubtreeScopeToggle(event, isOpen)}
                                    onSelect={this.handleSubtreeScopeSelect}
                                    onClear={this.handleSubtreeScopeClear}
                                    selections={memberOfEntryScope}
                                    isOpen={isSubtreeScopeOpen}
                                    aria-labelledby="typeAhead-subtrees"
                                    placeholderText={_("Type a subtree DN...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                    onCreateOption={this.handleSubtreeScopeCreateOption}
                                    validated={error.memberOfEntryScope ? "error" : "default"}
                                >
                                    {[""].map((dn, index) => (
                                        <SelectOption
                                            key={index}
                                            value={dn}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText  >
                                    {_("A subtree is required, and values must be valid DN's")}
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Checkbox
                                    id="memberOfAllBackends"
                                    isChecked={memberOfAllBackends}
                                    onChange={(e, checked) => { this.handleFieldChange(e) }}
                                    title={_("Specifies whether to search the local suffix for user entries on all available suffixes (memberOfAllBackends)")}
                                    label={_("All Backends")}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Specifies backends or multiple-nested suffixes for the MemberOf plug-in to exclude (memberOfEntryScopeExcludeSubtree)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Exclude Subtree")}
                            </GridItem>
                            <GridItem span={6}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a subtree DN"
                                    onToggle={(event, isOpen) => this.handleExcludeScopeToggle(event, isOpen)}
                                    onSelect={this.handleExcludeScopeSelect}
                                    onClear={this.handleExcludeScopeClear}
                                    selections={memberOfEntryScopeExcludeSubtree}
                                    isOpen={isExcludeScopeOpen}
                                    aria-labelledby="typeAhead-subtrees"
                                    placeholderText={_("Type a subtree DN...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                    onCreateOption={this.handleExcludeCreateOption}
                                    validated={error.memberOfEntryScopeExcludeSubtree ? "error" : "default"}
                                >
                                    {[""].map((dn, index) => (
                                        <SelectOption
                                            key={index}
                                            value={dn}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText  >
                                    {_("Values must be valid DN's")}
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Checkbox
                                    id="memberOfSkipNested"
                                    isChecked={memberOfSkipNested}
                                    onChange={(e, checked) => { this.handleFieldChange(e) }}
                                    title={_("Specifies wherher to skip nested groups or not (memberOfSkipNested)")}
                                    label={_("Skip Nested")}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("The value to set as nsslapd-pluginConfigArea")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Shared Config Entry")}
                            </GridItem>
                            <GridItem span={6}>
                                <TextInput
                                    value={memberOfConfigEntry}
                                    type="text"
                                    id="memberOfConfigEntry"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="memberOfConfigEntry"
                                    onChange={(e, str) => { this.handleFieldChange(e) }}
                                    validated={error.memberOfConfigEntry ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                                <FormHelperText  >
                                    {_("Value must be a valid DN")}
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Button
                                    variant="primary"
                                    isDisabled={memberOfConfigEntry === "" || !valid_dn(memberOfConfigEntry)}
                                    onClick={this.handleOpenModal}
                                >
                                    {_("Manage")}
                                </Button>
                            </GridItem>
                        </Grid>
                        <Grid title={_("If an entry does not have an object class that allows the memberOf attribute then the memberOf plugin will automatically add the object class listed in the memberOfAutoAddOC parameter")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Auto Add OC")}
                            </GridItem>
                            <GridItem span={8}>
                                <FormSelect
                                    id="memberOfAutoAddOC"
                                    value={memberOfAutoAddOC}
                                    onChange={(event, value) => {
                                        this.handleFieldChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="no_setting" value="" label="-" />
                                    {this.props.objectClasses.map((attr) => (
                                        <FormSelectOption key={attr} value={attr} label={attr} />
                                    ))}
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title={_("The fixup task will add the memberOf attribute to entries that are missing it.  This is typically only run once after enabling or changing the plugin.")}>
                            <GridItem className="ds-label ds-margin-top" span={3}>
                                {_("MemberOf Fixup Task")}<WrenchIcon className="ds-left-margin" />
                            </GridItem>
                            <GridItem span={9}>
                                <Button className="ds-margin-top" variant="secondary" onClick={this.handleToggleFixupModal}>
                                    {_("Run Task")}
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
                        {saveBtnName}
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
                    mTitle={_("Delete MemberOf Config Entry")}
                    mMsg={_("Are you sure you want to delete this config entry?")}
                    mSpinningMsg={_("Deleting ...")}
                    mBtnName={_("Delete")}
                />
            </div>
        );
    }
}

MemberOf.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    objectClasses: PropTypes.array,
    toggleLoadingHandler: PropTypes.func
};

MemberOf.defaultProps = {
    rows: [],
    serverId: "",
    objectClasses: [],
};

export default MemberOf;
