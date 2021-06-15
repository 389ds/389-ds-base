import cockpit from "cockpit";
import React from "react";
import {
    Row,
    Col,
    Form,
    FormGroup,
    FormControl,
    ControlLabel
} from "patternfly-react";
import {
    Button,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    Select,
    SelectOption,
    SelectVariant,
    // TextInput,
    noop
} from "@patternfly/react-core";
import { AutoMembershipDefinitionTable, AutoMembershipRegexTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
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
            regexesToDelete: [],
            attributes: [],
            modalSpinning: false,
            modalChecked: false,

            definitionName: "",
            defaultGroup: "",
            filter: "",
            groupingAttrMember: [],
            groupingAttrEntry: "",
            scope: "",

            regexName: "",
            regexExclusive: [],
            excludeOptions: [],
            isRegexExcludeOpen: false,
            regexTableKey: 0,

            regexInclusive: [],
            includeOptions: [],
            isRegexIncludeOpen: false,

            regexTargetGroup: "",
            isGroupAttrOpen: false,

            newDefinitionEntry: false,
            newRegexEntry: false,
            definitionEntryModalShow: false,
            regexEntryModalShow: false,
            showConfirmDelete: false,

            // Select Typeahead
            subtreeEntriesOcSelectExpanded: false,
        };

        this.onGroupAttrSelect = (event, selection) => {
            this.setState({
                groupingAttrMember: selection,
                isGroupAttrOpen: false
            });
        };
        this.onGroupAttrToggle = isGroupAttrOpen => {
            this.setState({
                isGroupAttrOpen
            });
        };
        this.clearGroupAttrSelection = () => {
            this.setState({
                groupingAttrMember: "",
                isGroupAttrOpen: false
            });
        };

        this.onRegexExcludeSelect = (event, selection) => {
            const { regexExclusive } = this.state;
            if (regexExclusive.includes(selection)) {
                this.setState(
                    prevState => ({
                        regexExclusive: prevState.regexExclusive.filter(item => item !== selection),
                        isRegexExcludeOpen: false
                    })
                );
            } else {
                this.setState(
                    prevState => ({
                        regexExclusive: [...prevState.regexExclusive, selection],
                        isRegexExcludeOpen: false,
                    })
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
            });
        };

        this.onRegexIncludeSelect = (event, selection) => {
            const { regexInclusive } = this.state;
            if (regexInclusive.includes(selection)) {
                this.setState(
                    prevState => ({
                        regexInclusive: prevState.regexInclusive.filter(item => item !== selection),
                        isRegexIncludeOpen: false
                    })
                );
            } else {
                this.setState(
                    prevState => ({
                        regexInclusive: [...prevState.regexInclusive, selection],
                        isRegexIncludeOpen: false
                    })
                );
            }
        };
        this.onCreateRegexIncludeOption = newValue => {
            this.setState({
                includeOptions: [...this.state.includeOptions, newValue]
            });
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
            });
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);

        this.loadDefinitions = this.loadDefinitions.bind(this);
        this.loadRegexes = this.loadRegexes.bind(this);
        this.getAttributes = this.getAttributes.bind(this);

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

    handleFieldChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    loadDefinitions() {
        this.setState({
            firstLoad: false
        });
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
                    let myObject = JSON.parse(content);
                    this.setState({
                        definitionRows: myObject.items.map(item => item.attrs)
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadDefinitions failed", errMsg.desc);
                    }
                });
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
                    let myObject = JSON.parse(content);
                    let regexTableKey = this.state.regexTableKey + 1;
                    this.setState({
                        regexRows: myObject.items.map(item => item.attrs),
                        regexTableKey: regexTableKey
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
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
        this.getAttributes();
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
                scope: ""
            });
        } else {
            this.loadRegexes(name);
            let cmd = [
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
                        let definitionEntry = JSON.parse(content).attrs;
                        this.setState({
                            definitionEntryModalShow: true,
                            newDefinitionEntry: false,
                            definitionName:
                            definitionEntry["cn"] === undefined ? "" : definitionEntry["cn"][0],
                            defaultGroup:
                            definitionEntry["automemberdefaultgroup"] === undefined
                                ? ""
                                : definitionEntry["automemberdefaultgroup"][0],
                            filter:
                            definitionEntry["automemberfilter"] === undefined
                                ? ""
                                : definitionEntry["automemberfilter"][0],
                            scope:
                            definitionEntry["automemberscope"] === undefined
                                ? ""
                                : definitionEntry["automemberscope"][0]
                        });

                        if (definitionEntry["automembergroupingattr"] === undefined) {
                            this.setState({
                                groupingAttrMember: "",
                                groupingAttrEntry: ""
                            });
                        } else {
                            let groupingAttr = definitionEntry["automembergroupingattr"][0];
                            this.setState({
                                groupingAttrMember: [groupingAttr.split(":")[0]],
                                groupingAttrEntry: groupingAttr.split(":")[1]
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
                            scope: ""
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
        this.setState({ definitionEntryModalShow: false });
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
            if (groupingAttrMember.length != 0 && groupingAttrEntry.length != 0) {
                cmd = [...cmd, `${groupingAttrMember}:${groupingAttrEntry}`];
            } else if (action == "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }

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
                        let errMsg = JSON.parse(err);
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

        for (let regexToDelete of regexesToDelete) {
            let cmd = [
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
                        let errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Error during the regex "${regexToDelete}" entry delete operation - ${
                                errMsg.desc
                            }`
                        );
                    });
        }

        for (let row of regexRows) {
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

            let regexTargetGroup = row.automembertargetgroup[0];
            let regexExclusive = row.automemberexclusiveregex;
            let regexInclusive = row.automemberinclusiveregex;

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
                    for (let regex of regexExclusive) {
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
                    for (let regex of regexInclusive) {
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
                        let errMsg = JSON.parse(err);
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
        let cmd = [
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
                    this.closeModal();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the definition entry removal operation - ${errMsg.desc}`
                    );
                    this.loadDefinitions();
                    this.closeModal();
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
            let regexEntry = this.state.regexRows.filter(row => row.cn[0] === name)[0];

            let exclusiveRegexList = [];
            let inclusiveRegexList = [];
            // Get all the attributes and matching rules now
            if (regexEntry["automemberexclusiveregex"] === undefined) {
                this.setState({ regexExclusive: [] });
            } else {
                for (let value of regexEntry["automemberexclusiveregex"]) {
                    exclusiveRegexList = [...exclusiveRegexList, value];
                }
                this.setState({ regexExclusive: exclusiveRegexList });
            }
            if (regexEntry["automemberinclusiveregex"] === undefined) {
                this.setState({ regexInclusive: [] });
            } else {
                for (let value of regexEntry["automemberinclusiveregex"]) {
                    inclusiveRegexList = [...inclusiveRegexList, value];
                }
                this.setState({ regexInclusive: inclusiveRegexList });
            }
            this.setState({
                regexEntryModalShow: true,
                newRegexEntry: false,
                regexName: regexEntry["cn"][0],
                regexTargetGroup:
                    regexEntry["automembertargetgroup"] === undefined
                        ? ""
                        : regexEntry["automembertargetgroup"][0]
            });
        } else {
            this.setState({
                regexEntryModalShow: true,
                newRegexEntry: true,
                regexName: "",
                regexExclusive: [],
                regexInclusive: [],
                regexTargetGroup: ""
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
            let regexTableKey = this.state.regexTableKey + 1;
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
                                        ? regexExclusive.map(regex => regex)
                                        : [],
                                automemberinclusiveregex:
                                    regexInclusive.length !== 0
                                        ? regexInclusive.map(regex => regex)
                                        : [],
                                needsadd: true
                            }
                        ],
                        regexTableKey: regexTableKey
                    }));
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
                                        ? regexExclusive.map(regex => regex)
                                        : [],
                                automemberinclusiveregex:
                                    regexInclusive.length !== 0
                                        ? regexInclusive.map(regex => regex)
                                        : [],
                                needsupdate: true
                            }
                        ],
                        regexTableKey: regexTableKey
                    }));
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
        let regexTableKey = this.state.regexTableKey + 1;
        if (regexRows.some(row => row.cn[0] === regexName)) {
            this.setState({
                regexRows: regexRows.filter(row => row.cn[0] !== regexName),
                regexTableKey: regexTableKey
            });
            this.setState(prevState => ({
                regexesToDelete: [...prevState.regexesToDelete, regexName]
            }));
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

    getAttributes() {
        const attr_cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "attributetypes",
            "list"
        ];
        log_cmd("getAttributes", "Get attrs", attr_cmd);
        cockpit
                .spawn(attr_cmd, { superuser: true, err: "message" })
                .done(content => {
                    const attrContent = JSON.parse(content);
                    let attrs = [];
                    for (let content of attrContent["items"]) {
                        attrs.push(content.name[0]);
                    }
                    this.setState({
                        attributes: attrs
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification("error", `Failed to get attributes - ${errMsg.desc}`);
                });
    }

    render() {
        const {
            regexRows,
            definitionEntryModalShow,
            definitionName,
            newDefinitionEntry,
            regexEntryModalShow,
            newRegexEntry,
            attributes,
            groupingAttrEntry,
            groupingAttrMember,
            regexName,
            regexExclusive,
            regexInclusive,
            regexTargetGroup
        } = this.state;

        const modalDefinitionFields = {
            defaultGroup: {
                name: "Default Group",
                value: this.state.defaultGroup,
                help: `Sets default or fallback group to add the entry to as a member attribute in group entry (autoMemberDefaultGroup)`
            },
            scope: {
                name: "Scope",
                value: this.state.scope,
                help: "Sets the subtree DN to search for entries (autoMemberScope)"
            },
            filter: {
                name: "Filter",
                value: this.state.filter,
                help:
                    "Sets a standard LDAP search filter to use to search for matching entries (autoMemberFilter)"
            }
        };

        let title = (newDefinitionEntry ? "Add" : "Edit") + " Auto Membership Plugin Definition Entry";

        return (
            <div>
                <Modal
                    variant={ModalVariant.large}
                    title={title}
                    isOpen={definitionEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeModal}
                    actions={[
                        <Button key="confirm" variant="primary" onClick={newDefinitionEntry ? this.addDefinition : this.editDefinition}>
                            Save
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Row className="ds-margin-top">
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup key="definitionName" controlId="definitionName">
                                    <Col componentClass={ControlLabel} sm={3}>
                                        Definition Name
                                    </Col>
                                    <Col sm={9}>
                                        <FormControl
                                            required
                                            type="text"
                                            value={definitionName}
                                            onChange={this.handleFieldChange}
                                            disabled={!newDefinitionEntry}
                                        />
                                    </Col>
                                </FormGroup>
                                {Object.entries(modalDefinitionFields).map(
                                    ([id, content]) => (
                                        <FormGroup key={id} controlId={id}>
                                            <Col componentClass={ControlLabel} sm={3} title={content.help}>
                                                {content.name}
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={content.value}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                    )
                                )}
                                <FormGroup
                                    key="groupingAttrEntry"
                                    controlId="groupingAttrEntry"
                                >
                                    <Col componentClass={ControlLabel} sm={3} title="Specifies the name of the member attribute in the group entry and the attribute in the object entry that supplies the member attribute value, in the format group_member_attr:entry_attr (autoMemberGroupingAttr)">
                                        Grouping Attributes
                                    </Col>
                                    <Col sm={4}>
                                        <Select
                                            variant={SelectVariant.typeahead}
                                            onToggle={(isExpanded) => {
                                                this.setState({
                                                    groupingAttrMemberSelectExpanded: isExpanded
                                                });
                                            }}
                                            onSelect={(e, values) => {
                                                this.setState({
                                                    groupingAttrMember: values
                                                });
                                            }}
                                            onClear={e => {
                                                this.setState({
                                                    groupingAttrMemberSelectExpanded: false,
                                                    groupingAttrMember: []
                                                });
                                            }}
                                            selections={groupingAttrMember}
                                            isOpen={this.state.groupingAttrMemberSelectExpanded}
                                            placeholderText="Type an attribute..."
                                            noResultsFoundText="There are no matching entries"
                                            isCreatable
                                            onCreateOption={(values) => {
                                                this.setState({
                                                    groupingAttrMember: values
                                                });
                                            }}
                                            >
                                            {attributes.map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                                ))}
                                        </Select>
                                    </Col>
                                    <Col sm={1}>:</Col>
                                    <Col sm={4}>
                                        <FormControl
                                            required
                                            type="text"
                                            value={groupingAttrEntry}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
                    <hr />
                    <h5 className="ds-center">Membership Regular Expressions</h5>
                    <Row>
                        <Col sm={12}>
                            <AutoMembershipRegexTable
                                rows={regexRows}
                                key={this.state.regexTableKey}
                                editConfig={this.showEditRegexModal}
                                deleteConfig={this.deleteRegex}
                            />
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={12}>
                            <Button
                                className="ds-margin-top"
                                variant="secondary"
                                onClick={this.showAddRegexModal}
                            >
                                Add Regex
                            </Button>
                        </Col>
                    </Row>
                    <hr />
                </Modal>

                <Modal
                    variant={ModalVariant.medium}
                    title="Manage Auto Membership Plugin Regex Entry"
                    isOpen={regexEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeRegexModal}
                    actions={[
                        <Button key="confirm" variant="primary" onClick={newRegexEntry ? this.addRegex : this.editRegex}>
                            Save
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeRegexModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Row>
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup key="regexName" controlId="regexName">
                                    <Col componentClass={ControlLabel} sm={3}>
                                        Regex Name
                                    </Col>
                                    <Col sm={9}>
                                        <FormControl
                                            required
                                            type="text"
                                            value={regexName}
                                            onChange={this.handleFieldChange}
                                            disabled={!newRegexEntry}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="regexExclusive" controlId="regexExclusive">
                                    <Col componentClass={ControlLabel} sm={3} title="Sets a single regular expression to use to identify entries to exclude (autoMemberExclusiveRegex)">
                                        Exclusive Regex
                                    </Col>
                                    <Col sm={9}>
                                        <Select
                                            variant={SelectVariant.typeaheadMulti}
                                            onToggle={(isExpanded) => {
                                                this.setState({
                                                    regexExclusiveSelectExpanded: isExpanded
                                                });
                                            }}
                                            onSelect={(e, values) => {
                                                if (!this.state.regexExclusive.includes(values)) {
                                                    this.setState({
                                                        regexExclusive: [...this.state.regexExclusive, values]
                                                    });
                                                }
                                            }}
                                            onClear={e => {
                                                this.setState({
                                                    regexExclusiveSelectExpanded: false,
                                                    regexExclusive: []
                                                });
                                            }}
                                            selections={regexExclusive}
                                            isOpen={this.state.regexExclusiveSelectExpanded}
                                            placeholderText="Type a regex..."
                                            isCreatable
                                            onCreateOption={(values) => {
                                                if (!this.state.regexExclusive.includes(values)) {
                                                    this.setState({
                                                        regexExclusive: [...this.state.regexExclusive, values]
                                                    });
                                                }
                                            }}
                                            >
                                            {[].map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                                ))}
                                        </Select>
                                    </Col>
                                </FormGroup>
                                <FormGroup key="regexInclusive" controlId="regexInclusive">
                                    <Col componentClass={ControlLabel} sm={3} title="Sets a single regular expression to use to identify entries to exclude (autoMemberExclusiveRegex)">
                                        Inclusive Regex
                                    </Col>
                                    <Col sm={9}>
                                        <Select
                                            variant={SelectVariant.typeaheadMulti}
                                            onToggle={(isExpanded) => {
                                                this.setState({
                                                    regexInclusiveSelectExpanded: isExpanded
                                                });
                                            }}
                                            onSelect={(e, values) => {
                                                if (!this.state.regexInclusive.includes(values)) {
                                                    this.setState({
                                                        regexInclusive: [...this.state.regexInclusive, values]
                                                    });
                                                }
                                            }}
                                            onClear={e => {
                                                this.setState({
                                                    regexInclusiveSelectExpanded: false,
                                                    regexInclusive: []
                                                });
                                            }}
                                            selections={regexInclusive}
                                            isOpen={this.state.regexInclusiveSelectExpanded}
                                            placeholderText="Type a regex..."
                                            isCreatable
                                            onCreateOption={(values) => {
                                                if (!this.state.regexInclusive.includes(values)) {
                                                    this.setState({
                                                        regexInclusive: [...this.state.regexInclusive, values]
                                                    });
                                                }
                                            }}
                                            >
                                            {[].map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                            ))}
                                        </Select>
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="regexTargetGroup"
                                    controlId="regexTargetGroup"
                                >
                                    <Col componentClass={ControlLabel} sm={3} title="Sets which group to add the entry to as a member, if it meets the regular expression conditions (autoMemberTargetGroup)">
                                        Target Group
                                    </Col>
                                    <Col sm={9}>
                                        <FormControl
                                            required
                                            type="text"
                                            value={regexTargetGroup}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
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
                    <Row>
                        <Col sm={12}>
                            <AutoMembershipDefinitionTable
                                rows={this.state.definitionRows}
                                key={this.state.definitionRows}
                                editConfig={this.showEditDefinitionModal}
                                deleteConfig={this.showConfirmDelete}
                            />
                            <Button
                                className="ds-margin-top"
                                variant="primary"
                                onClick={this.showAddDefinitionModal}
                            >
                                Add Definition
                            </Button>
                        </Col>
                    </Row>
                </PluginBasicConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.handleFieldChange}
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
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default AutoMembership;
