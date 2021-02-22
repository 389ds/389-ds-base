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
import { ManagedEntriesTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";

class ManagedEntries extends React.Component {
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
        this.state = {
            configArea: "",
            configRows: [],
            attributes: [],

            configName: "",
            originScope: "",
            originFilter: "",
            managedBase: "",
            managedTemplate: "",

            templateDN: "",
            templateRDNAttr: [],
            templateStaticAttr: [],
            templateMappedAttr: [],

            newConfigEntry: false,
            configEntryModalShow: false,
            newTemplateEntry: false,
            templateEntryModalShow: false
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);

        this.updateFields = this.updateFields.bind(this);
        this.loadConfigs = this.loadConfigs.bind(this);
        this.getAttributes = this.getAttributes.bind(this);

        this.openModal = this.openModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.showEditConfigModal = this.showEditConfigModal.bind(this);
        this.showAddConfigModal = this.showAddConfigModal.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);

        this.openTemplateModal = this.openTemplateModal.bind(this);
        this.closeTemplateModal = this.closeTemplateModal.bind(this);
        this.cmdTemplateOperation = this.cmdTemplateOperation.bind(this);
        this.deleteTemplate = this.deleteTemplate.bind(this);
        this.addTemplate = this.addTemplate.bind(this);
        this.editTemplate = this.editTemplate.bind(this);
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    loadConfigs() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "list",
            "configs"
        ];
        log_cmd("loadConfigs", "Get Managed Entries Plugin configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    this.setState({
                        configRows: myObject.items.map(item => item.attrs)
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
        this.openModal(rowData.cn[0]);
    }

    showAddConfigModal(rowData) {
        this.openModal();
    }

    openModal(name) {
        this.getAttributes();
        if (!name) {
            this.setState({
                configEntryModalShow: true,
                newConfigEntry: true,
                configName: "",
                originScope: "",
                originFilter: "",
                managedBase: "",
                managedTemplate: ""
            });
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "managed-entries",
                "config",
                name,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openModal", "Fetch the Managed Entries Plugin config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        let configEntry = JSON.parse(content).attrs;
                        this.setState({
                            configEntryModalShow: true,
                            newConfigEntry: false,
                            configName: configEntry["cn"] === undefined ? "" : configEntry["cn"][0],
                            originScope:
                            configEntry["originscope"] === undefined
                                ? ""
                                : configEntry["originscope"][0],
                            originFilter:
                            configEntry["originfilter"] === undefined
                                ? ""
                                : configEntry["originfilter"][0],
                            managedBase:
                            configEntry["managedbase"] === undefined
                                ? ""
                                : configEntry["managedbase"][0],
                            managedTemplate:
                            configEntry["managedtemplate"] === undefined
                                ? ""
                                : configEntry["managedtemplate"][0]
                        });

                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newConfigEntry: true,
                            configName: "",
                            originScope: "",
                            originFilter: "",
                            managedBase: "",
                            managedTemplate: ""
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    closeModal() {
        this.setState({ configEntryModalShow: false });
    }

    deleteConfig(rowData) {
        let configName = rowData.cn[0];
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "config",
            configName,
            "delete"
        ];

        this.props.toggleLoadingHandler();
        log_cmd("deleteConfig", "Delete the Managed Entries Plugin config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Config entry ${configName} was successfully deleted`
                    );
                    this.loadConfigs();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry removal operation - ${errMsg.desc}`
                    );
                    this.loadConfigs();
                    this.props.toggleLoadingHandler();
                });
    }

    addConfig() {
        this.cmdOperation("add");
    }

    editConfig() {
        this.cmdOperation("set");
    }

    cmdOperation(action) {
        const { configName, originScope, originFilter, managedBase, managedTemplate } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "config",
            configName,
            action,
            "--scope",
            originScope || action == "add" ? originScope : "delete",
            "--filter",
            originFilter || action == "add" ? originFilter : "delete",
            "--managed-base",
            managedBase || action == "add" ? managedBase : "delete",
            "--managed-template",
            managedTemplate || action == "add" ? managedTemplate : "delete"
        ];

        this.props.toggleLoadingHandler();
        log_cmd("cmdOperation", `Do the ${action} operation on the Managed Entries Plugin`, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("cmdOperation", "Result", content);
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

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "Managed Entries");

            this.setState({
                configArea:
                    pluginRow["nsslapd-pluginConfigArea"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginConfigArea"][0]
            });
            this.loadConfigs();
        }
    }

    openTemplateModal() {
        this.getAttributes();
        if (!this.state.managedTemplate) {
            this.setState({
                templateEntryModalShow: true,
                newTemplateEntry: true,
                templateDN: "",
                templateRDNAttr: [],
                templateStaticAttr: [],
                templateMappedAttr: []
            });
        } else {
            let templateMappedAttrObjectList = [];
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "managed-entries",
                "template",
                this.state.managedTemplate,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openTemplateModal", "Fetch the Managed Entries Plugin config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        let configEntry = JSON.parse(content).attrs;
                        this.setState({
                            templateEntryModalShow: true,
                            newTemplateEntry: false,
                            templateDN: this.state.managedTemplate,
                            templateRDNAttr:
                            configEntry["meprdnattr"] === undefined
                                ? []
                                : [
                                    configEntry["meprdnattr"][0]
                                ],
                            templateStaticAttr:
                            configEntry["mepstaticattr"] === undefined
                                ? []
                                : [
                                    configEntry["mepstaticattr"][0]
                                ]
                        });
                        if (configEntry["mepmappedattr"] === undefined) {
                            this.setState({ templateMappedAttr: [] });
                        } else {
                            for (let value of configEntry["mepmappedattr"]) {
                                templateMappedAttrObjectList = [
                                    ...templateMappedAttrObjectList,
                                    value
                                ];
                            }
                            this.setState({
                                templateMappedAttr: templateMappedAttrObjectList
                            });
                        }
                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            templateEntryModalShow: true,
                            newTemplateEntry: true,
                            templateDN: this.state.managedTemplate,
                            templateRDNAttr: [],
                            templateStaticAttr: [],
                            templateMappedAttr: []
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    closeTemplateModal() {
        this.setState({ templateEntryModalShow: false });
    }

    cmdTemplateOperation(action) {
        const { templateDN, templateRDNAttr, templateStaticAttr, templateMappedAttr } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "template",
            templateDN,
            action
        ];

        cmd = [...cmd, "--rdn-attr"];
        if (templateRDNAttr.length != 0) {
            cmd = [...cmd, templateRDNAttr[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--static-attr"];
        if (templateStaticAttr.length != 0) {
            cmd = [...cmd, templateStaticAttr[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--mapped-attr"];
        if (templateMappedAttr.length != 0) {
            for (let value of templateMappedAttr) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        this.props.toggleLoadingHandler();
        log_cmd(
            "managedEntriesOperation",
            `Do the ${action} operation on the Managed Entries Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("managedEntriesOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Config entry ${templateDN} was successfully ${action}ed`
                    );
                    this.closeTemplateModal();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry ${action} operation - ${errMsg.desc}`
                    );
                    this.closeTemplateModal();
                    this.props.toggleLoadingHandler();
                });
    }

    deleteTemplate() {
        const { templateDN } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "template",
            templateDN,
            "delete"
        ];

        this.props.toggleLoadingHandler();
        log_cmd("deleteConfig", "Delete the Managed Entries Plugin config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Config entry ${templateDN} was successfully deleted`
                    );
                    this.closeTemplateModal();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry removal operation - ${errMsg.desc}`
                    );
                    this.closeTemplateModal();
                    this.props.toggleLoadingHandler();
                });
    }

    addTemplate() {
        this.cmdTemplateOperation("add");
    }

    editTemplate() {
        this.cmdTemplateOperation("set");
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
            configArea,
            configRows,
            attributes,
            configName,
            originScope,
            originFilter,
            managedBase,
            managedTemplate,
            templateDN,
            templateRDNAttr,
            templateStaticAttr,
            templateMappedAttr,
            newConfigEntry,
            configEntryModalShow,
            newTemplateEntry,
            templateEntryModalShow
        } = this.state;

        let specificPluginCMD = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "set",
            "--config-area",
            configArea || "delete"
        ];

        const modalConfigFields = {
            originScope: {
                name: "Scope",
                value: originScope,
                help: `Sets the search base DN to use to see which entries the plug-in monitors (originScope)`
            },
            originFilter: {
                name: "Filter",
                value: originFilter,
                help: `Sets the search filter to use to search for and identify the entries within the subtree which require a managed entry (originFilter)`
            },
            managedBase: {
                name: "Managed Base",
                value: managedBase,
                help: "Sets the subtree under which to create the managed entries (managedBase)"
            }
        };

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
                                {newConfigEntry ? "Add" : "Edit"} Managed Entries Plugin Config
                                Entry
                            </Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup key="configName" controlId="configName">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Config Name
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    required
                                                    type="text"
                                                    value={configName}
                                                    onChange={this.handleFieldChange}
                                                    disabled={!newConfigEntry}
                                                />
                                            </Col>
                                        </FormGroup>
                                        {Object.entries(modalConfigFields).map(([id, content]) => (
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
                                        ))}
                                        <FormGroup
                                            key="managedTemplate"
                                            controlId="managedTemplate"
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Identifies the template entry to use to create the managed entry (managedTemplate)"
                                            >
                                                Managed Template
                                            </Col>
                                            <Col sm={6}>
                                                <FormControl
                                                    type="text"
                                                    value={managedTemplate}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                            <Col sm={3}>
                                                <Button
                                                    bsStyle="primary"
                                                    onClick={this.openTemplateModal}
                                                >
                                                    Manage
                                                </Button>
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
                                onClick={newConfigEntry ? this.addConfig : this.editConfig}
                            >
                                Save
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <Modal show={templateEntryModalShow} onHide={this.closeTemplateModal}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={this.closeTemplateModal}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>Handle Managed Entries Plugin Template Entry</Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup controlId="templateDN">
                                            <Col componentClass={ControlLabel} sm={4} title="DN of the template entry">
                                                Template DN
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    type="text"
                                                    value={templateDN}
                                                    onChange={this.handleFieldChange}
                                                    disabled={!newTemplateEntry}
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
                                            key="templateRDNAttr"
                                            controlId="templateRDNAttr"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Sets which attribute to use as the naming attribute in the automatically-generated entry (mepRDNAttr)"
                                            >
                                                RDN Attribute
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            templateRDNAttr: value
                                                        });
                                                    }}
                                                    selected={templateRDNAttr}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a RDN attribute: "
                                                    placeholder="Type an attribute..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="templateStaticAttr"
                                            controlId="templateStaticAttr"
                                            disabled={false}
                                        >
                                            <Col componentClass={ControlLabel} sm={4} title="Sets an attribute with a defined value that must be added to the automatically-generated entry (mepStaticAttr)">
                                                Static Attribute
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    onChange={value => {
                                                        this.setState({
                                                            templateStaticAttr: value
                                                        });
                                                    }}
                                                    selected={templateStaticAttr}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a static attribute: "
                                                    placeholder="Type an attribute..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="templateMappedAttr"
                                            controlId="templateMappedAttr"
                                            disabled={false}
                                        >
                                            <Col componentClass={ControlLabel} sm={4} title="Sets attributes in the Managed Entries template entry which must exist in the generated entry (mepMappedAttr)">
                                                Mapped Attributes
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={value => {
                                                        this.setState({
                                                            templateMappedAttr: value
                                                        });
                                                    }}
                                                    selected={templateMappedAttr}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a mapped attribute: "
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
                                onClick={this.closeTemplateModal}
                            >
                                Cancel
                            </Button>
                            <Button
                                bsStyle="primary"
                                onClick={this.deleteTemplate}
                                disabled={newTemplateEntry}
                            >
                                Delete
                            </Button>
                            <Button
                                bsStyle="primary"
                                onClick={this.editTemplate}
                                disabled={newTemplateEntry}
                            >
                                Save
                            </Button>
                            <Button
                                bsStyle="primary"
                                onClick={this.addTemplate}
                                disabled={!newTemplateEntry}
                            >
                                Add
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Managed Entries"
                    pluginName="Managed Entries"
                    cmdName="managed-entries"
                    specificPluginCMD={specificPluginCMD}
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <FormGroup key="configArea" controlId="configArea">
                        <Col
                            componentClass={ControlLabel}
                            sm={3}
                            title="DN of the shared config entry (nsslapd-pluginConfigArea)"
                        >
                            Shared Config Area
                        </Col>
                        <Col sm={6}>
                            <FormControl
                                type="text"
                                value={configArea}
                                onChange={this.handleFieldChange}
                            />
                        </Col>
                    </FormGroup>
                    <Col sm={12}>
                        <ManagedEntriesTable
                            rows={configRows}
                            editConfig={this.showEditConfigModal}
                            deleteConfig={this.deleteConfig}
                        />
                        <Button
                            className="ds-margin-top"
                            bsStyle="primary"
                            onClick={this.showAddConfigModal}
                        >
                            Add Config
                        </Button>
                    </Col>
                </PluginBasicConfig>
            </div>
        );
    }
}

ManagedEntries.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

ManagedEntries.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default ManagedEntries;
