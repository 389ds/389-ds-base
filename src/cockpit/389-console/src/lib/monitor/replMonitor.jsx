import React from "react";
import cockpit from "cockpit";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import {
    ReportCredentialsTable,
    ReportAliasesTable,
    ReplDSRCTable,
    ReplDSRCAliasTable,
} from "./monitorTables.jsx";
import {
    FullReportContent,
    ReportLoginModal,
    ReportCredentialsModal,
    ReportConnectionModal,
    ReportAliasesModal,
    AgmtDetailsModal,
} from "./monitorModals.jsx";
import {
    Button,
    ExpandableSection,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import {
    SortByDirection,
} from '@patternfly/react-table';
import TrashAltIcon from '@patternfly/react-icons/dist/js/icons/trash-alt-icon';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faSyncAlt } from '@fortawesome/free-solid-svg-icons';
import { DoubleConfirmModal } from "../notifications.jsx";

const _ = cockpit.gettext;

export class ReplMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeKey: 0,
            activeConfigKey: 0,
            logData: "",
            showBindModal: false,
            showLogModal: false,
            showFullReportModal: false,
            showReportLoginModal: false,
            showCredentialsModal: false,
            showAliasesModal: false,
            loadingDSRC: false,

            modalSpinning: false,
            modalChecked: false,
            lagAgmts: [],
            credsData: [],
            aliasData: [],
            reportData: [],
            agmt: "",
            binddn: "cn=Directory Manager",
            bindpw: "",
            errObj: {},
            aliasList: [],
            newEntry: false,
            initCreds: true,
            isExpanded: false,

            fullReportProcess: {},
            interruptLoginCredsInput: false,
            doFullReportCleanup: false,
            reportRefreshing: false,
            reportLoading: false,
            // dsrc
            credRows: [...this.props.credRows],
            aliasRows: [...this.props.aliasRows],
            showAddDSRCCredModal: false,
            showAddDSRCAliasModal: false,
            showConfirmDeleteDSRCCred: false,
            showConfirmDeleteDSRCAlias: false,
            showConfirmOverwriteDSRC: false,
            deleteConn: "",
            deleteAlias: "",

            credsInstanceName: "",
            disableBinddn: false,
            loginBinddn: "",
            loginBindpw: "",
            inputLoginData: false,

            credsHostname: "",
            credsPort: 389,
            credsBinddn: "cn=Directory Manager",
            credsBindpw: "",
            pwInputInterractive: false,

            aliasHostname: "",
            aliasPort: 389,
            aliasName: "",

            credentialsList: [],
            dynamicCredentialsList: [],
            credSortBy: {},
            aliasesList: [],
            aliasSortBy: {},
        };

        this.onToggle = (isExpanded) => {
            this.setState({
                isExpanded
            });
        };

        this.maxValue = 65534;

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleNavConfigSelect = this.handleNavConfigSelect.bind(this);
        this.showAgmtModalRemote = this.showAgmtModalRemote.bind(this);
        this.closeAgmtModal = this.closeAgmtModal.bind(this);
        this.closeReportLoginModal = this.closeReportLoginModal.bind(this);

        // Replication report functions
        this.addCreds = this.addCreds.bind(this);
        this.editCreds = this.editCreds.bind(this);
        this.removeCreds = this.removeCreds.bind(this);
        this.openCredsModal = this.openCredsModal.bind(this);
        this.showAddCredsModal = this.showAddCredsModal.bind(this);
        this.showEditCredsModal = this.showEditCredsModal.bind(this);
        this.closeCredsModal = this.closeCredsModal.bind(this);
        this.onCredSort = this.onCredSort.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.addAliases = this.addAliases.bind(this);
        this.editAliases = this.editAliases.bind(this);
        this.removeAliases = this.removeAliases.bind(this);
        this.openAliasesModal = this.openAliasesModal.bind(this);
        this.showAddAliasesModal = this.showAddAliasesModal.bind(this);
        this.showEditAliasesModal = this.showEditAliasesModal.bind(this);
        this.closeAliasesModal = this.closeAliasesModal.bind(this);
        this.onAliasSort = this.onAliasSort.bind(this);

        this.doFullReport = this.doFullReport.bind(this);
        this.processCredsInput = this.processCredsInput.bind(this);
        this.closeReportModal = this.closeReportModal.bind(this);
        this.refreshFullReport = this.refreshFullReport.bind(this);

        this.onMinusConfig = this.onMinusConfig.bind(this);
        this.onConfigChange = this.onConfigChange.bind(this);
        this.onPlusConfig = this.onPlusConfig.bind(this);

        // dsrc
        this.loadDSRC = this.loadDSRC.bind(this);
        this.showAddDSRCCred = this.showAddDSRCCred.bind(this);
        this.showAddDSRCAlias = this.showAddDSRCAlias.bind(this);
        this.closeAddDSRCCred = this.closeAddDSRCCred.bind(this);
        this.closeAddDSRCAlias = this.closeAddDSRCAlias.bind(this);
        this.addDSRCCred = this.addDSRCCred.bind(this);
        this.addDSRCAlias = this.addDSRCAlias.bind(this);
        this.showConfirmDeleteDSRCCred = this.showConfirmDeleteDSRCCred.bind(this);
        this.showConfirmDeleteDSRCAlias = this.showConfirmDeleteDSRCAlias.bind(this);
        this.showConfirmOverwriteDSRC = this.showConfirmOverwriteDSRC.bind(this);
        this.closeConfirmDeleteDSRCCred = this.closeConfirmDeleteDSRCCred.bind(this);
        this.closeConfirmDeleteDSRCAlias = this.closeConfirmDeleteDSRCAlias.bind(this);
        this.closeConfirmOverwriteDSRC = this.closeConfirmOverwriteDSRC.bind(this);
        this.deleteDSRCCred = this.deleteDSRCCred.bind(this);
        this.deleteDSRCAlias = this.deleteDSRCAlias.bind(this);
        this.getAliasDeleteButton = this.getAliasDeleteButton.bind(this);
        this.getCredDeleteButton = this.getCredDeleteButton.bind(this);
        this.overwriteDSRC = this.overwriteDSRC.bind(this);
    }

    componentDidUpdate(prevProps, prevState) {
        if (!(prevState.showReportLoginModal) && (this.state.showReportLoginModal)) {
            // When the login modal turned on
            // We set timeout to close it and stop the report
            if (this.timer) window.clearTimeout(this.timer);

            this.timer = window.setTimeout(() => {
                this.setState({
                    showFullReportModal: false
                });
                this.timer = null;
            }, 300);
        }
        if ((prevState.showReportLoginModal) && !(this.state.showReportLoginModal)) {
            // When the login modal turned off
            // We clear the timeout
            if (this.timer) window.clearTimeout(this.timer);
        }
    }

    componentWillUnmount() {
        // It's important to do so we don't get the error
        // on the unmounted component
        if (this.timer) window.clearTimeout(this.timer);
    }

    loadDSRC() {
        // Load dsrc replication report configuration
        this.setState({
            loadingDSRC: true,
        });
        const dsrc_cmd = ["dsctl", "-j", this.props.serverId, "dsrc", "display"];
        log_cmd("loadDSRC", "Check for replication monitor configurations in the .dsrc file", dsrc_cmd);
        cockpit
                .spawn(dsrc_cmd, { superuser: true, err: "message" })
                .done(dsrc_content => {
                    const content = JSON.parse(dsrc_content);
                    let credRows = [];
                    let aliasRows = [];
                    if ("repl-monitor-connections" in content) {
                        const report_config = content["repl-monitor-connections"];
                        for (const [connection, value] of Object.entries(report_config)) {
                            const conn = connection + ":" + value;
                            credRows.push(conn.split(':'));
                            // [repl-monitor-connections]
                            // connection1 = server1.example.com:389:cn=Directory Manager:*
                        }
                    }
                    if ("repl-monitor-aliases" in content) {
                        const report_config = content["repl-monitor-aliases"];
                        for (const [alias_name, value] of Object.entries(report_config)) {
                            const alias = alias_name + ":" + value;
                            aliasRows.push(alias.split(':'));
                            // [repl-monitor-aliases]
                            // M1 = server1.example.com:38901
                        }
                    }
                    this.setState({
                        credRows,
                        aliasRows,
                        loadingDSRC: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to get .dsrc information: ${errMsg.desc}`
                    );
                    this.setState({
                        loadingDSRC: false,
                    });
                });
    }

    componentDidMount() {
        if (this.state.initCreds) {
            const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "config", "get", "nsslapd-port", "nsslapd-localhost", "nsslapd-rootdn"];
            log_cmd("ReplMonitor", "add credentials during componentDidMount", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        const config = JSON.parse(content);
                        this.setState(prevState => ({
                            credentialsList: [
                                ...prevState.credentialsList,
                                {
                                    connData: `${config.attrs["nsslapd-localhost"][0]}:${config.attrs["nsslapd-port"][0]}`,
                                    credsBinddn: config.attrs["nsslapd-rootdn"][0],
                                    credsBindpw: "",
                                    pwInputInterractive: true
                                }
                            ]
                        }));
                        if ('replAgmts' in this.props.data) {
                            for (const agmt of this.props.data.replAgmts) {
                                this.setState(prevState => ({
                                    credentialsList: [
                                        ...prevState.credentialsList,
                                        {
                                            connData: `${agmt.replica}`,
                                            credsBinddn: config.attrs["nsslapd-rootdn"][0],
                                            credsBindpw: "",
                                            pwInputInterractive: true
                                        }
                                    ],
                                    initCreds: false
                                }));
                            }
                        }
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Failed to get config nsslapd-port, nsslapd-localhost and nasslapd-rootdn: ${errMsg.desc}`
                        );
                    });
        }
        this.props.enableTree();
    }

    onMinusConfig(id) {
        this.setState({
            [id]: Number(this.state[id]) - 1
        });
    }

    onConfigChange(event, id, min) {
        const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
        this.setState({
            [id]: newValue > this.maxValue ? this.maxValue : newValue < min ? min : newValue
        });
    }

    onPlusConfig(id) {
        this.setState({
            [id]: Number(this.state[id]) + 1
        });
    }

    handleChange(value, evt) {
        // PF 4 version
        if (evt.target.type === 'number') {
            if (value) {
                value = parseInt(value);
            } else {
                value = 1;
            }
        }
        this.setState({
            [evt.target.id]: value
        });
    }

    handleFieldChange(e) {
        // PF 3 version
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        if (e.target.type === 'number') {
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

    handleNavSelect(event, tabIndex) {
        this.setState({
            activeKey: tabIndex
        });
    }

    handleNavConfigSelect(event, tabIndex) {
        this.setState({
            activeConfigKey: tabIndex
        });
    }

    getCredDeleteButton(name) {
        return (
            <a>
                <TrashAltIcon
                    className="ds-center"
                    onClick={() => {
                        this.showConfirmDeleteDSRCCred(name);
                    }}
                    title="Delete replica connection"
                />
            </a>
        );
    }

    getAliasDeleteButton(name) {
        return (
            <a>
                <TrashAltIcon
                    className="ds-center"
                    onClick={() => {
                        this.showConfirmDeleteDSRCAlias(name);
                    }}
                    title="Delete replica alias"
                />
            </a>
        );
    }

    closeLogModal() {
        this.setState({
            showLogModal: false
        });
    }

    showAgmtModalRemote (supplierName, replicaName, agmtName) {
        if (!agmtName) {
            this.props.addNotification(
                "error",
                `The agreement doesn't exist!`
            );
        } else {
            for (const supplier of this.state.reportData) {
                if (supplier.name == supplierName) {
                    for (const replica of supplier.data) {
                        if (`${replica.replica_root}:${replica.replica_id}` == replicaName) {
                            for (const agmt of replica.agmts_status) {
                                if (agmt['agmt-name'][0] == agmtName) {
                                    this.setState({
                                        showAgmtModal: true,
                                        agmt: agmt
                                    });
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    closeAgmtModal() {
        this.setState({
            showAgmtModal: false,
        });
    }

    changeCreds(action) {
        const {
            credentialsList, oldCredsHostname, oldCredsPort, credsHostname,
            credsPort, credsBinddn, credsBindpw, pwInputInterractive
        } = this.state;

        if (credsHostname === "" || credsPort === "" || credsBinddn === "") {
            this.props.addNotification("warning", "Host, Port, and Bind DN are required.");
        } else if (credsBindpw === "" && !pwInputInterractive) {
            this.props.addNotification("warning", "Password field can't be empty, if Interractive Input is not selected");
        } else {
            let credsExist = false;
            if ((action == "add") && (credentialsList.some(row => row.connData === `${credsHostname}:${credsPort}`))) {
                credsExist = true;
            }
            if ((action == "edit") && (credentialsList.some(row => row.connData === `${oldCredsHostname}:${oldCredsPort}`))) {
                this.setState({
                    credentialsList: credentialsList.filter(
                        row => row.connData !== `${oldCredsHostname}:${oldCredsPort}`
                    )
                });
            }

            if (!credsExist) {
                this.setState(prevState => ({
                    credentialsList: [
                        ...prevState.credentialsList,
                        {
                            connData: `${credsHostname}:${credsPort}`,
                            credsBinddn: credsBinddn,
                            credsBindpw: credsBindpw,
                            pwInputInterractive: pwInputInterractive
                        }
                    ]
                }));
            } else {
                this.props.addNotification(
                    "error",
                    `Credentials "${credsHostname}:${credsPort}" already exists`
                );
            }
            this.closeCredsModal();
        }
    }

    addCreds() {
        this.changeCreds("add");
    }

    editCreds() {
        this.changeCreds("edit");
    }

    removeCreds(connData) {
        this.setState({
            credentialsList: this.state.credentialsList.filter(
                row => row.connData !== connData
            )
        });
    }

    openCredsModal() {
        this.setState({
            showCredentialsModal: true
        });
    }

    showAddCredsModal() {
        this.openCredsModal();
        this.setState({
            newEntry: true,
            oldCredsHostname: "",
            oldCredsPort: "",
            credsHostname: "",
            credsPort: 389,
            credsBinddn: "cn=Directory Manager",
            credsBindpw: "",
            pwInputInterractive: false
        });
    }

    showEditCredsModal(connData, bindDN, bindPW, pwInteractive) {
        this.openCredsModal();
        this.setState({
            newEntry: false,
            oldCredsHostname: connData.split(':')[0],
            oldCredsPort: connData.split(':')[1],
            credsHostname: connData.split(':')[0],
            credsPort: parseInt(connData.split(':')[1]),
            credsBinddn: bindDN,
            credsBindpw: bindPW,
            pwInputInterractive: pwInteractive
        });
    }

    closeCredsModal() {
        this.setState({
            showCredentialsModal: false
        });
    }

    onCredSort(_event, index, direction) {
        const sorted_creds = [];
        const rows = [];

        // Convert the aliases into a sortable array based on the column indexes
        for (const row of this.state.credentialsList) {
            sorted_creds.push({
                1: row.connData,
                2: row.credsBinddn,
                3: row.credsBindpw,
                4: row.pwInputInterractive,
            });
        }

        // Sort the connections and build the new rows
        sorted_creds.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_creds.reverse();
        }
        for (const cred of sorted_creds) {
            rows.push({
                connData: cred['1'],
                credsBinddn: cred['2'],
                credsBindpw: cred['3'],
                pwInputInterractive: cred['4']
            });
        }

        this.setState({
            credSortBy: {
                index,
                direction
            },
            credentialsList: rows,
        });
    }

    onAliasSort(_event, index, direction) {
        const sorted_alias = [];
        const rows = [];

        // Convert the aliases into a sortable array based on the column indexes
        for (const row of this.state.aliasesList) {
            sorted_alias.push({
                1: row[0],
                2: row[1],
            });
        }

        // Sort the connections and build the new rows
        sorted_alias.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_alias.reverse();
        }
        for (const alias of sorted_alias) {
            rows.push([alias['1'], alias['2']]);
        }

        this.setState({
            aliasSortBy: {
                index,
                direction
            },
            aliasesList: rows,
        });
    }

    changeAlias(action) {
        const { aliasesList, aliasHostname, aliasPort, oldAliasName, aliasName } = this.state;
        let new_aliases = [...aliasesList];
        if (aliasPort === "" || aliasHostname === "" || aliasName === "") {
            this.props.addNotification("warning", "Host, Port, and Alias are required.");
        } else {
            let aliasExists = false;
            if ((action == "add") && (aliasesList.some(row => row[0] === aliasName))) {
                aliasExists = true;
            }
            if ((action == "edit") && (aliasesList.some(row => row[0] === oldAliasName))) {
                new_aliases = aliasesList.filter(row => row[0] !== oldAliasName);
            }

            if (!aliasExists) {
                const connData = `${aliasHostname}:${aliasPort}`;
                new_aliases.push([aliasName, connData]);
                this.setState({
                    aliasesList: new_aliases
                });
            } else {
                this.props.addNotification("error", `Alias "${aliasName}" already exists`);
            }
            this.closeAliasesModal();
        }
    }

    addAliases() {
        this.changeAlias("add");
    }

    editAliases() {
        this.changeAlias("edit");
    }

    removeAliases(alias) {
        this.setState({
            aliasesList: this.state.aliasesList.filter(row => row[0] !== alias)
        });
    }

    openAliasesModal() {
        this.setState({
            showAliasesModal: true,
        });
    }

    showAddAliasesModal() {
        this.openAliasesModal();
        this.setState({
            newEntry: true,
            aliasHostname: "",
            aliasPort: 389,
            oldAliasName: "",
            aliasName: ""
        });
    }

    showEditAliasesModal(alias, connData) {
        this.openAliasesModal();
        this.setState({
            newEntry: false,
            aliasHostname: connData.split(':')[0],
            aliasPort: parseInt(connData.split(':')[1]),
            oldAliasName: alias,
            aliasName: alias
        });
    }

    closeAliasesModal() {
        this.setState({
            showAliasesModal: false
        });
    }

    showConfirmOverwriteDSRC() {
        this.setState({
            showConfirmOverwriteDSRC: true
        });
    }

    closeConfirmOverwriteDSRC() {
        this.setState({
            showConfirmOverwriteDSRC: false
        });
    }

    overwriteDSRC () {
        // Get current DSRC Settings
        const dsrc_cmd = ["dsctl", "-j", this.props.serverId, "dsrc", "display"];
        log_cmd("overwriteDSRC", "gather conns and aliases from .dsrc file", dsrc_cmd);
        cockpit
                .spawn(dsrc_cmd, { superuser: true, err: "message" })
                .done(dsrc_content => {
                    const content = JSON.parse(dsrc_content);
                    let dsrcCreds = [];
                    let dsrcAliases = [];
                    let deleteCmd = ["dsctl", "-j", this.props.serverId, "dsrc", "repl-mon"];
                    let addCmd = ["dsctl", "-j", this.props.serverId, "dsrc", "repl-mon"];

                    // Gather the names of the replica connections and aliases
                    if ("repl-monitor-connections" in content) {
                        const report_config = content["repl-monitor-connections"];
                        for (const [cred, value] of Object.entries(report_config)) {
                            dsrcCreds.push(cred);
                        }
                    }
                    if ("repl-monitor-aliases" in content) {
                        const report_config = content["repl-monitor-aliases"];
                        for (const [alias, value] of Object.entries(report_config)) {
                            dsrcAliases.push(alias);
                        }
                    }

                    // Remove existing replica connections and aliases
                    if (this.state.credentialsList.length > 0 && dsrcCreds.length > 0) {
                        // Ok we have new replica connections, remove the old ones
                        deleteCmd.push("--del-conn");
                        for (const repl of dsrcCreds) {
                            deleteCmd.push(repl);
                        }
                    }
                    if (this.state.aliasesList.length > 0 && dsrcAliases.length > 0) {
                        // Ok we have new aliases, remove the old ones
                        deleteCmd.push("--del-alias");
                        for (const alias of dsrcAliases) {
                            deleteCmd.push(alias);
                        }
                    }
                    if (deleteCmd.length === 5) {
                        deleteCmd == "echo";  // do nothing
                    }

                    // Write new replica connections and aliases
                    if (this.state.credentialsList.length > 0) {
                        addCmd.push("--add-conn");
                        for (const rowIdx in this.state.credentialsList) {
                            const row = this.state.credentialsList[rowIdx];
                            let password = row.credsBindpw;
                            if (row.pwInputInterractive) {
                                password = "*";
                            }
                            const idx = parseInt(rowIdx) + 1;
                            const cred = `replica_${idx}:${row.connData}:${row.credsBinddn}:${password}`;
                            addCmd.push(cred);
                        }
                    }
                    if (this.state.aliasesList.length > 0) {
                        addCmd.push("--add-alias");
                        for (const row of this.state.aliasesList) {
                            const alias = `${row[0]}:${row[1]}`;
                            addCmd.push(alias);
                        }
                    }
                    log_cmd("overwriteDSRC", "delete conns and aliases in the .dsrc file", deleteCmd);
                    cockpit
                            .spawn(deleteCmd, { superuser: true, err: "message" })
                            .done( () => {
                                log_cmd("overwriteDSRC", "add conns and aliases in the .dsrc file", addCmd);
                                cockpit
                                        .spawn(addCmd, { superuser: true, err: "message" })
                                        .done( () => {
                                            this.setState({
                                                showConfirmOverwriteDSRC: false
                                            }, this.loadDSRC);
                                            this.props.addNotification(
                                                "success",
                                                "Successfully saved monitor configuration to the .dsrc file"
                                            );
                                        })
                                        .fail(err => {
                                            const errMsg = JSON.parse(err);
                                            this.setState({
                                                showConfirmOverwriteDSRC: false
                                            });
                                            this.props.addNotification(
                                                "error",
                                                `Failed to delete from .dsrc file: ${errMsg.desc}`
                                            );
                                        });
                                })
                                .fail(err => {
                                    const errMsg = JSON.parse(err);
                                    this.props.addNotification(
                                        "error",
                                        `Failed to add to .dsrc content: ${errMsg.desc}`
                                    );
                                    this.setState({
                                        showConfirmOverwriteDSRC: false
                                    });
                                });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to get .dsrc content: ${errMsg.desc}`
                    );
                    this.setState({
                        showConfirmOverwriteDSRC: false
                    });
                });
    }

    refreshFullReport() {
        this.doFullReport();
        this.setState({
            reportRefreshing: true
        });
    }

    doFullReport(dsrc) {
        // Initiate the report and continue the processing in the input window
        this.setState({
            reportLoading: true,
            activeKey: 2
        });

        let password = "";
        const credentials = [];
        const printCredentials = [];
        const aliases = [];
        if (dsrc) {
            // Use the monitor info from .dsrc
            for (const row of this.state.credRows) {
                credentials.push(`${row[1]}:${row[2]}:${row[3]}:${row[4]}`);
                printCredentials.push(`${row[1]}:${row[2]}:${row[3]}:********`);
            }
            for (const row of this.state.aliasRows) {
                aliases.push(`${row[0]}=${row[1]}:${row[2]}`);
            }
        } else {
            for (const row of this.state.credentialsList) {
                if (row.pwInputInterractive) {
                    password = "*";
                } else {
                    password = `${row.credsBindpw}`;
                }
                credentials.push(`${row.connData}:${row.credsBinddn}:${password}`);
                printCredentials.push(`${row.connData}:${row.credsBinddn}:********`);
            }

            for (const row of this.state.aliasesList) {
                aliases.push(`${row[0]}=${row[1]}`);
            }
        }

        let buffer = "";
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication",
            "monitor"
        ];

        if (aliases.length != 0) {
            cmd = [...cmd, "-a"];
            for (const value of aliases) {
                cmd = [...cmd, value];
            }
        }

        // We should not print the passwords to console.log
        let printCmd = cmd;
        if (credentials.length != 0) {
            cmd = [...cmd, "-c"];
            for (const value of credentials) {
                cmd = [...cmd, value];
            }
            printCmd = [...printCmd, "-c"];
            for (const value of printCredentials) {
                printCmd = [...printCmd, value];
            }
        }

        log_cmd("doFullReport", "Get the report for the current instance topology", printCmd);
        // We need to set it here because 'input' will be run from inside
        const proc = cockpit.spawn(cmd, { pty: true, environ: ["LC_ALL=C"], superuser: true, err: "message", directory: self.path });
        // We use it in processCredsInput
        this.setState({
            fullReportProcess: proc
        });
        proc
                .done(data => {
                    // Use the buffer from stream. 'data' is empty
                    const report = JSON.parse(buffer);
                    // We need to reparse the report data because agmts json wasn't parsed correctly because it was too nested
                    let agmts_reparsed = [];
                    let replica_reparsed = [];
                    const supplier_reparsed = [];
                    for (const supplier of report.items) {
                        replica_reparsed = [];
                        for (const replica of supplier.data) {
                            agmts_reparsed = [];
                            let agmts_done = false;
                            if (replica.hasOwnProperty("agmts_status")) {
                                for (const agmt of replica.agmts_status) {
                                    // We need this for Agreement View Modal
                                    agmt.supplierName = [supplier.name];
                                    agmt.replicaName = [`${replica.replica_root}:${replica.replica_id}`];
                                    agmt.replicaStatus = [`${replica.replica_status}`];
                                    agmt.rowKey = [`${supplier.name}:${replica.replica_root}:${replica.replica_id}:${agmt["agmt-name"]}`];
                                    agmts_reparsed.push(agmt);
                                    agmts_done = true;
                                }
                            }
                            if (!agmts_done) {
                                const agmt_empty = {};
                                agmt_empty.supplierName = [supplier.name];
                                if (replica.replica_root || replica.replica_id) {
                                    agmt_empty.replicaName = [`${replica.replica_root || ""}:${replica.replica_id || ""}`];
                                } else {
                                    agmt_empty.replicaName = [""];
                                }
                                agmt_empty.replicaStatus = [`${replica.replica_status}`];
                                agmt_empty.rowKey = [`${supplier.name}:${replica.replica_root}:${replica.replica_id}:None`];
                                agmts_reparsed.push(agmt_empty);
                            }
                            replica_reparsed.push({ ...replica, agmts_status: agmts_reparsed });
                        }
                        supplier_reparsed.push({ ...supplier, data: replica_reparsed });
                    }

                    const report_reparsed = { ...report, items: supplier_reparsed };
                    this.setState({
                        reportData: report_reparsed.items,
                        showFullReportModal: true,
                        reportLoading: false,
                        doFullReportCleanup: true
                    });
                })
                .fail(_ => {
                    const errMsg = JSON.parse(buffer);
                    this.props.addNotification(
                        "error",
                        `Sync report has failed - ${errMsg.desc}`
                    );
                    this.setState({
                        dynamicCredentialsList: [],
                        reportLoading: false,
                        doFullReportCleanup: true,
                        activeReportKey: 1
                    });
                })
                // Stream is run each time as a new character arrives
                .stream(data => {
                    buffer += data;
                    const lines = buffer.split("\n");
                    const last_line = lines[lines.length - 1];
                    let found_creds = false;
                    // Interractive Input is required
                    // Check for Bind DN first
                    if (last_line.startsWith("Enter a bind DN") && last_line.endsWith(": ")) {
                        buffer = "";
                        // Get the instance name. We need it for fetching the creds data from stored state list
                        this.setState({
                            credsInstanceName: data.split("a bind DN for ")[1].split(": ")[0]
                        });

                        // First check if DN is in the list already (either from previous run or during this execution)
                        for (const creds of this.state.dynamicCredentialsList) {
                            if (creds.credsInstanceName == this.state.credsInstanceName) {
                                found_creds = true;
                                proc.input(`${creds.binddn}\n`, true);
                            }
                        }
                        // If we don't have the creds - open the modal window and ask the user for input
                        if (!found_creds) {
                            this.setState({
                                showReportLoginModal: true,
                                binddnRequired: true,
                                disableBinddn: false,
                                credsInstanceName: this.state.credsInstanceName,
                                loginBinddn: "",
                                loginBindpw: ""
                            });
                        }

                    // Check for password
                } else if ((last_line.startsWith("Enter a password") || last_line.startsWith("File ")) && last_line.endsWith(": ")) {
                        buffer = "";
                        // Do the same logic for password but the string parsing is different
                        this.setState({
                            credsInstanceName: data.split(" on ")[1].split(": ")[0]
                        });

                        for (const creds of this.state.dynamicCredentialsList) {
                            if (creds.credsInstanceName == this.state.credsInstanceName) {
                                found_creds = true;
                                proc.input(`${creds.bindpw}\n`, true);
                                this.setState({
                                    credsInstanceName: ""
                                });
                            }
                        }

                        if (!found_creds) {
                            this.setState({
                                showReportLoginModal: true,
                                bindpwRequired: true,
                                credsInstanceName: this.state.credsInstanceName,
                                disableBinddn: true,
                                loginBinddn: data.split("nter a password for ")[1].split(" on")[0],
                                loginBindpw: ""
                            });
                        }
                    }
                });
    }

    closeReportLoginModal() {
        this.setState({
            showReportLoginModal: false,
            reportLoading: false,
            activeReportKey: 1
        });
    }

    processCredsInput() {
        const {
            loginBinddn,
            loginBindpw,
            credsInstanceName,
            fullReportProcess
        } = this.state;

        if (loginBinddn == "" || loginBindpw == "") {
            this.props.addNotification("warning", "Bind DN and password are required.");
        } else {
            this.setState({
                showReportLoginModal: false,
                reportLoading: false
            });

            // Store the temporary data in state
            this.setState(prevState => ({
                dynamicCredentialsList: [
                    ...prevState.dynamicCredentialsList,
                    {
                        binddn: loginBinddn,
                        bindpw: loginBindpw,
                        credsInstanceName: credsInstanceName
                    }
                ]
            }));

            // We wait for some input - put the right one here
            if (this.state.binddnRequired) {
                fullReportProcess.input(`${loginBinddn}\n`, true);
                this.setState({
                    binddnRequired: false
                });
            } else if (this.state.bindpwRequired) {
                // fullReportProcess.input(`${loginBindpw}\n`, true);
                fullReportProcess.input(loginBindpw + "\n", true);
                this.setState({
                    bindpwRequired: false
                });
            }
        }
    }

    closeReportModal() {
        this.setState({
            showFullReportModal: false,
            reportLoading: false
        });
    }

    // dsrc
    showAddDSRCCred() {
        // Add a connection to dsrc file
        this.setState({
            showAddDSRCCredModal: true,
            modalChecked: false,
            modalSpinning: false,
            connName: "",
            connHostname: "",
            connPort: 636,
            connBindDN: "",
            credsCred: "",
        });
    }

    closeAddDSRCCred () {
        this.setState({
            showAddDSRCCredModal: false,
        });
    }

    showAddDSRCAlias() {
        // Add alias to dsrc file
        this.setState({
            showAddDSRCAliasModal: true,
            modalChecked: false,
            modalSpinning: false,
            newEntry: true,
            aliasName: "",
            aliasPort: 636,
            aliasHostname: "",
        });
    }

    closeAddDSRCAlias () {
        this.setState({
            showAddDSRCAliasModal: false,
        });
    }

    showConfirmDeleteDSRCCred (name) {
        this.setState({
            showConfirmDeleteDSRCCred: true,
            connName: name,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmDeleteDSRCCred () {
        this.setState({
            showConfirmDeleteDSRCCred: false,
        });
    }

    showConfirmDeleteDSRCAlias (name) {
        this.setState({
            showConfirmDeleteDSRCAlias: true,
            aliasName: name,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmDeleteDSRCAlias () {
        this.setState({
            showConfirmDeleteDSRCAlias: false,
        });
    }

    deleteDSRCCred () {
        const dsrc_cmd = ["dsctl", "-j", this.props.serverId, "dsrc", "repl-mon", "--del-conn=" + this.state.connName];

        this.setState({
            loadingDSRC: true,
        });

        log_cmd("deleteDSRCCred", "Delete a replica connection from the .dsrc file", dsrc_cmd);
        cockpit
                .spawn(dsrc_cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.loadDSRC();
                    this.setState({
                        showConfirmDeleteDSRCCred: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to update .dsrc information: ${errMsg.desc}`
                    );
                    this.loadDSRC();
                });
    }

    deleteDSRCAlias () {
        const dsrc_cmd = ["dsctl", "-j", this.props.serverId, "dsrc", "repl-mon", "--del-alias=" + this.state.aliasName];

        this.setState({
            loadingDSRC: true,
        });

        log_cmd("deleteDSRCCred", "Delete a replication monitor alias from the .dsrc file", dsrc_cmd);
        cockpit
                .spawn(dsrc_cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.loadDSRC();
                    this.setState({
                        showConfirmDeleteDSRCAlias: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to update .dsrc information: ${errMsg.desc}`
                    );
                    this.loadDSRC();
                });
    }

    addDSRCCred () {
        const {
            connName,
            connHostname,
            connPort,
            connBindDN,
            connCred
        } = this.state;
        const conn = connName + ":" + connHostname + ":" + connPort + ":" + connBindDN + ":" + connCred;
        const dsrc_cmd = ["dsctl", "-j", this.props.serverId, "dsrc", "repl-mon", "--add-conn=" + conn];

        this.setState({
            loadingDSRC: true,
        });

        log_cmd("addDSRCCred", "Add a replica connection to the .dsrc file", dsrc_cmd);
        cockpit
                .spawn(dsrc_cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.setState({
                        showAddDSRCCredModal: false,
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully added connection to .dsrc config file"
                    );
                    this.loadDSRC();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to update .dsrc information: ${errMsg.desc}`
                    );
                    this.loadDSRC();
                });
    }

    addDSRCAlias() {
        const alias = this.state.aliasName + ":" + this.state.aliasHostname + ":" + this.state.aliasPort;
        const dsrc_cmd = ["dsctl", "-j", this.props.serverId, "dsrc", "repl-mon", "--add-alias=" + alias];

        this.setState({
            loadingDSRC: true,
        });

        log_cmd("addDSRCAlias", "Add an alias to the .dsrc file", dsrc_cmd);
        cockpit
                .spawn(dsrc_cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.setState({
                        showAddDSRCAliasModal: false,
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully added alias to .dsrc config file"
                    );
                    this.loadDSRC();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to update .dsrc information: ${errMsg.desc}`
                    );
                    this.loadDSRC();
                });
    }

    render() {
        const reportData = this.state.reportData;
        const credentialsList = this.state.credentialsList;
        const aliasesList = this.state.aliasesList;
        const fullReportModal = "";
        let reportLoginModal = "";
        let reportCredentialsModal = "";
        let reportAliasesModal = "";
        let agmtDetailModal = "";
        const winsyncAgmtDetailModal = "";

        if (this.state.showReportLoginModal) {
            reportLoginModal =
                <ReportLoginModal
                    showModal={this.state.showReportLoginModal}
                    closeHandler={this.closeReportLoginModal}
                    handleChange={this.handleFieldChange}
                    processCredsInput={this.processCredsInput}
                    instanceName={this.state.credsInstanceName}
                    disableBinddn={this.state.disableBinddn}
                    loginBinddn={this.state.loginBinddn}
                    loginBindpw={this.state.loginBindpw}
                />;
        }
        if (this.state.showCredentialsModal) {
            reportCredentialsModal =
                <ReportCredentialsModal
                    showModal={this.state.showCredentialsModal}
                    closeHandler={this.closeCredsModal}
                    handleFieldChange={this.handleFieldChange}
                    onMinusConfig={this.onMinusConfig}
                    onConfigChange={this.onConfigChange}
                    onPlusConfig={this.onPlusConfig}
                    newEntry={this.state.newEntry}
                    hostname={this.state.credsHostname}
                    port={this.state.credsPort}
                    binddn={this.state.credsBinddn}
                    bindpw={this.state.credsBindpw}
                    pwInputInterractive={this.state.pwInputInterractive}
                    addConfig={this.addCreds}
                    editConfig={this.editCreds}
                />;
        }
        if (this.state.showAliasesModal) {
            reportAliasesModal =
                <ReportAliasesModal
                    showModal={this.state.showAliasesModal}
                    closeHandler={this.closeAliasesModal}
                    handleFieldChange={this.handleFieldChange}
                    onMinusConfig={this.onMinusConfig}
                    onConfigChange={this.onConfigChange}
                    onPlusConfig={this.onPlusConfig}
                    newEntry={this.state.newEntry}
                    hostname={this.state.aliasHostname}
                    port={this.state.aliasPort}
                    alias={this.state.aliasName}
                    addConfig={this.addAliases}
                    editConfig={this.editAliases}
                />;
        }
        if (this.state.showAgmtModal) {
            agmtDetailModal =
                <AgmtDetailsModal
                    showModal={this.state.showAgmtModal}
                    closeHandler={this.closeAgmtModal}
                    agmt={this.state.agmt}
                />;
        }

        let reportBtnName = "Generate Report";
        const extraPrimaryProps = {};
        if (this.state.reportLoading) {
            reportBtnName = "Generating ...";
            extraPrimaryProps.spinnerAriaValueText = "Generating";
        }

        let reportContent =
            <div className="ds-margin-top-lg ds-indent ds-margin-bottom-md">
                <Tabs isFilled activeKey={this.state.activeKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Saved Report Configuration</TabTitleText>}>
                        <Tabs className="ds-margin-top-lg" isBox activeKey={this.state.activeConfigKey} onSelect={this.handleNavConfigSelect}>
                            <Tab eventKey={0} title={<TabTitleText>Replica Credentials</TabTitleText>}>
                                <ReplDSRCTable
                                    key={this.state.credRows}
                                    rows={this.state.credRows}
                                    getDeleteButton={this.getCredDeleteButton}
                                />
                                <Button
                                    className="ds-margin-top-lg"
                                    variant="secondary"
                                    onClick={this.showAddDSRCCred}
                                    title="Add a replica credential to the .dsrc file"
                                >
                                    Add Connection
                                </Button>
                            </Tab>
                            <Tab eventKey={1} title={<TabTitleText>Replica Naming Aliases</TabTitleText>}>
                                <ReplDSRCAliasTable
                                    key={this.state.aliasRows}
                                    rows={this.state.aliasRows}
                                    getDeleteButton={this.getAliasDeleteButton}
                                />
                                <Button
                                    className="ds-margin-top-lg"
                                    variant="secondary"
                                    onClick={this.showAddDSRCAlias}
                                    title="Add a replica alias to the .dsrc file"
                                >
                                    Add Alias
                                </Button>
                            </Tab>
                        </Tabs>
                        <hr />
                        {this.state.credRows.length > 0 && (
                            <Button
                                variant="primary"
                                onClick={() => { this.doFullReport(1) }}
                                title="Use the specified credentials and display full topology report"
                                isLoading={this.state.reportLoading}
                                isDisabled={this.state.reportLoading}
                                spinnerAriaValueText={this.state.reportLoading ? "Generating" : undefined}
                                {...extraPrimaryProps}
                            >
                                {reportBtnName}
                            </Button>
                        )}
                    </Tab>
                    <Tab eventKey={1} id="prepare-new-report" title={<TabTitleText>{"Prepare New Report"}</TabTitleText>}>
                        <ExpandableSection
                            toggleText={this.state.isExpanded ? 'Hide Help' : 'Show Help'}
                            onToggle={this.onToggle}
                            isExpanded={this.state.isExpanded}
                            className="ds-margin-top-lg ds-left-margin"
                        >
                            <div className="ds-left-indent-md">
                                <TextContent>
                                    <Text component={TextVariants.h3}>
                                        How To Use Replication Sync Report
                                    </Text>
                                </TextContent>
                                <ol className="ds-left-indent-md ds-margin-top">
                                    <li>
                                        Update The <b>Replication Credentials</b>
                                        <ul>
                                            <li>• Initially, the table is populated with the local instance's replication
                                                agreements, which includes the local instance.
                                            </li>
                                            <li>• Add the remaining replica server credentials from your replication topology.</li>
                                            <li>• It is advised to use an <b>Interactive Input</b> option for the
                                                password because it's more secure.
                                            </li>
                                        </ul>
                                    </li>
                                    <li>
                                        Add <b>Replica Aliases</b> (if desired)
                                        <ul>
                                            <li>• Adding aliases will make the report more readable.</li>
                                            <li>• Each Replica can have one alias. For example, you can give names like this:
                                                <b> Alias</b>=Main Supplier, <b>Hostname</b>=192.168.122.01, <b>Port</b>=38901
                                            </li>
                                            <li>• In the report result, the report will have an entry like this:
                                                <b> Supplier: Main Supplier (192.168.122.01:38901)</b>.
                                            </li>
                                        </ul>
                                    </li>
                                    <li>
                                        Press <b>Generate Report</b> Button
                                        <ul>
                                            <li>• It will initiate the report creation.</li>
                                            <li>• You may be asked for the credentials while the process is running through the agreements.</li>
                                        </ul>
                                    </li>
                                </ol>
                                <p />
                            </div>
                        </ExpandableSection>
                        <Button
                            className="ds-margin-top-lg"
                            variant="primary"
                            onClick={this.doFullReport}
                            title="Use the specified credentials and display full topology report"
                            isLoading={this.state.reportLoading}
                            isDisabled={this.state.reportLoading}
                            spinnerAriaValueText={this.state.reportLoading ? "Generating" : undefined}
                            {...extraPrimaryProps}
                        >
                            {reportBtnName}
                        </Button>
                        <Button
                            className="ds-margin-top-lg ds-left-margin"
                            variant="secondary"
                            onClick={this.showConfirmOverwriteDSRC}
                            title="Save the report configuration in the .dsrc file for future use."
                        >
                            Save Report Configuration
                        </Button>
                        <hr />
                        <ReportCredentialsTable
                            rows={credentialsList}
                            deleteConfig={this.removeCreds}
                            editConfig={this.showEditCredsModal}
                            sortBy={this.state.credSortBy}
                            onSort={this.onCredSort}
                        />
                        <Button
                            className="ds-margin-top"
                            variant="secondary"
                            onClick={this.showAddCredsModal}
                        >
                            Add Credentials
                        </Button>
                        <ReportAliasesTable
                            rows={aliasesList}
                            deleteConfig={this.removeAliases}
                            editConfig={this.showEditAliasesModal}
                            sortBy={this.state.aliasSortBy}
                            onSort={this.onAliasSort}
                        />
                        <Button
                            className="ds-margin-top"
                            variant="secondary"
                            onClick={this.showAddAliasesModal}
                        >
                            Add Alias
                        </Button>
                    </Tab>
                    {reportData.length > 0 && (
                        <Tab eventKey={2} title={<TabTitleText>{"Report Result"}</TabTitleText>}>
                            <div className="ds-indent ds-margin-top-lg">
                                <FullReportContent
                                    reportData={reportData}
                                    viewAgmt={this.showAgmtModalRemote}
                                    handleRefresh={this.refreshFullReport}
                                    reportRefreshing={this.state.reportRefreshing}
                                    reportLoading={this.state.reportLoading}
                                />
                            </div>
                        </Tab>
                    )}
                </Tabs>
            </div>;

        if (this.state.loadingDSRC) {
            reportContent =
                <div className="ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Loading Replication DSRC Information ...
                        </Text>
                    </TextContent>
                    <Spinner className="ds-margin-top-lg" size="xl" />
                </div>;
        }

        let overwriteWarning = (
            "Only one monitor configuraton can be saved in the server's " +
            "'~/.dsrc' file.  There is already an existing monitor " +
            "configuration, and if you proceed it will be completely " +
            "overwritten with the new configuraton.");
        if (this.state.credRows.length == 0 && this.state.aliasRows.length == 0) {
            overwriteWarning = (
                "This will save the current credentials and aliases to the " +
                "server's '~/.dsrc' file so it can be reused in the future.");
        }

        return (
            <div id="repl-monitor-page" className="ds-tab-table">
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Synchronization Report
                            <FontAwesomeIcon
                                size="lg"
                                className="ds-left-margin ds-refresh"
                                icon={faSyncAlt}
                                title="Refresh replication monitor"
                                onClick={this.props.reload}
                            />
                        </Text>
                    </TextContent>
                </div>

                {reportContent}
                {fullReportModal}
                {reportLoginModal}
                {reportCredentialsModal}
                {reportAliasesModal}
                {agmtDetailModal}
                {winsyncAgmtDetailModal}
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteDSRCCred}
                    closeHandler={this.closeConfirmDeleteDSRCCred}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.deleteDSRCCred}
                    spinning={this.state.modalSpinning}
                    item={this.state.connName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Replica Connection"
                    mMsg="Are you really sure you want to delete this connection from the '~/.dsrc' file?"
                    mSpinningMsg="Deleting Connection ..."
                    mBtnName="Delete Connection"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteDSRCAlias}
                    closeHandler={this.closeConfirmDeleteDSRCAlias}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.deleteDSRCAlias}
                    spinning={this.state.modalSpinning}
                    item={this.state.aliasName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Replica Alias"
                    mMsg="Are you really sure you want to delete this alias from the '~/.dsrc' file?"
                    mSpinningMsg="Deleting Alias ..."
                    mBtnName="Delete Alias"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmOverwriteDSRC}
                    closeHandler={this.closeConfirmOverwriteDSRC}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.overwriteDSRC}
                    spinning={this.state.modalSpinning}
                    item="Are you sure you want to proceed?"
                    checked={this.state.modalChecked}
                    mTitle="Overwrite Monitor Configuration"
                    mMsg={overwriteWarning}
                    mSpinningMsg="Writing DSRC ..."
                    mBtnName="Write DSRC"
                />
                <ReportAliasesModal
                    showModal={this.state.showAddDSRCAliasModal}
                    closeHandler={this.closeAddDSRCAlias}
                    handleFieldChange={this.handleFieldChange}
                    onMinusConfig={this.onMinusConfig}
                    onConfigChange={this.onConfigChange}
                    onPlusConfig={this.onPlusConfig}
                    newEntry={this.state.newEntry}
                    hostname={this.state.aliasHostname}
                    port={this.state.aliasPort}
                    alias={this.state.aliasName}
                    addConfig={this.addDSRCAlias}
                    editConfig={this.addDSRCAlias}
                />
                <ReportConnectionModal
                    showModal={this.state.showAddDSRCCredModal}
                    closeHandler={this.closeAddDSRCCred}
                    handleFieldChange={this.handleFieldChange}
                    onMinusConfig={this.onMinusConfig}
                    onConfigChange={this.onConfigChange}
                    onPlusConfig={this.onPlusConfig}
                    name={this.state.connName}
                    hostname={this.state.connHostname}
                    port={this.state.connPort}
                    binddn={this.state.connBindDN}
                    bindpw={this.state.connCred}
                    pwInputInterractive={this.state.pwInputInterractive}
                    addConn={this.addDSRCCred}
                />
            </div>
        );
    }
}

// Props and defaultProps

ReplMonitor.propTypes = {
    data: PropTypes.object,
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    credRows: PropTypes.array,
    aliasRows: PropTypes.array,
    addNotification: PropTypes.func,
    reloadConflicts: PropTypes.func,
    enableTree: PropTypes.func,
};

ReplMonitor.defaultProps = {
    data: {},
    suffix: "",
    serverId: "",
    credRows: [],
    aliasRows: [],
};

export default ReplMonitor;
