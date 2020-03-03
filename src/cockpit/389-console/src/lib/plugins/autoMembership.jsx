import cockpit from "cockpit";
import React from "react";
import {
    Icon,
    Modal,
    Button,
    Row,
    Col,
    Form,
    noop,
    FormGroup,
    FormControl,
    ControlLabel
} from "patternfly-react";
import { Typeahead } from "react-bootstrap-typeahead";
import { AutoMembershipDefinitionTable, AutoMembershipRegexTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

class AutoMembership extends React.Component {
    componentWillMount() {
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

            definitionName: "",
            defaultGroup: "",
            filter: "",
            groupingAttrMember: [],
            groupingAttrEntry: "",
            scope: "",

            regexName: "",
            regexExclusive: [],
            regexInclusive: [],
            regexTargetGroup: "",

            newDefinitionEntry: false,
            newRegexEntry: false,
            definitionEntryModalShow: false,
            regexEntryModalShow: false
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
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
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
        this.props.toggleLoadingHandler();
        log_cmd("loadDefinitions", "Get Auto Membership Plugin definitions", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    this.setState({
                        definitionRows: myObject.items.map(item => JSON.parse(item).attrs)
                    });
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadDefinitions failed", errMsg.desc);
                    }
                    this.props.toggleLoadingHandler();
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
        this.props.toggleLoadingHandler();
        log_cmd("loadRegexes", "Get Auto Membership Plugin regexes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    this.setState({
                        regexRows: myObject.items.map(item => JSON.parse(item).attrs)
                    });
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadRegexes failed", errMsg.desc);
                    }
                    this.props.toggleLoadingHandler();
                });
    }

    showEditDefinitionModal(rowData) {
        this.openModal(rowData.cn[0]);
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
                groupingAttrMember: [],
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
                                groupingAttrMember: [],
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
                            groupingAttrMember: [],
                            groupingAttrEntry: "",
                            scope: ""
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    showEditRegexModal(rowData) {
        this.openRegexModal(rowData.cn[0]);
    }

    showAddRegexModal(rowData) {
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
            groupingAttrMember.length == 0 ||
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
                cmd = [...cmd, `${groupingAttrMember[0].id}:${groupingAttrEntry}`];
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

    deleteDefinition(rowData) {
        let definitionName = rowData.cn[0];
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "automember",
            "definition",
            definitionName,
            "delete"
        ];

        this.props.toggleLoadingHandler();
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
                        `Definition entry ${definitionName} was successfully deleted`
                    );
                    this.loadDefinitions();
                    this.closeModal();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the definition entry removal operation - ${errMsg.desc}`
                    );
                    this.loadDefinitions();
                    this.closeModal();
                    this.props.toggleLoadingHandler();
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
                        ]
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
                        ]
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

    deleteRegex(rowData) {
        const { regexRows } = this.state;
        const regexName = rowData.cn[0];

        if (regexRows.some(row => row.cn[0] === regexName)) {
            this.setState({
                regexRows: regexRows.filter(row => row.cn[0] !== regexName)
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

        return (
            <div>
                <Modal show={definitionEntryModalShow} onHide={this.closeModal}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={this.closeModal}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>
                                {newDefinitionEntry ? "Add" : "Edit"} Auto Membership Plugin
                                Definition Entry
                            </Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
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
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            groupingAttrMember: value
                                                        });
                                                    }}
                                                    selected={groupingAttrMember}
                                                    options={attributes}
                                                    newSelectionPrefix="Set an attribute: "
                                                    placeholder="Type an attribute..."
                                                />
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
                            <Row>
                                <Col sm={12}>
                                    <AutoMembershipRegexTable
                                        rows={regexRows}
                                        editConfig={this.showEditRegexModal}
                                        deleteConfig={this.deleteRegex}
                                    />
                                </Col>
                            </Row>
                            <Row>
                                <Col sm={12}>
                                    <Button
                                        className="ds-margin-top"
                                        bsStyle="primary"
                                        onClick={this.showAddRegexModal}
                                    >
                                        Add Regex
                                    </Button>
                                </Col>
                            </Row>
                        </Modal.Body>
                        <Modal.Footer>
                            <Button
                                bsStyle="default"
                                className="btn-cancel"
                                onClick={this.closeModal}
                            >
                                Cancel
                            </Button>
                            <Button
                                bsStyle="primary"
                                onClick={
                                    newDefinitionEntry ? this.addDefinition : this.editDefinition
                                }
                            >
                                Save
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <Modal show={regexEntryModalShow} onHide={this.closeRegexModal}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={this.closeRegexModal}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>Manage Auto Membership Plugin Regex Entry</Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
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
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={value => {
                                                        this.setState({
                                                            regexExclusive: value
                                                        });
                                                    }}
                                                    selected={regexExclusive}
                                                    options={[]}
                                                    newSelectionPrefix="Set an exclusive regex: "
                                                    placeholder="Type a regex..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="regexInclusive" controlId="regexInclusive">
                                            <Col componentClass={ControlLabel} sm={3} title="Sets a single regular expression to use to identify entries to exclude (autoMemberExclusiveRegex)">
                                                Inclusive Regex
                                            </Col>
                                            <Col sm={9}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={value => {
                                                        this.setState({
                                                            regexInclusive: value
                                                        });
                                                    }}
                                                    selected={regexInclusive}
                                                    options={[]}
                                                    newSelectionPrefix="Set an inclusive regex: "
                                                    placeholder="Type a regex..."
                                                />
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
                        </Modal.Body>
                        <Modal.Footer>
                            <Button
                                bsStyle="default"
                                className="btn-cancel"
                                onClick={this.closeRegexModal}
                            >
                                Cancel
                            </Button>
                            <Button
                                bsStyle="primary"
                                onClick={newRegexEntry ? this.addRegex : this.editRegex}
                            >
                                Save
                            </Button>
                        </Modal.Footer>
                    </div>
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
                                editConfig={this.showEditDefinitionModal}
                                deleteConfig={this.deleteDefinition}
                            />
                            <Button
                                className="ds-margin-top"
                                bsStyle="primary"
                                onClick={this.showAddDefinitionModal}
                            >
                                Add Definition
                            </Button>
                        </Col>
                    </Row>
                </PluginBasicConfig>
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
