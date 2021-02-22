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
import { ConfirmPopup } from "../notifications.jsx";
import { Typeahead } from "react-bootstrap-typeahead";
import { DNATable, DNASharedTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

class DNA extends React.Component {
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
            sharedConfigRows: [],
            attributes: [],

            configName: "",
            type: [],
            prefix: "",
            nextValue: "",
            maxValue: "",
            interval: "",
            magicRegen: "",
            filter: "",
            scope: "",
            remoteBindDN: "",
            remoteBindCred: "",
            sharedConfigEntry: "",
            threshold: "",
            nextRange: "",
            rangeRequesTimeout: "",

            sharedBaseDN: "",
            sharedHostname: "",
            sharedPort: "",
            sharedSecurePort: "",
            sharedRemainingValues: "",
            sharedRemoteBindMethod: "",
            sharedRemoteConnProtocol: "",

            newEntry: false,
            configEntryModalShow: false,
            sharedConfigListModalShow: false,
            sharedConfigEntryModalShow: false,
            showConfirmSharedSave: false
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);

        this.loadConfigs = this.loadConfigs.bind(this);
        this.loadSharedConfigs = this.loadSharedConfigs.bind(this);
        this.getAttributes = this.getAttributes.bind(this);

        this.openModal = this.openModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.showEditConfigModal = this.showEditConfigModal.bind(this);
        this.showAddConfigModal = this.showAddConfigModal.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);

        this.openSharedListModal = this.openSharedListModal.bind(this);
        this.closeSharedListModal = this.closeSharedListModal.bind(this);

        this.openSharedModal = this.openSharedModal.bind(this);
        this.closeSharedModal = this.closeSharedModal.bind(this);
        this.showEditSharedConfigModal = this.showEditSharedConfigModal.bind(this);
        this.deleteSharedConfig = this.deleteSharedConfig.bind(this);
        this.editSharedConfig = this.editSharedConfig.bind(this);

        this.showConfirmSharedSave = this.showConfirmSharedSave.bind(this);
        this.closeConfirmSharedSave = this.closeConfirmSharedSave.bind(this);
    }

    showConfirmSharedSave() {
        if (this.state.sharedConfigEntry != "") {
            this.setState({
                showConfirmSharedSave: true
            });
        } else {
            this.props.addNotification(
                "warning",
                "Shared Config Entry attribute is required for the 'Manage' operation"
            );
        }
    }

    closeConfirmSharedSave() {
        this.setState({
            showConfirmSharedSave: false
        });
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    loadConfigs() {
        this.setState({
            firstLoad: false
        });
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "list",
            "configs"
        ];
        log_cmd("loadConfigs", "Get DNA Plugin configs", cmd);
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

    loadSharedConfigs(basedn) {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "list",
            "shared-configs",
            basedn
        ];
        log_cmd("loadSharedConfigs", "Get DNA Plugin shared configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    this.setState({
                        sharedConfigRows: myObject.items.map(item => item.attrs)
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadSharedConfigs failed", errMsg.desc);
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
                newEntry: true,
                configName: "",
                type: [],
                prefix: "",
                nextValue: "",
                maxValue: "",
                interval: "",
                magicRegen: "",
                filter: "",
                scope: "",
                remoteBindDN: "",
                remoteBindCred: "",
                sharedConfigEntry: "",
                threshold: "",
                nextRange: "",
                rangeRequesTimeout: ""
            });
        } else {
            let dnaTypeList = [];
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "dna",
                "config",
                name,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openModal", "Fetch the DNA Plugin config entry", cmd);
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
                            prefix:
                            configEntry["dnaprefix"] === undefined
                                ? ""
                                : configEntry["dnaprefix"][0],
                            nextValue:
                            configEntry["dnanextvalue"] === undefined
                                ? ""
                                : configEntry["dnanextvalue"][0],
                            maxValue:
                            configEntry["dnamaxvalue"] === undefined
                                ? ""
                                : configEntry["dnamaxvalue"][0],
                            interval:
                            configEntry["dnainterval"] === undefined
                                ? ""
                                : configEntry["dnainterval"][0],
                            magicRegen:
                            configEntry["dnamagicregen"] === undefined
                                ? ""
                                : configEntry["dnamagicregen"][0],
                            filter:
                            configEntry["dnafilter"] === undefined
                                ? ""
                                : configEntry["dnafilter"][0],
                            scope:
                            configEntry["dnascope"] === undefined ? "" : configEntry["dnascope"][0],
                            remoteBindDN:
                            configEntry["dnaremotebindDN"] === undefined
                                ? ""
                                : configEntry["dnaremotebindDN"][0],
                            remoteBindCred:
                            configEntry["dnaremotebindcred"] === undefined
                                ? ""
                                : configEntry["dnaremotebindcred"][0],
                            sharedConfigEntry:
                            configEntry["dnasharedcfgdn"] === undefined
                                ? ""
                                : configEntry["dnasharedcfgdn"][0],
                            threshold:
                            configEntry["dnathreshold"] === undefined
                                ? ""
                                : configEntry["dnathreshold"][0],
                            nextRange:
                            configEntry["dnanextrange"] === undefined
                                ? ""
                                : configEntry["dnanextrange"][0],
                            rangeRequesTimeout:
                            configEntry["dnarangerequesttimeout"] === undefined
                                ? ""
                                : configEntry["dnarangerequesttimeout"][0]
                        });
                        if (configEntry["dnatype"] === undefined) {
                            this.setState({ type: [] });
                        } else {
                            for (let value of configEntry["dnatype"]) {
                                dnaTypeList = [...dnaTypeList, value];
                            }
                            this.setState({ type: dnaTypeList });
                        }

                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configName: "",
                            type: [],
                            prefix: "",
                            nextValue: "",
                            maxValue: "",
                            interval: "",
                            magicRegen: "",
                            filter: "",
                            scope: "",
                            remoteBindDN: "",
                            remoteBindCred: "",
                            sharedConfigEntry: "",
                            threshold: "",
                            nextRange: "",
                            rangeRequesTimeout: ""
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    showEditSharedConfigModal(rowData) {
        this.openSharedModal(rowData.dnahostname[0], rowData.dnaportnum[0]);
    }

    closeModal() {
        this.setState({ configEntryModalShow: false });
    }

    closeSharedModal() {
        this.setState({ sharedConfigEntryModalShow: false });
    }

    cmdOperation(action, muteError) {
        const {
            configName,
            type,
            prefix,
            nextValue,
            maxValue,
            interval,
            magicRegen,
            filter,
            scope,
            remoteBindDN,
            remoteBindCred,
            sharedConfigEntry,
            threshold,
            nextRange,
            rangeRequesTimeout
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "config",
            configName,
            action,
            "--prefix",
            prefix || action == "add" ? prefix : "delete",
            "--next-value",
            nextValue || action == "add" ? nextValue : "delete",
            "--max-value",
            maxValue || action == "add" ? maxValue : "delete",
            "--interval",
            interval || action == "add" ? interval : "delete",
            "--magic-regen",
            magicRegen || action == "add" ? magicRegen : "delete",
            "--filter",
            filter || action == "add" ? filter : "delete",
            "--scope",
            scope || action == "add" ? scope : "delete",
            "--remote-bind-dn",
            remoteBindDN || action == "add" ? remoteBindDN : "delete",
            "--remote-bind-cred",
            remoteBindCred || action == "add" ? remoteBindCred : "delete",
            "--shared-config-entry",
            sharedConfigEntry || action == "add" ? sharedConfigEntry : "delete",
            "--threshold",
            threshold || action == "add" ? threshold : "delete",
            "--next-range",
            nextRange || action == "add" ? nextRange : "delete",
            "--range-request-timeout",
            rangeRequesTimeout || action == "add" ? rangeRequesTimeout : "delete"
        ];

        // Delete attributes if the user set an empty value to the field
        if (!(action == "add" && type.length == 0)) {
            cmd = [...cmd, "--type"];
            if (type.length != 0) {
                for (let value of type) {
                    cmd = [...cmd, value];
                }
            } else if (action == "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }
        }

        this.props.toggleLoadingHandler();
        log_cmd("DNAOperation", `Do the ${action} operation on the DNA Plugin`, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("DNAOperation", "Result", content);
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
                    if (muteError !== true) {
                        this.props.addNotification(
                            "error",
                            `Error during the config entry ${action} operation - ${errMsg.desc}`
                        );
                    }
                    this.loadConfigs();
                    this.closeModal();
                    this.props.toggleLoadingHandler();
                });
    }

    deleteConfig(rowData) {
        let configName = rowData.cn[0];
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "config",
            configName,
            "delete"
        ];

        this.props.toggleLoadingHandler();
        log_cmd("deleteConfig", "Delete the DNA Plugin config entry", cmd);
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
                    this.closeModal();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry removal operation - ${errMsg.desc}`
                    );
                    this.loadConfigs();
                    this.closeModal();
                    this.props.toggleLoadingHandler();
                });
    }

    addConfig(muteError) {
        this.cmdOperation("add", muteError);
    }

    editConfig(muteError) {
        this.cmdOperation("set", muteError);
    }

    openSharedListModal(sharedConfigEntry) {
        // Save the config entry that is being edited now
        if (this.state.newEntry) {
            this.addConfig(true);
        } else {
            this.editConfig(true);
        }
        // Get all of the sharedConfig entries located under the base DN
        this.loadSharedConfigs(sharedConfigEntry);
        this.setState({ sharedConfigListModalShow: true });
    }

    closeSharedListModal() {
        this.setState({ sharedConfigListModalShow: false });
    }

    openSharedModal(hostname, port) {
        if (hostname && port) {
            // Get all the attributes and matching rules now
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "dna",
                "config",
                this.state.configName,
                "shared-config-entry",
                hostname + ":" + port,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openSharedModal", "Fetch the DNA Plugin shared config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        let configEntry = JSON.parse(content).attrs;
                        this.setState({
                            sharedConfigEntryModalShow: true,
                            sharedHostname:
                            configEntry["dnahostname"] === undefined
                                ? ""
                                : configEntry["dnahostname"][0],
                            sharedPort:
                            configEntry["dnaportnum"] === undefined
                                ? ""
                                : configEntry["dnaportnum"][0],
                            sharedSecurePort:
                            configEntry["dnasecureportnum"] === undefined
                                ? ""
                                : configEntry["dnasecureportnum"][0],
                            sharedRemainingValues:
                            configEntry["dnaremainingvalues"] === undefined
                                ? ""
                                : configEntry["dnaremainingvalues"][0],
                            sharedRemoteBindMethod:
                            configEntry["dnaremotebindmethod"] === undefined
                                ? ""
                                : configEntry["dnaremotebindmethod"][0],
                            sharedRemoteConnProtocol:
                            configEntry["dnaremoteconnprotocol"] === undefined
                                ? ""
                                : configEntry["dnaremoteconnprotocol"][0]
                        });
                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            sharedConfigEntryModalShow: true,
                            sharedBaseDN: "",
                            sharedHostname: "",
                            sharedPort: "",
                            sharedSecurePort: "",
                            sharedRemainingValues: "",
                            sharedRemoteBindMethod: "",
                            sharedRemoteConnProtocol: ""
                        });
                        this.props.toggleLoadingHandler();
                    });
        } else {
            this.setState({
                sharedConfigEntryModalShow: true,
                sharedBaseDN: "",
                sharedHostname: "",
                sharedPort: "",
                sharedSecurePort: "",
                sharedRemainingValues: "",
                sharedRemoteBindMethod: "",
                sharedRemoteConnProtocol: ""
            });
        }
    }

    editSharedConfig() {
        const {
            configName,
            sharedHostname,
            sharedPort,
            sharedRemoteBindMethod,
            sharedRemoteConnProtocol
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "config",
            configName,
            "shared-config-entry",
            sharedHostname + ":" + sharedPort,
            "set",
            "--remote-bind-method",
            sharedRemoteBindMethod || "delete",
            "--remote-conn-protocol",
            sharedRemoteConnProtocol || "delete",
        ];

        this.props.toggleLoadingHandler();
        log_cmd(
            "editSharedConfig",
            `Do the set operation on the DNA Plugin Shared Entry`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `The set operation was successfully done on "${sharedHostname}:${sharedPort}" entry`
                    );
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                    this.closeSharedModal();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry set operation - ${errMsg.desc}`
                    );
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                    this.closeSharedModal();
                    this.props.toggleLoadingHandler();
                });
    }

    deleteSharedConfig(rowData) {
        let sharedHostname = rowData.dnahostname[0];
        let sharedPort = rowData.dnaportnum[0];
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "config",
            this.state.configName,
            "shared-config-entry",
            sharedHostname + ":" + sharedPort,
            "delete"
        ];

        this.props.toggleLoadingHandler();
        log_cmd("deleteSharedConfig", "Delete the DNA Plugin Shared config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteSharedConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Shared config entry ${sharedHostname} and ${sharedPort} was successfully deleted`
                    );
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the shared config entry removal operation - ${errMsg.desc}`
                    );
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                    this.props.toggleLoadingHandler();
                });
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
            sharedConfigRows,
            configEntryModalShow,
            configName,
            newEntry,
            type,
            sharedConfigEntry,
            sharedConfigListModalShow,
            sharedConfigEntryModalShow,
            sharedHostname,
            sharedPort,
            sharedSecurePort,
            sharedRemainingValues,
            sharedRemoteBindMethod,
            sharedRemoteConnProtocol,
            attributes
        } = this.state;

        const modalConfigFields = {
            prefix: {
                name: "Prefix",
                value: this.state.prefix,
                help:
                    "Defines a prefix that can be prepended to the generated number values for the attribute (dnaPrefix)"
            },
            nextValue: {
                name: "Next Value",
                value: this.state.nextValue,
                help: "Gives the next available number which can be assigned (dnaNextValue)"
            },
            maxValue: {
                name: "Max Value",
                value: this.state.maxValue,
                help: "Sets the maximum value that can be assigned for the range (dnaMaxValue)"
            },
            interval: {
                name: "Interval",
                value: this.state.interval,
                help:
                    "Sets an interval to use to increment through numbers in a range (dnaInterval)"
            },
            magicRegen: {
                name: "Magic Regen",
                value: this.state.magicRegen,
                help:
                    "Sets a user-defined value that instructs the plug-in to assign a new value for the entry (dnaMagicRegen)"
            },
            filter: {
                name: "Filter",
                value: this.state.filter,
                help:
                    "Sets an LDAP filter to use to search for and identify the entries to which to apply the distributed numeric assignment range (dnaFilter)"
            },
            scope: {
                name: "Scope",
                value: this.state.scope,
                help:
                    "Sets the base DN to search for entries to which to apply the distributed numeric assignment (dnaScope)"
            },
            remoteBindDN: {
                name: "Remote Bind DN",
                value: this.state.remoteBindDN,
                help: "Specifies the Replication Manager DN (dnaRemoteBindDN)"
            },
            remoteBindCred: {
                name: "Remote Bind Credentials",
                value: this.state.remoteBindCred,
                help: "Specifies the Replication Manager's password (dnaRemoteBindCred)"
            },
            threshold: {
                name: "Threshold",
                value: this.state.threshold,
                help:
                    "Sets a threshold of remaining available numbers in the range. When the server hits the threshold, it sends a request for a new range (dnaThreshold)"
            },
            nextRange: {
                name: "Next Range",
                value: this.state.nextRange,
                help:
                    "Defines the next range to use when the current range is exhausted (dnaNextRange)"
            },
            rangeRequesTimeout: {
                name: "Range Request Timeout",
                value: this.state.rangeRequesTimeout,
                help:
                    "Sets a timeout period, in seconds, for range requests so that the server does not stall waiting on a new range from one server and can request a range from a new server (dnaRangeRequestTimeout)"
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
                                {newEntry ? "Add" : "Edit"} DNA Plugin Config Entry
                            </Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup key="configName" controlId="configName">
                                            <Col componentClass={ControlLabel} sm={4}>
                                                Config Name
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    required
                                                    type="text"
                                                    value={configName}
                                                    onChange={this.handleFieldChange}
                                                    disabled={!newEntry}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="sharedConfigEntry"
                                            controlId="sharedConfigEntry"
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={4}
                                                title="Defines a shared identity that the servers can use to transfer ranges to one another (dnaSharedCfgDN)"
                                            >
                                                Shared Config Entry
                                            </Col>
                                            <Col sm={5}>
                                                <FormControl
                                                    type="text"
                                                    value={sharedConfigEntry}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                            <Col sm={3}>
                                                <Button
                                                    bsStyle="primary"
                                                    onClick={this.showConfirmSharedSave}
                                                >
                                                    Manage
                                                </Button>
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="type" controlId="type">
                                            <Col componentClass={ControlLabel} sm={4} title="Sets which attributes have unique numbers being generated for them (dnaType)">
                                                Type
                                            </Col>
                                            <Col sm={8}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={value => {
                                                        this.setState({
                                                            type: value
                                                        });
                                                    }}
                                                    selected={type}
                                                    options={attributes}
                                                    newSelectionPrefix="Add a attribute: "
                                                    placeholder="Type an attribute..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        {Object.entries(modalConfigFields).map(([id, content]) => (
                                            <FormGroup key={id} controlId={id}>
                                                <Col componentClass={ControlLabel} sm={4} title={content.help}>
                                                    {content.name}
                                                </Col>
                                                <Col sm={8}>
                                                    <FormControl
                                                        type="text"
                                                        value={content.value}
                                                        onChange={this.handleFieldChange}
                                                    />
                                                </Col>
                                            </FormGroup>
                                        ))}
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
                <Modal show={sharedConfigListModalShow} onHide={this.closeSharedListModal}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={this.closeSharedListModal}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>List DNA Plugin Shared Config Entries</Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={9}>
                                    <ControlLabel>DNA Config: </ControlLabel> {configName}
                                </Col>
                                <Col sm={9}>
                                    <ControlLabel>Shared Config Base DN:</ControlLabel>{" "}
                                    {sharedConfigEntry}
                                </Col>
                            </Row>
                            <Row>
                                <Col sm={12}>
                                    <DNASharedTable
                                        rows={sharedConfigRows}
                                        editConfig={this.showEditSharedConfigModal}
                                        deleteConfig={this.deleteSharedConfig}
                                    />
                                </Col>
                            </Row>
                        </Modal.Body>
                        <Modal.Footer>
                            <Button
                                bsStyle="default"
                                className="btn-cancel"
                                onClick={this.closeSharedListModal}
                            >
                                Close
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <Modal show={sharedConfigEntryModalShow} onHide={this.closeSharedModal}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={this.closeSharedModal}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>Manage DNA Plugin Shared Config Entry</Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup key="sharedHostname" controlId="sharedHostname">
                                            <Col sm={4}>
                                                <ControlLabel>Config Hostname</ControlLabel>
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    type="text"
                                                    value={sharedHostname}
                                                    disabled
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="sharedPort" controlId="sharedPort">
                                            <Col sm={4}>
                                                <ControlLabel>Config Port</ControlLabel>
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    type="number"
                                                    value={sharedPort}
                                                    disabled
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="sharedSecurePort" controlId="sharedSecurePort">
                                            <Col sm={4} title="Gives the secure (TLS) port number to use to connect to the host identified in dnaHostname (dnaSecurePortNum)">
                                                <ControlLabel>Secure Port</ControlLabel>
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    type="number"
                                                    value={sharedSecurePort}
                                                    disabled
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="sharedRemainingValues" controlId="sharedRemainingValues">
                                            <Col sm={4} title="Contains the number of values that are remaining and available to a server to assign to entries (dnaRemainingValues)">
                                                <ControlLabel>Remaining Values</ControlLabel>
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    type="number"
                                                    value={sharedRemainingValues}
                                                    disabled
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="sharedRemoteBindMethod" controlId="sharedRemoteBindMethod">
                                            <Col sm={4} title="Specifies the remote bind method: 'SIMPLE', 'SSL' (for SSL client auth), 'SASL/GSSAPI', or 'SASL/DIGEST-MD5'. (dnaRemoteBindMethod)">
                                                <ControlLabel>Remote Bind Method</ControlLabel>
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    type="text"
                                                    value={sharedRemoteBindMethod}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="sharedRemoteConnProtocol" controlId="sharedRemoteConnProtocol">
                                            <Col sm={4} title="Specifies the remote connection protocol: 'LDAP', or 'TLS'. (dnaRemoteConnProtocol)">
                                                <ControlLabel>Remote Connection Protocol</ControlLabel>
                                            </Col>
                                            <Col sm={8}>
                                                <FormControl
                                                    type="text"
                                                    value={sharedRemoteConnProtocol}
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
                                onClick={this.closeSharedModal}
                            >
                                Cancel
                            </Button>
                            <Button
                                bsStyle="primary"
                                onClick={this.editSharedConfig}
                            >
                                Save
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Distributed Numeric Assignment Plugin"
                    pluginName="Distributed Numeric Assignment"
                    cmdName="dna"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={12}>
                            <DNATable
                                rows={this.state.configRows}
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
                    </Row>
                </PluginBasicConfig>
                <ConfirmPopup
                    showModal={this.state.showConfirmSharedSave}
                    closeHandler={this.closeConfirmSharedSave}
                    actionFunc={this.openSharedListModal}
                    actionParam={this.state.sharedConfigEntry}
                    msg="The current entry state will be saved to the directory. Also, make sure you've set the shared config entry area."
                    msgContent="Are you ready to proceed?"
                />
            </div>
        );
    }
}

DNA.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

DNA.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default DNA;
