import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { Plugins } from "./plugins.jsx";
import { Database } from "./database.jsx";
import { Monitor } from "./monitor.jsx";
import { Schema } from "./schema.jsx";
import { Replication } from "./replication.jsx";
import { Server } from "./server.jsx";
import { ConfirmPopup, DoubleConfirmModal, NotificationController } from "./lib/notifications.jsx";
import { BackupTable } from "./lib/database/databaseTables.jsx";
import { BackupModal, RestoreModal, DeleteBackupModal } from "./lib/database/backups.jsx";
import { log_cmd, bad_file_name } from "./lib/tools.jsx";
import {
    Nav,
    NavItem,
    DropdownButton,
    MenuItem,
    TabContainer,
    TabContent,
    TabPane,
    ProgressBar,
    FormControl,
    FormGroup,
    ControlLabel,
    Radio,
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
import "./css/ds.css";
import "./css/branding.css";

const staticStates = {
    noPackage: (
        <h3>
            There is no <b>389-ds-base</b> package installed on this system. Sorry there is nothing
            to manage...
        </h3>
    ),
    noInsts: <h3>There are no Directory Server instances to manage</h3>,
    notRunning: (
        <h3>
            This server instance is not running, either start it from the <b>Actions</b> dropdown
            menu, or choose a different instance
        </h3>
    ),
    notConnecting: (
        <h3>
            This server instance is running, but we can not connect to it. Check LDAPI is properly
            configured on this instance.
        </h3>
    )
};

export class DSInstance extends React.Component {
    componentWillMount() {
        this.loadInstanceList();
        this.updateProgress(25);
    }

    constructor(props) {
        super(props);
        this.state = {
            pageLoadingState: { state: "loading", jsx: "" },
            serverId: "",
            instList: [],
            backupRows: [],
            notifications: [],
            activeKey: 1,
            wasActiveList: [1],
            progressValue: 0,
            loadingOperate: false,

            showDeleteConfirm: false,
            modalSpinning: false,
            modalChecked: false,

            showSchemaReloadModal: false,
            showManageBackupsModal: false,
            showCreateInstanceModal: false
        };

        this.handleServerIdChange = this.handleServerIdChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.addNotification = this.addNotification.bind(this);
        this.removeNotification = this.removeNotification.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.loadInstanceList = this.loadInstanceList.bind(this);
        this.loadBackups = this.loadBackups.bind(this);
        this.setServerId = this.setServerId.bind(this);
        this.updateProgress = this.updateProgress.bind(this);
        this.openCreateInstanceModal = this.openCreateInstanceModal.bind(this);
        this.closeCreateInstanceModal = this.closeCreateInstanceModal.bind(this);
        this.operateInstance = this.operateInstance.bind(this);
        this.openManageBackupsModal = this.openManageBackupsModal.bind(this);
        this.closeManageBackupsModal = this.closeManageBackupsModal.bind(this);
        this.openSchemaReloadModal = this.openSchemaReloadModal.bind(this);
        this.closeSchemaReloadModal = this.closeSchemaReloadModal.bind(this);
        this.removeInstance = this.removeInstance.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
    }

    updateProgress(value) {
        this.setState(
            prevState => ({
                progressValue: prevState.progressValue + value
            }),
            () => {
                if (this.state.progressValue > 100) {
                    this.setState(prevState => ({
                        pageLoadingState: {
                            ...prevState.pageLoadingState,
                            state: "success"
                        }
                    }));
                }
            }
        );
    }

    setServerId(serverId, action) {
        // First we need to check if the instance is alive and well
        let cmd = ["dsctl", "-j", serverId, "status"];
        log_cmd("setServerId", "Test if instance is running ", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(status_data => {
                    let status_json = JSON.parse(status_data);
                    if (status_json.running) {
                        this.updateProgress(25);
                        let cmd = [
                            "dsconf",
                            "-j",
                            "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
                            "backend",
                            "suffix",
                            "list",
                            "--suffix"
                        ];
                        log_cmd("setServerId", "Test if instance is alive ", cmd);
                        cockpit
                                .spawn(cmd, { superuser: true, err: "message" })
                                .done(_ => {
                                    this.updateProgress(25);
                                    this.setState(
                                        {
                                            serverId: serverId
                                        },
                                        () => {
                                            this.loadBackups();
                                        }
                                    );
                                    if (action === "restart") {
                                        this.setState(
                                            {
                                                serverId: ""
                                            },
                                            () => {
                                                this.setState({
                                                    serverId: serverId
                                                });
                                            }
                                        );
                                    }
                                })
                                .fail(err => {
                                    let errMsg = JSON.parse(err);
                                    console.log("setServerId failed: ", errMsg.desc);
                                    this.setState(
                                        {
                                            pageLoadingState: {
                                                state: "notConnecting",
                                                jsx: staticStates["notConnecting"]
                                            }
                                        },
                                        () => {
                                            this.setState({
                                                serverId: serverId
                                            });
                                        }
                                    );
                                });
                    } else {
                        this.setState(
                            {
                                pageLoadingState: {
                                    state: "notRunning",
                                    jsx: staticStates["notRunning"]
                                }
                            },
                            () => {
                                this.setState({
                                    serverId: serverId
                                });
                            }
                        );
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    console.log("setServerId failed: ", errMsg.desc);
                    this.setState(
                        {
                            pageLoadingState: {
                                state: "notConnecting",
                                jsx: staticStates["notConnecting"]
                            }
                        },
                        () => {
                            this.setState({
                                serverId: serverId
                            });
                        }
                    );
                });
    }

    loadInstanceList(serverId, action) {
        if (serverId === undefined) {
            this.setState(prevState => ({
                pageLoadingState: {
                    ...prevState.pageLoadingState,
                    state: "loading"
                }
            }));
        }
        let cmd = ["dsctl", "-l", "-j"];
        log_cmd("loadInstanceList", "Load the instance list select", cmd);
        cockpit
                .spawn(cmd, { superuser: true })
                .done(data => {
                    this.updateProgress(25);
                    let myObject = JSON.parse(data);
                    this.setState({
                        instList: myObject.insts,
                        loadingOperate: false
                    });
                    // Set default value for the inst select
                    if (serverId !== undefined && serverId !== "") {
                        this.setState({
                            wasActiveList: [this.state.activeKey]
                        });
                        this.setServerId(serverId, action);
                    } else {
                        if (myObject.insts.length > 0) {
                            this.setState({
                                wasActiveList: [this.state.activeKey]
                            });
                            this.setServerId(myObject.insts[0].replace("slapd-", ""), action);
                        } else {
                            this.setState({
                                serverId: "",
                                pageLoadingState: {
                                    state: "noInsts",
                                    jsx: staticStates["noInsts"]
                                }
                            });
                        }
                    }
                })
                .fail(_ => {
                    this.setState({
                        instList: [],
                        serverId: "",
                        loadingOperate: false,
                        pageLoadingState: {
                            state: "noInsts",
                            jsx: staticStates["noInsts"]
                        }
                    });
                });
    }

    loadBackups() {
        const cmd = ["dsctl", "-j", this.state.serverId, "backups"];
        log_cmd("loadBackupsDSInstance", "Load Backups", cmd);
        cockpit.spawn(cmd, { superuser: true, err: "message" }).done(content => {
            this.updateProgress(25);
            const config = JSON.parse(content);
            let rows = [];
            for (let row of config.items) {
                rows.push({ name: row[0], date: [row[1]], size: [row[2]] });
            }
            this.setState({
                backupRows: rows,
            });
        });
    }

    addNotification(type, message, timerdelay, persistent) {
        this.setState(prevState => ({
            notifications: [
                ...prevState.notifications,
                {
                    key: prevState.notifications.length + 1,
                    type: type,
                    persistent: persistent,
                    timerdelay: timerdelay,
                    message: message
                }
            ]
        }));
    }

    removeNotification(notificationToRemove) {
        this.setState({
            notifications: this.state.notifications.filter(
                notification => notificationToRemove.key !== notification.key
            )
        });
    }

    handleNavSelect(key) {
        this.setState({
            activeKey: key
        });
        const { wasActiveList } = this.state;
        if (!wasActiveList.includes(key)) {
            let newList = wasActiveList.concat(key);
            this.setState({
                wasActiveList: newList
            });
        }
    }

    handleServerIdChange(e) {
        this.setState({
            pageLoadingState: { state: "loading", jsx: "" },
            progressValue: 25,
            serverId: e.target.value
        });
        this.loadInstanceList(e.target.value);
    }

    handleFieldChange(e) {
        let value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        if (e.target.type === "number") {
            if (e.target.value) {
                value = parseInt(e.target.value);
            } else {
                value = 1;
            }
        }
        this.setState({
            [e.target.id]: value
        });
    }

    removeInstance() {
        this.operateInstance();
        this.closeDeleteConfirm();
    }

    operateInstance(e) {
        this.setState({
            loadingOperate: true
        });

        let action = "remove";
        if (e !== undefined) {
            action = e.target.id.split("-")[0];
        }

        let cmd = ["dsctl", "-j", this.state.serverId, action];
        if (action === "remove") {
            cmd = [...cmd, "--do-it"];
        }
        log_cmd("operateInstance", `Do ${action} the instance`, cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(_ => {
                    if (action === "remove") {
                        this.loadInstanceList();
                    } else {
                        this.loadInstanceList(this.state.serverId, action);
                    }
                    if (action === "remove") {
                        this.addNotification("success", "Instance was successfully removed");
                    } else {
                        this.addNotification("success", `Instance was successfully ${action}ed`);
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.addNotification(
                        "error",
                        `Error during instance ${action} operation - ${errMsg.desc}`
                    );
                    this.loadInstanceList(this.state.serverId, action);
                });
    }

    openCreateInstanceModal() {
        this.setState({
            showCreateInstanceModal: true
        });
    }

    closeCreateInstanceModal() {
        this.setState({
            showCreateInstanceModal: false
        });
    }

    openManageBackupsModal() {
        this.setState({
            showManageBackupsModal: true
        });
    }

    closeManageBackupsModal() {
        this.setState({
            showManageBackupsModal: false
        });
    }

    openSchemaReloadModal() {
        this.setState({
            showSchemaReloadModal: true
        });
    }

    closeSchemaReloadModal() {
        this.setState({
            showSchemaReloadModal: false
        });
    }

    showDeleteConfirm() {
        this.setState({
            showDeleteConfirm: true,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeDeleteConfirm() {
        this.setState({
            showDeleteConfirm: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    render() {
        const {
            instList,
            serverId,
            progressValue,
            notifications,
            pageLoadingState,
            loadingOperate
        } = this.state;

        let mainContent = "";
        if (pageLoadingState.state === "loading") {
            mainContent = (
                <div id="loading-instances" className="all-pages ds-center">
                    <div id="loading-page" className="ds-center ds-loading">
                        <h4 id="loading-msg">Loading Directory Server Configuration...</h4>
                        <p className="ds-margin-top-lg">
                            <span className="spinner spinner-lg spinner-inline" />
                        </p>
                        <div className="progress ds-margin-top-lg">
                            <ProgressBar active now={progressValue} label={`${progressValue}%`} />
                        </div>
                    </div>
                </div>
            );
        } else if (pageLoadingState.state === "noInsts") {
            mainContent = (
                <div id="noInsts" className="all-pages ds-center">
                    {pageLoadingState.jsx}
                    <p>
                        <Button
                            id="no-inst-create-btn"
                            bsStyle="primary"
                            onClick={this.openCreateInstanceModal}
                        >
                            Create New Instance
                        </Button>
                    </p>
                </div>
            );
        } else {
            mainContent = (
                <div id={pageLoadingState.state} className="all-pages ds-center">
                    {pageLoadingState.jsx}
                </div>
            );
        }

        let operateSpinner = "";
        if (loadingOperate) {
            operateSpinner = <Spinner className="ds-operate-spinner" loading inline size="md" />;
        }

        return (
            <div>
                <NotificationController
                    notifications={notifications}
                    removeNotificationAction={this.removeNotification}
                />
                {pageLoadingState.state !== "loading" &&
                pageLoadingState.state !== "noInsts" &&
                pageLoadingState.state !== "noPackage" ? (
                    <div className="ds-logo" hidden={pageLoadingState.state === "loading"}>
                        <h2 className="ds-logo-style" id="main-banner">
                            <div className="dropdown ds-server-action">
                                <select
                                    className="btn btn-default dropdown"
                                    title="Directory Server Instance List"
                                    id="serverId"
                                    value={serverId}
                                    onChange={this.handleServerIdChange}
                                >
                                    {Object.entries(instList).map(([_, inst]) => (
                                        <option key={inst} value={inst.replace("slapd-", "")}>
                                            {inst}
                                        </option>
                                    ))}
                                </select>
                            </div>
                            {operateSpinner}
                            <div className="dropdown ds-float-right">
                                <DropdownButton
                                    pullRight
                                    id="ds-action"
                                    className="ds-action-button"
                                    bsStyle="primary"
                                    title="Actions"
                                >
                                    <MenuItem
                                        id="start-ds"
                                        eventKey="1"
                                        onClick={this.operateInstance}
                                    >
                                        Start Instance
                                    </MenuItem>
                                    <MenuItem
                                        id="stop-ds"
                                        eventKey="2"
                                        onClick={this.operateInstance}
                                    >
                                        Stop Instance
                                    </MenuItem>
                                    <MenuItem
                                        id="restart-ds"
                                        eventKey="3"
                                        onClick={this.operateInstance}
                                    >
                                        Restart Instance
                                    </MenuItem>
                                    <MenuItem
                                        id="manage-backup-ds"
                                        eventKey="4"
                                        onClick={this.openManageBackupsModal}
                                    >
                                        Manage Backups
                                    </MenuItem>
                                    <MenuItem
                                        id="reload-schema-ds"
                                        eventKey="5"
                                        onClick={this.openSchemaReloadModal}
                                    >
                                        Reload Schema Files
                                    </MenuItem>
                                    <MenuItem
                                        id="remove-ds"
                                        eventKey="6"
                                        onClick={this.showDeleteConfirm}
                                    >
                                        Remove Instance
                                    </MenuItem>
                                    <MenuItem
                                        id="create-ds"
                                        eventKey="7"
                                        onClick={this.openCreateInstanceModal}
                                    >
                                        Create Instance
                                    </MenuItem>
                                </DropdownButton>
                            </div>
                        </h2>
                    </div>
                ) : (
                    <div />
                )}
                {serverId !== "" &&
                (pageLoadingState.state === "success" || pageLoadingState.state === "loading") ? (
                    <div>
                        <div hidden={pageLoadingState.state === "loading"}>
                            <TabContainer
                                id="basic-tabs-pf"
                                onSelect={this.handleNavSelect}
                                activeKey={this.state.activeKey}
                            >
                                <div>
                                    <Nav className="nav nav-tabs nav-tabs-pf collapse navbar-collapse navbar-collapse-5 ds-nav navbar navbar-default">
                                        <NavItem className="ds-tab-main" eventKey={1}>
                                            Server Settings
                                        </NavItem>
                                        <NavItem className="ds-tab-main" eventKey={2}>
                                            Database
                                        </NavItem>
                                        <NavItem className="ds-tab-main" eventKey={3}>
                                            Replication
                                        </NavItem>
                                        <NavItem className="ds-tab-main" eventKey={4}>
                                            Schema
                                        </NavItem>
                                        <NavItem className="ds-tab-main" eventKey={5}>
                                            Plugins
                                        </NavItem>
                                        <NavItem className="ds-tab-main" eventKey={6}>
                                            Monitoring
                                        </NavItem>
                                    </Nav>
                                    <TabContent>
                                        <TabPane eventKey={1}>
                                            <Server
                                                addNotification={this.addNotification}
                                                serverId={this.state.serverId}
                                                wasActiveList={this.state.wasActiveList}
                                                key={this.state.serverId}
                                            />
                                        </TabPane>
                                        <TabPane eventKey={2}>
                                            <Database
                                                addNotification={this.addNotification}
                                                serverId={this.state.serverId}
                                                wasActiveList={this.state.wasActiveList}
                                                key={this.state.serverId}
                                            />
                                        </TabPane>
                                        <TabPane eventKey={3}>
                                            <Replication
                                                addNotification={this.addNotification}
                                                serverId={this.state.serverId}
                                                wasActiveList={this.state.wasActiveList}
                                                key={this.state.serverId}
                                            />
                                        </TabPane>
                                        <TabPane eventKey={4}>
                                            <Schema
                                                addNotification={this.addNotification}
                                                serverId={this.state.serverId}
                                                wasActiveList={this.state.wasActiveList}
                                                key={this.state.serverId}
                                            />
                                        </TabPane>
                                        <TabPane eventKey={5}>
                                            <Plugins
                                                addNotification={this.addNotification}
                                                serverId={this.state.serverId}
                                                wasActiveList={this.state.wasActiveList}
                                                key={this.state.serverId}
                                            />
                                        </TabPane>
                                        <TabPane eventKey={6}>
                                            <Monitor
                                                addNotification={this.addNotification}
                                                serverId={this.state.serverId}
                                                wasActiveList={this.state.wasActiveList}
                                                key={this.state.serverId}
                                            />
                                        </TabPane>
                                    </TabContent>
                                </div>
                            </TabContainer>
                        </div>
                        <div hidden={pageLoadingState.state !== "loading"}>{mainContent}</div>
                    </div>
                ) : (
                    <div>{mainContent}</div>
                )}
                <CreateInstanceModal
                    showModal={this.state.showCreateInstanceModal}
                    closeHandler={this.closeCreateInstanceModal}
                    addNotification={this.addNotification}
                    serverId={this.state.serverId}
                    loadInstanceList={this.loadInstanceList}
                />
                <SchemaReloadModal
                    showModal={this.state.showSchemaReloadModal}
                    closeHandler={this.closeSchemaReloadModal}
                    addNotification={this.addNotification}
                    serverId={this.state.serverId}
                />
                <ManageBackupsModal
                    addNotification={this.addNotification}
                    serverId={this.state.serverId}
                    showModal={this.state.showManageBackupsModal}
                    closeHandler={this.closeManageBackupsModal}
                    handleChange={this.handleFieldChange}
                    backups={this.state.backupRows}
                    reload={this.loadBackups}
                />
                <DoubleConfirmModal
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.removeInstance}
                    spinning={this.state.modalSpinning}
                    item={this.state.serverId}
                    checked={this.state.modalChecked}
                    mTitle="Remove Instance"
                    mMsg="Are you really sure you want to delete this instance?"
                    mSpinningMsg="Removing Instance..."
                    mBtnName="Remove Instance"
                />
            </div>
        );
    }
}

class CreateInstanceModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            createServerId: "",
            createPort: 389,
            createSecurePort: 636,
            createDM: "cn=Directory Manager",
            createDMPassword: "",
            createDMPasswordConfirm: "",
            createDBSuffix: "",
            createDBName: "",
            createTLSCert: true,
            createInitDB: "noInit",
            loadingCreate: false
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.createInstance = this.createInstance.bind(this);
    }

    handleFieldChange(e) {
        let value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        if (e.target.type === "number") {
            if (e.target.value) {
                value = parseInt(e.target.value);
            } else {
                value = 1;
            }
        }
        this.setState({
            [e.target.id]: value
        });
    }

    createInstance() {
        const {
            createServerId,
            createPort,
            createSecurePort,
            createDM,
            createDMPassword,
            createDMPasswordConfirm,
            createDBSuffix,
            createDBName,
            createTLSCert,
            createInitDB
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
        if (newServerId === "") {
            addNotification("warning", "Instance Name is required.");
            return;
        }
        newServerId = newServerId.replace(/^slapd-/i, ""); // strip "slapd-"
        if (newServerId.length > 128) {
            addNotification(
                "warning",
                "Instance name is too long, it must not exceed 128 characters"
            );
            return;
        }
        if (newServerId.match(/^[#%:A-Za-z0-9_-]+$/g)) {
            setup_inf = setup_inf.replace("INST_NAME", newServerId);
        } else {
            addNotification(
                "warning",
                "Instance name can only contain letters, numbers, and:  # % : - _"
            );
            return;
        }

        // Port
        if (createPort < 1 || createPort > 65535) {
            addNotification("warning", "Port must be a number between 1 and 65534!");
            return;
        } else {
            setup_inf = setup_inf.replace("PORT", createPort);
        }

        // Secure Port
        if (createSecurePort < 1 || createSecurePort > 65535) {
            addNotification("warning", "Secure Port must be a number between 1 and 65534!");
            return;
        } else {
            setup_inf = setup_inf.replace("SECURE_PORT", createSecurePort);
        }

        // Root DN
        if (createDM === "") {
            addNotification("warning", "You must provide a Directory Manager DN");
            return;
        } else {
            setup_inf = setup_inf.replace("ROOTDN", createDM);
        }

        // Setup Self-Signed Certs
        if (createTLSCert) {
            setup_inf = setup_inf.replace("SELF_SIGN", "True");
        } else {
            setup_inf = setup_inf.replace("SELF_SIGN", "False");
        }

        // Root DN password
        if (createDMPassword != createDMPasswordConfirm) {
            addNotification("warning", "Directory Manager passwords do not match!");
            return;
        } else if (createDMPassword == "") {
            addNotification("warning", "Directory Manager password can not be empty!");
            return;
        } else if (createDMPassword.length < 8) {
            addNotification(
                "warning",
                "Directory Manager password must have at least 8 characters"
            );
            return;
        } else {
            setup_inf = setup_inf.replace("ROOTPW", createDMPassword);
        }

        // Backend/Suffix
        if (
            (createDBName != "" && createDBSuffix == "") ||
            (createDBName == "" && createDBSuffix != "")
        ) {
            if (createDBName == "") {
                addNotification(
                    "warning",
                    "If you specify a backend suffix, you must also specify a backend name"
                );
                return;
            } else {
                addNotification(
                    "warning",
                    "If you specify a backend name, you must also specify a backend suffix"
                );
                return;
            }
        }
        if (createDBName != "") {
            // We definitely have a backend name and suffix, next validate the suffix is a DN
            let dn_regex = new RegExp("^([A-Za-z]+=.*)");
            if (dn_regex.test(createDBSuffix)) {
                // It's valid, add it
                setup_inf += "\n[backend-" + createDBName + "]\nsuffix = " + createDBSuffix + "\n";
            } else {
                // Not a valid DN
                addNotification("warning", "Invalid DN for Backend Suffix");
                return;
            }

            if (createInitDB === "createSample") {
                setup_inf += "\nsample_entries = yes\n";
            }
            if (createInitDB === "createSuffix") {
                setup_inf += "\ncreate_suffix_entry = yes\n";
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
                    cockpit
                            .spawn(create_file_cmd, { superuser: true, err: "message" })
                            .fail(err => {
                                let errMsg = JSON.parse(err);
                                this.setState({
                                    loadingCreate: false
                                });
                                addNotification(
                                    "error",
                                    `Failed to create installation file!" ${errMsg.desc}`
                                );
                            })
                            .done(_ => {
                                /*
                         * We have our new setup file, now set permissions on that setup file before we add sensitive data
                         */
                                let chmod_cmd = ["chmod", "600", setup_file];
                                cockpit
                                        .spawn(chmod_cmd, { superuser: true, err: "message" })
                                        .fail(err => {
                                            let errMsg = JSON.parse(err);
                                            cockpit.spawn(rm_cmd, { superuser: true }); // Remove Inf file with clear text password
                                            this.setState({
                                                loadingCreate: false
                                            });
                                            addNotification(
                                                "error",
                                                `Failed to set permission on setup file ${setup_file}: ${
                                                    errMsg.desc
                                                }`
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
                                            cockpit
                                                    .spawn(cmd, { superuser: true, err: "message" })
                                                    .fail(err => {
                                                        let errMsg = JSON.parse(err);
                                                        this.setState({
                                                            loadingCreate: false
                                                        });
                                                        addNotification(
                                                            "error",
                                                            `Failed to populate installation file! ${errMsg.desc}`
                                                        );
                                                    })
                                                    .done(_ => {
                                                        /*
                                         * Next, create the instance...
                                         */
                                                        let cmd = ["dscreate", "from-file", setup_file];
                                                        cockpit
                                                                .spawn(cmd, {
                                                                    superuser: true,
                                                                    err: "message"
                                                                })
                                                                .fail(_ => {
                                                                    cockpit.spawn(rm_cmd, { superuser: true }); // Remove Inf file with clear text password
                                                                    this.setState({
                                                                        loadingCreate: false
                                                                    });
                                                                    addNotification(
                                                                        "error",
                                                                        "Failed to create instance!"
                                                                    );
                                                                })
                                                                .done(_ => {
                                                                    // Success!!!  Now cleanup everything up...
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
            createDBSuffix,
            createDBName,
            createTLSCert,
            createInitDB
        } = this.state;

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
                            <FormGroup controlId="createServerId">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={5}
                                    title="The instance name, this is what gets appended to 'slapi-'. The instance name can only contain letters, numbers, and:  # % : - _"
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
                                        type="number"
                                        min="0"
                                        max="65535"
                                        value={createPort}
                                        onChange={this.handleFieldChange}
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
                                        type="number"
                                        min="0"
                                        max="65535"
                                        value={createSecurePort}
                                        onChange={this.handleFieldChange}
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
                                    title="The DN for the unrestricted  user"
                                >
                                    Directory Manager DN
                                </Col>
                                <Col sm={7}>
                                    <FormControl
                                        type="text"
                                        id="createDM"
                                        onChange={this.handleFieldChange}
                                        value={createDM}
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
                                    />
                                </Col>
                            </FormGroup>
                            <hr />
                            <h5 className="ds-center">Optional Database Settings</h5>
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
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup
                                key="createInitDBn"
                                controlId="createInitDBn"
                                disabled={false}
                            >
                                <Col smOffset={5} sm={7}>
                                    <Radio
                                        id="createInitDB"
                                        value="noInit"
                                        name="noInit"
                                        inline
                                        checked={createInitDB === "noInit"}
                                        onChange={this.handleFieldChange}
                                    >
                                        Do Not Initialize Database
                                    </Radio>
                                </Col>
                            </FormGroup>
                            <FormGroup
                                key="createInitDBs"
                                controlId="createInitDBs"
                                disabled={false}
                            >
                                <Col smOffset={5} sm={7}>
                                    <Radio
                                        id="createInitDB"
                                        value="createSuffix"
                                        name="createSuffix"
                                        inline
                                        checked={createInitDB === "createSuffix"}
                                        onChange={this.handleFieldChange}
                                    >
                                        Create Suffix Entry
                                    </Radio>
                                </Col>
                            </FormGroup>
                            <FormGroup
                                key="createInitDBp"
                                controlId="createInitDBp"
                                disabled={false}
                            >
                                <Col smOffset={5} sm={7}>
                                    <Radio
                                        id="createInitDB"
                                        value="createSample"
                                        name="createSample"
                                        inline
                                        checked={createInitDB === "createSample"}
                                        onChange={this.handleFieldChange}
                                    >
                                        Create Suffix Entry And Add Sample Entries
                                    </Radio>
                                </Col>
                            </FormGroup>
                        </Form>
                        {createSpinner}
                    </Modal.Body>
                    <Modal.Footer>
                        <Button bsStyle="default" className="btn-cancel" onClick={closeHandler}>
                            Cancel
                        </Button>
                        <Button bsStyle="primary" onClick={this.createInstance}>
                            Create Instance
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

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

class ManageBackupsModal extends React.Component {
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
                            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
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
                            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
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

export default DSInstance;
