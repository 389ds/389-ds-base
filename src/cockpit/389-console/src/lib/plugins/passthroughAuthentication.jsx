import cockpit from "cockpit";
import React from "react";
import {
    Icon,
    Modal,
    Button,
    Row,
    Col,
    Form,
    Radio,
    noop,
    FormGroup,
    FormControl,
    Checkbox,
    ControlLabel
} from "patternfly-react";
import { Typeahead } from "react-bootstrap-typeahead";
import { PassthroughAuthURLsTable, PassthroughAuthConfigsTable } from "./pluginTables.jsx";
import PluginBasicPAMConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

class PassthroughAuthentication extends React.Component {
    componentDidUpdate() {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.loadPAMConfigs();
                this.loadURLs();
            }
        }
    }

    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            pamConfigRows: [],
            urlRows: [],
            attributes: [],

            pamConfigName: "",
            pamExcludeSuffix: [],
            pamIncludeSuffix: [],
            pamMissingSuffix: "",
            pamFilter: "",
            pamIDAttr: [],
            pamIDMapMethod: "",
            pamFallback: false,
            pamSecure: false,
            pamService: "",

            oldURL: "",
            urlConnType: "ldap",
            urlAuthDS: "",
            urlSubtree: "",
            urlMaxConns: "3",
            urlMaxOps: "5",
            urlTimeout: "300",
            urlLDVer: "3",
            urlConnLifeTime: "300",
            urlStartTLS: false,

            newPAMConfigEntry: false,
            newURLEntry: false,
            pamConfigEntryModalShow: false,
            urlEntryModalShow: false
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleCheckboxChange = this.handleCheckboxChange.bind(this);

        this.loadPAMConfigs = this.loadPAMConfigs.bind(this);
        this.loadURLs = this.loadURLs.bind(this);
        this.getAttributes = this.getAttributes.bind(this);

        this.openPAMModal = this.openPAMModal.bind(this);
        this.closePAMModal = this.closePAMModal.bind(this);
        this.showEditPAMConfigModal = this.showEditPAMConfigModal.bind(this);
        this.showAddPAMConfigModal = this.showAddPAMConfigModal.bind(this);
        this.cmdPAMOperation = this.cmdPAMOperation.bind(this);
        this.deletePAMConfig = this.deletePAMConfig.bind(this);
        this.addPAMConfig = this.addPAMConfig.bind(this);
        this.editPAMConfig = this.editPAMConfig.bind(this);

        this.openURLModal = this.openURLModal.bind(this);
        this.closeURLModal = this.closeURLModal.bind(this);
        this.showEditURLModal = this.showEditURLModal.bind(this);
        this.showAddURLModal = this.showAddURLModal.bind(this);
        this.cmdURLOperation = this.cmdURLOperation.bind(this);
        this.deleteURL = this.deleteURL.bind(this);
        this.addURL = this.addURL.bind(this);
        this.editURL = this.editURL.bind(this);
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    handleCheckboxChange(e) {
        this.setState({
            [e.target.id]: e.target.checked
        });
    }

    loadPAMConfigs() {
        this.setState({
            firstLoad: false
        });
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "list",
            "pam-configs"
        ];
        log_cmd("loadPAMConfigs", "Get PAM Passthough Authentication Plugin pamConfigs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    this.setState({
                        pamConfigRows: myObject.items.map(item => item.attrs)
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadPAMConfigs failed", errMsg.desc);
                    }
                });
    }

    loadURLs() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "list",
            "urls"
        ];
        this.props.toggleLoadingHandler();
        log_cmd("loadURLs", "Get PAM Passthough Authentication Plugin pamConfigs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    this.setState({
                        urlRows: myObject.items
                    });
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadURLs failed", errMsg.desc);
                    }
                    this.props.toggleLoadingHandler();
                });
    }

    showEditPAMConfigModal(rowData) {
        this.openPAMModal(rowData.cn[0]);
    }

    showAddPAMConfigModal(rowData) {
        this.openPAMModal();
    }

    openPAMModal(name) {
        this.getAttributes();
        if (!name) {
            this.setState({
                pamConfigEntryModalShow: true,
                newPAMConfigEntry: true,
                pamConfigName: "",
                pamExcludeSuffix: [],
                pamIncludeSuffix: [],
                pamMissingSuffix: "",
                pamFilter: "",
                pamIDAttr: [],
                pamIDMapMethod: "",
                pamFallback: false,
                pamSecure: false,
                pamService: ""
            });
        } else {
            let pamExcludeSuffixList = [];
            let pamIncludeSuffixList = [];
            let pamIDAttrList = [];
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "pass-through-auth",
                "pam-config",
                name,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd(
                "openModal",
                "Fetch the PAM Passthough Authentication Plugin pamConfig entry",
                cmd
            );
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        let pamConfigEntry = JSON.parse(content).attrs;
                        this.setState({
                            pamConfigEntryModalShow: true,
                            newPAMConfigEntry: false,
                            pamConfigName:
                            pamConfigEntry["cn"] === undefined ? "" : pamConfigEntry["cn"][0],
                            pamMissingSuffix:
                            pamConfigEntry["pammissingsuffix"] === undefined
                                ? ""
                                : pamConfigEntry["pammissingsuffix"][0],
                            pamFilter:
                            pamConfigEntry["pamfilter"] === undefined
                                ? ""
                                : pamConfigEntry["pamfilter"][0],
                            pamIDMapMethod:
                            pamConfigEntry["pamidmapmethod"] === undefined
                                ? ""
                                : pamConfigEntry["pamidmapmethod"][0],
                            pamFallback: !(
                                pamConfigEntry["pamfallback"] === undefined ||
                            pamConfigEntry["pamfallback"][0] == "FALSE"
                            ),
                            pamSecure: !(
                                pamConfigEntry["pamsecure"] === undefined ||
                            pamConfigEntry["pamsecure"][0] == "FALSE"
                            ),
                            pamService:
                            pamConfigEntry["pamservice"] === undefined
                                ? ""
                                : pamConfigEntry["pamservice"][0]
                        });
                        if (pamConfigEntry["pamexcludesuffix"] === undefined) {
                            this.setState({ pamExcludeSuffix: [] });
                        } else {
                            for (let value of pamConfigEntry["pamexcludesuffix"]) {
                                pamExcludeSuffixList = [...pamExcludeSuffixList, value];
                            }
                            this.setState({
                                pamExcludeSuffix: pamExcludeSuffixList
                            });
                        }

                        if (pamConfigEntry["pamincludesuffix"] === undefined) {
                            this.setState({ pamIncludeSuffix: [] });
                        } else {
                            for (let value of pamConfigEntry["pamincludesuffix"]) {
                                pamIncludeSuffixList = [...pamIncludeSuffixList, value];
                            }
                            this.setState({
                                pamIncludeSuffix: pamIncludeSuffixList
                            });
                        }

                        if (pamConfigEntry["pamidattr"] === undefined) {
                            this.setState({ pamIDAttr: [] });
                        } else {
                            for (let value of pamConfigEntry["pamidattr"]) {
                                pamIDAttrList = [...pamIDAttrList, value];
                            }
                            this.setState({ pamIDAttr: pamIDAttrList });
                        }

                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            pamConfigEntryModalShow: true,
                            newPAMConfigEntry: true,
                            pamConfigName: "",
                            pamExcludeSuffix: [],
                            pamIncludeSuffix: [],
                            pamMissingSuffix: "",
                            pamFilter: "",
                            pamIDAttr: [],
                            pamIDMapMethod: "",
                            pamFallback: false,
                            pamSecure: false,
                            pamService: ""
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    closePAMModal() {
        this.setState({ pamConfigEntryModalShow: false });
    }

    deletePAMConfig(rowData) {
        let pamConfigName = rowData.cn[0];
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "pam-config",
            pamConfigName,
            "delete"
        ];

        this.props.toggleLoadingHandler();
        log_cmd(
            "deletePAMConfig",
            "Delete the PAM Passthough Authentication Plugin pamConfig entry",
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deletePAMConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `PAMConfig entry ${pamConfigName} was successfully deleted`
                    );
                    this.loadPAMConfigs();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the pamConfig entry removal operation - ${errMsg.desc}`
                    );
                    this.loadPAMConfigs();
                    this.props.toggleLoadingHandler();
                });
    }

    addPAMConfig() {
        this.cmdPAMOperation("add");
    }

    editPAMConfig() {
        this.cmdPAMOperation("set");
    }

    cmdPAMOperation(action) {
        // Save table here too
        const {
            pamConfigName,
            pamExcludeSuffix,
            pamIncludeSuffix,
            pamMissingSuffix,
            pamFilter,
            pamIDAttr,
            pamIDMapMethod,
            pamFallback,
            pamSecure,
            pamService
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "pam-config",
            pamConfigName,
            action,
            "--missing-suffix",
            pamMissingSuffix || action == "add" ? pamMissingSuffix : "delete",
            "--filter",
            pamFilter || action == "add" ? pamFilter : "delete",
            "--id_map_method",
            pamIDMapMethod || action == "add" ? pamIDMapMethod : "delete",
            "--fallback",
            pamFallback ? "TRUE" : "FALSE",
            "--secure",
            pamSecure ? "TRUE" : "FALSE",
            "--service",
            pamService || action == "add" ? pamService : "delete"
        ];

        cmd = [...cmd, "--exclude-suffix"];
        if (pamExcludeSuffix.length != 0) {
            for (let value of pamExcludeSuffix) {
                cmd = [...cmd, value];
            }
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }
        cmd = [...cmd, "--include-suffix"];
        if (pamIncludeSuffix.length != 0) {
            for (let value of pamIncludeSuffix) {
                cmd = [...cmd, value];
            }
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }
        cmd = [...cmd, "--id-attr"];
        if (pamIDAttr.length != 0) {
            for (let value of pamIDAttr) {
                cmd = [...cmd, value];
            }
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        this.props.toggleLoadingHandler();
        log_cmd(
            "pamPassthroughAuthOperation",
            `Do the ${action} operation on the PAM Passthough Authentication Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("pamPassthroughAuthOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        `The ${action} operation was successfully done on "${pamConfigName}" entry`
                    );
                    this.loadPAMConfigs();
                    this.closePAMModal();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the pamConfig entry ${action} operation - ${errMsg.desc}`
                    );
                    this.loadPAMConfigs();
                    this.closePAMModal();
                    this.props.toggleLoadingHandler();
                });
    }

    showEditURLModal(rowData) {
        this.openURLModal(rowData.url);
    }

    showAddURLModal(rowData) {
        this.openURLModal();
    }

    openURLModal(url) {
        if (!url) {
            this.setState({
                urlEntryModalShow: true,
                newURLEntry: true,
                oldURL: "",
                urlConnType: "ldap",
                urlAuthDS: "",
                urlSubtree: "",
                urlMaxConns: "3",
                urlMaxOps: "5",
                urlTimeout: "300",
                urlLDVer: "3",
                urlConnLifeTime: "300",
                urlStartTLS: false
            });
        } else {
            let link = url.split(" ")[0];
            let params = url.split(" ")[1];
            this.setState({
                urlEntryModalShow: true,
                oldURL: url,
                newURLEntry: false,
                urlConnType: link.split(":")[0],
                urlAuthDS: link.split("/")[2],
                urlSubtree: link.split("/")[3],
                urlMaxConns: params.split(",")[0],
                urlMaxOps: params.split(",")[1],
                urlTimeout: params.split(",")[2],
                urlLDVer: params.split(",")[3],
                urlConnLifeTime: params.split(",")[4],
                urlStartTLS: !(params.split(",")[5] == "0")
            });
        }
    }

    closeURLModal() {
        this.setState({ urlEntryModalShow: false });
    }

    deleteURL(rowData) {
        let url = rowData.url;
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "url",
            "delete",
            url
        ];

        this.props.toggleLoadingHandler();
        log_cmd("deleteURL", "Delete the Passthough Authentication Plugin URL entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteURL", "Result", content);
                    this.props.addNotification("success", `URL ${url} was successfully deleted`);
                    this.loadURLs();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the URL removal operation - ${errMsg.desc}`
                    );
                    this.loadURLs();
                    this.props.toggleLoadingHandler();
                });
    }

    addURL() {
        this.cmdURLOperation("add");
    }

    editURL() {
        this.cmdURLOperation("modify");
    }

    cmdURLOperation(action) {
        const {
            oldURL,
            urlConnType,
            urlAuthDS,
            urlSubtree,
            urlMaxConns,
            urlMaxOps,
            urlTimeout,
            urlLDVer,
            urlConnLifeTime,
            urlStartTLS
        } = this.state;

        if (!urlAuthDS || !urlSubtree) {
            this.props.addNotification(
                "warning",
                "Authentication Hostname and Subtree fields are required."
            );
        } else {
            const constructedURL = `${urlConnType}://${urlAuthDS}/${urlSubtree} ${urlMaxConns},${urlMaxOps},${urlTimeout},${urlLDVer},${urlConnLifeTime},${
                urlStartTLS ? "1" : "0"
            }`;

            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "pass-through-auth",
                "url",
                action
            ];
            if (oldURL != "" && action == "modify") {
                cmd = [...cmd, oldURL, constructedURL];
            } else {
                cmd = [...cmd, constructedURL];
            }

            this.props.toggleLoadingHandler();
            log_cmd(
                "PassthroughAuthOperation",
                `Do the ${action} operation on the Passthough Authentication Plugin`,
                cmd
            );
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        console.info("PassthroughAuthOperation", "Result", content);
                        this.props.addNotification(
                            "success",
                            `The ${action} operation was successfully done on "${constructedURL}" entry`
                        );
                        this.loadURLs();
                        this.closeURLModal();
                        this.props.toggleLoadingHandler();
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Error during the URL ${action} operation - ${errMsg.desc}`
                        );
                        this.loadURLs();
                        this.closeURLModal();
                        this.props.toggleLoadingHandler();
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
            urlRows,
            pamConfigRows,
            attributes,
            pamConfigName,
            pamMissingSuffix,
            pamExcludeSuffix,
            pamIncludeSuffix,
            pamFilter,
            pamIDAttr,
            pamIDMapMethod,
            pamFallback,
            pamSecure,
            pamService,
            urlConnType,
            urlLDVer,
            urlAuthDS,
            urlSubtree,
            urlMaxConns,
            urlMaxOps,
            urlTimeout,
            urlConnLifeTime,
            urlStartTLS,
            newPAMConfigEntry,
            newURLEntry,
            pamConfigEntryModalShow,
            urlEntryModalShow
        } = this.state;

        const modalPAMConfigFields = {
            pamFilter: {
                name: "Filter",
                value: pamFilter,
                help: `Sets an LDAP filter to use to identify specific entries within the included suffixes for which to use PAM pass-through authentication (pamFilter)`
            },
            pamIDMapMethod: {
                name: "Map Method",
                value: pamIDMapMethod,
                help: `Gives the method to use to map the LDAP bind DN to a PAM identity (pamIDMapMethod)`
            },
            pamService: {
                name: "Service",
                value: pamService,
                help: `Contains the service name to pass to PAM (pamService)`
            }
        };

        const modalURLFields = {
            urlAuthDS: {
                name: "Authentication Hostname",
                value: urlAuthDS,
                help: `The authenticating directory host name. The port number of the Directory Server can be given by adding a colon and then the port number. For example, dirserver.example.com:389. If the port number is not specified, the PTA server attempts to connect using either of the standard ports: Port 389 if ldap:// is specified in the URL. Port 636 if ldaps:// is specified in the URL.`
            },
            urlSubtree: {
                name: "Subtree",
                value: urlSubtree,
                help: `The pass-through subtree. The PTA Directory Server passes through bind requests to the authenticating Directory Server from all clients whose DN is in this subtree.`
            },
            urlMaxConns: {
                name: "Maximum Number of Connections",
                value: urlMaxConns,
                help: `The maximum number of connections the PTA directory can simultaneously open to the authenticating directory.`
            },
            urlMaxOps: {
                name: "Maximum Number of Simultaneous Operations",
                value: urlMaxOps,
                help: `The maximum number of simultaneous operations (usually bind requests) the PTA directory can send to the authenticating directory within a single connection.`
            },
            urlTimeout: {
                name: "Timeout",
                value: urlTimeout,
                help: `The time limit, in seconds, that the PTA directory waits for a response from the authenticating Directory Server. If this timeout is exceeded, the server returns an error to the client. The default is 300 seconds (five minutes). Specify zero (0) to indicate no time limit should be enforced.`
            },
            urlConnLifeTime: {
                name: "Connection Life Time",
                value: urlConnLifeTime,
                help: `The time limit, in seconds, within which a connection may be used.`
            }
        };
        return (
            <div>
                <Modal show={pamConfigEntryModalShow} onHide={this.closePAMModal}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={this.closePAMModal}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>
                                {newPAMConfigEntry ? "Add" : "Edit"} PAM Passthough Authentication
                                Plugin Config Entry
                            </Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup key="pamConfigName" controlId="pamConfigName">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Config Name
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    required
                                                    type="text"
                                                    value={pamConfigName}
                                                    onChange={this.handleFieldChange}
                                                    disabled={!newPAMConfigEntry}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="pamExcludeSuffix"
                                            controlId="pamExcludeSuffix"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Specifies a suffix to exclude from PAM authentication (pamExcludeSuffix)"
                                            >
                                                Exclude Suffix
                                            </Col>
                                            <Col sm={9}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={values => {
                                                        this.setState({
                                                            pamExcludeSuffix: values
                                                        });
                                                    }}
                                                    selected={pamExcludeSuffix}
                                                    options={[""]}
                                                    newSelectionPrefix="Add a suffix: "
                                                    placeholder="Type a suffix DN..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="pamIncludeSuffix"
                                            controlId="pamIncludeSuffix"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Sets a suffix to include for PAM authentication (pamIncludeSuffix)"
                                            >
                                                Include Suffix
                                            </Col>
                                            <Col sm={9}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={values => {
                                                        this.setState({
                                                            pamIncludeSuffix: values
                                                        });
                                                    }}
                                                    selected={pamIncludeSuffix}
                                                    options={[""]}
                                                    newSelectionPrefix="Add a suffix: "
                                                    placeholder="Type a suffix DN..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="pamIDAttr"
                                            controlId="pamIDAttr"
                                            disabled={false}
                                        >
                                            <Col componentClass={ControlLabel} sm={3} title="Contains the attribute name which is used to hold the PAM user ID (pamIDAttr)">
                                                ID Attribute
                                            </Col>
                                            <Col sm={9}>
                                                <Typeahead
                                                    allowNew
                                                    multiple
                                                    onChange={value => {
                                                        this.setState({
                                                            pamIDAttr: value
                                                        });
                                                    }}
                                                    selected={pamIDAttr}
                                                    options={attributes}
                                                    newSelectionPrefix="Add an attribute: "
                                                    placeholder="Type an attribute..."
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup
                                            key="pamMissingSuffix"
                                            controlId="pamMissingSuffix"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={3}
                                                title="Identifies how to handle missing include or exclude suffixes (pamMissingSuffix)"
                                            >
                                                Missing Suffix
                                            </Col>
                                            <Col sm={9}>
                                                <div>
                                                    <Radio
                                                        id="pamMissingSuffix"
                                                        value="ERROR"
                                                        name="ERROR"
                                                        inline
                                                        checked={pamMissingSuffix === "ERROR"}
                                                        onChange={this.handleFieldChange}
                                                    >
                                                        ERROR
                                                    </Radio>
                                                    <Radio
                                                        id="pamMissingSuffix"
                                                        value="ALLOW"
                                                        name="ALLOW"
                                                        inline
                                                        checked={pamMissingSuffix === "ALLOW"}
                                                        onChange={this.handleFieldChange}
                                                    >
                                                        ALLOW
                                                    </Radio>
                                                    <Radio
                                                        id="pamMissingSuffix"
                                                        value="IGNORE"
                                                        name="IGNORE"
                                                        inline
                                                        checked={pamMissingSuffix === "IGNORE"}
                                                        onChange={this.handleFieldChange}
                                                    >
                                                        IGNORE
                                                    </Radio>
                                                </div>
                                            </Col>
                                        </FormGroup>
                                        {Object.entries(modalPAMConfigFields).map(
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

                                        <FormGroup key="pamCheckboxes" controlId="pamCheckboxes">
                                            <Row>
                                                <Col smOffset={1} sm={7}>
                                                    <Checkbox
                                                        id="pamFallback"
                                                        checked={pamFallback}
                                                        onChange={this.handleCheckboxChange}
                                                        title={`Sets whether to fallback to regular LDAP authentication if PAM authentication fails (pamFallback)`}
                                                    >
                                                        Fallback Enabled
                                                    </Checkbox>
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top">
                                                <Col smOffset={1} sm={7}>
                                                    <Checkbox
                                                        id="pamSecure"
                                                        checked={pamSecure}
                                                        onChange={this.handleCheckboxChange}
                                                        title="Requires secure TLS connection for PAM authentication (pamSecure)"
                                                    >
                                                        Require Secure Connection
                                                    </Checkbox>
                                                </Col>
                                            </Row>
                                        </FormGroup>
                                    </Form>
                                </Col>
                            </Row>
                        </Modal.Body>
                        <Modal.Footer>
                            <Button
                                bsStyle="default"
                                className="btn-cancel"
                                onClick={this.closePAMModal}
                            >
                                Cancel
                            </Button>
                            <Button
                                bsStyle="primary"
                                onClick={newPAMConfigEntry ? this.addPAMConfig : this.editPAMConfig}
                            >
                                Save
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <Modal show={urlEntryModalShow} onHide={this.closeURLModal}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={this.closeURLModal}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>
                                {newPAMConfigEntry ? "Add " : "Edit "}
                                Passthough Authentication Plugin URL
                            </Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup
                                            key="urlConnType"
                                            controlId="urlConnType"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={5}
                                                title="Defines whether TLS is used for communication between the two Directory Servers."
                                            >
                                                Connection Type
                                            </Col>
                                            <Col sm={7}>
                                                <div>
                                                    <Radio
                                                        id="urlConnType"
                                                        value="ldap"
                                                        name="ldap"
                                                        inline
                                                        checked={urlConnType === "ldap"}
                                                        onChange={this.handleFieldChange}
                                                    >
                                                        ldap
                                                    </Radio>
                                                    <Radio
                                                        id="urlConnType"
                                                        value="ldaps"
                                                        name="ldaps"
                                                        inline
                                                        checked={urlConnType === "ldaps"}
                                                        onChange={this.handleFieldChange}
                                                    >
                                                        ldaps
                                                    </Radio>
                                                </div>
                                            </Col>
                                        </FormGroup>
                                        {Object.entries(modalURLFields).map(([id, content]) => (
                                            <FormGroup key={id} controlId={id}>
                                                <Col componentClass={ControlLabel} sm={5} title={content.help}>
                                                    {content.name}
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        type="text"
                                                        value={content.value}
                                                        onChange={this.handleFieldChange}
                                                    />
                                                </Col>
                                            </FormGroup>
                                        ))}

                                        <FormGroup
                                            key="urlLDVer"
                                            controlId="urlLDVer"
                                            disabled={false}
                                        >
                                            <Col
                                                componentClass={ControlLabel}
                                                sm={5}
                                                title={`The version of the LDAP protocol used to connect to the authenticating directory. Directory Server supports LDAP version 2 and 3. The default is version 3, and Red Hat strongly recommends against using LDAPv2, which is old and will be deprecated.`}
                                            >
                                                Version
                                            </Col>
                                            <Col sm={7}>
                                                <div>
                                                    <Radio
                                                        id="urlLDVer"
                                                        value="2"
                                                        name="LDAPv2"
                                                        inline
                                                        checked={urlLDVer === "2"}
                                                        onChange={this.handleFieldChange}
                                                    >
                                                        LDAPv2
                                                    </Radio>
                                                    <Radio
                                                        id="urlLDVer"
                                                        value="3"
                                                        name="LDAPv3"
                                                        inline
                                                        checked={urlLDVer === "3"}
                                                        onChange={this.handleFieldChange}
                                                    >
                                                        LDAPv3
                                                    </Radio>
                                                </div>
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="urlStartTLS" controlId="urlStartTLS">
                                            <Col componentClass={ControlLabel} sm={5}>
                                                <Checkbox
                                                    id="urlStartTLS"
                                                    checked={urlStartTLS}
                                                    onChange={this.handleCheckboxChange}
                                                    title={`A flag of whether to use Start TLS for the connection to the authenticating directory. Start TLS establishes a secure connection over the standard port, so it is useful for connecting using LDAP instead of LDAPS. The TLS server and CA certificates need to be available on both of the servers. To use Start TLS, the LDAP URL must use ldap:, not ldaps:.`}
                                                >
                                                    Enable StartTLS
                                                </Checkbox>
                                            </Col>
                                        </FormGroup>
                                        <FormGroup key="resultURL" controlId="resultURL">
                                            <Col componentClass={ControlLabel} sm={5} title="The URL that will be added or modified after you click 'Save'">
                                                Result URL
                                            </Col>
                                            <Col sm={7}>
                                                {urlConnType}://{urlAuthDS}/{urlSubtree}{" "}
                                                {urlMaxConns},{urlMaxOps},{urlTimeout},
                                                {urlLDVer},{urlConnLifeTime},
                                                {urlStartTLS ? "1" : "0"}
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
                                onClick={this.closeURLModal}
                            >
                                Cancel
                            </Button>
                            <Button
                                bsStyle="primary"
                                onClick={newURLEntry ? this.addURL : this.editURL}
                            >
                                Save
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <PluginBasicPAMConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Pass Through Authentication"
                    pluginName="Pass Through Authentication"
                    cmdName="pass-through-auth"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={12}>
                            <PassthroughAuthURLsTable
                                rows={urlRows}
                                editConfig={this.showEditURLModal}
                                deleteConfig={this.deleteURL}
                            />
                            <Button
                                className="ds-margin-top"
                                bsStyle="primary"
                                onClick={this.showAddURLModal}
                            >
                                Add URL
                            </Button>
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={12}>
                            <PassthroughAuthConfigsTable
                                rows={pamConfigRows}
                                editConfig={this.showEditPAMConfigModal}
                                deleteConfig={this.deletePAMConfig}
                            />
                            <Button
                                className="ds-margin-top"
                                bsStyle="primary"
                                onClick={this.showAddPAMConfigModal}
                            >
                                Add Config
                            </Button>
                        </Col>
                    </Row>
                </PluginBasicPAMConfig>
            </div>
        );
    }
}

PassthroughAuthentication.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

PassthroughAuthentication.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default PassthroughAuthentication;
