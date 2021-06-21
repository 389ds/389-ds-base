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
    Checkbox,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    Select,
    SelectVariant,
    SelectOption,
    // TextInput,
    noop
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd } from "../tools.jsx";

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
            altStateAttrNameOptions: [],
            alwaysRecordLogin: false,
            alwaysRecordLoginAttr: [],
            alwaysRecordLoginAttrOptions: [],
            limitAttrName: [],
            limitAttrNameOptions: [],
            specAttrName: [],
            specAttrNameOptions: [],
            stateAttrName: [],
            stateAttrNameOptions: [],
            configEntryModalShow: false,
            fixupModalShow: false,
            newEntry: false,

            isRecordLoginOpen: false,
            isSpecificAttrOpen: false,
            isStateAttrOpen: false,
            isAltStateAttrOpen: false,
            isLimitAttrOpen: false,
        };

        // Always Record Login Attribute
        this.onRecordLoginSelect = (event, selection) => {
            if (this.state.alwaysRecordLoginAttr.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        alwaysRecordLoginAttr: prevState.alwaysRecordLoginAttr.filter((item) => item !== selection),
                        isRecordLoginOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        alwaysRecordLoginAttr: [...prevState.alwaysRecordLoginAttr, selection],
                        isRecordLoginOpen: false
                    }),
                );
            }
        };
        this.onRecordLoginToggle = isRecordLoginOpen => {
            this.setState({
                isRecordLoginOpen
            });
        };
        this.onRecordLoginClear = () => {
            this.setState({
                alwaysRecordLoginAttr: [],
                isRecordLoginOpen: false
            });
        };
        this.onRecordLoginCreateOption = newValue => {
            if (!this.state.alwaysRecordLoginAttrOptions.includes(newValue)) {
                this.setState({
                    alwaysRecordLoginAttrOptions: [...this.state.alwaysRecordLoginAttrOptions, newValue],
                    isRecordLoginOpen: false
                });
            }
        };

        // Specific Attribute
        this.onSpecificAttrSelect = (event, selection) => {
            if (this.state.specAttrName.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        specAttrName: prevState.specAttrName.filter((item) => item !== selection),
                        isSpecificAttrOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        specAttrName: [...prevState.specAttrName, selection],
                        isSpecificAttrOpen: false
                    }),
                );
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
        this.onSpecificAttrCreateOption = newValue => {
            if (!this.state.specAttrNameOptions.includes(newValue)) {
                this.setState({
                    specAttrNameOptions: [...this.state.specAttrNameOptions, newValue],
                    isSpecificAttrOpen: false
                });
            }
        };

        // State Attribute
        this.onStateAttrSelect = (event, selection) => {
            if (this.state.stateAttrName.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        stateAttrName: prevState.stateAttrName.filter((item) => item !== selection),
                        isStateAttrOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        stateAttrName: [...prevState.stateAttrName, selection],
                        isStateAttrOpen: false
                    }),
                );
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
        this.onStateAttrCreateOption = newValue => {
            if (!this.state.stateAttrNameOptions.includes(newValue)) {
                this.setState({
                    stateAttrName: [...this.state.stateAttrName, newValue],
                    isStateAttrOpen: false
                });
            }
        };

        // Alternative State Attribute
        this.onAlternativeStateSelect = (event, selection) => {
            if (this.state.altStateAttrName.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        altStateAttrName: prevState.altStateAttrName.filter((item) => item !== selection),
                        isAltStateAttrOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        altStateAttrName: [...prevState.altStateAttrName, selection],
                        isAltStateAttrOpen: false
                    }),
                );
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
        this.onAlternativeStateCreateOption = newValue => {
            if (!this.state.altStateAttrNameOptions.includes(newValue)) {
                this.setState({
                    altStateAttrNameOptions: [...this.state.altStateAttrNameOptions, newValue],
                    isAltStateAttrOpen: false
                });
            }
        };
        // Limit Attribute
        this.onLimitAttrSelect = (event, selection) => {
            if (this.state.limitAttrName.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        limitAttrName: prevState.limitAttrName.filter((item) => item !== selection),
                        isLimitAttrOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        limitAttrName: [...prevState.limitAttrName, selection],
                        isLimitAttrOpen: false
                    }),
                );
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
        this.onLimitAttrCreateOption = newValue => {
            if (!this.state.onLimitAttrCreateOption.includes(newValue)) {
                this.setState({
                    onLimitAttrCreateOption: [...this.state.onLimitAttrCreateOption, newValue],
                    isLimitAttrOpen: false
                });
            }
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

    handleCheckboxChange(checked, e) {
        this.setState({
            [e.target.id]: checked
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
                <Modal
                    variant={ModalVariant.medium}
                    title="Manage Account Policy Plugin Shared Config Entry"
                    isOpen={configEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeModal}
                    actions={[
                        <Button key="delete" variant="primary" onClick={this.deleteConfig} isDisabled={newEntry}>
                            Delete
                        </Button>,
                        <Button key="save" variant="primary" onClick={this.editConfig} isDisabled={newEntry}>
                            Save
                        </Button>,
                        <Button key="add" variant="primary" onClick={this.addConfig} isDisabled={!newEntry}>
                            Add
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Row>
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup controlId="configDN">
                                    <Col sm={4} title="DN of the config entry" componentClass={ControlLabel}>
                                        Config DN
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
                                        <Select
                                            variant={SelectVariant.typeahead}
                                            typeAheadAriaLabel="Type an attribute name"
                                            onToggle={this.onRecordLoginToggle}
                                            onSelect={this.onRecordLoginSelect}
                                            onClear={this.onRecordLoginClear}
                                            selections={alwaysRecordLoginAttr}
                                            isOpen={this.state.isRecordLoginOpen}
                                            aria-labelledby="typeAhead-record-login"
                                            placeholderText="Type an attribute name..."
                                            noResultsFoundText="There are no matching entries"
                                            isCreatable
                                            onCreateOption={this.onRecordLoginCreateOption}
                                            >
                                            {attributes.map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                                ))}
                                        </Select>
                                    </Col>
                                    <Col sm={3}>
                                        <Checkbox
                                            id="alwaysRecordLogin"
                                            isChecked={alwaysRecordLogin}
                                            title="Sets that every entry records its last login time (alwaysRecordLogin)"
                                            onChange={this.handleCheckboxChange}
                                            label="Always Record Login"
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
                                        <Select
                                            variant={SelectVariant.typeahead}
                                            typeAheadAriaLabel="Type an attribute name"
                                            onToggle={this.onSpecificAttrToggle}
                                            onSelect={this.onSpecificAttrSelect}
                                            onClear={this.onSpecificAttrClear}
                                            selections={specAttrName}
                                            isOpen={this.state.isSpecificAttrOpen}
                                            aria-labelledby="typeAhead-specific-attr"
                                            placeholderText="Type an attribute..."
                                            noResultsFoundText="There are no matching entries"
                                            isCreatable
                                            onCreateOption={this.onSpecificAttrCreateOption}
                                            >
                                            {attributes.map((attr) => (
                                                <SelectOption
                                                    key={attr}
                                                    value={attr}
                                                />
                                                ))}
                                        </Select>
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="stateAttrName"
                                    controlId="stateAttrName"
                                    disabled={false}
                                >
                                    <Col sm={4} componentClass={ControlLabel} title="Specifies the primary time attribute used to evaluate an account policy (stateAttrName)">
                                        State Attribute
                                    </Col>
                                    <Col sm={8}>
                                        <Select
                                            variant={SelectVariant.typeahead}
                                            typeAheadAriaLabel="Type an attribute name"
                                            onToggle={this.onStateAttrToggle}
                                            onSelect={this.onStateAttrSelect}
                                            onClear={this.onStateAttrClear}
                                            selections={stateAttrName}
                                            isOpen={this.state.isStateAttrOpen}
                                            aria-labelledby="typeAhead-state-attr"
                                            placeholderText="Type an attribute name..."
                                            noResultsFoundText="There are no matching entries"
                                            isCreatable
                                            onCreateOption={this.onStateAttrCreateOption}
                                            >
                                            {attributes.map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                                ))}
                                        </Select>
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
                                        <Select
                                            variant={SelectVariant.typeahead}
                                            typeAheadAriaLabel="Type an attribute name"
                                            onToggle={this.onAlternativeStateToggle}
                                            onSelect={this.onAlternativeStateSelect}
                                            onClear={this.onAlternativeStateClear}
                                            selections={altStateAttrName}
                                            isOpen={this.state.isAltStateAttrOpen}
                                            aria-labelledby="typeAhead-alt-state-attr"
                                            placeholderText="Type an attribute..."
                                            noResultsFoundText="There are no matching entries"
                                            isCreatable
                                            onCreateOption={this.onAlternativeStateCreateOption}
                                            >
                                            {attributes.map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                                ))}
                                        </Select>
                                    </Col>
                                </FormGroup>
                                <FormGroup controlId="limitAttrName" disabled={false}>
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={4}
                                        title="Specifies the attribute within the policy to use for the account inactivation limit (limitAttrName)"
                                    >
                                        Limit Attribute
                                    </Col>
                                    <Col sm={8}>
                                        <Select
                                            variant={SelectVariant.typeahead}
                                            typeAheadAriaLabel="Type an attribute name"
                                            onToggle={this.onLimitAttrToggle}
                                            onSelect={this.onLimitAttrSelect}
                                            onClear={this.onLimitAttrClear}
                                            selections={limitAttrName}
                                            isOpen={this.state.isLimitAttrOpen}
                                            aria-labelledby="typeAhead-limit-attr"
                                            placeholderText="Type an attribute..."
                                            noResultsFoundText="There are no matching entries"
                                            isCreatable
                                            onCreateOption={this.onLimitAttrCreateOption}
                                            >
                                            {attributes.map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                                ))}
                                        </Select>
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
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
                                            key="manage"
                                            variant="primary"
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
