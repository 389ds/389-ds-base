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
    Checkbox,
    ControlLabel
} from "patternfly-react";
import { Typeahead } from "react-bootstrap-typeahead";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

class MemberOf extends React.Component {
    componentWillMount(prevProps) {
        this.getObjectClasses();
        this.updateFields();
    }

    componentDidUpdate(prevProps) {
        if (this.props.rows !== prevProps.rows) {
            this.updateFields();
        }
    }

    constructor(props) {
        super(props);

        this.getObjectClasses = this.getObjectClasses.bind(this);
        this.updateFields = this.updateFields.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleCheckboxChange = this.handleCheckboxChange.bind(this);
        this.openModal = this.openModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.runFixup = this.runFixup.bind(this);
        this.toggleFixupModal = this.toggleFixupModal.bind(this);

        this.state = {
            objectClasses: [],

            memberOfAttr: [],
            memberOfGroupAttr: [],
            memberOfEntryScope: "",
            memberOfEntryScopeExcludeSubtree: "",
            memberOfAutoAddOC: [],
            memberOfAllBackends: false,
            memberOfSkipNested: false,
            memberOfConfigEntry: "",
            configEntryModalShow: false,
            fixupModalShow: false,

            configDN: "",
            configAttr: [],
            configGroupAttr: [],
            configEntryScope: "",
            configEntryScopeExcludeSubtree: "",
            configAutoAddOC: [],
            configAllBackends: false,
            configSkipNested: false,
            newEntry: true,

            fixupDN: "",
            fixupFilter: ""
        };
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
                        let errMsg = JSON.parse(err);
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
        this.getObjectClasses();
        if (!this.state.memberOfConfigEntry) {
            this.setState({
                configEntryModalShow: true,
                newEntry: true,
                configDN: "",
                configAttr: [],
                configGroupAttr: [],
                configEntryScope: "",
                configEntryScopeExcludeSubtree: "",
                configAutoAddOC: [],
                configAllBackends: false,
                configSkipNested: false
            });
        } else {
            let configAttrObjectList = [];
            let configGroupAttrObjectList = [];
            let cmd = [
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
                        let configEntry = JSON.parse(content).attrs;
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: false,
                            configDN: this.state.memberOfConfigEntry,
                            configAutoAddOC:
                            configEntry["memberofautoaddoc"] === undefined
                                ? []
                                : [
                                    {
                                        id: configEntry["memberofautoaddoc"][0],
                                        label: configEntry["memberofautoaddoc"][0]
                                    }
                                ],
                            configAllBackends: !(
                                configEntry["memberofallbackends"] === undefined ||
                            configEntry["memberofallbackends"][0] == "off"
                            ),
                            configSkipNested: !(
                                configEntry["memberofskipnested"] === undefined ||
                            configEntry["memberofskipnested"][0] == "off"
                            ),
                            configConfigEntry:
                            configEntry["nsslapd-pluginConfigArea"] === undefined
                                ? ""
                                : configEntry["nsslapd-pluginConfigArea"][0],
                            configEntryScope:
                            configEntry["memberofentryscope"] === undefined
                                ? ""
                                : configEntry["memberofentryscope"][0],
                            configEntryScopeExcludeSubtree:
                            configEntry["memberofentryscopeexcludesubtree"] === undefined
                                ? ""
                                : configEntry["memberofentryscopeexcludesubtree"][0]
                        });
                        if (configEntry["memberofattr"] === undefined) {
                            this.setState({ configAttr: [] });
                        } else {
                            for (let value of configEntry["memberofattr"]) {
                                configAttrObjectList = [
                                    ...configAttrObjectList,
                                    { id: value, label: value }
                                ];
                            }
                            this.setState({ configAttr: configAttrObjectList });
                        }
                        if (configEntry["memberofgroupattr"] === undefined) {
                            this.setState({ configGroupAttr: [] });
                        } else {
                            for (let value of configEntry["memberofgroupattr"]) {
                                configGroupAttrObjectList = [
                                    ...configGroupAttrObjectList,
                                    { id: value, label: value }
                                ];
                            }
                            this.setState({
                                configGroupAttr: configGroupAttrObjectList
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
                            configEntryScope: "",
                            configEntryScopeExcludeSubtree: "",
                            configAutoAddOC: [],
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
                "--scope",
                configEntryScope || action == "add" ? configEntryScope : "delete",
                "--exclude",
                configEntryScopeExcludeSubtree || action == "add"
                    ? configEntryScopeExcludeSubtree
                    : "delete",
                "--allbackends",
                configAllBackends ? "on" : "off",
                "--skipnested",
                configSkipNested ? "on" : "off"
            ];

            cmd = [...cmd, "--autoaddoc"];
            if (configAutoAddOC.length != 0) {
                cmd = [...cmd, configAutoAddOC[0].label];
            } else if (action == "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }

            // Delete attributes if the user set an empty value to the field
            cmd = [...cmd, "--attr"];
            if (configAttr.length != 0) {
                for (let value of configAttr) {
                    cmd = [...cmd, value.label];
                }
            }
            cmd = [...cmd, "--groupattr"];
            if (configGroupAttr.length != 0) {
                for (let value of configGroupAttr) {
                    cmd = [...cmd, value.label];
                }
            }

            this.props.toggleLoadingHandler();
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
                            `Config entry ${configDN} was successfully ${action}ed`
                        );
                        this.props.pluginListHandler();
                        this.closeModal();
                        this.props.toggleLoadingHandler();
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Error during the config entry ${action} operation - ${errMsg.desc}`
                        );
                        this.props.pluginListHandler();
                        this.closeModal();
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    deleteConfig() {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "memberof",
            "config-entry",
            "delete",
            this.state.configDN
        ];

        this.props.toggleLoadingHandler();
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
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry removal operation - ${errMsg.desc}`
                    );
                    this.props.pluginListHandler();
                    this.closeModal();
                    this.props.toggleLoadingHandler();
                });
    }

    addConfig() {
        this.cmdOperation("add");
    }

    editConfig() {
        this.cmdOperation("set");
    }

    handleCheckboxChange(e) {
        this.setState({
            [e.target.id]: e.target.checked
        });
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    updateFields() {
        let memberOfAttrObjectList = [];
        let memberOfGroupAttrObjectList = [];

        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "MemberOf Plugin");

            this.setState({
                memberOfAutoAddOC:
                    pluginRow["memberofautoaddoc"] === undefined
                        ? []
                        : [
                            {
                                id: pluginRow["memberofautoaddoc"][0],
                                label: pluginRow["memberofautoaddoc"][0]
                            }
                        ],
                memberOfAllBackends: !(
                    pluginRow["memberofallbackends"] === undefined ||
                    pluginRow["memberofallbackends"][0] == "off"
                ),
                memberOfSkipNested: !(
                    pluginRow["memberofskipnested"] === undefined ||
                    pluginRow["memberofskipnested"][0] == "off"
                ),
                memberOfConfigEntry:
                    pluginRow["nsslapd-pluginConfigArea"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginConfigArea"][0],
                memberOfEntryScope:
                    pluginRow["memberofentryscope"] === undefined
                        ? ""
                        : pluginRow["memberofentryscope"][0],
                memberOfEntryScopeExcludeSubtree:
                    pluginRow["memberofentryscopeexcludesubtree"] === undefined
                        ? ""
                        : pluginRow["memberofentryscopeexcludesubtree"][0]
            });
            if (pluginRow["memberofattr"] === undefined) {
                this.setState({ memberOfAttr: [] });
            } else {
                for (let value of pluginRow["memberofattr"]) {
                    memberOfAttrObjectList = [
                        ...memberOfAttrObjectList,
                        { id: value, label: value }
                    ];
                }
                this.setState({ memberOfAttr: memberOfAttrObjectList });
            }
            if (pluginRow["memberofgroupattr"] === undefined) {
                this.setState({ memberOfGroupAttr: [] });
            } else {
                for (let value of pluginRow["memberofgroupattr"]) {
                    memberOfGroupAttrObjectList = [
                        ...memberOfGroupAttrObjectList,
                        { id: value, label: value }
                    ];
                }
                this.setState({
                    memberOfGroupAttr: memberOfGroupAttrObjectList
                });
            }
        }
    }

    getObjectClasses() {
        const oc_cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "objectclasses",
            "list"
        ];
        log_cmd("getObjectClasses", "Get objectClasses", oc_cmd);
        cockpit
                .spawn(oc_cmd, { superuser: true, err: "message" })
                .done(content => {
                    const ocContent = JSON.parse(content);
                    let ocs = [];
                    for (let content of ocContent["items"]) {
                        ocs.push({
                            id: content.name,
                            label: content.name
                        });
                    }
                    this.setState({
                        objectClasses: ocs
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification("error", `Failed to get objectClasses - ${errMsg.desc}`);
                });
    }

    render() {
        const {
            objectClasses,
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
            fixupFilter
        } = this.state;

        let specificPluginCMD = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "memberof",
            "set",
            "--scope",
            memberOfEntryScope || "delete",
            "--exclude",
            memberOfEntryScopeExcludeSubtree || "delete",
            "--config-entry",
            memberOfConfigEntry || "delete",
            "--allbackends",
            memberOfAllBackends ? "on" : "off",
            "--skipnested",
            memberOfSkipNested ? "on" : "off"
        ];

        specificPluginCMD = [...specificPluginCMD, "--autoaddoc"];
        if (memberOfAutoAddOC.length != 0) {
            specificPluginCMD = [...specificPluginCMD, memberOfAutoAddOC[0].label];
        } else {
            specificPluginCMD = [...specificPluginCMD, "delete"];
        }

        // Delete attributes if the user set an empty value to the field
        specificPluginCMD = [...specificPluginCMD, "--attr"];
        if (memberOfAttr.length != 0) {
            for (let value of memberOfAttr) {
                specificPluginCMD = [...specificPluginCMD, value.label];
            }
        } else {
            specificPluginCMD = [...specificPluginCMD, "delete"];
        }

        specificPluginCMD = [...specificPluginCMD, "--groupattr"];
        if (memberOfGroupAttr.length != 0) {
            for (let value of memberOfGroupAttr) {
                specificPluginCMD = [...specificPluginCMD, value.label];
            }
        } else {
            specificPluginCMD = [...specificPluginCMD, "delete"];
        }

        return (
            <div>
                <Modal show={fixupModalShow} onHide={this.toggleFixupModal}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={this.toggleFixupModal}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>Fixup MemberOf Task</Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup controlId="fixupDN" key="fixupDN">
                                            <Col sm={3}>
                                                <ControlLabel title="Base DN that contains entries to fix up">
                                                    Base DN
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={fixupDN}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup controlId="fixupFilter" key="fixupFilter">
                                            <Col sm={3}>
                                                <ControlLabel title="Filter for entries to fix up. If omitted, all entries with objectclass inetuser/inetadmin/nsmemberof under the specified base will have their memberOf attribute regenerated.">
                                                    Filter DN
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={fixupFilter}
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
                                onClick={this.toggleFixupModal}
                            >
                                Cancel
                            </Button>
                            <Button bsStyle="primary" onClick={this.runFixup}>
                                Run
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <Modal show={configEntryModalShow} onHide={this.closeModal}>
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
                            <Modal.Title>Manage MemberOf Plugin Shared Config Entry</Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup controlId="configDN">
                                            <Col sm={3}>
                                                <ControlLabel title="The config entry full DN">
                                                    Config DN
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={configDN}
                                                    onChange={this.handleFieldChange}
                                                    disabled={!newEntry}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="configAttr"
                                            controlId="configAttr"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Specifies the attribute in the user entry for the Directory Server to manage to reflect group membership (memberOfAttr)"
                                            >
                                                Attribute
                                            </Col>
                                            <Col sm={9}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={values => {
                                                        this.setState({
                                                            configAttr: values
                                                        });
                                                    }}
                                                    selected={configAttr}
                                                    newSelectionPrefix="Add a member: "
                                                    options={[
                                                        {
                                                            id: "memberOf",
                                                            label: "memberOf"
                                                        }
                                                    ]}
                                                    placeholder="Type a member attribute..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="configGroupAttr"
                                            controlId="configGroupAttr"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Specifies the attribute in the group entry to use to identify the DNs of group members (memberOfGroupAttr)"
                                            >
                                                Group Attribute
                                            </Col>
                                            <Col sm={9}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={values => {
                                                        this.setState({
                                                            configGroupAttr: values
                                                        });
                                                    }}
                                                    selected={configGroupAttr}
                                                    newSelectionPrefix="Add a group member: "
                                                    options={[
                                                        {
                                                            id: "member",
                                                            label: "member"
                                                        }
                                                    ]}
                                                    placeholder="Type a member group attribute..."
                                                />
                                            </Col>
                                        </FormGroup>
                                    </Form>
                                </Col>
                            </Row>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup
                                            key="configEntryScope"
                                            controlId="configEntryScope"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Specifies backends or multiple-nested suffixes for the MemberOf plug-in to work on (memberOfEntryScope)"
                                            >
                                                Entry Scope
                                            </Col>
                                            <Col sm={6}>
                                                <FormControl
                                                    type="text"
                                                    value={configEntryScope}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                            <Col sm={3}>
                                                <Checkbox
                                                    id="configAllBackends"
                                                    checked={configAllBackends}
                                                    onChange={this.handleCheckboxChange}
                                                    title="Specifies whether to search the local suffix for user entries on all available suffixes (memberOfAllBackends)"
                                                >
                                                    All Backends
                                                </Checkbox>
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="configEntryScopeExcludeSubtree"
                                            controlId="configEntryScopeExcludeSubtree"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Specifies backends or multiple-nested suffixes for the MemberOf plug-in to exclude (memberOfEntryScopeExcludeSubtree)"
                                            >
                                                Entry Scope Exclude Subtree
                                            </Col>
                                            <Col sm={6}>
                                                <FormControl
                                                    type="text"
                                                    value={configEntryScopeExcludeSubtree}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                            <Col sm={3}>
                                                <Checkbox
                                                    id="configSkipNested"
                                                    checked={configSkipNested}
                                                    onChange={this.handleCheckboxChange}
                                                    title="Specifies wherher to skip nested groups or not (memberOfSkipNested)"
                                                >
                                                    Skip Nested
                                                </Checkbox>
                                            </Col>
                                        </FormGroup>
                                    </Form>
                                </Col>
                            </Row>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup controlId="configAutoAddOC" disabled={false}>
                                            <Col sm={3}>
                                                <ControlLabel title="If an entry does not have an object class that allows the memberOf attribute then the memberOf plugin will automatically add the object class listed in the memberOfAutoAddOC parameter">
                                                    Auto Add OC
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={9}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            configAutoAddOC: value
                                                        });
                                                    }}
                                                    selected={configAutoAddOC}
                                                    options={objectClasses}
                                                    newSelectionPrefix="Add a memberOf objectClass: "
                                                    placeholder="Type an objectClass..."
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
                                onClick={this.closeModal}
                            >
                                Cancel
                            </Button>
                            <Button
                                bsStyle="primary"
                                onClick={this.deleteConfig}
                                disabled={newEntry}
                            >
                                Delete
                            </Button>
                            <Button bsStyle="primary" onClick={this.editConfig} disabled={newEntry}>
                                Save
                            </Button>
                            <Button bsStyle="primary" onClick={this.addConfig} disabled={!newEntry}>
                                Add
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="MemberOf Plugin"
                    pluginName="MemberOf"
                    cmdName="memberof"
                    specificPluginCMD={specificPluginCMD}
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={9}>
                            <Form horizontal>
                                <FormGroup
                                    key="memberOfAttr"
                                    controlId="memberOfAttr"
                                    disabled={false}
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="Specifies the attribute in the user entry for the Directory Server to manage to reflect group membership (memberOfAttr)"
                                    >
                                        Attribute
                                    </Col>
                                    <Col sm={9}>
                                        <Typeahead
                                            allowNew
                                            multiple
                                            onChange={values => {
                                                this.setState({
                                                    memberOfAttr: values
                                                });
                                            }}
                                            selected={memberOfAttr}
                                            newSelectionPrefix="Add a member: "
                                            options={[
                                                {
                                                    id: "member",
                                                    label: "member"
                                                },
                                                {
                                                    id: "memberCertificate",
                                                    label: "memberCertificate"
                                                },
                                                {
                                                    id: "uniqueMember",
                                                    label: "uniqueMember"
                                                }
                                            ]}
                                            placeholder="Type a member attribute..."
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="memberOfGroupAttr"
                                    controlId="memberOfGroupAttr"
                                    disabled={false}
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="Specifies the attribute in the group entry to use to identify the DNs of group members (memberOfGroupAttr)"
                                    >
                                        Group Attribute
                                    </Col>
                                    <Col sm={9}>
                                        <Typeahead
                                            allowNew
                                            multiple
                                            onChange={values => {
                                                this.setState({
                                                    memberOfGroupAttr: values
                                                });
                                            }}
                                            selected={memberOfGroupAttr}
                                            newSelectionPrefix="Add a group member: "
                                            options={[
                                                {
                                                    id: "groupOfNames",
                                                    label: "groupOfNames"
                                                },
                                                {
                                                    id: "groupOfURLs",
                                                    label: "groupOfURLs"
                                                },
                                                {
                                                    id: "groupOfUniqueNames",
                                                    label: "groupOfUniqueNames"
                                                },
                                                {
                                                    id: "groupOfCertificates",
                                                    label: "groupOfCertificates"
                                                }
                                            ]}
                                            placeholder="Type a member group attribute..."
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={9}>
                            <Form horizontal>
                                <FormGroup
                                    key="memberOfEntryScope"
                                    controlId="memberOfEntryScope"
                                    disabled={false}
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="Specifies backends or multiple-nested suffixes for the MemberOf plug-in to work on (memberOfEntryScope)"
                                    >
                                        Entry Scope
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={memberOfEntryScope}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                    <Col sm={3}>
                                        <Checkbox
                                            id="memberOfAllBackends"
                                            checked={memberOfAllBackends}
                                            onChange={this.handleCheckboxChange}
                                            title="Specifies whether to search the local suffix for user entries on all available suffixes (memberOfAllBackends)"
                                        >
                                            All Backends
                                        </Checkbox>
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="memberOfEntryScopeExcludeSubtree"
                                    controlId="memberOfEntryScopeExcludeSubtree"
                                    disabled={false}
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="Specifies backends or multiple-nested suffixes for the MemberOf plug-in to exclude (memberOfEntryScopeExcludeSubtree)"
                                    >
                                        Entry Scope Exclude Subtree
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={memberOfEntryScopeExcludeSubtree}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                    <Col sm={3}>
                                        <Checkbox
                                            id="memberOfSkipNested"
                                            checked={memberOfSkipNested}
                                            onChange={this.handleCheckboxChange}
                                            title="Specifies wherher to skip nested groups or not (memberOfSkipNested)"
                                        >
                                            Skip Nested
                                        </Checkbox>
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={9}>
                            <Form horizontal>
                                <FormGroup
                                    key="memberOfConfigEntry"
                                    controlId="memberOfConfigEntry"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="The value to set as nsslapd-pluginConfigArea"
                                    >
                                        Shared Config Entry
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={memberOfConfigEntry}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                    <Col sm={3}>
                                        <Button
                                            bsSize="large"
                                            bsStyle="primary"
                                            onClick={this.openModal}
                                        >
                                            Manage
                                        </Button>
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={9}>
                            <Form horizontal>
                                <FormGroup controlId="memberOfAutoAddOC" disabled={false}>
                                    <Col
                                        sm={3}
                                        title="If an entry does not have an object class that allows the memberOf attribute then the memberOf plugin will automatically add the object class listed in the memberOfAutoAddOC parameter"
                                    >
                                        <ControlLabel>Auto Add OC</ControlLabel>
                                    </Col>
                                    <Col sm={9}>
                                        <Typeahead
                                            allowNew
                                            onChange={value => {
                                                this.setState({
                                                    memberOfAutoAddOC: value
                                                });
                                            }}
                                            selected={memberOfAutoAddOC}
                                            options={objectClasses}
                                            newSelectionPrefix="Add a memberOf objectClass: "
                                            placeholder="Type an objectClass..."
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={9}>
                            <Button
                                bsSize="large"
                                bsStyle="primary"
                                onClick={this.toggleFixupModal}
                            >
                                Run Fixup Task
                            </Button>
                        </Col>
                    </Row>
                </PluginBasicConfig>
            </div>
        );
    }
}

MemberOf.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

MemberOf.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default MemberOf;
