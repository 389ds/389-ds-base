import cockpit from "cockpit";
import React from "react";
import { Plugins } from "./plugins.jsx";
import { Database } from "./database.jsx";
import { Monitor } from "./monitor.jsx";
import { Schema } from "./schema.jsx";
import { Replication } from "./replication.jsx";
import { Server } from "./server.jsx";
import { DoubleConfirmModal, NotificationController } from "./lib/notifications.jsx";
import { ManageBackupsModal, SchemaReloadModal, CreateInstanceModal } from "./dsModals.jsx";
import { log_cmd } from "./lib/tools.jsx";

import {
    Nav,
    NavItem,
    DropdownButton,
    MenuItem,
    TabContainer,
    TabContent,
    TabPane,
    ProgressBar,
    Spinner,
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
    componentDidMount() {
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
            wasActiveList: [],
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
                                            serverId: serverId,
                                            wasActiveList: [this.state.activeKey]
                                        },
                                        () => {
                                            this.loadBackups();
                                        }
                                    );
                                    if (action === "restart") {
                                        this.setState(
                                            {
                                                serverId: "",
                                                wasActiveList: []
                                            },
                                            () => {
                                                this.setState({
                                                    serverId: serverId,
                                                    wasActiveList: [this.state.activeKey]
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
                                                serverId: serverId,
                                                wasActiveList: []
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
                                    serverId: serverId,
                                    wasActiveList: []
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
                                serverId: serverId,
                                wasActiveList: []
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
        this.setState(
            {
                wasActiveList: []
            },
            () => {
                let cmd = ["dsctl", "-l", "-j"];
                log_cmd(
                    "loadInstanceList",
                    "Load the instance list select",
                    cmd
                );
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
                                this.setServerId(serverId, action);
                            } else {
                                if (myObject.insts.length > 0) {
                                    this.setServerId(
                                        myObject.insts[0].replace("slapd-", ""),
                                        action
                                    );
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
        );
    }

    loadBackups() {
        let cmd = ["dsctl", "-j", this.state.serverId, "backups"];
        log_cmd("loadBackups", "Load Backups", cmd);
        cockpit.spawn(cmd, { superuser: true, err: "message" }).done(content => {
            this.updateProgress(25);
            const config = JSON.parse(content);
            let rows = [];
            for (let row of config.items) {
                rows.push({ name: row[0], date: [row[1]], size: [row[2]] });
            }
            // Get the server version from the monitor
            cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.state.serverId + ".socket", "monitor", "server"];
            log_cmd("loadBackups", "Get the server version", cmd);
            cockpit.spawn(cmd, { superuser: true, err: "message" }).done(content => {
                let monitor = JSON.parse(content);
                this.setState({
                    backupRows: rows,
                    version: monitor.attrs['version'][0],
                });
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
            progressValue: 25
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
                        <h2 className="ds-logo-style" id="main-banner" title={this.state.version}>
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
                                                version={this.state.version}
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

export default DSInstance;
