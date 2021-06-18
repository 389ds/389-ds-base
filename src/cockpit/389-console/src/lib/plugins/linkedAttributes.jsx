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
    SelectVariant,
    SelectOption,
    // TextInput,
    noop
} from "@patternfly/react-core";
import { LinkedAttributesTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

class LinkedAttributes extends React.Component {
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
            tableKey: 1,

            configName: "",
            linkType: [],
            managedType: [],
            linkScope: "",

            newEntry: false,
            showConfigModal: false,
            showConfirmDelete: false,
            modalChecked: false,
            modalSpinning: false,

            isLinkTypeOpen: false,
            isManagedTypeOpen: false,
        };

        // Link Type
        this.onLinkTypeSelect = (event, selection) => {
            if (this.state.linkType.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        linkType: prevState.linkType.filter((item) => item !== selection)
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({ linkType: [...prevState.linkType, selection] }),
                );
            }
        };
        this.onLinkTypeToggle = isLinkTypeOpen => {
            this.setState({
                isLinkTypeOpen
            });
        };
        this.onLinkTypeClear = () => {
            this.setState({
                linkType: [],
                isLinkTypeOpen: false
            });
        };
        this.onLinkTypeCreateOption = newValue => {
            if (!this.state.linkType.includes(newValue)) {
                this.setState({
                    linkType: [...this.state.linkType, newValue],
                    isLinkTypeOpen: false
                });
            }
        };

        // Managed Type
        this.onManagedTypeSelect = (event, selection) => {
            if (this.state.managedType.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        managedType: prevState.managedType.filter((item) => item !== selection)
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({ managedType: [...prevState.managedType, selection] }),
                );
            }
        };
        this.onManagedTypeToggle = isManagedTypeOpen => {
            this.setState({
                isManagedTypeOpen
            });
        };
        this.onManagedTypeClear = () => {
            this.setState({
                managedType: [],
                isManagedTypeOpen: false
            });
        };
        this.onManagedTypeCreateOption = newValue => {
            if (!this.state.managedType.includes(newValue)) {
                this.setState({
                    managedType: [...this.state.managedType, newValue],
                    isManagedTypeOpen: false
                });
            }
        };

        this.getAttributes = this.getAttributes.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.loadConfigs = this.loadConfigs.bind(this);
        this.showEditConfigModal = this.showEditConfigModal.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
        this.showAddConfigModal = this.showAddConfigModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.openModal = this.openModal.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
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

    handleFieldChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
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
            "linked-attr",
            "list"
        ];
        log_cmd("loadConfigs", "Get Linked Attributes Plugin configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    let tableKey = this.state.tableKey + 1;
                    this.setState({
                        configRows: myObject.items.map(item => item.attrs),
                        tableKey: tableKey
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadConfigs failed", errMsg.desc);
                    }
                });
    }

    showEditConfigModal(rowData) {
        this.openModal(rowData);
    }

    showAddConfigModal(rowData) {
        this.openModal();
    }

    openModal(name) {
        this.getAttributes();
        if (!name) {
            this.setState({
                configEntryModalShow: true,
                newEntry: true,
                configName: "",
                linkType: [],
                managedType: [],
                linkScope: ""
            });
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "linked-attr",
                "config",
                name,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openModal", "Fetch the Linked Attributes Plugin config entry", cmd);
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
                            linkType:
                            configEntry["linktype"] === undefined
                                ? []
                                : [configEntry["linktype"][0]],
                            managedType:
                            configEntry["managedtype"] === undefined
                                ? []
                                : [configEntry["managedtype"][0]],
                            linkScope:
                            configEntry["linkscope"] === undefined
                                ? ""
                                : configEntry["linkscope"][0]
                        });

                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configName: "",
                            linkType: [],
                            managedType: [],
                            linkScope: ""
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    closeModal() {
        this.setState({ configEntryModalShow: false });
    }

    cmdOperation(action) {
        const { configName, linkType, managedType, linkScope } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "linked-attr",
            "config",
            configName,
            action,
            "--link-scope",
            linkScope || action == "add" ? linkScope : "delete"
        ];

        cmd = [...cmd, "--link-type"];
        if (linkType.length != 0) {
            cmd = [...cmd, linkType[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--managed-type"];
        if (managedType.length != 0) {
            cmd = [...cmd, managedType[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        this.props.toggleLoadingHandler();
        log_cmd(
            "linkedAttributesOperation",
            `Do the ${action} operation on the Linked Attributes Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("linkedAttributesOperation", "Result", content);
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
            "linked-attr",
            "config",
            this.state.deleteName,
            "delete"
        ];

        this.setState({
            modalSpinning: true
        });

        log_cmd("deleteConfig", "Delete the Linked Attributes Plugin config entry", cmd);
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
                    this.closeModal();
                    this.closeConfirmDelete();
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

    render() {
        const {
            configEntryModalShow,
            configName,
            linkType,
            managedType,
            linkScope,
            newEntry,
            attributes
        } = this.state;

        let title = (newEntry ? "Add" : "Edit") + " Linked Attributes Plugin Config Entry";
        return (
            <div>
                <Modal
                    variant={ModalVariant.medium}
                    title={title}
                    isOpen={configEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeModal}
                    actions={[
                        <Button key="confirm" variant="primary" onClick={newEntry ? this.addConfig : this.editConfig}>
                            Save
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Row>
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup controlId="configName">
                                    <Col componentClass={ControlLabel} sm={3} title="The Linked Attributes configuration name">
                                        Config Name
                                    </Col>
                                    <Col sm={9}>
                                        <FormControl
                                            type="text"
                                            value={configName}
                                            onChange={this.handleFieldChange}
                                            disabled={!newEntry}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup controlId="linkType">
                                    <Col componentClass={ControlLabel} sm={3} title="Sets the attribute that is managed manually by administrators (linkType)">
                                        Link Type
                                    </Col>
                                    <Col sm={9}>
                                        <Select
                                            variant={SelectVariant.typeahead}
                                            typeAheadAriaLabel="Type an attribute name"
                                            onToggle={this.onLinkTypeToggle}
                                            onSelect={this.onLinkTypeSelect}
                                            onClear={this.onLinkTypeClear}
                                            selections={linkType}
                                            isOpen={this.state.isLinkTypeOpen}
                                            aria-labelledby="typeAhead-link-type"
                                            placeholderText="Type an attribute..."
                                            noResultsFoundText="There are no matching entries"
                                            isCreatable
                                            onCreateOption={this.onLinkTypeCreateOption}
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
                                <FormGroup controlId="managedType">
                                    <Col componentClass={ControlLabel} sm={3} title="Sets the attribute that is created dynamically by the plugin (managedType)">
                                        Managed Type
                                    </Col>
                                    <Col sm={9}>
                                        <Select
                                            variant={SelectVariant.typeahead}
                                            typeAheadAriaLabel="Type an attribute name"
                                            onToggle={this.onManagedTypeToggle}
                                            onSelect={this.onManagedTypeSelect}
                                            onClear={this.onManagedTypeClear}
                                            selections={managedType}
                                            isOpen={this.state.isManagedTypeOpen}
                                            placeholderText="Type an attribute..."
                                            aria-labelledby="typeAhead-managed-type"
                                            noResultsFoundText="There are no matching entries"
                                            isCreatable
                                            onCreateOption={this.onManagedTypeCreateOption}
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
                                <FormGroup controlId="linkScope">
                                    <Col componentClass={ControlLabel} sm={3} title="Sets the base DN that restricts the plugin to a specific part of the directory tree (linkScope)">
                                        Link Scope
                                    </Col>
                                    <Col sm={9}>
                                        <FormControl
                                            type="text"
                                            value={linkScope}
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
                    key={this.state.configRows}
                    serverId={this.props.serverId}
                    cn="Linked Attributes"
                    pluginName="Linked Attributes"
                    cmdName="linked-attr"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={12}>
                            <LinkedAttributesTable
                                rows={this.state.configRows}
                                key={this.state.tableKey}
                                editConfig={this.showEditConfigModal}
                                deleteConfig={this.showConfirmDelete}
                            />
                            <Button
                                key="addconf"
                                className="ds-margin-top"
                                variant="primary"
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
                    handleChange={this.handleFieldChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Linked Attribute Configuration"
                    mMsg="Are you sure you want to delete this configuration?"
                    mSpinningMsg="Deleting Configuration..."
                    mBtnName="Delete Configuration"
                />
            </div>
        );
    }
}

LinkedAttributes.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

LinkedAttributes.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default LinkedAttributes;
