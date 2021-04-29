import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { ConfirmPopup } from "./lib/notifications.jsx";
import { BackupTable } from "./lib/database/databaseTables.jsx";
import { BackupModal, RestoreModal, DeleteBackupModal } from "./lib/database/backups.jsx";
import { log_cmd, bad_file_name, valid_dn, valid_port } from "./lib/tools.jsx";

import {
    FormControl,
    FormGroup,
    ControlLabel,
    Form,
    noop,
    Checkbox,
    Spinner,
    Row,
    Modal,
    Icon,
    Col,
    Button
} from "patternfly-react";

export class CreateInstanceModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            createServerId: "",
            createPort: 389,
            createSecurePort: 636,
            createDM: "cn=Directory Manager",
            createDMPassword: "",
            createDMPasswordConfirm: "",
            createDBCheckbox: false,
            createDBSuffix: "",
            createDBName: "",
            createTLSCert: true,
            createInitDB: "noInit",
            loadingCreate: false,
            createOK: false,
            modalMsg: "",
            errObj: {},
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.createInstance = this.createInstance.bind(this);
        this.validInstName = this.validInstName.bind(this);
        this.validRootDN = this.validRootDN.bind(this);
        this.resetModal = this.resetModal.bind(this);
    }

    componentDidMount() {
        this.resetModal();
    }

    resetModal() {
        this.setState({
            createServerId: "",
            createPort: 389,
            createSecurePort: 636,
            createDM: "cn=Directory Manager",
            createDMPassword: "",
            createDMPasswordConfirm: "",
            createDBCheckbox: false,
            createDBSuffix: "",
            createDBName: "",
            createTLSCert: true,
            createInitDB: "noInit",
            loadingCreate: false,
            createOK: false,
            modalMsg: "",
            errObj: {
                createServerId: true,
                createDMPassword: true,
                createDMPasswordConfirm: true,
                createDBSuffix: false,
                createDBName: false,
            },
        });
    }

    validInstName(name) {
        return /^[\w@_:-]*$/.test(name);
    }

    validRootDN(dn) {
        // Validate a DN for Directory Manager.  We have to be stricter than
        // valid_dn() and only allow stand ascii characters for the value
        if (dn.endsWith(",")) {
            return false;
        }
        // Check that the attr is only letters  [A-Za-z]+  and the value does not
        // start with a space (?=\\S) AND all the characters are standard
        // ascii ([ -~]+)
        let dn_regex = new RegExp("^([A-Za-z]+=(?=\\S)([ -~]+)$)");

        let result = dn_regex.test(dn);
        return result;
    }

    handleFieldChange(e) {
        let value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        let target_id = e.target.id;
        let valueErr = false;
        let errObj = this.state.errObj;
        let all_good = true;
        let modal_msg = "";

        errObj[target_id] = valueErr;
        if (target_id == 'createServerId') {
            if (value == "") {
                all_good = false;
                errObj['createServerId'] = true;
            } else if (value.length > 80) {
                all_good = false;
                errObj['createServerId'] = true;
                modal_msg = "Instance name must be less than 80 characters";
            } else if (!this.validInstName(value)) {
                all_good = false;
                errObj['createServerId'] = true;
                modal_msg = "Instance name can only contain letters, numbers, and these 4 characters:  - @ : _";
            }
        } else if (this.state.createServerId == "") {
            all_good = false;
            errObj['createServerId'] = true;
        } else if (!this.validInstName(this.state.createServerId)) {
            all_good = false;
            errObj['createServerId'] = true;
            modal_msg = "Not all required fields have values";
        }
        if (target_id == 'createPort') {
            if (value == "") {
                all_good = false;
                errObj['createPort'] = true;
            } else if (!valid_port(value)) {
                all_good = false;
                errObj['createPort'] = true;
                modal_msg = "Invalid Port number.  The port must be between 1 and 65535";
            }
        } else if (this.state.createPort == "") {
            all_good = false;
            errObj['createPort'] = true;
        } else if (!valid_port(this.state.createPort)) {
            all_good = false;
            errObj['createPort'] = true;
            modal_msg = "Invalid Port number.  The port must be between 1 and 65535";
        }
        if (target_id == 'createSecurePort') {
            if (value == "") {
                all_good = false;
                errObj['createSecurePort'] = true;
            } else if (!valid_port(value)) {
                all_good = false;
                errObj['createSecurePort'] = true;
                modal_msg = "Invalid Secure Port number.  Port must be between 1 and 65535";
            }
        } else if (this.state.createSecurePort == "") {
            all_good = false;
            errObj['createSecurePort'] = true;
        }
        if (target_id == 'createDM') {
            if (value == "") {
                all_good = false;
                errObj['createDM'] = true;
            }
            if (!this.validRootDN(value)) {
                all_good = false;
                errObj['createDM'] = true;
                modal_msg = "Invalid DN for Directory Manager";
            }
        } else if (this.state.createDM == "") {
            all_good = false;
            errObj['createDM'] = true;
        } else if (!this.validRootDN(this.state.createDM)) {
            all_good = false;
            errObj['createDM'] = true;
            modal_msg = "Invalid DN for Directory Manager";
        }
        if (e.target.id == 'createDMPassword') {
            if (value == "") {
                all_good = false;
                errObj['createDMPassword'] = true;
            } else if (value != this.state.createDMPasswordConfirm) {
                all_good = false;
                errObj['createDMPassword'] = true;
                errObj['createDMPasswordConfirm'] = true;
                modal_msg = "Passwords Do Not Match";
            } else if (value.length < 8) {
                all_good = false;
                errObj['createDMPassword'] = true;
                modal_msg = "Directory Manager password must be at least 8 characters long";
            } else {
                errObj['createDMPassword'] = false;
                errObj['createDMPasswordConfirm'] = false;
            }
        } else if (this.state.createDMPassword == "") {
            all_good = false;
            errObj['createDMPasswordConfirm'] = true;
        }
        if (e.target.id == 'createDMPasswordConfirm') {
            if (value == "") {
                all_good = false;
                errObj['createDMPasswordConfirm'] = true;
            } else if (value != this.state.createDMPassword) {
                all_good = false;
                errObj['createDMPassword'] = true;
                errObj['createDMPasswordConfirm'] = true;
                modal_msg = "Passwords Do Not Match";
            } else if (value.length < 8) {
                all_good = false;
                errObj['createDMPasswordConfirm'] = true;
                modal_msg = "Directory Manager password must be at least 8 characters long";
            } else {
                errObj['createDMPassword'] = false;
                errObj['createDMPasswordConfirm'] = false;
            }
        } else if (this.state.createDMPasswordConfirm == "") {
            all_good = false;
            errObj['createDMPasswordConfirm'] = true;
        }

        // Optional settings
        if (target_id == 'createDBCheckbox') {
            if (!value) {
                errObj['createDBSuffix'] = false;
                errObj['createDBName'] = false;
            } else {
                if (this.state.createDBSuffix == "") {
                    all_good = false;
                    errObj['createDBSuffix'] = true;
                } else if (!valid_dn(this.state.createDBSuffix)) {
                    all_good = false;
                    errObj['createDBSuffix'] = true;
                    modal_msg = "Invalid DN for suffix";
                }
                if (this.state.createDBName == "") {
                    all_good = false;
                    errObj['createDBName'] = true;
                } else if (!valid_dn(this.state.createDBName)) {
                    all_good = false;
                    errObj['createDBName'] = true;
                    modal_msg = "Invalid name for database";
                }
            }
        } else if (this.state.createDBCheckbox) {
            if (target_id == 'createDBSuffix') {
                if (value == "") {
                    all_good = false;
                    errObj['createDBSuffix'] = true;
                } else if (!valid_dn(value)) {
                    all_good = false;
                    errObj['createDBSuffix'] = true;
                    modal_msg = "Invalid DN for suffix";
                }
            } else if (this.state.createDBSuffix == "") {
                all_good = false;
                errObj['createDBSuffix'] = true;
            } else if (!valid_dn(this.state.createDBSuffix)) {
                all_good = false;
                errObj['createDBSuffix'] = true;
                modal_msg = "Invalid DN for suffix";
            }
            if (target_id == 'createDBName') {
                if (value == "") {
                    all_good = false;
                    errObj['createDBName'] = true;
                } else if (/\s/.test(value)) {
                    // name has some kind of white space character
                    all_good = false;
                    errObj['createDBName'] = true;
                    modal_msg = "Database name can not contain any spaces";
                }
            } else if (this.state.createDBName == "") {
                all_good = false;
                errObj['createDBName'] = true;
            } else if (/\s/.test(this.state.createDBName)) {
                all_good = false;
                errObj['createDBName'] = true;
                modal_msg = "Invalid database name";
            }
        } else {
            errObj['createDBSuffix'] = false;
            errObj['createDBName'] = false;
        }

        this.setState({
            [target_id]: value,
            errObj: errObj,
            createOK: all_good,
            modalMsg: modal_msg,
        });
    }

    createInstance() {
        const {
            createServerId,
            createPort,
            createSecurePort,
            createDM,
            createDMPassword,
            createDBSuffix,
            createDBName,
            createTLSCert,
            createInitDB,
            createDBCheckbox
        } = this.state;
        const { closeHandler, addNotification, loadInstanceList } = this.props;

        let setup_inf =
            "[general]\n" +
            "config_version = 2\n" +
            "full_machine_name = FQDN\n\n" +
            "[slapd]\n" +
            "user = dirsrv\n" +
            "group = dirsrv\n" +
            "instance_name = INST_NAME\n" +
            "port = PORT\n" +
            "root_dn = ROOTDN\n" +
            "root_password = ROOTPW\n" +
            "secure_port = SECURE_PORT\n" +
            "self_sign_cert = SELF_SIGN\n";

        // Server ID
        let newServerId = createServerId;
        newServerId = newServerId.replace(/^slapd-/i, ""); // strip "slapd-"
        setup_inf = setup_inf.replace("INST_NAME", newServerId);
        setup_inf = setup_inf.replace("PORT", createPort);
        setup_inf = setup_inf.replace("SECURE_PORT", createSecurePort);
        setup_inf = setup_inf.replace("ROOTDN", createDM);
        setup_inf = setup_inf.replace("ROOTPW", createDMPassword);
        // Setup Self-Signed Certs
        if (createTLSCert) {
            setup_inf = setup_inf.replace("SELF_SIGN", "True");
        } else {
            setup_inf = setup_inf.replace("SELF_SIGN", "False");
        }

        if (createDBCheckbox) {
            setup_inf += "\n[backend-" + createDBName + "]\nsuffix = " + createDBSuffix + "\n";
            if (createInitDB === "createSample") {
                setup_inf += "sample_entries = yes\n";
            }
            if (createInitDB === "createSuffix") {
                setup_inf += "create_suffix_entry = yes\n";
            }
        }

        /*
         * Here are steps we take to create the instance
         *
         * [1] Get FQDN Name for nsslapd-localhost setting in setup file
         * [2] Create a file for the inf setup parameters
         * [3] Set strict permissions on that file
         * [4] Populate the new setup file with settings (including cleartext password)
         * [5] Create the instance
         * [6] Remove setup file
         */
        this.setState({
            loadingCreate: true
        });
        cockpit
                .spawn(["hostnamectl", "status", "--static"], { superuser: true, err: "message" })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.setState({
                        loadingCreate: false
                    });
                    addNotification("error", `Failed to get hostname!", ${errMsg.desc}`);
                })
                .done(data => {
                    /*
                     * We have FQDN, so set the hostname in inf file, and create the setup file
                     */
                    setup_inf = setup_inf.replace("FQDN", data);
                    let setup_file = "/tmp/389-setup-" + new Date().getTime() + ".inf";
                    let rm_cmd = ["rm", setup_file];
                    let create_file_cmd = ["touch", setup_file];
                    log_cmd("createInstance", "Setting FQDN...", create_file_cmd);
                    cockpit
                            .spawn(create_file_cmd, { superuser: true, err: "message" })
                            .fail(err => {
                                this.setState({
                                    loadingCreate: false
                                });
                                addNotification(
                                    "error",
                                    `Failed to create installation file!" ${err.message}`
                                );
                            })
                            .done(_ => {
                                /*
                                 * We have our new setup file, now set permissions on that setup file before we add sensitive data
                                 */
                                let chmod_cmd = ["chmod", "600", setup_file];
                                log_cmd("createInstance", "Setting initial INF file permissions...", chmod_cmd);
                                cockpit
                                        .spawn(chmod_cmd, { superuser: true, err: "message" })
                                        .fail(err => {
                                            cockpit.spawn(rm_cmd, { superuser: true, err: "message" }); // Remove Inf file with clear text password
                                            this.setState({
                                                loadingCreate: false
                                            });
                                            addNotification(
                                                "error",
                                                `Failed to set permissions on setup file ${setup_file}: ${err.message}`
                                            );
                                        })
                                        .done(_ => {
                                            /*
                                             * Success we have our setup file and it has the correct permissions.
                                             * Now populate the setup file...
                                             */
                                            let cmd = [
                                                "/bin/sh",
                                                "-c",
                                                '/usr/bin/echo -e "' + setup_inf + '" >> ' + setup_file
                                            ];
                                            // Do not log inf file as it contains the DM password
                                            log_cmd("createInstance", "Apply changes to INF file...", "");
                                            cockpit
                                                    .spawn(cmd, { superuser: true, err: "message" })
                                                    .fail(err => {
                                                        this.setState({
                                                            loadingCreate: false
                                                        });
                                                        addNotification(
                                                            "error",
                                                            `Failed to populate installation file! ${err.message}`
                                                        );
                                                    })
                                                    .done(_ => {
                                                        /*
                                                         * Next, create the instance...
                                                         */
                                                        let cmd = ["dscreate", "-j", "from-file", setup_file];
                                                        log_cmd("createInstance", "Creating instance...", cmd);
                                                        cockpit
                                                                .spawn(cmd, {
                                                                    superuser: true,
                                                                    err: "message"
                                                                })
                                                                .fail(err => {
                                                                    let errMsg = JSON.parse(err);
                                                                    cockpit.spawn(rm_cmd, { superuser: true }); // Remove Inf file with clear text password
                                                                    this.setState({
                                                                        loadingCreate: false
                                                                    });
                                                                    addNotification(
                                                                        "error",
                                                                        `${errMsg.desc}`
                                                                    );
                                                                })
                                                                .done(_ => {
                                                                    // Success!!!  Now cleanup everything up...
                                                                    log_cmd("createInstance", "Instance creation compelete, clean everything up...", rm_cmd);
                                                                    cockpit.spawn(rm_cmd, { superuser: true }); // Remove Inf file with clear text password
                                                                    this.setState({
                                                                        loadingCreate: false
                                                                    });

                                                                    loadInstanceList(createServerId);
                                                                    addNotification(
                                                                        "success",
                                                                        `Successfully created instance: slapd-${createServerId}`
                                                                    );
                                                                    closeHandler();
                                                                    this.resetModal();
                                                                });
                                                    });
                                        });
                            });
                });
    }

    render() {
        const { showModal, closeHandler } = this.props;

        const {
            loadingCreate,
            createServerId,
            createPort,
            createSecurePort,
            createDM,
            createDMPassword,
            createDMPasswordConfirm,
            createDBCheckbox,
            createDBSuffix,
            createDBName,
            createTLSCert,
            createInitDB,
            createOK,
            modalMsg,
            errObj,
        } = this.state;
        let errMsgClass = "";
        let errMsg = "";
        let createSpinner = "";
        if (loadingCreate) {
            createSpinner = (
                <Row>
                    <div className="ds-margin-top-lg ds-modal-spinner">
                        <Spinner loading inline size="lg" />
                        Creating instance...
                    </div>
                </Row>
            );
        }

        if (modalMsg == "") {
            // No errors, but to keep the modal nice and stable during input
            // field validation we need "invisible" text to keep the modal form
            // from jumping up and down.
            errMsgClass = "ds-clear-text";
            errMsg = "no errors";
        } else {
            // We have error text to report
            errMsgClass = "ds-modal-error";
            errMsg = modalMsg;
        }

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title className="ds-center">Create New Server Instance</Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal>
                            <Row>
                                <Col className="ds-center" sm={12}>
                                    <p className={errMsgClass}>{errMsg}</p>
                                </Col>
                            </Row>
                            <FormGroup controlId="createServerId" className="ds-margin-top-lg">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={5}
                                    title="The instance name, this is what gets appended to 'slapi-'. The instance name can only contain letters, numbers, and:  # @ : - _"
                                >
                                    Instance Name
                                </Col>
                                <Col sm={7}>
                                    <FormControl
                                        id="createServerId"
                                        type="text"
                                        placeholder="Your_Instance_Name"
                                        value={createServerId}
                                        onChange={this.handleFieldChange}
                                        className={errObj.createServerId ? "ds-input-bad" : ""}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup controlId="createPort">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={5}
                                    title="The server port number"
                                >
                                    Port
                                </Col>
                                <Col sm={7}>
                                    <FormControl
                                        id="createPort"
                                        type="number"
                                        min="0"
                                        max="65535"
                                        value={createPort}
                                        onChange={this.handleFieldChange}
                                        className={errObj.createPort ? "ds-input-bad" : ""}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup controlId="createSecurePort">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={5}
                                    title="The secure port number for TLS connections"
                                >
                                    Secure Port
                                </Col>
                                <Col sm={7}>
                                    <FormControl
                                        id="createSecurePort"
                                        type="number"
                                        min="0"
                                        max="65535"
                                        value={createSecurePort}
                                        onChange={this.handleFieldChange}
                                        className={errObj.createSecurePort ? "ds-input-bad" : ""}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup controlId="createTLSCert">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={5}
                                    title="Create a self-signed certificate database"
                                >
                                    Create Self-Signed TLS Certificate
                                </Col>
                                <Col sm={7}>
                                    <Checkbox
                                        id="createTLSCert"
                                        checked={createTLSCert}
                                        onChange={this.handleFieldChange}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup controlId="createDM">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={5}
                                    title="The DN for the unrestricted user"
                                >
                                    Directory Manager DN
                                </Col>
                                <Col sm={7}>
                                    <FormControl
                                        type="text"
                                        id="createDM"
                                        onChange={this.handleFieldChange}
                                        value={createDM}
                                        className={errObj.createDM ? "ds-input-bad" : ""}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup controlId="createDMPassword">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={5}
                                    title="Directory Manager password."
                                >
                                    Directory Manager Password
                                </Col>
                                <Col sm={7}>
                                    <FormControl
                                        id="createDMPassword"
                                        type="password"
                                        placeholder="Enter password"
                                        onChange={this.handleFieldChange}
                                        value={createDMPassword}
                                        className={errObj.createDMPassword ? "ds-input-bad" : ""}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup controlId="createDMPasswordConfirm">
                                <Col componentClass={ControlLabel} sm={5} title="Confirm password.">
                                    Confirm Password
                                </Col>
                                <Col sm={7}>
                                    <FormControl
                                        id="createDMPasswordConfirm"
                                        type="password"
                                        placeholder="Confirm password"
                                        onChange={this.handleFieldChange}
                                        value={createDMPasswordConfirm}
                                        className={errObj.createDMPasswordConfirm ? "ds-input-bad" : ""}
                                    />
                                </Col>
                            </FormGroup>
                            <hr />
                            <FormGroup controlId="createDBCheckbox">
                                <Col componentClass={ControlLabel} sm={5} title="Confirm password.">
                                    <Checkbox
                                        id="createDBCheckbox"
                                        checked={createDBCheckbox}
                                        onChange={this.handleFieldChange}
                                    >
                                        Create Database
                                    </Checkbox>
                                </Col>
                            </FormGroup>
                            <FormGroup className="ds-margin-top-lg" controlId="createDBSuffix">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={5}
                                    title="Database suffix, like 'dc=example,dc=com'. The suffix must be a valid LDAP Distiguished Name (DN)"
                                >
                                    Database Suffix
                                </Col>
                                <Col sm={7}>
                                    <FormControl
                                        type="text"
                                        id="createDBSuffix"
                                        placeholder="e.g. dc=example,dc=com"
                                        onChange={this.handleFieldChange}
                                        value={createDBSuffix}
                                        disabled={!createDBCheckbox}
                                        className={errObj.createDBSuffix ? "ds-input-bad" : ""}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup controlId="createDBName">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={5}
                                    title="The name for the backend database, like 'userroot'. The name can be a combination of alphanumeric characters, dashes (-), and underscores (_). No other characters are allowed, and the name must be unique across all backends."
                                >
                                    Database Name
                                </Col>
                                <Col sm={7}>
                                    <FormControl
                                        type="text"
                                        id="createDBName"
                                        placeholder="e.g. userRoot"
                                        onChange={this.handleFieldChange}
                                        value={createDBName}
                                        disabled={!createDBCheckbox}
                                        className={errObj.createDBName ? "ds-input-bad" : ""}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup
                                key="createInitDB"
                                controlId="createInitDB"
                            >
                                <Col componentClass={ControlLabel} sm={5}>
                                    Database Initialization
                                </Col>
                                <Col sm={7}>
                                    <select
                                        className="btn btn-default dropdown"
                                        id="createInitDB"
                                        onChange={this.handleFieldChange}
                                        disabled={!createDBCheckbox}
                                        value={createInitDB}
                                    >
                                        <option value="noInit">Do Not Initialize Database</option>
                                        <option value="createSuffix">Create Suffix Entry</option>
                                        <option value="createSample">Create Sample Entries</option>
                                    </select>
                                </Col>
                            </FormGroup>
                        </Form>
                        {createSpinner}
                    </Modal.Body>
                    <Modal.Footer>
                        <Button bsStyle="default" className="btn-cancel" onClick={closeHandler}>
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={this.createInstance}
                            disabled={!createOK}
                        >
                            Create Instance
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

export class SchemaReloadModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            reloadSchemaDir: "",
            loadingSchemaTask: false
        };

        this.reloadSchema = this.reloadSchema.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    reloadSchema(e) {
        const { addNotification, serverId, closeHandler } = this.props;
        const { reloadSchemaDir } = this.state;

        this.setState({
            loadingSchemaTask: true
        });

        let cmd = ["dsconf", "-j", serverId, "schema", "reload", "--wait"];
        if (reloadSchemaDir !== "") {
            cmd = [...cmd, "--schemadir", reloadSchemaDir];
        }
        log_cmd("reloadSchemaDir", "Reload schema files", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(data => {
                    addNotification("success", "Successfully reloaded schema");
                    this.setState({
                        loadingSchemaTask: false
                    });
                    closeHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    addNotification("error", `Failed to reload schema files - ${errMsg.desc}`);
                    closeHandler();
                });
    }

    render() {
        const { loadingSchemaTask, reloadSchemaDir } = this.state;
        const { showModal, closeHandler } = this.props;

        let spinner = "";
        if (loadingSchemaTask) {
            spinner = (
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner loading inline size="md" />
                        Reloading schema files...
                    </div>
                </Row>
            );
        }

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>Reload Schema Files</Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <Row title="The name of the database link.">
                                <Col sm={3}>
                                    <ControlLabel>Schema File Directory:</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <FormControl
                                        type="text"
                                        id="reloadSchemaDir"
                                        value={reloadSchemaDir}
                                        onChange={this.handleFieldChange}
                                    />
                                </Col>
                            </Row>
                            {spinner}
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button bsStyle="default" className="btn-cancel" onClick={closeHandler}>
                            Cancel
                        </Button>
                        <Button bsStyle="primary" onClick={this.reloadSchema}>
                            Reload Schema
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

export class ManageBackupsModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            activeKey: 1,
            showConfirmBackupDelete: false,
            showConfirmBackup: false,
            showConfirmRestore: false,
            showConfirmRestoreReplace: false,
            showConfirmLDIFReplace: false,
            showRestoreSpinningModal: false,
            showDelBackupSpinningModal: false,
            showBackupModal: false,
            backupSpinning: false,
            backupName: "",
            deleteBackup: "",
            errObj: {}
        };

        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleChange = this.handleChange.bind(this);

        // Backups
        this.doBackup = this.doBackup.bind(this);
        this.deleteBackup = this.deleteBackup.bind(this);
        this.restoreBackup = this.restoreBackup.bind(this);
        this.showConfirmRestore = this.showConfirmRestore.bind(this);
        this.closeConfirmRestore = this.closeConfirmRestore.bind(this);
        this.showConfirmBackup = this.showConfirmBackup.bind(this);
        this.closeConfirmBackup = this.closeConfirmBackup.bind(this);
        this.showConfirmBackupDelete = this.showConfirmBackupDelete.bind(this);
        this.closeConfirmBackupDelete = this.closeConfirmBackupDelete.bind(this);
        this.showBackupModal = this.showBackupModal.bind(this);
        this.closeBackupModal = this.closeBackupModal.bind(this);
        this.showRestoreSpinningModal = this.showRestoreSpinningModal.bind(this);
        this.closeRestoreSpinningModal = this.closeRestoreSpinningModal.bind(this);
        this.showDelBackupSpinningModal = this.showDelBackupSpinningModal.bind(this);
        this.closeDelBackupSpinningModal = this.closeDelBackupSpinningModal.bind(this);
        this.validateBackup = this.validateBackup.bind(this);
        this.closeConfirmRestoreReplace = this.closeConfirmRestoreReplace.bind(this);
    }

    closeExportModal() {
        this.setState({
            showExportModal: false
        });
    }

    showDelBackupSpinningModal() {
        this.setState({
            showDelBackupSpinningModal: true
        });
    }

    closeDelBackupSpinningModal() {
        this.setState({
            showDelBackupSpinningModal: false
        });
    }

    showRestoreSpinningModal() {
        this.setState({
            showRestoreSpinningModal: true
        });
    }

    closeRestoreSpinningModal() {
        this.setState({
            showRestoreSpinningModal: false
        });
    }

    showBackupModal() {
        this.setState({
            showBackupModal: true,
            backupSpinning: false,
            backupName: ""
        });
    }

    closeBackupModal() {
        this.setState({
            showBackupModal: false
        });
    }

    showConfirmBackup(item) {
        // call deleteLDIF
        this.setState({
            showConfirmBackup: true,
            backupName: item.name
        });
    }

    closeConfirmBackup() {
        // call importLDIF
        this.setState({
            showConfirmBackup: false
        });
    }

    showConfirmRestore(item) {
        this.setState({
            showConfirmRestore: true,
            backupName: item.name
        });
    }

    closeConfirmRestore() {
        // call importLDIF
        this.setState({
            showConfirmRestore: false
        });
    }

    showConfirmBackupDelete(item) {
        // calls deleteBackup
        this.setState({
            showConfirmBackupDelete: true,
            backupName: item.name
        });
    }

    closeConfirmBackupDelete() {
        // call importLDIF
        this.setState({
            showConfirmBackupDelete: false
        });
    }

    closeConfirmRestoreReplace() {
        this.setState({
            showConfirmRestoreReplace: false
        });
    }

    validateBackup() {
        for (let i = 0; i < this.props.backups.length; i++) {
            if (this.state.backupName == this.props.backups[i]["name"]) {
                this.setState({
                    showConfirmRestoreReplace: true
                });
                return;
            }
        }
        this.doBackup();
    }

    doBackup() {
        this.setState({
            backupSpinning: true
        });

        let cmd = ["dsctl", "-j", this.props.serverId, "status"];
        cockpit
                .spawn(cmd, { superuser: true })
                .done(status_data => {
                    let status_json = JSON.parse(status_data);
                    if (status_json.running == true) {
                        let cmd = [
                            "dsconf",
                            "-j",
                            "ldapi://%2fvar%2frun%2fslapd-" + "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket" + ".socket",
                            "backup",
                            "create"
                        ];
                        if (this.state.backupName != "") {
                            if (bad_file_name(this.state.backupName)) {
                                this.props.addNotification(
                                    "warning",
                                    `Backup name should not be a path.  All backups are stored in the server's backup directory`
                                );
                                return;
                            }
                            cmd.push(this.state.backupName);
                        }

                        log_cmd("doBackup", "Add backup task online", cmd);
                        cockpit
                                .spawn(cmd, { superuser: true, err: "message" })
                                .done(content => {
                                    this.props.reload();
                                    this.closeBackupModal();
                                    this.props.addNotification("success", `Server has been backed up`);
                                })
                                .fail(err => {
                                    let errMsg = JSON.parse(err);
                                    this.props.reload();
                                    this.closeBackupModal();
                                    this.props.addNotification(
                                        "error",
                                        `Failure backing up server - ${errMsg.desc}`
                                    );
                                });
                    } else {
                        const cmd = ["dsctl", "-j", this.props.serverId, "db2bak"];
                        if (this.state.backupName != "") {
                            if (bad_file_name(this.state.backupName)) {
                                this.props.addNotification(
                                    "warning",
                                    `Backup name should not be a path.  All backups are stored in the server's backup directory`
                                );
                                return;
                            }
                            cmd.push(this.state.backupName);
                        }
                        log_cmd("doBackup", "Doing backup of the server offline", cmd);
                        cockpit
                                .spawn(cmd, { superuser: true })
                                .done(content => {
                                    this.props.reload();
                                    this.closeBackupModal();
                                    this.props.addNotification("success", `Server has been backed up`);
                                })
                                .fail(err => {
                                    let errMsg = JSON.parse(err);
                                    this.props.reload();
                                    this.closeBackupModal();
                                    this.props.addNotification(
                                        "error",
                                        `Failure backing up server - ${errMsg.desc}`
                                    );
                                });
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    console.log("Failed to check the server status", errMsg.desc);
                });
    }

    restoreBackup() {
        this.showRestoreSpinningModal();
        let cmd = ["dsctl", "-j", this.props.serverId, "status"];
        cockpit
                .spawn(cmd, { superuser: true })
                .done(status_data => {
                    let status_json = JSON.parse(status_data);
                    if (status_json.running == true) {
                        const cmd = [
                            "dsconf",
                            "-j",
                            "ldapi://%2fvar%2frun%2fslapd-" + "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket" + ".socket",
                            "backup",
                            "restore",
                            this.state.backupName
                        ];
                        log_cmd("restoreBackup", "Restoring server online", cmd);
                        cockpit
                                .spawn(cmd, { superuser: true, err: "message" })
                                .done(content => {
                                    this.closeRestoreSpinningModal();
                                    this.props.addNotification("success", `Server has been restored`);
                                })
                                .fail(err => {
                                    let errMsg = JSON.parse(err);
                                    this.closeRestoreSpinningModal();
                                    this.props.addNotification(
                                        "error",
                                        `Failure restoring up server - ${errMsg.desc}`
                                    );
                                });
                    } else {
                        const cmd = [
                            "dsctl",
                            "-j",
                            this.props.serverId,
                            "bak2db",
                            this.state.backupName
                        ];
                        log_cmd("restoreBackup", "Restoring server offline", cmd);
                        cockpit
                                .spawn(cmd, { superuser: true, err: "message" })
                                .done(content => {
                                    this.closeRestoreSpinningModal();
                                    this.props.addNotification("success", `Server has been restored`);
                                })
                                .fail(err => {
                                    let errMsg = JSON.parse(err);
                                    this.closeRestoreSpinningModal();
                                    this.props.addNotification(
                                        "error",
                                        `Failure restoring up server - ${errMsg.desc}`
                                    );
                                });
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    console.log("Failed to check the server status", errMsg.desc);
                });
    }

    deleteBackup(e) {
        // Show confirmation
        this.showDelBackupSpinningModal();

        const cmd = [
            "dsctl",
            "-j",
            this.props.serverId,
            "backups",
            "--delete",
            this.state.backupName
        ];
        log_cmd("deleteBackup", "Deleting backup", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.closeDelBackupSpinningModal();
                    this.props.addNotification("success", `Backup was successfully deleted`);
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.closeDelBackupSpinningModal();
                    this.props.addNotification("error", `Failure deleting backup - ${errMsg.desc}`);
                });
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    handleChange(e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        let valueErr = false;
        let errObj = this.state.errObj;
        if (value == "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        });
    }

    render() {
        const { showModal, closeHandler, backups, reload, loadingBackup } = this.props;

        let backupSpinner = "";
        if (loadingBackup) {
            backupSpinner = (
                <Row>
                    <div className="ds-margin-top-lg ds-modal-spinner">
                        <Spinner loading inline size="lg" />
                        Creating instance...
                    </div>
                </Row>
            );
        }

        return (
            <div>
                <Modal show={showModal} onHide={closeHandler}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={closeHandler}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>Manage Backups</Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <div className="ds-margin-top-xlg">
                                <BackupTable
                                    rows={backups}
                                    confirmRestore={this.showConfirmRestore}
                                    confirmDelete={this.showConfirmBackupDelete}
                                />
                            </div>
                            {backupSpinner}
                        </Modal.Body>
                        <Modal.Footer>
                            <Button
                                bsStyle="primary"
                                onClick={this.showBackupModal}
                                className="ds-margin-top"
                            >
                                Create Backup
                            </Button>
                            <Button
                                bsStyle="default"
                                onClick={reload}
                                className="ds-left-margin ds-margin-top"
                            >
                                Refresh Backups
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <BackupModal
                    showModal={this.state.showBackupModal}
                    closeHandler={this.closeBackupModal}
                    handleChange={this.handleChange}
                    saveHandler={this.validateBackup}
                    spinning={this.state.backupSpinning}
                    error={this.state.errObj}
                />
                <RestoreModal
                    showModal={this.state.showRestoreSpinningModal}
                    closeHandler={this.closeRestoreSpinningModal}
                    msg={this.state.backupName}
                />
                <DeleteBackupModal
                    showModal={this.state.showDelBackupSpinningModal}
                    closeHandler={this.closeDelBackupSpinningModal}
                    msg={this.state.backupName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmRestore}
                    closeHandler={this.closeConfirmRestore}
                    actionFunc={this.restoreBackup}
                    actionParam={this.state.backupName}
                    msg="Are you sure you want to restore this backup?"
                    msgContent={this.state.backupName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmBackupDelete}
                    closeHandler={this.closeConfirmBackupDelete}
                    actionFunc={this.deleteBackup}
                    actionParam={this.state.backupName}
                    msg="Are you sure you want to delete this backup?"
                    msgContent={this.state.backupName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmRestoreReplace}
                    closeHandler={this.closeConfirmRestoreReplace}
                    actionFunc={this.doBackup}
                    msg="Replace Existing Backup"
                    msgContent="A backup already eixsts with the same name, do you want to replace it?"
                />
            </div>
        );
    }
}

// Proptyes and defaults

CreateInstanceModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    addNotification: PropTypes.func,
    loadInstanceList: PropTypes.func
};

CreateInstanceModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    addNotification: noop,
    loadInstanceList: noop
};

SchemaReloadModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    addNotification: PropTypes.func,
    serverId: PropTypes.string
};

SchemaReloadModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    addNotification: noop,
    serverId: ""
};

ManageBackupsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    addNotification: PropTypes.func,
    serverId: PropTypes.string
};

ManageBackupsModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    addNotification: noop,
    serverId: ""
};
