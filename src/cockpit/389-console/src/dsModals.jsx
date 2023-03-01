import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { DoubleConfirmModal } from "./lib/notifications.jsx";
import { BackupTable } from "./lib/database/databaseTables.jsx";
import { BackupModal } from "./lib/database/backups.jsx";
import { log_cmd, bad_file_name, valid_dn, callCmdStreamPassword } from "./lib/tools.jsx";
import {
    Button,
    Checkbox,
    Form,
    FormHelperText,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    NumberInput,
    TextInput,
    ValidatedOptions,
    Spinner,
} from "@patternfly/react-core";

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

        this.maxValue = 65535;
        this.onMinusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validate() });
        };
        this.onConfigChange = (event, id, min) => {
            const newValue = isNaN(event.target.value) ? min : Number(event.target.value);
            this.setState({
                [id]: newValue > this.maxValue ? this.maxValue : newValue < min ? min : newValue
            }, () => { this.validate() });
        };
        this.onPlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validate() });
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.validate = this.validate.bind(this);
        this.handleCreateInstance = this.handleCreateInstance.bind(this);
        this.validInstName = this.validInstName.bind(this);
        this.validRootDN = this.validRootDN.bind(this);
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
        const dn_regex = new RegExp("^([A-Za-z]+=(?=\\S)([ -~]+)$)");

        const result = dn_regex.test(dn);
        return result;
    }

    validate() {
        let all_good = true;
        let createServerIdMsg = "";
        const errObj = {};

        const reqAttrs = [
            'createServerId', 'createDM', 'createDMPassword', 'createDMPasswordConfirm'
        ];

        const dnAttrs = [
            'createDM'
        ];

        const optionalAttrs = [
            'createDBName'
        ];

        // Handle server ID
        if (this.state.createServerId !== "") {
            if (this.state.createServerId.length > 80) {
                all_good = false;
                errObj.createServerId = true;
                createServerIdMsg = "Instance name must be less than 80 characters";
            } else if (!this.validInstName(this.state.createServerId)) {
                all_good = false;
                errObj.createServerId = true;
                createServerIdMsg = "Instance name can only contain letters, numbers, and these 4 characters:  - @ : _";
            }
        }

        for (const attr of reqAttrs) {
            if (this.state[attr] === "") {
                all_good = false;
                errObj[attr] = true;
            }
        }

        for (const attr of dnAttrs) {
            if (this.state[attr] !== "" && !valid_dn(this.state[attr])) {
                all_good = false;
                errObj[attr] = true;
            }
        }

        if (this.state.createDMPassword !== this.state.createDMPasswordConfirm ||
            this.state.createDMPassword.length < 8) {
            all_good = false;
            errObj.createDMPassword = true;
            errObj.createDMPasswordConfirm = true;
        }

        if (this.state.createDBCheckbox) {
            for (const attr of optionalAttrs) {
                if (this.state[attr] === "") {
                    all_good = false;
                    errObj[attr] = true;
                }
            }
            if (!valid_dn(this.state.createDBSuffix)) {
                all_good = false;
                errObj.createDBSuffix = true;
            }
        }

        this.setState({
            createOK: all_good,
            createServerIdMsg: createServerIdMsg,
            errObj: errObj
        });
    }

    handleFieldChange(e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value,
        }, () => { this.validate() });
    }

    handleCreateInstance() {
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
        let self_sign = "False";
        if (createTLSCert) {
            self_sign = "True";
        }
        let newServerId = createServerId;
        newServerId = newServerId.replace(/^slapd-/i, ""); // strip "slapd-"
        let setup_inf =
            "[general]\n" +
            "config_version = 2\n" +
            "full_machine_name = FQDN\n\n" +
            "[slapd]\n" +
            "user = dirsrv\n" +
            "group = dirsrv\n" +
            "instance_name = " + newServerId + "\n" +
            "port = " + createPort + "\n" +
            "root_dn = " + createDM + "\n" +
            // "root_password = ROOTPW\n" +
            "secure_port = " + createSecurePort + "\n" +
            "self_sign_cert = " + self_sign + "\n";

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
        const hostname_cmd = ["hostnamectl", "status", "--static"];
        log_cmd("handleCreateInstance", "Get FQDN ...", hostname_cmd);
        cockpit
                .spawn(hostname_cmd, { superuser: true, err: "message" })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.setState({
                        loadingCreate: false
                    });
                    addNotification("error", `Failed to get hostname!", ${errMsg.desc}`);
                })
                .done(data => {
                    /*
                     * We have FQDN, so set the hostname in inf file, and create the setup file
                     */
                    if (data.trim() === "") {
                        data = "localhost.localdomain";
                    }
                    setup_inf = setup_inf.replace("FQDN", data);
                    const setup_file = "/tmp/389-setup-" + new Date().getTime() + ".inf";
                    const rm_cmd = ["rm", setup_file];
                    const create_file_cmd = ["touch", setup_file];
                    log_cmd("handleCreateInstance", "Setting FQDN...", create_file_cmd);
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
                                const chmod_cmd = ["chmod", "600", setup_file];
                                log_cmd("handleCreateInstance", "Setting initial INF file permissions...", chmod_cmd);
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
                                            const cmd = [
                                                '/bin/sh', '-c',
                                                '/usr/bin/echo -e \'' + setup_inf + '\' >> ' + setup_file
                                            ];

                                            // Do not log inf file as it contains the DM password
                                            log_cmd("handleCreateInstance", "Apply changes to INF file...", "");
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
                                                        const cmd = ["dscreate", "-j", "from-file", setup_file];
                                                        log_cmd("handleCreateInstance", "Creating instance...", cmd);
                                                        cockpit
                                                                .spawn(cmd, {
                                                                    superuser: true,
                                                                    err: "message"
                                                                })
                                                                .fail(err => {
                                                                    const errMsg = JSON.parse(err.message);
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
                                                                    // Success!!!  Now set Root DN pw, and cleanup everything up...
                                                                    log_cmd("handleCreateInstance", "Instance creation compelete, remove INF file...", rm_cmd);
                                                                    cockpit.spawn(rm_cmd, { superuser: true });

                                                                    const dm_pw_cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + newServerId + '.socket',
                                                                                       'directory_manager', 'password_change'];
                                                                    const config = {
                                                                        cmd: dm_pw_cmd,
                                                                        promptArg: "",
                                                                        passwd: createDMPassword,
                                                                        addNotification: addNotification,
                                                                        success_msg: `Successfully created instance: slapd-${createServerId}`,
                                                                        error_msg: "Failed to set Directory Manager password",
                                                                        state_callback: () => { this.setState({ loadingCreate: false }) },
                                                                        reload_func: loadInstanceList,
                                                                        reload_arg: createServerId,
                                                                        ext_func: closeHandler,
                                                                        ext_arg: "",
                                                                        funcName: "handleCreateInstance",
                                                                        funcDesc: "Set Directory Manager password..."
                                                                    };
                                                                    callCmdStreamPassword(config);
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
            errObj,
        } = this.state;

        let saveBtnName = "Create Instance";
        const extraPrimaryProps = {};
        if (loadingCreate) {
            saveBtnName = "Creating Instance ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Create New Server Instance"
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={this.handleCreateInstance}
                        isDisabled={!createOK || loadingCreate}
                        isLoading={loadingCreate}
                        spinnerAriaValueText={loadingCreate ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <div className={loadingCreate ? "ds-disabled" : ""}>
                    <Form isHorizontal autoComplete="off">
                        <Grid className="ds-margin-top" title="The instance name, this is what gets appended to 'slapi-'. The instance name can only contain letters, numbers, and: # @ : - _">
                            <GridItem className="ds-label" span={4}>
                                Instance Name
                            </GridItem>
                            <GridItem span={8}>
                                <TextInput
                                    value={createServerId}
                                    type="text"
                                    id="createServerId"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="createServerId"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={errObj.createServerId ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                                <FormHelperText isError isHidden={!errObj.createServerId}>
                                    {this.state.createServerIdMsg}
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid title="The server port number should be in the range of 0 to 65534.">
                            <GridItem className="ds-label" span={4}>
                                Port
                            </GridItem>
                            <GridItem span={8}>
                                <NumberInput
                                    value={createPort}
                                    min={1}
                                    max={65534}
                                    onMinus={() => { this.onMinusConfig("createPort") }}
                                    onChange={(e) => { this.onConfigChange(e, "createPort", 0) }}
                                    onPlus={() => { this.onPlusConfig("createPort") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                                <FormHelperText className="ds-info-color" isHidden={createPort !== 0}>
                                    Port 0 will disable non-TLS connections
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid title="The secure port number for TLS connections. It should be in the range of 1 to 65534.">
                            <GridItem className="ds-label" span={4}>
                                Secure Port
                            </GridItem>
                            <GridItem span={8}>
                                <NumberInput
                                    value={createSecurePort}
                                    min={1}
                                    max={65534}
                                    onMinus={() => { this.onMinusConfig("createSecurePort") }}
                                    onChange={(e) => { this.onConfigChange(e, "createSecurePort", 1) }}
                                    onPlus={() => { this.onPlusConfig("createSecurePort") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Create a self-signed certificate database in /etc/dirsrc/ssca directory.">
                            <GridItem className="ds-label-checkbox" span={4}>
                                Create Self-Signed TLS Certificate
                            </GridItem>
                            <GridItem span={8}>
                                <Checkbox
                                    id="createTLSCert"
                                    isChecked={createTLSCert}
                                    onChange={(checked, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="The DN for the unrestricted user">
                            <GridItem className="ds-label" span={4}>
                                Directory Manager DN
                            </GridItem>
                            <GridItem span={8}>
                                <TextInput
                                    value={createDM}
                                    type="text"
                                    id="createDM"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="createDM"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={errObj.createDM ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                                <FormHelperText isError isHidden={!errObj.createDM}>
                                    Enter a valid DN
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid title="Directory Manager password must be at least 8 characters in length.">
                            <GridItem className="ds-label" span={4}>
                                Directory Manager Password
                            </GridItem>
                            <GridItem span={8}>
                                <TextInput
                                    value={createDMPassword}
                                    type="password"
                                    id="createDMPassword"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="createDMPassword"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={errObj.createDMPassword ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                                <FormHelperText isError isHidden={!errObj.createDMPassword}>
                                    Password must be set and it must match the confirmation password.
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid title="Confirm the previously entered password.">
                            <GridItem className="ds-label" span={4}>
                                Confirm Password
                            </GridItem>
                            <GridItem span={8}>
                                <TextInput
                                    value={createDMPasswordConfirm}
                                    type="password"
                                    id="createDMPasswordConfirm"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="createDMPasswordConfirm"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={errObj.createDMPasswordConfirm ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                                <FormHelperText isError isHidden={!errObj.createDMPasswordConfirm}>
                                    Confirmation password must be set and it must match the first password.
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top" title="Create a database during the installation.">
                            <Checkbox
                                id="createDBCheckbox"
                                label="Create Database"
                                isChecked={createDBCheckbox}
                                onChange={(checked, e) => {
                                    this.handleFieldChange(e);
                                }}
                            />
                        </Grid>
                        <div className={createDBCheckbox ? "" : "ds-hidden"}>
                            <Grid title="Database suffix, like 'dc=example,dc=com'. The suffix must be a valid LDAP Distiguished Name (DN)">
                                <GridItem className="ds-label" offset={1} span={3}>
                                    Database Suffix
                                </GridItem>
                                <GridItem span={8}>
                                    <TextInput
                                        value={createDBSuffix}
                                        placeholder="e.g. dc=company,dc=com"
                                        type="text"
                                        id="createDBSuffix"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="createDBSuffix"
                                        isDisabled={!createDBCheckbox}
                                        onChange={(str, e) => {
                                            this.handleFieldChange(e);
                                        }}
                                        validated={errObj.createDBSuffix ? ValidatedOptions.error : ValidatedOptions.default}
                                    />
                                    <FormHelperText isError isHidden={!errObj.createDBSuffix}>
                                        Value must be a valid DN
                                    </FormHelperText>
                                </GridItem>
                            </Grid>
                            <Grid title="The name for the backend database, like 'userroot'. The name can be a combination of alphanumeric characters, dashes (-), and underscores (_). No other characters are allowed, and the name must be unique across all backends.">
                                <GridItem className="ds-label" offset={1} span={3}>
                                    Database Name
                                </GridItem>
                                <GridItem span={8}>
                                    <TextInput
                                        value={createDBName}
                                        placeholder="e.g. userRoot"
                                        type="text"
                                        id="createDBName"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="createDBName"
                                        isDisabled={!createDBCheckbox}
                                        onChange={(str, e) => {
                                            this.handleFieldChange(e);
                                        }}
                                        validated={errObj.createDBName ? ValidatedOptions.error : ValidatedOptions.default}
                                    />
                                    <FormHelperText isError isHidden={!errObj.createDBName}>
                                        Name is required
                                    </FormHelperText>
                                </GridItem>
                            </Grid>
                            <Grid>
                                <GridItem className="ds-label" offset={1} span={3}>
                                    Database Initialization
                                </GridItem>
                                <GridItem span={8}>
                                    <FormSelect
                                        id="createInitDB"
                                        value={createInitDB}
                                        onChange={(value, event) => {
                                            this.handleFieldChange(event);
                                        }}
                                        aria-label="FormSelect Input"
                                        isDisabled={!createDBCheckbox}
                                    >
                                        <FormSelectOption key="1" value="noInit" label="Do Not Initialize Database" />
                                        <FormSelectOption key="2" value="createSuffix" label="Create Suffix Entry" />
                                        <FormSelectOption key="3" value="createSample" label="Create Sample Entries" />
                                    </FormSelect>
                                </GridItem>
                            </Grid>
                        </div>
                        <div className={createDBCheckbox ? "ds-margin-bottom" : "ds-margin-bottom-md"} />
                    </Form>
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

        this.handleReloadSchema = this.handleReloadSchema.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    handleReloadSchema(e) {
        const { addNotification, serverId, closeHandler } = this.props;
        const { reloadSchemaDir } = this.state;

        this.setState({
            loadingSchemaTask: true
        });

        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket", "schema", "reload", "--wait"];
        if (reloadSchemaDir !== "") {
            cmd = [...cmd, "--schemadir", reloadSchemaDir];
        }
        log_cmd("handleReloadSchema", "Reload schema files", cmd);
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
                    const errMsg = JSON.parse(err);
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
                <Grid>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="lg" />
                        Reloading schema files...
                    </div>
                </Grid>
            );
        }

        return (
            <Modal
                variant={ModalVariant.small}
                title="Reload Schema Files"
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={this.handleReloadSchema}>
                        Reload Schema
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid title="The name of the database link.">
                        <GridItem className="ds-label" span={3}>
                            Schema File Directory
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={reloadSchemaDir}
                                type="text"
                                id="reloadSchemaDir"
                                aria-describedby="horizontal-form-name-helper"
                                name="reloadSchemaDir"
                                onChange={(str, e) => {
                                    this.handleFieldChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    {spinner}
                </Form>
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
            showBackupModal: false,
            backupSpinning: false,
            refreshing: false,
            backupName: "",
            deleteBackup: "",
            modalSpinning: false,
            modalChecked: false,
            errObj: {}
        };

        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.onModalChange = this.onModalChange.bind(this);

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
        this.handleShowBackupModal = this.handleShowBackupModal.bind(this);
        this.closeBackupModal = this.closeBackupModal.bind(this);
        this.validateBackup = this.validateBackup.bind(this);
        this.closeConfirmRestoreReplace = this.closeConfirmRestoreReplace.bind(this);
    }

    closeExportModal() {
        this.setState({
            showExportModal: false
        });
    }

    handleShowBackupModal() {
        this.setState({
            showBackupModal: true,
            backupSpinning: false,
            backupName: "",
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeBackupModal() {
        this.setState({
            showBackupModal: false
        });
    }

    showConfirmBackup(name) {
        // call deleteLDIF
        this.setState({
            showConfirmBackup: true,
            backupName: name,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmBackup() {
        // call importLDIF
        this.setState({
            showConfirmBackup: false
        });
    }

    showConfirmRestore(name) {
        this.setState({
            showConfirmRestore: true,
            backupName: name,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmRestore() {
        // call importLDIF
        this.setState({
            showConfirmRestore: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    showConfirmBackupDelete(name) {
        // calls deleteBackup
        this.setState({
            showConfirmBackupDelete: true,
            backupName: name,
            modalChecked: false,
            modalSpinning: false,
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
            if (this.state.backupName === this.props.backups[i].name) {
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

        const cmd = ["dsctl", "-j", this.props.serverId, "status"];
        cockpit
                .spawn(cmd, { superuser: true })
                .done(status_data => {
                    const status_json = JSON.parse(status_data);
                    if (status_json.running === true) {
                        const cmd = [
                            "dsconf",
                            "-j",
                            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                            "backup",
                            "create"
                        ];
                        if (this.state.backupName !== "") {
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
                                    const cmd = [
                                        "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                                        "config", "get", "nsslapd-bakdir"
                                    ];
                                    log_cmd("doBackup", "Get the backup directory", cmd);
                                    cockpit
                                            .spawn(cmd, { superuser: true, err: "message" })
                                            .done(content => {
                                                const config = JSON.parse(content);
                                                const attrs = config.attrs;
                                                this.props.addNotification(
                                                    "success",
                                                    `Server has been backed up. You can find the backup in ${attrs['nsslapd-bakdir'][0]} directory on the server machine.`
                                                );
                                            })
                                            .fail(err => {
                                                const errMsg = JSON.parse(err);
                                                this.props.addNotification(
                                                    "success",
                                                    `Server has been backed up.`
                                                );
                                                this.props.addNotification(
                                                    "error",
                                                    `Error while trying to get the server's backup directory- ${errMsg.desc}`
                                                );
                                            });
                                })
                                .fail(err => {
                                    const errMsg = JSON.parse(err);
                                    this.props.reload();
                                    this.closeBackupModal();
                                    this.props.addNotification(
                                        "error",
                                        `Failure backing up server - ${errMsg.desc}`
                                    );
                                });
                    } else {
                        const cmd = ["dsctl", "-j", this.props.serverId, "db2bak"];
                        if (this.state.backupName !== "") {
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
                                    const errMsg = JSON.parse(err);
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
                    const errMsg = JSON.parse(err);
                    console.log("Failed to check the server status", errMsg.desc);
                });
    }

    restoreBackup() {
        this.setState({
            modalSpinning: true
        });
        const cmd = ["dsctl", "-j", this.props.serverId, "status"];
        cockpit
                .spawn(cmd, { superuser: true })
                .done(status_data => {
                    const status_json = JSON.parse(status_data);
                    if (status_json.running === true) {
                        const cmd = [
                            "dsconf",
                            "-j",
                            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                            "backup",
                            "restore",
                            this.state.backupName
                        ];
                        log_cmd("restoreBackup", "Restoring server online", cmd);
                        cockpit
                                .spawn(cmd, { superuser: true, err: "message" })
                                .done(content => {
                                    this.closeConfirmRestore();
                                    this.props.addNotification("success", `Server has been restored`);
                                })
                                .fail(err => {
                                    const errMsg = JSON.parse(err);
                                    this.closeConfirmRestore();
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
                                    const errMsg = JSON.parse(err);
                                    this.closeRestoreSpinningModal();
                                    this.props.addNotification(
                                        "error",
                                        `Failure restoring up server - ${errMsg.desc}`
                                    );
                                });
                    }
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    console.log("Failed to check the server status", errMsg.desc);
                });
    }

    deleteBackup(e) {
        this.setState({
            modalSpinning: true,
        });
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
                    this.setState({
                        modalSpinning: false,
                    });
                    this.props.addNotification("success", `Backup was successfully deleted`);
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload();
                    this.setState({
                        modalSpinning: false,
                    });
                    this.props.addNotification("error", `Failure deleting backup - ${errMsg.desc}`);
                });
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    onModalChange(e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        if (value === "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        });
    }

    render() {
        const { showModal, closeHandler, backups } = this.props;

        return (
            <div>
                <Modal
                    variant={ModalVariant.medium}
                    title="Manage Backups"
                    aria-labelledby="ds-modal"
                    isOpen={showModal}
                    onClose={closeHandler}
                    actions={[
                        <Button key="confirm" variant="primary" onClick={this.handleShowBackupModal}>
                            Create Backup
                        </Button>,
                    ]}
                >
                    <BackupTable
                        className="ds-margin-top-xlg"
                        rows={backups}
                        key={backups}
                        confirmRestore={this.showConfirmRestore}
                        confirmDelete={this.showConfirmBackupDelete}
                    />
                </Modal>
                <BackupModal
                    showModal={this.state.showBackupModal}
                    closeHandler={this.closeBackupModal}
                    handleChange={this.onModalChange}
                    saveHandler={this.validateBackup}
                    spinning={this.state.backupSpinning}
                    error={this.state.errObj}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmRestore}
                    closeHandler={this.closeConfirmRestore}
                    handleChange={this.onModalChange}
                    actionHandler={this.restoreBackup}
                    spinning={this.state.modalSpinning}
                    item={this.state.backupName}
                    checked={this.state.modalChecked}
                    mTitle="Restore Backup"
                    mMsg="Are you sure you want to restore this backup?"
                    mSpinningMsg="Restoring ..."
                    mBtnName="Restore Backup"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmBackupDelete}
                    closeHandler={this.closeConfirmBackupDelete}
                    handleChange={this.onModalChange}
                    actionHandler={this.deleteBackup}
                    spinning={this.state.modalSpinning}
                    item={this.state.backupName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Backup"
                    mMsg="Are you sure you want to delete this backup?"
                    mSpinningMsg="Deleting ..."
                    mBtnName="Delete Backup"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmRestoreReplace}
                    closeHandler={this.closeConfirmRestoreReplace}
                    handleChange={this.onModalChange}
                    actionHandler={this.deleteBackup}
                    spinning={this.state.modalSpinning}
                    item={this.state.doBackup}
                    checked={this.state.modalChecked}
                    mTitle="Replace Existing Backup"
                    mMsg=" backup already eixsts with the same name, do you want to replace it?"
                    mSpinningMsg="Replacing ..."
                    mBtnName="Replace Backup"
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
};

SchemaReloadModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    addNotification: PropTypes.func,
    serverId: PropTypes.string
};

SchemaReloadModal.defaultProps = {
    showModal: false,
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
    serverId: ""
};
