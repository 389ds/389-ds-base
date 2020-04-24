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

// Use default aacount policy name

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

        this.getAttributes = this.getAttributes.bind(this);
        this.updateFields = this.updateFields.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleCheckboxChange = this.handleCheckboxChange.bind(this);
        this.openModal = this.openModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);

        this.state = {
            attributes: [],
            configArea: "",
            configDN: "",
            altStateAttrName: [],
            alwaysRecordLogin: false,
            alwaysRecordLoginAttr: [],
            limitAttrName: [],
            specAttrName: [],
            stateAttrName: [],
            configEntryModalShow: false,
            fixupModalShow: false,
            newEntry: false
        };
    }

    openModal() {
        this.getAttributes();
        if (!this.state.configArea) {
            this.setState({
                configEntryModalShow: true,
                newEntry: true,
                configDN: "",
                altStateAttrName: [],
                alwaysRecordLogin: false,
                alwaysRecordLoginAttr: [],
                limitAttrName: [],
                specAttrName: [],
                stateAttrName: []
            });
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "account-policy",
                "config-entry",
                "show",
                this.state.configArea
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openModal", "Fetch the Account Policy Plugin config entry", cmd);
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
                            configDN: this.state.configArea,
                            altStateAttrName:
                            configEntry["altstateattrname"] === undefined
                                ? []
                                : [
                                    configEntry["altstateattrname"][0]
                                ],
                            alwaysRecordLogin: !(
                                configEntry["alwaysrecordlogin"] === undefined ||
                            configEntry["alwaysrecordlogin"][0] == "no"
                            ),
                            alwaysRecordLoginAttr:
                            configEntry["alwaysrecordloginattr"] === undefined
                                ? []
                                : [
                                    configEntry["alwaysrecordloginattr"][0]
                                ],
                            limitAttrName:
                            configEntry["limitattrname"] === undefined
                                ? []
                                : [
                                    configEntry["limitattrname"][0]
                                ],
                            specAttrName:
                            configEntry["specattrname"] === undefined
                                ? []
                                : [
                                    configEntry["specattrname"][0]
                                ],
                            stateAttrName:
                            configEntry["stateattrname"] === undefined
                                ? []
                                : [
                                    configEntry["stateattrname"][0],
                                ]
                        });
                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configDN: this.state.configArea,
                            altStateAttrName: [],
                            alwaysRecordLogin: false,
                            alwaysRecordLoginAttr: [],
                            limitAttrName: [],
                            specAttrName: [],
                            stateAttrName: []
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
            altStateAttrName,
            alwaysRecordLogin,
            alwaysRecordLoginAttr,
            limitAttrName,
            specAttrName,
            stateAttrName
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
            alwaysRecordLogin ? "yes" : "no"
        ];

        cmd = [...cmd, "--alt-state-attr"];
        if (altStateAttrName.length != 0) {
            cmd = [...cmd, altStateAttrName[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--always-record-login-attr"];
        if (alwaysRecordLoginAttr.length != 0) {
            cmd = [...cmd, alwaysRecordLoginAttr[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--limit-attr"];
        if (limitAttrName.length != 0) {
            cmd = [...cmd, limitAttrName[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--spec-attr"];
        if (specAttrName.length != 0) {
            cmd = [...cmd, specAttrName[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--state-attr"];
        if (stateAttrName.length != 0) {
            cmd = [...cmd, stateAttrName[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        this.props.toggleLoadingHandler();
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
            "account-policy",
            "config-entry",
            "delete",
            this.state.configDN
        ];

        this.props.toggleLoadingHandler();
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
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "Account Policy Plugin");

            this.setState({
                configArea:
                    pluginRow["nsslapd_pluginconfigarea"] === undefined
                        ? ""
                        : pluginRow["nsslapd_pluginconfigarea"][0]
            });
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
            attributes,
            configArea,
            configDN,
            altStateAttrName,
            alwaysRecordLogin,
            alwaysRecordLoginAttr,
            limitAttrName,
            specAttrName,
            stateAttrName,
            newEntry,
            configEntryModalShow
        } = this.state;

        let specificPluginCMD = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "account-policy",
            "set",
            "--config-entry",
            configArea || "delete"
        ];

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
                                Manage Account Policy Plugin Shared Config Entry
                            </Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup controlId="configDN">
                                            <Col sm={4}>
                                                <ControlLabel title="DN of the config entry">
                                                    Config DN
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    type="text"
                                                    value={configDN}
                                                    onChange={this.handleFieldChange}
                                                    disabled={!newEntry}
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
                                            key="alwaysRecordLoginAttr"
                                            controlId="alwaysRecordLoginAttr"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Specifies the attribute to store the time of the last successful login in this attribute in the users directory entry (alwaysRecordLoginAttr)"
                                            >
                                                Always Record Login Attribute
                                            </Col>
                                            <Col sm={5}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            alwaysRecordLoginAttr: value
                                                        });
                                                    }}
                                                    selected={alwaysRecordLoginAttr}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a managed attribute: "
                                                    placeholder="Type an attribute..."
                                                />
                                            </Col>
                                            <Col sm={3}>
                                                <Checkbox
                                                    id="alwaysRecordLogin"
                                                    checked={alwaysRecordLogin}
                                                    title="Sets that every entry records its last login time (alwaysRecordLogin)"
                                                    onChange={this.handleCheckboxChange}
                                                >
                                                    Always Record Login
                                                </Checkbox>
                                            </Col>
                                        </FormGroup>
                                    </Form>
                                </Col>
                            </Row>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup
                                            key="specAttrName"
                                            controlId="specAttrName"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Specifies the attribute to identify which entries are account policy configuration entries (specAttrName)"
                                            >
                                                Specific Attribute
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            specAttrName: value
                                                        });
                                                    }}
                                                    selected={specAttrName}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a managed attribute: "
                                                    placeholder="Type an attribute..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="stateAttrName"
                                            controlId="stateAttrName"
                                            disabled={false}
                                        >
                                            <Col sm={4}>
                                                <ControlLabel title="Specifies the primary time attribute used to evaluate an account policy (stateAttrName)">
                                                    State Attribute
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            stateAttrName: value
                                                        });
                                                    }}
                                                    selected={stateAttrName}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a managed attribute: "
                                                    placeholder="Type an attribute..."
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
                                            key="altStateAttrName"
                                            controlId="altStateAttrName"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Provides a backup attribute for the server to reference to evaluate the expiration time (altStateAttrName)"
                                            >
                                                Alternative State Attribute
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            altStateAttrName: value
                                                        });
                                                    }}
                                                    selected={altStateAttrName}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a managed attribute: "
                                                    placeholder="Type an attribute..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup controlId="limitAttrName" disabled={false}>
                                            <Col sm={4}>
                                                <ControlLabel title="Specifies the attribute within the policy to use for the account inactivation limit (limitAttrName)">
                                                    Limit Attribute
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            limitAttrName: value
                                                        });
                                                    }}
                                                    selected={limitAttrName}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a managed attribute: "
                                                    placeholder="Type an attribute..."
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
                    cn="Account Policy Plugin"
                    pluginName="Account Policy"
                    cmdName="account-policy"
                    specificPluginCMD={specificPluginCMD}
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={10}>
                            <Form horizontal>
                                <FormGroup key="configAreaAP" controlId="configAreaAP">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="DN of the shared config entry (nsslapd-pluginConfigArea)"
                                    >
                                        Shared Config Entry
                                    </Col>
                                    <Col sm={7}>
                                        <FormControl
                                            type="text"
                                            value={configArea}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                    <Col sm={2}>
                                        <Button
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
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default AccountPolicy;
