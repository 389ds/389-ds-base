import cockpit from "cockpit";
import React from "react";
import {
    noop,
    FormGroup,
    FormControl,
    Modal,
    Icon,
    Row,
    Col,
    Form,
    Button,
    ControlLabel
} from "patternfly-react";
import { Typeahead } from "react-bootstrap-typeahead";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

class ReferentialIntegrity extends React.Component {
    componentWillMount(prevProps) {
        this.getAttributes();
        this.updateFields();
    }

    componentDidUpdate(prevProps) {
        if (this.props.rows !== prevProps.rows) {
            this.updateFields();
        }
    }

    constructor(props) {
        super(props);

        this.state = {
            updateDelay: "",
            membershipAttr: [],
            entryScope: "",
            excludeEntryScope: "",
            containerScope: "",
            logFile: "",
            referintConfigEntry: "",
            configEntryModalShow: false,

            configDN: "",
            configUpdateDelay: "",
            configMembershipAttr: [],
            configEntryScope: "",
            configExcludeEntryScope: "",
            configContainerScope: "",
            configLogFile: "",
            newEntry: true,

            attributes: []
        };

        this.updateFields = this.updateFields.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.getAttributes = this.getAttributes.bind(this);
        this.openModal = this.openModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    openModal() {
        this.getAttributes();
        if (!this.state.referintConfigEntry) {
            this.setState({
                configEntryModalShow: true,
                newEntry: true,
                configDN: "",
                configUpdateDelay: "",
                configMembershipAttr: [],
                configEntryScope: "",
                configExcludeEntryScope: "",
                configContainerScope: "",
                configLogFile: ""
            });
        } else {
            let membershipAttrList = [];
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "referential-integrity",
                "config-entry",
                "show",
                this.state.referintConfigEntry
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openModal", "Fetch the Referential Integrity Plugin config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        let pluginRow = JSON.parse(content).attrs;
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: false,
                            configDN: this.state.referintConfigEntry,
                            configUpdateDelay:
                            pluginRow["referint-update-delay"] === undefined
                                ? ""
                                : pluginRow["referint-update-delay"][0],
                            configEntryScope:
                            pluginRow["nsslapd-pluginentryscope"] === undefined
                                ? ""
                                : pluginRow["nsslapd-pluginentryscope"][0],
                            configExcludeEntryScope:
                            pluginRow["nsslapd-pluginexcludeentryscope"] === undefined
                                ? ""
                                : pluginRow["nsslapd-pluginexcludeentryscope"][0],
                            configContainerScope:
                            pluginRow["nsslapd-plugincontainerscope"] === undefined
                                ? ""
                                : pluginRow["nsslapd-plugincontainerscope"][0],
                            configLogFile:
                            pluginRow["referint-logfile"] === undefined
                                ? ""
                                : pluginRow["referint-logfile"][0]
                        });

                        if (pluginRow["referint-membership-attr"] === undefined) {
                            this.setState({ configMembershipAttr: [] });
                        } else {
                            for (let value of pluginRow["referint-membership-attr"]) {
                                membershipAttrList = [
                                    ...membershipAttrList,
                                    { id: value, label: value }
                                ];
                            }
                            this.setState({
                                configMembershipAttr: membershipAttrList
                            });
                        }
                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configDN: this.state.referintConfigEntry,
                            configUpdateDelay: "",
                            configMembershipAttr: [],
                            configEntryScope: "",
                            configExcludeEntryScope: "",
                            configContainerScope: "",
                            configLogFile: ""
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
            configUpdateDelay,
            configMembershipAttr,
            configEntryScope,
            configExcludeEntryScope,
            configContainerScope,
            configLogFile
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "referential-integrity",
            "config-entry",
            action,
            configDN,
            "--update-delay",
            configUpdateDelay || action == "add" ? configUpdateDelay : "delete",
            "--entry-scope",
            configEntryScope || action == "add" ? configEntryScope : "delete",
            "--exclude-entry-scope",
            configExcludeEntryScope || action == "add" ? configExcludeEntryScope : "delete",
            "--container-scope",
            configContainerScope || action == "add" ? configContainerScope : "delete",
            "--log-file",
            configLogFile || action == "add" ? configLogFile : "delete"
        ];

        // Delete attributes if the user set an empty value to the field
        cmd = [...cmd, "--membership-attr"];
        if (configMembershipAttr.length != 0) {
            for (let value of configMembershipAttr) {
                cmd = [...cmd, value.label];
            }
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }
        this.props.toggleLoadingHandler();
        log_cmd(
            "referintOperation",
            `Do the ${action} operation on the Referential Integrity Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("referintOperation", "Result", content);
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

    deleteConfig() {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "referential-integrity",
            "config-entry",
            "delete",
            this.state.configDN
        ];

        this.props.toggleLoadingHandler();
        log_cmd("deleteConfig", "Delete the Referential Integrity Plugin config entry", cmd);
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

    updateFields() {
        let membershipAttrList = [];

        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(
                row => row.cn[0] === "referential integrity postoperation"
            );

            this.setState({
                updateDelay:
                    pluginRow["referint-update-delay"] === undefined
                        ? ""
                        : pluginRow["referint-update-delay"][0],
                entryScope:
                    pluginRow["nsslapd-pluginEntryScope"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginEntryScope"][0],
                excludeEntryScope:
                    pluginRow["nsslapd-pluginExcludeEntryScope"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginExcludeEntryScope"][0],
                containerScope:
                    pluginRow["nsslapd-pluginContainerScope"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginContainerScope"][0],
                referintConfigEntry:
                    pluginRow["nsslapd-pluginConfigArea"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginConfigArea"][0],
                logFile:
                    pluginRow["referint-logfile"] === undefined
                        ? ""
                        : pluginRow["referint-logfile"][0]
            });

            if (pluginRow["referint-membership-attr"] === undefined) {
                this.setState({ membershipAttr: [] });
            } else {
                for (let value of pluginRow["referint-membership-attr"]) {
                    membershipAttrList = [...membershipAttrList, { id: value, label: value }];
                }
                this.setState({ membershipAttr: membershipAttrList });
            }
        }
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
                        attrs.push({
                            id: content.name,
                            label: content.name
                        });
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
            updateDelay,
            membershipAttr,
            entryScope,
            excludeEntryScope,
            containerScope,
            logFile,
            referintConfigEntry,
            attributes,
            configDN,
            configUpdateDelay,
            configMembershipAttr,
            configEntryScope,
            configExcludeEntryScope,
            configContainerScope,
            configEntryModalShow,
            configLogFile,
            newEntry
        } = this.state;

        let specificPluginCMD = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "referential-integrity",
            "set",
            "--update-delay",
            updateDelay || "delete",
            "--entry-scope",
            entryScope || "delete",
            "--exclude-entry-scope",
            excludeEntryScope || "delete",
            "--container-scope",
            containerScope || "delete",
            "--config-entry",
            referintConfigEntry || "delete",
            "--log-file",
            logFile || "delete"
        ];

        // Delete attributes if the user set an empty value to the field
        specificPluginCMD = [...specificPluginCMD, "--membership-attr"];
        if (membershipAttr.length != 0) {
            for (let value of membershipAttr) {
                specificPluginCMD = [...specificPluginCMD, value.label];
            }
        } else {
            specificPluginCMD = [...specificPluginCMD, "delete"];
        }

        return (
            <div>
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
                            <Modal.Title>
                                Manage Referential Integrity Plugin Shared Config Entry
                            </Modal.Title>
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
                                            key="configUpdateDelay"
                                            controlId="configUpdateDelay"
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Sets the update interval. Special values: 0 - The check is performed immediately, -1 - No check is performed (referint-update-delay)"
                                            >
                                                Update Delay
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={configUpdateDelay}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="configMembershipAttr"
                                            controlId="configMembershipAttr"
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Specifies attributes to check for and update (referint-membership-attr)"
                                            >
                                                Membership Attribute
                                            </Col>
                                            <Col sm={9}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={value => {
                                                        this.setState({
                                                            configMembershipAttr: value
                                                        });
                                                    }}
                                                    selected={configMembershipAttr}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a membership attribute: "
                                                    placeholder="Type an attribute..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="configEntryScope"
                                            controlId="configEntryScope"
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Defines the subtree in which the plug-in looks for the delete or rename operations of a user entry (nsslapd-pluginEntryScope)"
                                            >
                                                Entry Scope
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={configEntryScope}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="configExcludeEntryScope"
                                            controlId="configExcludeEntryScope"
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Defines the subtree in which the plug-in ignores any operations for deleting or renaming a user (nsslapd-pluginExcludeEntryScope)"
                                            >
                                                Exclude Entry Scope
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={configExcludeEntryScope}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="configContainerScope"
                                            controlId="configContainerScope"
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Specifies which branch the plug-in searches for the groups to which the user belongs. It only updates groups that are under the specified container branch, and leaves all other groups not updated (nsslapd-pluginContainerScope)"
                                            >
                                                Container Scope
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={configContainerScope}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="configLogFile" controlId="configLogFile">
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title={`Specifies a path to the Referential integrity logfile. For example: /var/log/dirsrv/slapd-${
                                                    this.props.serverId
                                                }/referint`}
                                            >
                                                Logfile
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={configLogFile}
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
                    cn="referential integrity postoperation"
                    pluginName="Referential Integrity"
                    cmdName="referential-integrity"
                    specificPluginCMD={specificPluginCMD}
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup key="updateDelay" controlId="updateDelay">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Sets the update interval. Special values: 0 - The check is performed immediately, -1 - No check is performed (referint-update-delay)"
                                    >
                                        Update Delay
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={updateDelay}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="membershipAttr" controlId="membershipAttr">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Specifies attributes to check for and update (referint-membership-attr)"
                                    >
                                        Membership Attribute
                                    </Col>
                                    <Col sm={6}>
                                        <Typeahead
                                            allowNew
                                            multiple
                                            onChange={value => {
                                                this.setState({
                                                    membershipAttr: value
                                                });
                                            }}
                                            selected={membershipAttr}
                                            options={attributes}
                                            newSelectionPrefix="Add a membership attribute: "
                                            placeholder="Type an attribute..."
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="entryScope" controlId="entryScope">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Defines the subtree in which the plug-in looks for the delete or rename operations of a user entry (nsslapd-pluginEntryScope)"
                                    >
                                        Entry Scope
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={entryScope}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="excludeEntryScope" controlId="excludeEntryScope">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Defines the subtree in which the plug-in ignores any operations for deleting or renaming a user (nsslapd-pluginExcludeEntryScope)"
                                    >
                                        Exclude Entry Scope
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={excludeEntryScope}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="containerScope" controlId="containerScope">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Specifies which branch the plug-in searches for the groups to which the user belongs. It only updates groups that are under the specified container branch, and leaves all other groups not updated (nsslapd-pluginContainerScope)"
                                    >
                                        Container Scope
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={containerScope}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="logFile" controlId="logFile">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title={`Specifies a path to the Referential integrity logfile. For example: /var/log/dirsrv/slapd-${
                                            this.props.serverId
                                        }/referint`}
                                    >
                                        Logfile
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={logFile}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="referintConfigEntry"
                                    controlId="referintConfigEntry"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="The value to set as nsslapd-pluginConfigArea"
                                    >
                                        Shared Config Entry
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={referintConfigEntry}
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
                </PluginBasicConfig>
            </div>
        );
    }
}

ReferentialIntegrity.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

ReferentialIntegrity.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default ReferentialIntegrity;
