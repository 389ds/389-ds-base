import cockpit from "cockpit";
import React from "react";
import { Plugins } from "./plugins.jsx";
import { Database } from "./database.jsx";
import { Monitor } from "./monitor.jsx";
import { Schema } from "./schema.jsx";
import { Replication } from "./replication.jsx";
import { Server } from "./server.jsx";
import { DoubleConfirmModal } from "./lib/notifications.jsx";
import { ManageBackupsModal, SchemaReloadModal, CreateInstanceModal } from "./dsModals.jsx";
import { LDAPEditor } from "./LDAPEditor.jsx";
import { log_cmd } from "./lib/tools.jsx";
import {
    Alert,
    AlertGroup,
    AlertActionCloseButton,
    AlertVariant,
    Button,
    Dropdown,
    DropdownToggle,
    DropdownItem,
    DropdownPosition,
    DropdownSeparator,
    Grid, GridItem,
    FormSelect,
    FormSelectOption,
    PageSectionVariants,
    Progress,
    ProgressMeasureLocation,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants
} from "@patternfly/react-core";
import CaretDownIcon from '@patternfly/react-icons/dist/esm/icons/caret-down-icon';

const staticStates = {
    noPackage: (
        <TextContent>
            <Text className="ds-margin-top-xlg" component={TextVariants.h2}>
                There is no <b>389-ds-base</b> package installed on this system. Sorry there is nothing
                to manage...
            </Text>
        </TextContent>
    ),
    noInsts: (
        <TextContent>
            <Text className="ds-margin-top-xlg ds-indent-md" component={TextVariants.h2}>
                There are no Directory Server instances to manage
            </Text>
        </TextContent>
    ),
    notRunning: (
        <TextContent>
            <Text className="ds-margin-top-xlg ds-indent-md" component={TextVariants.h2}>
                This server instance is not running, either start it from the <b>Actions</b> dropdown
                menu, or choose a different instance
            </Text>
        </TextContent>
    ),
    notConnecting: (
        <TextContent>
            <Text className="ds-margin-top-xlg ds-indent-md" component={TextVariants.h2}>
                This server instance is running, but we can not connect to it. Check LDAPI is properly
                configured on this instance.
            </Text>
        </TextContent>
    ),
    ldapiIssue: (
        <TextContent>
            <Text className="ds-margin-top-xlg ds-indent-md" component={TextVariants.h2}>
                Problem accessing required server configuration. Check LDAPI is properly
                configured on this instance.
            </Text>
        </TextContent>
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
            activeTabKey: 1,
            createKey: 0,
            wasActiveList: [],
            progressValue: 0,
            loadingOperate: false,
            dropdownIsOpen: false,
            variant: PageSectionVariants.default,

            showDeleteConfirm: false,
            modalSpinning: false,
            modalChecked: false,

            showSchemaReloadModal: false,
            showManageBackupsModal: false,
            showCreateInstanceModal: false
        };

        // Dropdown tasks
        this.handleToggle = dropdownIsOpen => {
            this.setState({
                dropdownIsOpen
            });
        };

        this.handleDropdown = event => {
            this.setState({
                dropdownIsOpen: !this.state.dropdownIsOpen
            });
            this.onFocus();
        };

        this.onFocus = () => {
            const element = document.getElementById('ds-dropdown');
            element.focus();
        };

        this.handleNavSelect = (event, tabIndex) => {
            const { wasActiveList } = this.state;
            if (!wasActiveList.includes(tabIndex)) {
                const newList = wasActiveList.concat(tabIndex);
                this.setState({
                    wasActiveList: newList,
                    activeTabKey: tabIndex
                });
            } else {
                this.setState({
                    activeTabKey: tabIndex
                });
            }
        };

        this.setPageSectionVariant = isLight => {
            const variant = isLight
                ? PageSectionVariants.light
                : PageSectionVariants.default;
            this.setState({
                variant,
            });
        };

        this.handleServerIdChange = this.handleServerIdChange.bind(this);
        this.onFieldChange = this.onFieldChange.bind(this);
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

    addNotification(variant, title) {
        const key = new Date().getTime();
        if (variant === "error" || variant === "danger") {
            variant = "danger";
            // To print exceptions reported by lib389, it looks best in pre tags
            title = <pre>{title}</pre>;
        }

        this.setState({
            notifications: [...this.state.notifications, { title: title, variant: variant, key }]
        });
    }

    removeNotification(key) {
        this.setState({
            notifications: [...this.state.notifications.filter(el => el.key !== key)]
        });
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
        const cmd = ["dsctl", "-j", serverId, "status"];
        log_cmd("setServerId", "Test if instance is running ", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(status_data => {
                    const status_json = JSON.parse(status_data);
                    if (status_json.running) {
                        this.updateProgress(25);

                        const cfg_cmd = [
                            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
                            "config", "get"
                        ];
                        log_cmd("setServerId", "Load server configuration", cfg_cmd);
                        cockpit
                                .spawn(cfg_cmd, { superuser: true, err: "message" })
                                .done(content => {
                                    const config = JSON.parse(content);
                                    const attrs = config.attrs;
                                    if (Object.keys(attrs).length === 0) {
                                        // Could not load config, access control issue (LDAPI misconfigured)
                                        this.setState({
                                            pageLoadingState: {
                                                state: "ldapiIssue",
                                                jsx: staticStates.ldapiIssue,
                                                loading: "ldapiError"
                                            },
                                            serverId: serverId,
                                            wasActiveList: []
                                        });
                                        return;
                                    }

                                    const cmd = [
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
                                                        wasActiveList: [this.state.activeTabKey]
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
                                                                wasActiveList: [this.state.activeTabKey]
                                                            });
                                                        }
                                                    );
                                                }
                                            })
                                            .fail(err => {
                                                const errMsg = JSON.parse(err);
                                                console.log("setServerId failed: ", errMsg.desc);
                                                this.setState(
                                                    {
                                                        pageLoadingState: {
                                                            state: "notConnecting",
                                                            jsx: staticStates.notConnecting
                                                        }
                                                    },
                                                    () => {
                                                        this.setState(
                                                            {
                                                                serverId: serverId,
                                                                wasActiveList: []
                                                            },
                                                            () => {
                                                                this.loadBackups();
                                                            }
                                                        );
                                                    }
                                                );
                                            });
                                })
                                .fail(err => {
                                    const errMsg = JSON.parse(err);
                                    console.log("setServerId failed: ", errMsg.desc);
                                    this.setState(
                                        {
                                            pageLoadingState: {
                                                state: "notConnecting",
                                                jsx: staticStates.notConnecting
                                            }
                                        },
                                        () => {
                                            this.setState(
                                                {
                                                    serverId: serverId,
                                                    wasActiveList: []
                                                },
                                                () => {
                                                    this.loadBackups();
                                                }
                                            );
                                        }
                                    );
                                });
                    } else {
                        this.setState(
                            {
                                pageLoadingState: {
                                    state: "notRunning",
                                    jsx: staticStates.notRunning
                                }
                            },
                            () => {
                                this.setState(
                                    {
                                        serverId: serverId,
                                        wasActiveList: []
                                    }
                                );
                            }
                        );
                    }
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    console.log("setServerId failed: ", errMsg.desc);
                    this.setState(
                        {
                            pageLoadingState: {
                                state: "notConnecting",
                                jsx: staticStates.notConnecting
                            }
                        },
                        () => {
                            this.setState(
                                {
                                    serverId: serverId,
                                    wasActiveList: []
                                },
                                () => {
                                    this.loadBackups();
                                }
                            );
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
                const cmd = ["dsctl", "-l", "-j"];
                log_cmd(
                    "loadInstanceList",
                    "Load the instance list select",
                    cmd
                );
                cockpit
                        .spawn(cmd, { superuser: true })
                        .done(data => {
                            this.updateProgress(25);
                            const myObject = JSON.parse(data);
                            const options = [];
                            for (const inst of myObject.insts) {
                                options.push({ value: inst.replace("slapd-", ""), label: inst, disabled: false });
                            }

                            this.setState({
                                instList: options,
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
                                            jsx: staticStates.noInsts
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
                                    jsx: staticStates.noInsts
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
            const rows = [];
            for (const row of config.items) {
                rows.push([row[0], row[1], row[2]]);
            }
            // Get the server version from the monitor
            cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.state.serverId + ".socket", "monitor", "server"];
            log_cmd("loadBackups", "Get the server version", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" }).done(content => {
                        const monitor = JSON.parse(content);
                        this.setState({
                            backupRows: rows,
                            version: monitor.attrs.version[0],
                        });
                    })
                    .fail(_ => {
                        this.setState({
                            backupRows: rows,
                        });
                    });
        });
    }

    handleServerIdChange(e) {
        this.setState({
            pageLoadingState: { state: "loading", jsx: "" },
            progressValue: 25
        });
        this.loadInstanceList(e);
    }

    onFieldChange(e) {
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
        this.setState({
            loadingOperate: true
        });

        const cmd = ["dsctl", "-j", this.state.serverId, "remove", "--do-it"];
        log_cmd("removeInstance", `Remove the instance`, cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(_ => {
                    this.loadInstanceList();
                    this.addNotification("success", "Instance was successfully removed");
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.loadInstanceList();
                    this.addNotification(
                        "error",
                        `Error during instance remove operation - ${errMsg.desc}`
                    );
                });
        this.closeDeleteConfirm();
    }

    operateInstance(action) {
        this.setState({
            loadingOperate: true
        });

        const cmdStatus = ["dsctl", "-j", this.state.serverId, "status"];
        log_cmd("operateInstance", `Check instance status`, cmdStatus);
        cockpit
                .spawn(cmdStatus, { superuser: true, err: "message" })
                .done(status_data => {
                    const status_json = JSON.parse(status_data);
                    if (status_json.running && action === "start") {
                        this.addNotification("success", `Instance is already running`);
                        this.setState({
                            loadingOperate: false
                        });
                    } else if (!status_json.running && action === "stop") {
                        this.addNotification("success", `Instance is already stopped`);
                        this.setState({
                            loadingOperate: false,
                            pageLoadingState: {
                                state: "notRunning",
                                jsx: staticStates.notRunning
                            }
                        });
                    } else {
                        const cmd = ["dsctl", "-j", this.state.serverId, action];
                        log_cmd("operateInstance", `Do ${action} the instance`, cmd);
                        cockpit
                                .spawn(cmd, { superuser: true, err: "message" })
                                .done(_ => {
                                    this.loadInstanceList(this.state.serverId, action);
                                    if (action === "stop") {
                                        action = "stopp"; // Fixes typo in notification
                                    }
                                    this.addNotification("success", `Instance was successfully ${action}ed`);
                                })
                                .fail(err => {
                                    const errMsg = JSON.parse(err);
                                    this.addNotification(
                                        "error",
                                        `Error during instance ${action} operation - ${errMsg.desc}`
                                    );
                                    this.loadInstanceList(this.state.serverId, action);
                                });
                    }
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.addNotification(
                        "error",
                        `Error during instance check status operation - ${errMsg.desc}`
                    );
                    this.loadInstanceList(this.state.serverId, action);
                });
    }

    openCreateInstanceModal() {
        const key = this.state.createKey + 1;
        this.setState({
            showCreateInstanceModal: true,
            createKey: key
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
            loadingOperate,
            dropdownIsOpen,
            activeTabKey
        } = this.state;

        const dropdownItems = [
            <DropdownItem id="start-ds" key="start" component="button" onClick={() => (this.operateInstance("start"))}>
                Start Instance
            </DropdownItem>,
            <DropdownItem id="stop-ds" key="stop" component="button" onClick={() => (this.operateInstance("stop"))}>
                Stop Instance
            </DropdownItem>,
            <DropdownItem id="restart-ds" key="restart" component="button" onClick={() => (this.operateInstance("restart"))}>
                Restart Instance
            </DropdownItem>,
            <DropdownItem id="manage-backup-ds" key="backups" component="button" onClick={() => (this.openManageBackupsModal())}>
                Manage Backups
            </DropdownItem>,
            <DropdownItem id="reload-schema-ds" key="reload" component="button" onClick={() => (this.openSchemaReloadModal())}>
                Reload Schema Files
            </DropdownItem>,
            <DropdownSeparator key="separator" />,
            <DropdownItem id="remove-ds" key="remove" component="button" onClick={() => (this.showDeleteConfirm())}>
                Remove This Instance
            </DropdownItem>,
            <DropdownItem id="create-ds" key="create" component="button" onClick={() => (this.openCreateInstanceModal())}>
                Create New Instance
            </DropdownItem>
        ];

        let mainContent = "";
        if (pageLoadingState.state === "loading") {
            mainContent = (
                <div id="loading-instances" className="all-pages ds-center">
                    <div id="loading-page" className="ds-center ds-loading">
                        <TextContent>
                            <Text id="loading-msg" component={TextVariants.h3}>
                                Loading Directory Server Configuration ...
                            </Text>
                        </TextContent>
                        <p className="ds-margin-top-lg">
                            <span className="spinner spinner-lg spinner-inline" />
                        </p>
                        <div className="ds-margin-top-lg">
                            <Progress value={progressValue} label={`${progressValue}%`} measureLocation={ProgressMeasureLocation.inside} />
                        </div>
                    </div>
                </div>
            );
        } else if (pageLoadingState.state === "noInsts") {
            mainContent = (
                <div id="noInsts" className="all-pages ds-center">
                    {pageLoadingState.jsx}
                    <p className="ds-margin-top-xlg">
                        <Button
                            id="no-inst-create-btn"
                            variant="primary"
                            onClick={() => (this.openCreateInstanceModal())}
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
            operateSpinner = <Spinner className="ds-operate-spinner" size="md" />;
        }

        let serverDropdown = "";

        if (pageLoadingState.state !== "loading" &&
            pageLoadingState.state !== "noInsts" &&
            pageLoadingState.state !== "noPackage") {
            serverDropdown =
                <Grid className="ds-logo" hidden={pageLoadingState.state === "loading"}>
                    <GridItem span={10}>
                        <TextContent className="ds-logo-style" title={this.state.version}>
                            <Text id="main-banner" component={TextVariants.h1}>
                                <div className="ds-server-action">
                                    <FormSelect
                                        title="Directory Server instance list"
                                        value={serverId}
                                        id="serverId"
                                        onChange={this.handleServerIdChange}
                                        aria-label="FormSelect Input"
                                        className="ds-instance-select"
                                    >
                                        {instList.map((option, index) => (
                                            <FormSelectOption
                                                isDisabled={option.disabled}
                                                key={index}
                                                value={option.value.replace("slapd-", "")}
                                                label={option.label}
                                            />
                                        ))}
                                    </FormSelect>
                                </div>
                                {operateSpinner}
                            </Text>
                        </TextContent>
                    </GridItem>
                    <GridItem span={2}>
                        <Dropdown
                            id="ds-action"
                            className="ds-float-right ds-margin-top ds-margin-right"
                            position={DropdownPosition.right}
                            onSelect={this.handleDropdown}
                            toggle={
                                <DropdownToggle onToggle={this.handleToggle} toggleIndicator={CaretDownIcon} isPrimary id="ds-dropdown">
                                    Actions
                                </DropdownToggle>
                            }
                            isOpen={dropdownIsOpen}
                            dropdownItems={dropdownItems}
                        />
                    </GridItem>
                </Grid>;
        }

        let mainPage = <div>{mainContent}</div>;
        if (serverId !== "" && (pageLoadingState.state === "success" || pageLoadingState.state === "loading")) {
            mainPage =
                <div className="ds-margin-top">
                    <div hidden={pageLoadingState.state === "loading" || pageLoadingState.state === "notRunning"}>
                        <Tabs isFilled activeKey={activeTabKey} onSelect={this.handleNavSelect}>
                            <Tab eventKey={1} title={<TabTitleText><b>Server</b></TabTitleText>}>
                                <Server
                                    addNotification={this.addNotification}
                                    serverId={this.state.serverId}
                                    wasActiveList={this.state.wasActiveList}
                                    version={this.state.version}
                                    key={this.state.serverId}
                                />
                            </Tab>
                            <Tab eventKey={2} title={<TabTitleText><b>Database</b></TabTitleText>}>
                                <Database
                                    addNotification={this.addNotification}
                                    serverId={this.state.serverId}
                                    wasActiveList={this.state.wasActiveList}
                                    key={this.state.serverId}
                                />
                            </Tab>
                            <Tab eventKey={3} title={<TabTitleText><b>Replication</b></TabTitleText>}>
                                <Replication
                                    addNotification={this.addNotification}
                                    serverId={this.state.serverId}
                                    wasActiveList={this.state.wasActiveList}
                                    key={this.state.serverId}
                                />
                            </Tab>
                            <Tab eventKey={4} title={<TabTitleText><b>Schema</b></TabTitleText>}>
                                <Schema
                                    addNotification={this.addNotification}
                                    serverId={this.state.serverId}
                                    wasActiveList={this.state.wasActiveList}
                                    key={this.state.serverId}
                                />
                            </Tab>
                            <Tab eventKey={5} title={<TabTitleText><b>Plugins</b></TabTitleText>}>
                                <Plugins
                                    addNotification={this.addNotification}
                                    serverId={this.state.serverId}
                                    wasActiveList={this.state.wasActiveList}
                                    key={this.state.serverId}
                                />
                            </Tab>
                            <Tab eventKey={6} title={<TabTitleText><b>Monitoring</b></TabTitleText>}>
                                <Monitor
                                    addNotification={this.addNotification}
                                    serverId={this.state.serverId}
                                    wasActiveList={this.state.wasActiveList}
                                    key={this.state.serverId}
                                />
                            </Tab>
                            <Tab eventKey={7} title={<TabTitleText><b>LDAP Browser</b></TabTitleText>}>
                                <LDAPEditor
                                    key="ldap-editor"
                                    addNotification={this.addNotification}
                                    serverId={this.state.serverId}
                                    wasActiveList={this.state.wasActiveList}
                                    setPageSectionVariant={this.setPageSectionVariant}
                                />
                            </Tab>
                        </Tabs>
                    </div>
                    <div hidden={pageLoadingState.state !== "loading"}>{mainContent}</div>
                </div>;
        }

        return (
            <div className={loadingOperate ? "ds-disabled" : ""}>
                <AlertGroup isToast>
                    {notifications.map(({ key, variant, title }) => (
                        <Alert
                            isLiveRegion
                            variant={AlertVariant[variant]}
                            title={title}
                            actionClose={
                                <AlertActionCloseButton
                                    title={title}
                                    variantLabel={`${variant} alert`}
                                    onClose={() => this.removeNotification(key)}
                                />
                            }
                            timeout
                            key={key}
                        />
                    ))}
                </AlertGroup>
                {serverDropdown}
                {mainPage}
                <CreateInstanceModal
                    key={this.state.createKey}
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
                    handleChange={this.onFieldChange}
                    backups={this.state.backupRows}
                    reload={this.loadBackups}
                />
                <DoubleConfirmModal
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    handleChange={this.onFieldChange}
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
