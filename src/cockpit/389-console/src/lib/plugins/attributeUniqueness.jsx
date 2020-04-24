import cockpit from "cockpit";
import React from "react";
import {
    Icon,
    Modal,
    Button,
    Row,
    Col,
    Form,
    Switch,
    noop,
    FormGroup,
    FormControl,
    Checkbox,
    ControlLabel
} from "patternfly-react";
import { Typeahead } from "react-bootstrap-typeahead";
import { AttrUniqConfigTable } from "./pluginTables.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

class AttributeUniqueness extends React.Component {
    componentDidMount() {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.loadConfigs();
            }
        }
    }

    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            configRows: [],
            attributes: [],
            objectClasses: [],
            modalChecked: false,
            modalSpinning: false,

            configName: "",
            configEnabled: false,
            attrNames: [],
            subtrees: [],
            acrossAllSubtrees: false,
            topEntryOc: [],
            subtreeEnriesOc: [],

            newEntry: false,
            showConfigModal: false,
            showConfirmDelete: false
        };

        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.handleCheckboxChange = this.handleCheckboxChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleTypeaheadChange = this.handleTypeaheadChange.bind(this);
        this.loadConfigs = this.loadConfigs.bind(this);
        this.showEditConfigModal = this.showEditConfigModal.bind(this);
        this.showAddConfigModal = this.showAddConfigModal.bind(this);
        this.getAttributes = this.getAttributes.bind(this);
        this.getObjectClasses = this.getObjectClasses.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.openModal = this.openModal.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);
    }

    handleSwitchChange(value) {
        this.setState({
            configEnabled: !value
        });
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

    handleTypeaheadChange(values) {
        // When typaheads allow new values, an object is returned
        // instead of string.  Grab the "label" in this case
        let new_values = [];
        for (let val of values) {
            new_values.push(val.label);
        }
        this.setState({
            subtrees: new_values
        });
    }

    loadConfigs() {
        this.setState({
            firstLoad: false
        });
        // Get all the attributes and matching rules now
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "attr-uniq",
            "list"
        ];
        log_cmd("loadConfigs", "Get Attribute Uniqueness Plugin configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    this.setState({
                        configRows: myObject.items.map(item => item.attrs)
                    });
                })
                .fail(err => {
                    if (err != 0) {
                        let errMsg = JSON.parse(err);
                        console.log("loadConfigs failed", errMsg.desc);
                    }
                });
    }

    showEditConfigModal(rowData) {
        this.openModal(rowData.cn[0]);
    }

    showAddConfigModal(rowData) {
        this.openModal();
    }

    openModal(name) {
        this.getAttributes();
        this.getObjectClasses();
        if (!name) {
            this.setState({
                configEntryModalShow: true,
                newEntry: true,
                configName: "",
                attrNames: [],
                subtrees: [],
                acrossAllSubtrees: false,
                topEntryOc: [],
                subtreeEnriesOc: []
            });
        } else {
            let configAttrNamesList = [];
            let configSubtreesList = [];
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "attr-uniq",
                "show",
                name
            ];

            log_cmd("openModal", "Fetch the Attribute Uniqueness Plugin config entry", cmd);
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
                            configName: configEntry["cn"] === undefined ? "" : configEntry["cn"][0],
                            configEnabled: !(
                                configEntry["nsslapd-pluginenabled"] === undefined ||
                            configEntry["nsslapd-pluginenabled"][0] == "off"
                            ),
                            acrossAllSubtrees: !(
                                configEntry["uniqueness-across-all-subtrees"] === undefined ||
                            configEntry["uniqueness-across-all-subtrees"][0] == "off"
                            ),
                            topEntryOc:
                            configEntry["uniqueness-top-entry-oc"] === undefined
                                ? []
                                : [configEntry["uniqueness-top-entry-oc"][0]],
                            subtreeEnriesOc:
                            configEntry["uniqueness-subtree-entries-oc"] === undefined
                                ? []
                                : [configEntry["uniqueness-subtree-entries-oc"][0]]
                        });

                        if (configEntry["uniqueness-attribute-name"] === undefined) {
                            this.setState({ attrNames: [] });
                        } else {
                            for (let value of configEntry["uniqueness-attribute-name"]) {
                                configAttrNamesList = [...configAttrNamesList, value];
                            }
                            this.setState({ attrNames: configAttrNamesList });
                        }
                        if (configEntry["uniqueness-subtrees"] === undefined) {
                            this.setState({ subtrees: [] });
                        } else {
                            for (let value of configEntry["uniqueness-subtrees"]) {
                                configSubtreesList = [...configSubtreesList, value];
                            }
                            this.setState({ subtrees: configSubtreesList });
                        }
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configName: "",
                            attrNames: [],
                            subtrees: [],
                            acrossAllSubtrees: false,
                            topEntryOc: [],
                            subtreeEnriesOc: []
                        });
                    });
        }
    }

    closeModal() {
        this.setState({ configEntryModalShow: false });
    }

    cmdOperation(action) {
        const {
            configName,
            configEnabled,
            attrNames,
            subtrees,
            acrossAllSubtrees,
            topEntryOc,
            subtreeEnriesOc
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "attr-uniq",
            action,
            configName,
            "--enabled",
            configEnabled ? "on" : "off",
            "--across-all-subtrees",
            acrossAllSubtrees ? "on" : "off"
        ];

        if (subtrees.length == 0 && subtreeEnriesOc.length == 0) {
            // There me a subtree or entry OC sets
            this.props.addNotification(
                "error",
                `There must be at least one Subtree or Subtree Entries OC set`
            );
            return;
        }

        // Delete attributes if the user set an empty value to the field
        if (!(action == "add" && attrNames.length == 0)) {
            cmd = [...cmd, "--attr-name"];
            if (attrNames.length != 0) {
                for (let value of attrNames) {
                    cmd = [...cmd, value];
                }
            } else if (action == "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }
        }

        if (!(action == "add" && subtrees.length == 0)) {
            cmd = [...cmd, "--subtree"];
            if (subtrees.length != 0) {
                for (let value of subtrees) {
                    cmd = [...cmd, value];
                }
            } else if (action == "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }
        }

        cmd = [...cmd, "--top-entry-oc"];
        if (topEntryOc.length != 0) {
            cmd = [...cmd, topEntryOc[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--subtree-entries-oc"];
        if (subtreeEnriesOc.length != 0) {
            cmd = [...cmd, subtreeEnriesOc[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        this.props.toggleLoadingHandler();
        log_cmd(
            "attrUniqOperation",
            `Do the ${action} operation on the Attribute Uniqueness Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("attrUniqOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        `The ${action} operation was successfully done on "${configName}" entry`
                    );
                    this.loadConfigs();
                    this.closeModal();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry ${action} operation - ${errMsg.desc}`
                    );
                    this.loadConfigs();
                    this.props.toggleLoadingHandler();
                });
    }

    showConfirmDelete (name) {
        this.setState({
            showConfirmDelete: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: name
        });
    }

    closeConfirmDelete () {
        this.setState({
            showConfirmDelete: false,
            modalChecked: false,
            modalSpinning: false,
            deleteName: ""
        });
    }

    deleteConfig() {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "attr-uniq",
            "delete",
            this.state.deleteName
        ];

        this.setState({
            modalSpinning: true
        });

        log_cmd("deleteConfig", "Delete the Attribute Uniqueness Plugin config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Config entry ${this.state.deleteName} was successfully deleted`
                    );
                    this.loadConfigs();
                    this.closeModal();
                    this.closeConfirmDelete();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry removal operation - ${errMsg.desc}`
                    );
                    this.loadConfigs();
                    this.closeConfirmDelete();
                    this.closeModal();
                });
    }

    addConfig() {
        this.cmdOperation("add");
    }

    editConfig() {
        this.cmdOperation("set");
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
                        ocs.push(content.name[0]);
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
            configEntryModalShow,
            configName,
            attrNames,
            subtrees,
            acrossAllSubtrees,
            configEnabled,
            topEntryOc,
            subtreeEnriesOc,
            newEntry,
            attributes,
            objectClasses
        } = this.state;

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
                                {newEntry ? "Add" : "Edit"} Attribute Uniqueness Plugin Config Entry
                            </Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup controlId="configName">
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title='Sets the name of the plug-in configuration record. (cn) You can use any string, but "attribute_name Attribute Uniqueness" is recommended.'
                                            >
                                                Config Name
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    type="text"
                                                    value={configName}
                                                    onChange={this.handleFieldChange}
                                                    disabled={!newEntry}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="attrNames"
                                            controlId="attrNames"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Sets the name of the attribute whose values must be unique. This attribute is multi-valued. (uniqueness-attribute-name)"
                                            >
                                                Attribute Names
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={values => {
                                                        this.setState({
                                                            attrNames: values
                                                        });
                                                    }}
                                                    selected={attrNames}
                                                    newSelectionPrefix="Add an attribute: "
                                                    options={attributes}
                                                    placeholder="Type an attribute name..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="subtrees"
                                            controlId="subtrees"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Sets the DN under which the plug-in checks for uniqueness of the attributes value. This attribute is multi-valued (uniqueness-subtrees)"
                                            >
                                                Subtrees
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={values => {
                                                        this.handleTypeaheadChange(values);
                                                    }}
                                                    selected={subtrees}
                                                    options={[""]}
                                                    newSelectionPrefix="Add a subtree: "
                                                    placeholder="Type a subtree DN..."
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
                                            key="topEntryOc"
                                            controlId="topEntryOc"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Verifies that the value of the attribute set in uniqueness-attribute-name is unique in this subtree (uniqueness-top-entry-oc)"
                                            >
                                                Top Entry OC
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            topEntryOc: value
                                                        });
                                                    }}
                                                    selected={topEntryOc}
                                                    options={objectClasses}
                                                    newSelectionPrefix="Add a top entry objectClass: "
                                                    placeholder="Type an objectClass..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="subtreeEnriesOc"
                                            controlId="subtreeEnriesOc"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Verifies if an attribute is unique, if the entry contains the object class set in this parameter (uniqueness-subtree-entries-oc)"
                                            >
                                                Subtree Entries OC
                                            </Col>
                                            <Col sm={5}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            subtreeEnriesOc: value
                                                        });
                                                    }}
                                                    selected={subtreeEnriesOc}
                                                    options={objectClasses}
                                                    newSelectionPrefix="Add a subtree entries objectClass: "
                                                    placeholder="Type an objectClass..."
                                                />
                                            </Col>
                                            <Col sm={3}>
                                                <Checkbox
                                                    id="acrossAllSubtrees"
                                                    checked={acrossAllSubtrees}
                                                    title="If enabled (on), the plug-in checks that the attribute is unique across all subtrees set. If you set the attribute to off, uniqueness is only enforced within the subtree of the updated entry (uniqueness-across-all-subtrees)"
                                                    onChange={this.handleCheckboxChange}
                                                >
                                                    Across All Subtrees
                                                </Checkbox>
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="configEnabled"
                                            controlId="configEnabled"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Identifies whether or not the config is enabled."
                                            >
                                                Enable config
                                            </Col>
                                            <Col sm={3}>
                                                <Switch
                                                    bsSize="normal"
                                                    title="normal"
                                                    id="configEnabled"
                                                    value={configEnabled}
                                                    onChange={() =>
                                                        this.handleSwitchChange(configEnabled)
                                                    }
                                                    animate={false}
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
                                onClick={newEntry ? this.addConfig : this.editConfig}
                            >
                                Save
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <PluginBasicConfig
                    removeSwitch
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="attribute uniqueness"
                    pluginName="Attribute Uniqueness"
                    cmdName="attr-uniq"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={12}>
                            <AttrUniqConfigTable
                                rows={this.state.configRows}
                                editConfig={this.showEditConfigModal}
                                deleteConfig={this.showConfirmDelete}
                            />
                            <Button
                                className="ds-margin-top"
                                bsStyle="primary"
                                onClick={this.showAddConfigModal}
                            >
                                Add Config
                            </Button>
                        </Col>
                    </Row>
                </PluginBasicConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.handleCheckboxChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Attribute Uniqueness Configuration"
                    mMsg="Are you sure you want to delete this configuration?"
                    mSpinningMsg="Deleting attribute uniqueness configuration..."
                    mBtnName="Delete Configuration"
                />
            </div>
        );
    }
}

AttributeUniqueness.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

AttributeUniqueness.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default AttributeUniqueness;
