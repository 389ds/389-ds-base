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
    Select,
    SelectVariant,
    SelectOption,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";
import {
    WrenchIcon,
} from '@patternfly/react-icons';

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
        this.handleChange = this.handleChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.openModal = this.openModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.runFixup = this.runFixup.bind(this);
        this.toggleFixupModal = this.toggleFixupModal.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
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
        this.onConfigAttrSelect = (event, selection) => {
            if (selection == this.state.configAttr) {
                this.onConfigAttrClear();
            } else {
                this.setState({
                    configAttr: selection,
                    isConfigAttrOpen: false
                }, () => { this.validateModal() });
            }
        };
        this.onConfigAttrToggle = isConfigAttrOpen => {
            this.setState({
                isConfigAttrOpen
            });
        };
        this.onConfigAttrClear = () => {
            this.setState({
                configAttr: "",
                isConfigAttrOpen: false
            }, () => { this.validateModal() });
        };

        // Config Group Attribute
        this.onConfigGroupAttrSelect = (event, selection) => {
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
        this.onConfigGroupAttrToggle = isConfigGroupAttrOpen => {
            this.setState({
                isConfigGroupAttrOpen
            });
        };
        this.onConfigGroupAttrClear = () => {
            this.setState({
                configGroupAttr: [],
                isConfigGroupAttrOpen: false
            }, () => { this.validateModal() });
        };

        // MemberOf Attribute
        this.onMemberOfAttrSelect = (event, selection) => {
            if (selection == this.state.configAttr) {
                this.onMemberOfAttrClear();
            } else {
                this.setState({
                    memberOfAttr: selection,
                    isMemberOfAttrOpen: false
                }, () => { this.validateModal() });
            }
        };
        this.onMemberOfAttrToggle = isMemberOfAttrOpen => {
            this.setState({
                isMemberOfAttrOpen
            });
        };
        this.onMemberOfAttrClear = () => {
            this.setState({
                memberOfAttr: [],
                isMemberOfAttrOpen: false
            }, () => { this.validateConfig() });
        };

        // MemberOf Group Attribute
        this.onMemberOfGroupAttrSelect = (event, selection) => {
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
        this.onMemberOfGroupAttrToggle = isMemberOfGroupAttrOpen => {
            this.setState({
                isMemberOfGroupAttrOpen
            });
        };
        this.onMemberOfGroupAttrClear = () => {
            this.setState({
                memberOfGroupAttr: [],
                isMemberOfGroupAttrOpen: false
            }, () => { this.validateConfig() });
        };

        // Handle scope subtree
        this.onSubtreeScopeSelect = (event, selection) => {
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
        this.onSubtreeScopeToggle = isSubtreeScopeOpen => {
            this.setState({
                isSubtreeScopeOpen
            }, () => { this.validateConfig() });
        };
        this.onSubtreeScopeClear = () => {
            this.setState({
                memberOfEntryScope: [],
                isSubtreeScopeOpen: false
            }, () => { this.validateConfig() });
        };
        this.onSubtreeScopeCreateOption = newValue => {
            if (!this.state.memberOfEntryScopeOptions.includes(newValue)) {
                this.setState({
                    memberOfEntryScopeOptions: [...this.state.memberOfEntryScopeOptions, newValue],
                    isSubtreeScopeOpen: false
                }, () => { this.validateConfig() });
            }
        };

        // Handle Exclude Scope subtree
        this.onExcludeScopeSelect = (event, selection) => {
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
        this.onExcludeScopeToggle = isExcludeScopeOpen => {
            this.setState({
                isExcludeScopeOpen
            }, () => { this.validateConfig() });
        };
        this.onExcludeScopeClear = () => {
            this.setState({
                memberOfEntryScopeExcludeSubtree: [],
                isExcludeScopeOpen: false
            }, () => { this.validateConfig() });
        };
        this.onExcludeCreateOption = newValue => {
            if (!this.state.memberOfEntryScopeOptions.includes(newValue)) {
                this.setState({
                    memberOfEntryScopeExcludeOptions: [...this.state.memberOfEntryScopeExcludeOptions, newValue],
                    isExcludeScopeOpen: false
                }, () => { this.validateConfig() });
            }
        };

        // Modal scope and exclude Scope
        // Handle scope subtree
        this.onConfigScopeSelect = (event, selection) => {
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
        this.onConfigScopeToggle = isConfigSubtreeScopeOpen => {
            this.setState({
                isConfigSubtreeScopeOpen
            }, () => { this.validateModal() });
        };
        this.onConfigScopeClear = () => {
            this.setState({
                configEntryScope: [],
                isConfigSubtreeScopeOpen: false
            }, () => { this.validateModal() });
        };
        this.onConfigCreateOption = newValue => {
            if (!this.state.configEntryScopeOptions.includes(newValue)) {
                this.setState({
                    configEntryScopeOptions: [...this.state.configEntryScopeOptions, newValue],
                    isConfigSubtreeScopeOpen: false
                }, () => { this.validateModal() });
            }
        };

        // Handle Exclude Scope subtree
        this.onConfigExcludeScopeSelect = (event, selection) => {
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
        this.onConfigExcludeScopeToggle = isConfigExcludeScopeOpen => {
            this.setState({
                isConfigExcludeScopeOpen
            }, () => { this.validateModal() });
        };
        this.onConfigExcludeScopeClear = () => {
            this.setState({
                configEntryScopeExcludeSubtree: [],
                isConfigExcludeScopeOpen: false
            }, () => { this.validateModal() });
        };
        this.onConfigExcludeCreateOption = newValue => {
            if (!this.state.configEntryScopeExcludeOptions.includes(newValue)) {
                this.setState({
                    configEntryScopeExcludeOptions: [...this.state.configEntryScopeExcludeOptions, newValue],
                    isConfigExcludeScopeOpen: false
                }, () => { this.validateModal() });
            }
        };
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
            if (this.state[attr] == "") {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check required Lists are not empty
        for (const attr of reqLists) {
            if (this.state[attr].length == 0) {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check DN attrs
        for (const attr of dnAttrs) {
            if (this.state[attr] != "" && !valid_dn(this.state[attr])) {
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
                if (this.state[check_attr] != this.state['_' + check_attr]) {
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
            if (this.state[attr] == "") {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check required Lists are not empty
        for (const attr of reqLists) {
            if (this.state[attr].length == 0) {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check DN attrs
        for (const attr of dnAttrs) {
            if (this.state[attr] != "" && !valid_dn(this.state[attr])) {
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

    handleChange(e) {
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

    runFixup() {
        if (!this.state.fixupDN) {
            this.props.addNotification("warning", "Fixup DN is required.");
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
            log_cmd("runFixup", "Run fixup MemberOf Plugin ", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        this.props.addNotification(
                            "success",
                            `Fixup task for ${this.state.fixupDN} was successfull`
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
                            `Fixup task for ${this.state.fixupDN} has failed ${errMsg.desc}`
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            fixupModalShow: false
                        });
                    });
        }
    }

    openModal() {
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
                            configEntry.memberofallbackends[0] == "off"
                            ),
                            configSkipNested: !(
                                configEntry.memberofskipnested === undefined ||
                            configEntry.memberofskipnested[0] == "off"
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

    closeModal() {
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

        if (configAttr.length == 0 || configGroupAttr.length == 0) {
            this.props.addNotification(
                "warning",
                "Config Attribute and Group Attribute are required."
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
                configAttr || action == "add" ? configAttr : "delete",
                "--allbackends",
                configAllBackends ? "on" : "off",
                "--skipnested",
                configSkipNested ? "on" : "off",
                "--autoaddoc",
                configAutoAddOC || action == "add" ? configAutoAddOC : "delete",
            ];

            // Delete attributes if the user set an empty value to the field
            cmd = [...cmd, "--scope"];
            if (configEntryScope.length != 0) {
                for (const value of configEntryScope) {
                    cmd = [...cmd, value];
                }
            } else {
                cmd = [...cmd, "delete"];
            }
            cmd = [...cmd, "--exclude"];
            if (configEntryScopeExcludeSubtree.length != 0) {
                for (const value of configEntryScopeExcludeSubtree) {
                    cmd = [...cmd, value];
                }
            } else {
                cmd = [...cmd, "delete"];
            }
            cmd = [...cmd, "--groupattr"];
            if (configGroupAttr.length != 0) {
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
                        this.props.addNotification(
                            "success",
                            `Config entry ${configDN} was successfully ${action == "set" ? "edit" : "add"}ed`
                        );
                        this.props.pluginListHandler();
                        this.closeModal();
                        this.setState({
                            savingModal: false,
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Error during the config entry ${action} operation - ${errMsg.desc}`
                        );
                        this.props.pluginListHandler();
                        this.closeModal();
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
                        `Config entry ${this.state.configDN} was successfully deleted`
                    );
                    this.props.pluginListHandler();
                    this.closeModal();
                    this.setState({
                        modalSpinning: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry removal operation - ${errMsg.desc}`
                    );
                    this.props.pluginListHandler();
                    this.closeModal();
                    this.setState({
                        modalSpinning: false,
                    });
                });
    }

    addConfig() {
        this.cmdOperation("add");
    }

    editConfig() {
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
                    pluginRow.memberofallbackends[0] == "off"
                ),
                memberOfSkipNested: !(
                    pluginRow.memberofskipnested === undefined ||
                    pluginRow.memberofskipnested[0] == "off"
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
                    pluginRow.memberofallbackends[0] == "off"
                ),
                _memberOfSkipNested: !(
                    pluginRow.memberofskipnested === undefined ||
                    pluginRow.memberofskipnested[0] == "off"
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

    saveConfig() {
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
        if (memberOfEntryScope.length != 0) {
            for (const value of memberOfEntryScope) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--exclude"];
        if (memberOfEntryScopeExcludeSubtree.length != 0) {
            for (const value of memberOfEntryScopeExcludeSubtree) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--groupattr"];
        if (memberOfGroupAttr.length != 0) {
            for (const value of memberOfGroupAttr) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        this.setState({
            saving: true
        });
        log_cmd("saveConfig", `Save MemberOf Plugin`, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Successfully updated MemberOf Plugin`
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

        let saveBtnName = "Save Config";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Saving Config ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
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
                    isDisabled={saveBtnDisabledModal || savingModal}
                    isLoading={savingModal}
                    spinnerAriaValueText={savingModal ? "Saving" : undefined}
                    {...extraPrimaryProps}
                >
                    {savingModal ? "Adding ..." : "Add Config"}
                </Button>,
                <Button key="cancel" variant="link" onClick={this.closeModal}>
                    Cancel
                </Button>
            ];
        }

        return (
            <div>
                <Modal
                    variant={ModalVariant.small}
                    aria-labelledby="ds-modal"
                    title="MemberOf Plugin FixupTask"
                    isOpen={fixupModalShow}
                    onClose={this.toggleFixupModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            onClick={this.runFixup}
                            isDisabled={!valid_dn(fixupDN)}
                        >
                            Run
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.toggleFixupModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Grid>
                        <GridItem span={12}>
                            <Form isHorizontal autoComplete="off">
                                <TextContent>
                                    <Text className="ds-margin-top" component={TextVariants.h4}>
                                        This task only needs to be run after enabling the plugin for the first time,
                                        or if the plugin configuration has changed in a way that will impact the
                                        group memberships.
                                    </Text>
                                </TextContent>
                                <Grid className="ds-margin-top" title="Base DN that contains entries to fix up.">
                                    <GridItem className="ds-label" span={3}>
                                        Subtree DN
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={fixupDN}
                                            type="text"
                                            id="fixupDN"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="fixupDN"
                                            onChange={(str, e) => { this.handleFieldChange(e) }}
                                            validated={!valid_dn(fixupDN) ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={valid_dn(fixupDN)}>
                                            Value must be a valid DN
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-bottom" title="Optional. Filter for finding entries to fix up. For example:  (uid=*).  If omitted, all entries with objectclass 'inetuser', 'inetadmin', or 'nsmemberof' under the specified subtree DN will have their memberOf attribute regenerated.">
                                    <GridItem span={3} className="ds-label">
                                        Search Filter
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={fixupFilter}
                                            type="text"
                                            id="fixupFilter"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="fixupFilter"
                                            onChange={(str, e) => { this.handleFieldChange(e) }}
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
                    title="Manage MemberOf Plugin Shared Config Entry"
                    isOpen={configEntryModalShow}
                    onClose={this.closeModal}
                    actions={modalButtons}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid className="ds-margin-top" title="The config entry full DN">
                            <GridItem className="ds-label" span={3}>
                                Config DN
                            </GridItem>
                            <GridItem span={9}>
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
                        <Grid title="Specifies the attribute in the user entry for the Directory Server to manage to reflect group membership (memberOfAttr)">
                            <GridItem className="ds-label" span={3}>
                                Membership Attribute
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a member attribute"
                                    onToggle={this.onConfigAttrToggle}
                                    onSelect={this.onConfigAttrSelect}
                                    onClear={this.onConfigAttrClear}
                                    selections={configAttr}
                                    isOpen={this.state.isConfigAttrOpen}
                                    aria-labelledby="typeAhead-config-attr"
                                    placeholderText="Type a member attribute..."
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid className="ds-margin-top" title="Specifies the attribute in the group entry to use to identify the DNs of group members (memberOfGroupAttr)">
                            <GridItem className="ds-label" span={3}>
                                Group Attribute
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a member group attribute"
                                    onToggle={this.onConfigGroupAttrToggle}
                                    onSelect={this.onConfigGroupAttrSelect}
                                    onClear={this.onConfigGroupAttrClear}
                                    selections={configGroupAttr}
                                    isOpen={this.state.isConfigGroupAttrOpen}
                                    aria-labelledby="typeAhead-config-group-attr"
                                    placeholderText="Type a member group attribute..."
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid className="ds-margin-top" title="Specifies backends or multiple-nested suffixes for the MemberOf plug-in to work on (memberOfEntryScope)">
                            <GridItem className="ds-label" span={3}>
                                Subtree Scope
                            </GridItem>
                            <GridItem span={6}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a subtree DN"
                                    onToggle={this.onConfigScopeToggle}
                                    onSelect={this.onConfigScopeSelect}
                                    onClear={this.onConfigScopeClear}
                                    selections={configEntryScope}
                                    isOpen={isConfigSubtreeScopeOpen}
                                    aria-labelledby="typeAhead-subtrees"
                                    placeholderText="Type a subtree DN..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={this.onConfigCreateOption}
                                    validated={errorModal.configEntryScope ? "error" : "default"}
                                >
                                    {[""].map((dn, index) => (
                                        <SelectOption
                                            key={index}
                                            value={dn}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText isError isHidden={!errorModal.configEntryScope}>
                                    Values must be valid DN's
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Checkbox
                                    id="configAllBackends"
                                    isChecked={configAllBackends}
                                    onChange={(checked, e) => { this.handleModalChange(e) }}
                                    title="Specifies whether to search the local suffix for user entries on all available suffixes (memberOfAllBackends)"
                                    label="All Backends"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Specifies backends or multiple-nested suffixes for the MemberOf plug-in to exclude (memberOfEntryScopeExcludeSubtree)">
                            <GridItem className="ds-label" span={3}>
                                Exclude Subtree
                            </GridItem>
                            <GridItem span={6}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a subtree DN"
                                    onToggle={this.onConfigExcludeScopeToggle}
                                    onSelect={this.onConfigExcludeScopeSelect}
                                    onClear={this.onConfigExcludeScopeClear}
                                    selections={configEntryScopeExcludeSubtree}
                                    isOpen={isConfigExcludeScopeOpen}
                                    aria-labelledby="typeAhead-subtrees"
                                    placeholderText="Type a subtree DN..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={this.onConfigExcludeCreateOption}
                                    validated={errorModal.configEntryScopeExcludeSubtree ? "error" : "default"}
                                >
                                    {[""].map((dn, index) => (
                                        <SelectOption
                                            key={index}
                                            value={dn}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText isError isHidden={!errorModal.configEntryScopeExcludeSubtree}>
                                    Values must be valid DN's
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Checkbox
                                    id="configSkipNested"
                                    isChecked={configSkipNested}
                                    onChange={(checked, e) => { this.handleModalChange(e) }}
                                    title="Specifies wherher to skip nested groups or not (memberOfSkipNested)"
                                    label="Skip Nested"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="If an entry does not have an object class that allows the memberOf attribute then the memberOf plugin will automatically add the object class listed in the memberOfAutoAddOC parameter">
                            <GridItem className="ds-label" span={3}>
                                Auto Add OC
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="configAutoAddOC"
                                    value={configAutoAddOC}
                                    onChange={(value, event) => {
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
                        <Grid title="Specifies the attribute in the user entry for the Directory Server to manage to reflect group membership (memberOfAttr)">
                            <GridItem className="ds-label" span={3}>
                                Membership Attribute
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a member attribute"
                                    onToggle={this.onMemberOfAttrToggle}
                                    onSelect={this.onMemberOfAttrSelect}
                                    onClear={this.onMemberOfAttrClear}
                                    selections={memberOfAttr}
                                    isOpen={this.state.isMemberOfAttrOpen}
                                    aria-labelledby="typeAhead-memberof-attr"
                                    placeholderText="Type a member attribute..."
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid className="ds-margin-top" title="Specifies the attribute in the group entry to use to identify the DNs of group members (memberOfGroupAttr)">
                            <GridItem className="ds-label" span={3}>
                                Group Attribute
                            </GridItem>
                            <GridItem span={8}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a member group attribute"
                                    onToggle={this.onMemberOfGroupAttrToggle}
                                    onSelect={this.onMemberOfGroupAttrSelect}
                                    onClear={this.onMemberOfGroupAttrClear}
                                    selections={memberOfGroupAttr}
                                    isOpen={this.state.isMemberOfGroupAttrOpen}
                                    aria-labelledby="typeAhead-memberof-group-attr"
                                    placeholderText="Type a member group attribute..."
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid className="ds-margin-top" title="Specifies backends or multiple-nested suffixes for the MemberOf plug-in to work on (memberOfEntryScope)">
                            <GridItem className="ds-label" span={3}>
                                Subtree Scope
                            </GridItem>
                            <GridItem span={6}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a subtree DN"
                                    onToggle={this.onSubtreeScopeToggle}
                                    onSelect={this.onSubtreeScopeSelect}
                                    onClear={this.onSubtreeScopeClear}
                                    selections={memberOfEntryScope}
                                    isOpen={isSubtreeScopeOpen}
                                    aria-labelledby="typeAhead-subtrees"
                                    placeholderText="Type a subtree DN..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={this.onSubtreeScopeCreateOption}
                                    validated={error.memberOfEntryScope ? "error" : "default"}
                                >
                                    {[""].map((dn, index) => (
                                        <SelectOption
                                            key={index}
                                            value={dn}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText isError isHidden={!error.memberOfEntryScope}>
                                    A subtree is required, and values must be valid DN's
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Checkbox
                                    id="memberOfAllBackends"
                                    isChecked={memberOfAllBackends}
                                    onChange={(checked, e) => { this.handleFieldChange(e) }}
                                    title="Specifies whether to search the local suffix for user entries on all available suffixes (memberOfAllBackends)"
                                    label="All Backends"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Specifies backends or multiple-nested suffixes for the MemberOf plug-in to exclude (memberOfEntryScopeExcludeSubtree)">
                            <GridItem className="ds-label" span={3}>
                                Exclude Subtree
                            </GridItem>
                            <GridItem span={6}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a subtree DN"
                                    onToggle={this.onExcludeScopeToggle}
                                    onSelect={this.onExcludeScopeSelect}
                                    onClear={this.onExcludeScopeClear}
                                    selections={memberOfEntryScopeExcludeSubtree}
                                    isOpen={isExcludeScopeOpen}
                                    aria-labelledby="typeAhead-subtrees"
                                    placeholderText="Type a subtree DN..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={this.onExcludeCreateOption}
                                    validated={error.memberOfEntryScopeExcludeSubtree ? "error" : "default"}
                                >
                                    {[""].map((dn, index) => (
                                        <SelectOption
                                            key={index}
                                            value={dn}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText isError isHidden={!error.memberOfEntryScopeExcludeSubtree}>
                                    Values must be valid DN's
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Checkbox
                                    id="memberOfSkipNested"
                                    isChecked={memberOfSkipNested}
                                    onChange={(checked, e) => { this.handleFieldChange(e) }}
                                    title="Specifies wherher to skip nested groups or not (memberOfSkipNested)"
                                    label="Skip Nested"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="The value to set as nsslapd-pluginConfigArea">
                            <GridItem className="ds-label" span={3}>
                                Shared Config Entry
                            </GridItem>
                            <GridItem span={6}>
                                <TextInput
                                    value={memberOfConfigEntry}
                                    type="text"
                                    id="memberOfConfigEntry"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="memberOfConfigEntry"
                                    onChange={(str, e) => { this.handleFieldChange(e) }}
                                    validated={error.memberOfConfigEntry ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                                <FormHelperText isError isHidden={!error.memberOfConfigEntry}>
                                    Value must be a valid DN
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={3}>
                                <Button
                                    variant="primary"
                                    isDisabled={memberOfConfigEntry == "" || !valid_dn(memberOfConfigEntry)}
                                    onClick={this.openModal}
                                >
                                    Manage
                                </Button>
                            </GridItem>
                        </Grid>
                        <Grid title="If an entry does not have an object class that allows the memberOf attribute then the memberOf plugin will automatically add the object class listed in the memberOfAutoAddOC parameter">
                            <GridItem className="ds-label" span={3}>
                                Auto Add OC
                            </GridItem>
                            <GridItem span={8}>
                                <FormSelect
                                    id="memberOfAutoAddOC"
                                    value={memberOfAutoAddOC}
                                    onChange={(value, event) => {
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
                        <Grid title="The fixup task will add the memberOf attribute to entries that are missing it.  This is typically only run once after enabling or changing the plugin.">
                            <GridItem className="ds-label ds-margin-top" span={3}>
                                MemberOf Fixup Task<WrenchIcon className="ds-left-margin" />
                            </GridItem>
                            <GridItem span={9}>
                                <Button className="ds-margin-top" variant="secondary" onClick={this.toggleFixupModal}>
                                    Run Task
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
                        {saveBtnName}
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
                    mTitle="Delete MemberOf Config Entry"
                    mMsg="Are you sure you want to delete this config entry?"
                    mSpinningMsg="Deleting ..."
                    mBtnName="Delete"
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
