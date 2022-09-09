import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Select,
    SelectOption,
    SelectVariant,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions,
} from "@patternfly/react-core";
import {
    ArrowRightIcon,
} from '@patternfly/react-icons';
import { AutoMembershipDefinitionTable, AutoMembershipRegexTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, listsEqual, valid_dn } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

class AutoMembership extends React.Component {
    componentDidMount() {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.loadDefinitions();
            }
        }
    }

    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            definitionRows: [],
            regexRows: [],
            _regexRows: [],
            regexesToDelete: [],
            modalSpinning: false,
            modalChecked: false,
            saving: false,
            savingRegex: false,
            saveBtnDisabled: true,
            saveRegexBtnDisabled: true,
            error: {},
            errorRegex: {},

            // Definition settings
            definitionName: "",
            defaultGroup: "",
            filter: "",
            groupingAttrMember: "",
            groupingAttrEntry: "",
            scope: "",
            _definitionName: "",
            _defaultGroup: "",
            _filter: "",
            _groupingAttrMember: "",
            _groupingAttrEntry: "",
            _scope: "",

            // Regex settings
            regexName: "",
            regexTargetGroup: "",
            regexExclusive: [],
            regexInclusive: [],
            _regexName: "",
            _regexTargetGroup: "",
            _regexExclusive: [],
            _excludeOptions: [],
            _regexInclusive: [],
            isRegexExcludeOpen: false,
            excludeOptions: [],
            isRegexIncludeOpen: false,
            includeOptions: [],

            regexTableKey: 0,
            newDefinitionEntry: false,
            newRegexEntry: false,
            definitionEntryModalShow: false,
            regexEntryModalShow: false,
            showConfirmDelete: false,
        };

        // This vastly improves rendering performance during handleChange()
        let attrs = [...this.props.attributes, 'dn'].sort();
        this.attrRows = attrs.map((attr) => (
            <FormSelectOption key={attr} value={attr} label={attr} />
        ));

        this.onRegexExcludeSelect = (event, selection) => {
            const { regexExclusive } = this.state;
            if (regexExclusive.includes(selection)) {
                this.setState(
                    prevState => ({
                        regexExclusive: prevState.regexExclusive.filter(item => item !== selection),
                        isRegexExcludeOpen: false
                    }), () => { this.validateRegex() }
                );
            } else {
                this.setState(
                    prevState => ({
                        regexExclusive: [...prevState.regexExclusive, selection],
                        isRegexExcludeOpen: false,
                    }), () => { this.validateRegex() }
                );
            }
        };
        this.onCreateRegexExcludeOption = newValue => {
            if (!this.state.excludeOptions.includes(newValue)) {
                this.setState({
                    excludeOptions: [...this.state.excludeOptions, newValue],
                    isRegexExcludeOpen: false
                });
            }
        };
        this.onRegexExcludeToggle = isRegexExcludeOpen => {
            this.setState({
                isRegexExcludeOpen
            });
        };
        this.clearRegexExcludeSelection = () => {
            this.setState({
                regexExclusive: [],
                isRegexExcludeOpen: false
            }, () => { this.validateRegex() });
        };

        this.onRegexIncludeSelect = (event, selection) => {
            const { regexInclusive } = this.state;
            if (regexInclusive.includes(selection)) {
                this.setState(
                    prevState => ({
                        regexInclusive: prevState.regexInclusive.filter(item => item !== selection),
                        isRegexIncludeOpen: false
                    }), () => { this.validateRegex() }
                );
            } else {
                this.setState(
                    prevState => ({
                        regexInclusive: [...prevState.regexInclusive, selection],
                        isRegexIncludeOpen: false
                    }), () => { this.validateRegex() }
                );
            }
        };
        this.onCreateRegexIncludeOption = newValue => {
            if (!this.state.includeOptions.includes(newValue)) {
                this.setState({
                    includeOptions: [...this.state.includeOptions, newValue],
                    isRegexIncludeOpen: false
                });
            }
        };
        this.onRegexIncludeToggle = isRegexIncludeOpen => {
            this.setState({
                isRegexIncludeOpen
            });
        };
        this.clearRegexIncludeSelection = () => {
            this.setState({
                regexInclusive: [],
                isRegexIncludeOpen: false
            }, () => { this.validateRegex() });
        };

        this.validateModal = this.validateModal.bind(this);
        this.validateRegex = this.validateRegex.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleRegexChange = this.handleRegexChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.loadDefinitions = this.loadDefinitions.bind(this);
        this.loadRegexes = this.loadRegexes.bind(this);
        this.openModal = this.openModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.showEditDefinitionModal = this.showEditDefinitionModal.bind(this);
        this.showAddDefinitionModal = this.showAddDefinitionModal.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.deleteDefinition = this.deleteDefinition.bind(this);
        this.addDefinition = this.addDefinition.bind(this);
        this.editDefinition = this.editDefinition.bind(this);
        this.openRegexModal = this.openRegexModal.bind(this);
        this.closeRegexModal = this.closeRegexModal.bind(this);
        this.showEditRegexModal = this.showEditRegexModal.bind(this);
        this.showAddRegexModal = this.showAddRegexModal.bind(this);
        this.cmdRegexOperation = this.cmdRegexOperation.bind(this);
        this.deleteRegex = this.deleteRegex.bind(this);
        this.addRegex = this.addRegex.bind(this);
        this.editRegex = this.editRegex.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
    }

    validateModal () {
        const error = {};
        let all_good = true;
        const dnAttrs = [
            'defaultGroup', 'scope',
        ];
        const reqAttrs = [
            'definitionName', 'scope', 'filter', 'groupingAttrMember',
            'groupingAttrEntry'
        ];

        for (const attr of reqAttrs) {
            if (this.state[attr] == "") {
                error[attr] = true;
                all_good = false;
            }
        }

        for (const attr of dnAttrs) {
            if (this.state[attr] != "" && !valid_dn(this.state[attr])) {
                error[attr] = true;
                all_good = false;
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrs = [
                'definitionName', 'defaultGroup', 'filter',
                'groupingAttrMember', 'groupingAttrEntry', 'scope'
            ];
            for (const check_attr of attrs) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
            // If regexes changed, we need to check that here too
            if (this.state.regexRows.length != this.state._regexRows.length) {
                all_good = true;
            }
            if (!all_good) {
                for (const row of this.state.regexRows) {
                    let found = false;
                    for (const orig_row of this.state._regexRows) {
                        if (orig_row.cn[0] == row.cn[0]) {
                            found = true;
                            if (orig_row.automembertargetgroup[0] != row.automembertargetgroup[0]) {
                                all_good = true;
                                break;
                            }
                            for (const attr of ['automemberinclusiveregex', 'automemberexclusiveregex']) {
                                if (!listsEqual(orig_row[attr], row[attr])) {
                                    all_good = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!found) {
                        all_good = true;
                        break;
                    }
                }
                if (!all_good) {
                    // Go backwards and check if original rows is different
                    for (const orig_row of this.state._regexRows) {
                        let found = false;
                        for (const row of this.state.regexRows) {
                            if (orig_row.cn[0] == row.cn[0]) {
                                found = true;
                            }
                        }
                        if (!found) {
                            all_good = true;
                            break;
                        }
                    }
                }
            }
        }

        this.setState({
            saveBtnDisabled: !all_good,
            error: error
        });
    }

    validateRegex () {
        const error = {};
        let all_good = true;
        const dnAttrs = [
            'regexTargetGroup',
        ];
        const reqAttrs = [
            'regexName', 'regexTargetGroup'
        ];

        for (const attr of reqAttrs) {
            if (this.state[attr] == "") {
                error[attr] = true;
                all_good = false;
            }
        }

        for (const attr of dnAttrs) {
            if (this.state[attr] != "" && !valid_dn(this.state[attr])) {
                error[attr] = true;
                all_good = false;
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrs = [
                'regexTargetGroup'
            ];
            const attrsList = [
                'regexExclusive', 'regexInclusive'
            ];
            for (const check_attr of attrs) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }

            for (const check_attr of attrsList) {
                if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                    all_good = true;
                    break;
                }
            }
        }

        this.setState({
            saveRegexBtnDisabled: !all_good,
            error: error
        }, () => { this.validateModal() });
    }

    handleChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    handleFieldChange(e) {
        const value = e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validateModal() });
    }

    handleRegexChange(e) {
        const value = e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validateRegex() });
    }

    loadDefinitions() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "automember",
            "list",
            "definitions"
        ];
        log_cmd("loadDefinitions", "Get Auto Membership Plugin definitions", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    this.setState({
                        definitionRows: myObject.items.map(item => item.attrs),
                        firstLoad: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadDefinitions failed", errMsg.desc);
                    }
                });
    }

    initRegexRows(rows) {
        for (const row of rows) {
            if (row.automembertargetgroup === undefined) {
                row.automembertargetgroup = "";
            }
            if (row.automemberexclusiveregex === undefined) {
                row.automemberexclusiveregex = [];
            }
            if (row.automemberinclusiveregex === undefined) {
                row.automemberinclusiveregex = [];
            }
        }
    }

    loadRegexes(defName) {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "automember",
            "list",
            "regexes",
            defName
        ];
        log_cmd("loadRegexes", "Get Auto Membership Plugin regexes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    const regexTableKey = this.state.regexTableKey + 1;
                    const rows = myObject.items.map(item => item.attrs);
                    this.initRegexRows(rows);
                    this.setState({
                        regexRows: rows,
                        _regexRows: JSON.parse(JSON.stringify(rows)),
                        regexTableKey: regexTableKey
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadRegexes failed", errMsg.desc);
                    }
                });
    }

    showEditDefinitionModal(rowData) {
        this.openModal(rowData);
    }

    showAddDefinitionModal(rowData) {
        this.openModal();
    }

    openModal(name) {
        if (!name) {
            this.setState({
                definitionEntryModalShow: true,
                newDefinitionEntry: true,
                regexRows: [],
                definitionName: "",
                defaultGroup: "",
                filter: "",
                groupingAttrMember: "",
                groupingAttrEntry: "",
                scope: "",
                saveBtnDisabled: true,
            });
        } else {
            this.loadRegexes(name);
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "automember",
                "definition",
                name,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openModal", "Fetch the Auto Membership Plugin definition entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const definitionEntry = JSON.parse(content).attrs;
                        this.setState({
                            saveBtnDisabled: true,
                            definitionEntryModalShow: true,
                            newDefinitionEntry: false,
                            definitionName:
                            definitionEntry.cn === undefined ? "" : definitionEntry.cn[0],
                            defaultGroup:
                            definitionEntry.automemberdefaultgroup === undefined
                                ? ""
                                : definitionEntry.automemberdefaultgroup[0],
                            filter:
                            definitionEntry.automemberfilter === undefined
                                ? ""
                                : definitionEntry.automemberfilter[0],
                            scope:
                            definitionEntry.automemberscope === undefined
                                ? ""
                                : definitionEntry.automemberscope[0],
                            _definitionName:
                            definitionEntry.cn === undefined ? "" : definitionEntry.cn[0],
                            _defaultGroup:
                            definitionEntry.automemberdefaultgroup === undefined
                                ? ""
                                : definitionEntry.automemberdefaultgroup[0],
                            _filter:
                            definitionEntry.automemberfilter === undefined
                                ? ""
                                : definitionEntry.automemberfilter[0],
                            _scope:
                            definitionEntry.automemberscope === undefined
                                ? ""
                                : definitionEntry.automemberscope[0]
                        });

                        if (definitionEntry.automembergroupingattr === undefined) {
                            this.setState({
                                groupingAttrMember: "",
                                groupingAttrEntry: "",
                                _groupingAttrMember: "",
                                _groupingAttrEntry: ""
                            });
                        } else {
                            const groupingAttr = definitionEntry.automembergroupingattr[0];
                            this.setState({
                                groupingAttrMember: groupingAttr.split(":")[0],
                                groupingAttrEntry: groupingAttr.split(":")[1],
                                _groupingAttrMember: groupingAttr.split(":")[0],
                                _groupingAttrEntry: groupingAttr.split(":")[1]
                            });
                        }

                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            definitionEntryModalShow: true,
                            newDefinitionEntry: true,
                            regexRows: [],
                            definitionName: "",
                            defaultGroup: "",
                            filter: "",
                            groupingAttrMember: "",
                            groupingAttrEntry: "",
                            scope: "",
                            _definitionName: "",
                            _defaultGroup: "",
                            _filter: "",
                            _groupingAttrMember: "",
                            _groupingAttrEntry: "",
                            _scope: ""
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    showEditRegexModal(name) {
        this.openRegexModal(name);
    }

    showAddRegexModal() {
        this.openRegexModal();
    }

    closeModal() {
        this.setState({
            definitionEntryModalShow: false,
            saving: false
        });
    }

    closeRegexModal() {
        this.setState({ regexEntryModalShow: false });
    }

    cmdOperation(action) {
        const {
            definitionName,
            defaultGroup,
            filter,
            groupingAttrMember,
            groupingAttrEntry,
            scope
        } = this.state;

        if (
            definitionName === "" ||
            scope === "" ||
            filter === "" ||
            groupingAttrMember == "" ||
            groupingAttrEntry === ""
        ) {
            this.props.addNotification(
                "warning",
                "Name, Scope, Filter and Grouping Attribute are required."
            );
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "automember",
                "definition",
                definitionName,
                action,
                "--default-group",
                defaultGroup || action == "add" ? defaultGroup : "delete",
                "--filter",
                filter || action == "add" ? filter : "delete",
                "--scope",
                scope || action == "add" ? scope : "delete"
            ];

            cmd = [...cmd, "--grouping-attr"];
            if (groupingAttrMember != "" && groupingAttrEntry != "") {
                cmd = [...cmd, `${groupingAttrMember}:${groupingAttrEntry}`];
            } else if (action == "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }

            this.setState({
                saving: true
            });

            this.props.toggleLoadingHandler();
            log_cmd(
                "AutoMembershipOperation",
                `Do the ${action} operation on the Auto Membership Plugin`,
                cmd
            );
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        console.info("AutoMembershipOperation", "Result", content);
                        this.props.addNotification(
                            "success",
                            `The ${action} operation was successfully done on "${definitionName}" entry`
                        );
                        this.loadDefinitions();
                        this.purgeRegexUpdate();
                        this.closeModal();
                        this.props.toggleLoadingHandler();
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        if (errMsg.desc.indexOf("nothing to set") === 0) {
                            this.props.addNotification(
                                "error",
                                `Error during the definition entry ${action} operation - ${errMsg.desc}`
                            );
                        } else {
                            this.purgeRegexUpdate();
                        }
                        this.loadDefinitions();
                        this.closeModal();
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    purgeRegexUpdate() {
        const { definitionName, regexesToDelete, regexRows } = this.state;

        for (const regexToDelete of regexesToDelete) {
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "automember",
                "definition",
                definitionName,
                "regex",
                regexToDelete,
                "delete"
            ];

            log_cmd(
                "AutoMembershipRegexOperation",
                `Do the delete operation on the Auto Membership Plugin Regex Entry`,
                cmd
            );
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        console.info(
                            "AutoMembershipRegexOperation",
                            "Result",
                            `The delete operation was successfully done on "${regexToDelete}" entry`
                        );
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Error during the regex "${regexToDelete}" entry delete operation - ${
                                errMsg.desc
                            }`
                        );
                    });
        }

        for (const row of regexRows) {
            let action = "";
            let regexName = "";
            if (row.needsadd !== undefined) {
                action = "add";
                regexName = row.cn[0];
            } else if (row.needsupdate !== undefined) {
                action = "set";
                regexName = row.cn[0];
            } else {
                continue;
            }

            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "automember",
                "definition",
                definitionName,
                "regex",
                regexName,
                action
            ];

            const regexTargetGroup = row.automembertargetgroup[0];
            const regexExclusive = row.automemberexclusiveregex === undefined ? [] : row.automemberexclusiveregex;
            const regexInclusive = row.automemberinclusiveregex === undefined ? [] : row.automemberinclusiveregex;

            if (!(action == "add" && regexTargetGroup == 0)) {
                cmd = [...cmd, "--target-group"];
                if (regexTargetGroup) {
                    cmd = [...cmd, regexTargetGroup];
                } else if (action == "add") {
                    cmd = [...cmd, ""];
                } else {
                    cmd = [...cmd, "delete"];
                }
            }

            if (!(action == "add" && regexExclusive.length == 0)) {
                cmd = [...cmd, "--exclusive"];
                if (regexExclusive.length != 0) {
                    for (const regex of regexExclusive) {
                        cmd = [...cmd, regex];
                    }
                } else if (action == "add") {
                    cmd = [...cmd, ""];
                } else {
                    cmd = [...cmd, "delete"];
                }
            }
            if (!(action == "add" && regexInclusive.length == 0)) {
                cmd = [...cmd, "--inclusive"];
                if (regexInclusive.length != 0) {
                    for (const regex of regexInclusive) {
                        cmd = [...cmd, regex];
                    }
                } else if (action == "add") {
                    cmd = [...cmd, ""];
                } else {
                    cmd = [...cmd, "delete"];
                }
            }

            log_cmd(
                "AutoMembershipRegexOperation",
                `Do the ${action} operation on the Auto Membership Plugin Regex Entry`,
                cmd
            );
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        console.info(
                            "AutoMembershipRegexOperation",
                            "Result",
                            `The ${action} operation was successfully done on "${regexName}" entry`
                        );
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Error during the regex "${regexName}" entry ${action} operation - ${
                                errMsg.desc
                            }`
                        );
                    });
        }
    }

    showConfirmDelete(definitionName) {
        this.setState({
            showConfirmDelete: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: definitionName
        });
    }

    closeConfirmDelete() {
        this.setState({
            showConfirmDelete: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    deleteDefinition() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "automember",
            "definition",
            this.state.deleteName,
            "delete"
        ];

        this.setState({
            modalSpinning: true
        });
        log_cmd("deleteDefinition", "Delete the Auto Membership Plugin definition entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteDefinition", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Definition entry ${this.state.deleteName} was successfully deleted`
                    );
                    this.loadDefinitions();
                    this.closeConfirmDelete();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the definition entry removal operation - ${errMsg.desc}`
                    );
                    this.loadDefinitions();
                    this.closeConfirmDelete();
                });
    }

    addDefinition() {
        this.cmdOperation("add");
    }

    editDefinition() {
        this.cmdOperation("set");
    }

    openRegexModal(name) {
        if (name) {
            const regexEntry = this.state.regexRows.filter(row => row.cn[0] === name)[0];

            let exclusiveRegexList = [];
            let inclusiveRegexList = [];
            // Get all the attributes and matching rules now
            if (regexEntry.automemberexclusiveregex === undefined) {
                this.setState({ regexExclusive: [], _regexExclusive: [] });
            } else {
                for (const value of regexEntry.automemberexclusiveregex) {
                    exclusiveRegexList = [...exclusiveRegexList, value];
                }
                this.setState({
                    regexExclusive: exclusiveRegexList,
                    _regexExclusive: [...exclusiveRegexList],
                });
            }
            if (regexEntry.automemberinclusiveregex === undefined) {
                this.setState({ regexInclusive: [], _regexInclusive: [] });
            } else {
                for (const value of regexEntry.automemberinclusiveregex) {
                    inclusiveRegexList = [...inclusiveRegexList, value];
                }
                this.setState({
                    regexInclusive: inclusiveRegexList,
                    _regexInclusive: [...inclusiveRegexList],
                });
            }
            this.setState({
                regexEntryModalShow: true,
                newRegexEntry: false,
                regexName: regexEntry.cn[0],
                regexTargetGroup:
                    regexEntry.automembertargetgroup === undefined
                        ? ""
                        : regexEntry.automembertargetgroup[0],
                _regexName: regexEntry.cn[0],
                _regexTargetGroup:
                    regexEntry.automembertargetgroup === undefined
                        ? ""
                        : regexEntry.automembertargetgroup[0],
                saveRegexBtnDisabled: true
            });
        } else {
            this.setState({
                regexEntryModalShow: true,
                newRegexEntry: true,
                regexName: "",
                regexExclusive: [],
                regexInclusive: [],
                regexTargetGroup: "",
                _regexName: "",
                _regexExclusive: [],
                _regexInclusive: [],
                _regexTargetGroup: "",
                saveRegexBtnDisabled: true
            });
        }
    }

    cmdRegexOperation(action) {
        const {
            regexRows,
            regexName,
            regexExclusive,
            regexInclusive,
            regexTargetGroup
        } = this.state;

        let regexExists = false;
        if (regexRows.some(row => row.cn[0] === regexName)) {
            regexExists = true;
        }

        if (regexName === "" || regexTargetGroup === "") {
            this.props.addNotification("warning", "Name and Target Group are required.");
        } else {
            const regexTableKey = this.state.regexTableKey + 1;
            if (action == "add") {
                if (!regexExists) {
                    this.setState(prevState => ({
                        regexRows: [
                            ...prevState.regexRows,
                            {
                                cn: [regexName],
                                automembertargetgroup: [regexTargetGroup],
                                automemberexclusiveregex:
                                    regexExclusive.length !== 0
                                        ? [...regexExclusive]
                                        : [],
                                automemberinclusiveregex:
                                    regexInclusive.length !== 0
                                        ? [...regexInclusive]
                                        : [],
                                needsadd: true
                            }
                        ],
                        regexTableKey: regexTableKey
                    }), () => { this.validateModal() });
                } else {
                    this.props.addNotification("error", `Regex "${regexName}" already exists`);
                }
            } else if (action == "set") {
                if (regexExists) {
                    this.setState({
                        regexRows: regexRows.filter(row => row.cn[0] !== regexName)
                    });
                    this.setState(prevState => ({
                        regexRows: [
                            ...prevState.regexRows,
                            {
                                cn: [regexName],
                                automembertargetgroup: [regexTargetGroup],
                                automemberexclusiveregex:
                                    regexExclusive.length !== 0
                                        ? [...regexExclusive]
                                        : [],
                                automemberinclusiveregex:
                                    regexInclusive.length !== 0
                                        ? [...regexInclusive]
                                        : [],
                                needsupdate: true
                            }
                        ],
                        regexTableKey: regexTableKey
                    }), () => { this.validateModal() });
                } else {
                    this.props.addNotification(
                        "error",
                        `Regex "${regexName}" does not exist - "${action}" is impossible`
                    );
                }
            }
            this.closeRegexModal();
        }
    }

    deleteRegex(regexName) {
        const { regexRows } = this.state;
        const regexTableKey = this.state.regexTableKey + 1;
        if (regexRows.some(row => row.cn[0] === regexName)) {
            this.setState({
                regexRows: regexRows.filter(row => row.cn[0] !== regexName),
                regexTableKey: regexTableKey
            });
            this.setState(prevState => ({
                regexesToDelete: [...prevState.regexesToDelete, regexName]
            }), () => { this.validateModal() });
        } else {
            this.props.addNotification(
                "error",
                `Regex "${regexName}" does not exist - impossible to delete`
            );
        }
    }

    addRegex() {
        this.cmdRegexOperation("add");
    }

    editRegex() {
        this.cmdRegexOperation("set");
    }

    render() {
        const {
            regexRows,
            definitionEntryModalShow,
            definitionName,
            defaultGroup,
            scope,
            filter,
            newDefinitionEntry,
            regexEntryModalShow,
            newRegexEntry,
            groupingAttrEntry,
            groupingAttrMember,
            regexName,
            regexExclusive,
            regexInclusive,
            regexTargetGroup,
            saving,
            saveBtnDisabled,
            saveRegexBtnDisabled,
            firstLoad,
        } = this.state;

        const title = (newDefinitionEntry ? "Add" : "Edit") + " Auto Membership Plugin Definition Entry";
        const extraPrimaryProps = {};
        let saveBtnText = newDefinitionEntry ? "Add Definition" : "Save Definition";
        if (saving) {
            // Main plugin config
            saveBtnText = newDefinitionEntry ? "Adding ..." : "Saving ...";
        }

        return (
            <div className={saving || firstLoad ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    title={title}
                    isOpen={definitionEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            onClick={newDefinitionEntry ? this.addDefinition : this.editDefinition}
                            isDisabled={saveBtnDisabled || saving}
                            isLoading={saving}
                            spinnerAriaValueText={saving ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnText}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid className="ds-margin-top">
                            <GridItem className="ds-label" span={3}>
                                Definition Name
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={definitionName}
                                    type="text"
                                    id="definitionName"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="definitionName"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    isDisabled={!newDefinitionEntry}
                                    validated={this.state.error.definitionName || definitionName == "" ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Sets the subtree DN to search for entries (autoMemberScope).">
                            <GridItem className="ds-label" span={3}>
                                Subtree Scope
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={scope}
                                    type="text"
                                    id="scope"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="scope"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={this.state.error.scope || scope == "" ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Sets a standard LDAP search filter to use to search for matching entries (autoMemberFilter)">
                            <GridItem className="ds-label" span={3}>
                                Entry Filter
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={filter}
                                    type="text"
                                    id="filter"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="filter"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={this.state.error.filter || filter == "" ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Specifies the name of the member attribute in the group entry and the attribute in the object entry that supplies the member attribute value, in the format group_member_attr:entry_attr (autoMemberGroupingAttr)">
                            <GridItem className="ds-label" span={3}>
                                Grouping Attribute
                            </GridItem>
                            <GridItem span={4}>
                                <FormSelect
                                    id="groupingAttrMember"
                                    value={groupingAttrMember}
                                    onChange={(value, event) => {
                                        this.handleFieldChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                    validated={groupingAttrMember == "" ? "error" : "default"}
                                >
                                    <FormSelectOption key="no-setting" value="" label="-" />
                                    {this.attrRows}
                                </FormSelect>
                            </GridItem>
                            <GridItem className="ds-center" span={1}>
                                <ArrowRightIcon className="ds-lower-field-lg" />
                            </GridItem>
                            <GridItem span={4}>
                                <FormSelect
                                    id="groupingAttrEntry"
                                    value={groupingAttrEntry}
                                    onChange={(value, event) => {
                                        this.handleFieldChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                    validated={groupingAttrEntry == "" ? "error" : "default"}
                                >
                                    <FormSelectOption key="no-setting" value="" label="-" />
                                    {this.attrRows}
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title="Sets an optional default or fallback group to add the entry to as a member (autoMemberDefaultGroup)">
                            <GridItem className="ds-label" span={3}>
                                Default Group
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={defaultGroup}
                                    type="text"
                                    id="defaultGroup"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="defaultGroup"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={this.state.error.defaultGroup ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                    <hr />
                    <TextContent>
                        <Text className="ds-center" component={TextVariants.h3}>
                            Membership Regular Expressions
                        </Text>
                    </TextContent>
                    <Grid>
                        <GridItem span={12}>
                            <AutoMembershipRegexTable
                                rows={regexRows}
                                key={this.state.regexTableKey}
                                editConfig={this.showEditRegexModal}
                                deleteConfig={this.deleteRegex}
                            />
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem span={12}>
                            <Button
                                variant="secondary"
                                onClick={this.showAddRegexModal}
                            >
                                Add Regex
                            </Button>
                        </GridItem>
                    </Grid>
                    <hr />
                </Modal>

                <Modal
                    variant={ModalVariant.medium}
                    title="Manage Auto Membership Plugin Regex Entry"
                    isOpen={regexEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeRegexModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            isDisabled={saveRegexBtnDisabled}
                            onClick={newRegexEntry ? this.addRegex : this.editRegex}
                        >
                            Save
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeRegexModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid>
                            <GridItem className="ds-label" span={3}>
                                Regex Name
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={regexName}
                                    type="text"
                                    id="regexName"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="regexName"
                                    onChange={(str, e) => {
                                        this.handleRegexChange(e);
                                    }}
                                    isDisabled={!newRegexEntry}
                                    validated={this.state.errorRegex.regexName ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Sets a single regular expression to use to identify entries to exclude (autoMemberExclusiveRegex)">
                            <GridItem className="ds-label" span={3}>
                                Exclusive Regex
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a regex"
                                    onToggle={this.onRegexExcludeToggle}
                                    onSelect={this.onRegexExcludeSelect}
                                    onClear={this.clearRegexExcludeSelection}
                                    selections={regexExclusive}
                                    isOpen={this.state.isRegexExcludeOpen}
                                    aria-labelledby="typeAhead-excl-regex"
                                    placeholderText="Type a regex..."
                                    isCreatable
                                    onCreateOption={this.onCreateRegexExcludeOption}
                                >
                                    {[].map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title="Sets a single regular expression to use to identify entries to exclude (autoMemberExclusiveRegex)">
                            <GridItem className="ds-label" span={3}>
                                Inclusive Regex
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a regex"
                                    onToggle={this.onRegexIncludeToggle}
                                    onSelect={this.onRegexIncludeSelect}
                                    onClear={this.clearRegexIncludeSelection}
                                    selections={regexInclusive}
                                    isOpen={this.state.isRegexIncludeOpen}
                                    aria-labelledby="typeAhead-incl-regex"
                                    placeholderText="Type a regex..."
                                    isCreatable
                                    onCreateOption={this.onCreateRegexIncludeOption}
                                >
                                    {[].map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title="Sets which group to add the entry to as a member, if it meets the regular expression conditions (autoMemberTargetGroup)">
                            <GridItem className="ds-label" span={3}>
                                Target Group
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={regexTargetGroup}
                                    type="text"
                                    id="regexTargetGroup"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="regexTargetGroup"
                                    onChange={(str, e) => {
                                        this.handleRegexChange(e);
                                    }}
                                    validated={this.state.errorRegex.regexTargetGroup ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>

                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Auto Membership Plugin"
                    pluginName="Auto Membership"
                    cmdName="automember"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Grid>
                        <GridItem span={12}>
                            <AutoMembershipDefinitionTable
                                rows={this.state.definitionRows}
                                key={this.state.definitionRows}
                                editConfig={this.showEditDefinitionModal}
                                deleteConfig={this.showConfirmDelete}
                            />
                            <Button
                                variant="primary"
                                onClick={this.showAddDefinitionModal}
                            >
                                Add Definition
                            </Button>
                        </GridItem>
                    </Grid>
                </PluginBasicConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.handleChange}
                    actionHandler={this.deleteDefinition}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Autemebership Configuration"
                    mMsg="Are you sure you want to delete this configuration?"
                    mSpinningMsg="Deleting Configuration..."
                    mBtnName="Delete Configuration"
                />
            </div>
        );
    }
}

AutoMembership.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

AutoMembership.defaultProps = {
    rows: [],
    serverId: "",
};

export default AutoMembership;
