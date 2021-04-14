import React from "react";
import cockpit from "cockpit";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import { DoubleConfirmModal } from "../notifications.jsx";
import {
    ReportCredentialsTable,
    ReportAliasesTable,
    AgmtTable,
    WinsyncAgmtTable,
    CleanALLRUVTable,
    AbortCleanALLRUVTable,
    ConflictTable,
    GlueTable,
} from "./monitorTables.jsx";
import {
    FullReportContent,
    ReportLoginModal,
    ReportCredentialsModal,
    ReportAliasesModal,
    TaskLogModal,
    AgmtDetailsModal,
    ConflictCompareModal,
} from "./monitorModals.jsx";
import {
    Nav,
    NavItem,
    TabContent,
    TabPane,
    TabContainer,
} from "patternfly-react";
import {
    Button,
    ExpandableSection,
    // Tab,
    // Tabs,
    // TabTitleText,
    // Grid,
    // GridItem,
    Tooltip,
    noop
} from "@patternfly/react-core";
import {
    SortByDirection,
} from '@patternfly/react-table';

const _ = cockpit.gettext;

export class ReplMonitor extends React.Component {
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

    componentDidMount() {
        if (this.state.initCreds) {
            let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "config", "get", "nsslapd-port", "nsslapd-localhost", "nsslapd-rootdn"];
            log_cmd("ReplMonitor", "add credentials during componentDidMount", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        let config = JSON.parse(content);
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
                        for (let agmt of this.props.data.replAgmts) {
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
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Failed to get config nsslapd-port, nsslapd-localhost and nasslapd-rootdn: ${errMsg.desc}`
                        );
                    });
        }
        this.props.enableTree();
    }

    constructor (props) {
        super(props);
        this.state = {
            activeKey: 1,
            activeReportKey: 1,
            logData: "",
            showBindModal: false,
            showLogModal: false,
            showFullReportModal: false,
            showReportLoginModal: false,
            showCredentialsModal: false,
            showAliasesModal: false,
            showCompareModal: false,
            showConfirmDeleteGlue: false,
            showConfirmConvertGlue: false,
            showConfirmSwapConflict: false,
            showConfirmConvertConflict: false,
            showConfirmDeleteConflict: false,
            modalSpinning: false,
            modalChecked: false,
            lagAgmts: [],
            credsData: [],
            aliasData: [],
            reportData: [],
            agmt: "",
            convertRDN: "",
            glueEntry: "",
            conflictEntry: "",
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

            credsInstanceName: "",
            disableBinddn: false,
            loginBinddn: "",
            loginBindpw: "",
            inputLoginData: false,

            credsHostname: "",
            credsPort: "",
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

            deleteConflictRadio: true,
            swapConflictRadio: false,
            convertConflictRadio: false,
        };

        this.onToggle = (isExpanded) => {
            this.setState({
                isExpanded
            });
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleReportNavSelect = this.handleReportNavSelect.bind(this);
        this.pokeAgmt = this.pokeAgmt.bind(this);
        this.pokeWinsyncAgmt = this.pokeWinsyncAgmt.bind(this);
        this.showAgmtModalRemote = this.showAgmtModalRemote.bind(this);
        this.closeAgmtModal = this.closeAgmtModal.bind(this);
        this.viewCleanLog = this.viewCleanLog.bind(this);
        this.viewAbortLog = this.viewAbortLog.bind(this);
        this.closeLogModal = this.closeLogModal.bind(this);
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

        // Conflict entry functions
        this.convertConflict = this.convertConflict.bind(this);
        this.swapConflict = this.swapConflict.bind(this);
        this.deleteConflict = this.deleteConflict.bind(this);
        this.resolveConflict = this.resolveConflict.bind(this);
        this.convertGlue = this.convertGlue.bind(this);
        this.deleteGlue = this.deleteGlue.bind(this);
        this.closeCompareModal = this.closeCompareModal.bind(this);
        this.confirmDeleteGlue = this.confirmDeleteGlue.bind(this);
        this.confirmConvertGlue = this.confirmConvertGlue.bind(this);
        this.closeConfirmDeleteGlue = this.closeConfirmDeleteGlue.bind(this);
        this.closeConfirmConvertGlue = this.closeConfirmConvertGlue.bind(this);
        this.handleRadioChange = this.handleRadioChange.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleConflictConversion = this.handleConflictConversion.bind(this);
        this.confirmDeleteConflict = this.confirmDeleteConflict.bind(this);
        this.confirmConvertConflict = this.confirmConvertConflict.bind(this);
        this.confirmSwapConflict = this.confirmSwapConflict.bind(this);
        this.closeConfirmDeleteConflict = this.closeConfirmDeleteConflict.bind(this);
        this.closeConfirmConvertConflict = this.closeConfirmConvertConflict.bind(this);
        this.closeConfirmSwapConflict = this.closeConfirmSwapConflict.bind(this);
    }

    handleRadioChange(value, evt) {
        // Handle the radio button changes
        let radioID = {
            'swapConflictRadio': false,
            'deleteConflictRadio': false,
            'convertConflictRadio': false,
        };

        radioID[evt.target.id] = value;
        this.setState({
            'swapConflictRadio': radioID.swapConflictRadio,
            'deleteConflictRadio': radioID.deleteConflictRadio,
            'convertConflictRadio': radioID.convertConflictRadio,
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

    convertConflict () {
        this.setState({modalSpinning: true});
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "convert", this.state.conflictEntry, "--new-rdn=" + this.state.convertRDN];
        log_cmd("convertConflict", "convert conflict entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication conflict entry was converted into a valid entry`
                    );
                    this.setState({
                        showCompareModal: false,
                    });
                    this.closeConfirmConvertConflict();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to convert conflict entry entry: ${this.state.conflictEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmConvertConflict();
                });
    }

    swapConflict () {
        this.setState({modalSpinning: true});
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "swap", this.state.conflictEntry];
        log_cmd("swapConflict", "swap in conflict entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication Conflict Entry is now the Valid Entry`
                    );
                    this.setState({
                        showCompareModal: false,
                    });
                    this.closeConfirmSwapConflict();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to swap in conflict entry: ${this.state.conflictEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmSwapConflict();
                });
    }

    deleteConflict () {
        this.setState({modalSpinning: true});
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "delete", this.state.conflictEntry];

        log_cmd("deleteConflict", "Delete conflict entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication conflict entry was deleted`
                    );
                    this.setState({
                        showCompareModal: false,
                    });
                    this.closeConfirmConvertConflict();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete conflict entry: ${this.state.conflictEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmDeleteConflict();
                });
    }

    resolveConflict (dn) {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "compare", dn];
        log_cmd("resolveConflict", "Compare conflict entry with valid entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let entries = JSON.parse(content);
                    this.setState({
                        cmpConflictEntry: entries.items[0],
                        cmpValidEntry: entries.items[1],
                        showCompareModal: true,
                        deleteConflictRadio: true,
                        swapConflictRadio: false,
                        convertConflictRadio: false,
                        convertRDN: "",
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to get conflict entries: ${dn} - ${errMsg.desc}`
                    );
                });
    }

    confirmConvertGlue (dn) {
        this.setState({
            showConfirmConvertGlue: true,
            glueEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmConvertGlue () {
        this.setState({
            showConfirmConvertGlue: false,
            glueEntry: "",
            modalChecked: false,
            modalSpinning: false,
        });
    }

    convertGlue () {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "convert-glue", this.state.glueEntry];
        log_cmd("convertGlue", "Convert glue entry to normal entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication glue entry was converted`
                    );
                    this.closeConfirmConvertGlue();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to convert glue entry: ${this.state.glueEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmConvertGlue();
                });
    }

    confirmDeleteGlue (dn) {
        this.setState({
            showConfirmDeleteGlue: true,
            glueEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    deleteGlue () {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "delete-glue", this.state.glueEntry];
        log_cmd("deleteGlue", "Delete glue entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication glue entry was deleted`
                    );
                    this.closeConfirmDeleteGlue();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete glue entry: ${this.state.glueEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmDeleteGlue();
                });
    }

    closeConfirmDeleteGlue () {
        this.setState({
            showConfirmDeleteGlue: false,
            glueEntry: "",
            modalChecked: false,
            modalSpinning: false,
        });
    }

    handleConflictConversion (dn) {
        // Follow the radio button and perform the conflict resolution
        if (this.state.deleteConflictRadio) {
            this.confirmDeleteConflict(dn);
        } else if (this.state.swapConflictRadio) {
            this.confirmSwapConflict(dn);
        } else {
            this.confirmConvertConflict(dn);
        }
    }

    confirmConvertConflict (dn) {
        if (this.state.convertRDN == "") {
            this.props.addNotification(
                "error",
                `You must provide a RDN if you want to convert the Conflict Entry`
            );
            return;
        }
        this.setState({
            showConfirmConvertConflict: true,
            conflictEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmConvertConflict () {
        this.setState({
            showConfirmConvertConflict: false,
            conflictEntry: "",
            modalChecked: false,
            modalSpinning: false,
            convertRDN: "",
        });
    }

    confirmSwapConflict (dn) {
        this.setState({
            showConfirmSwapConflict: true,
            conflictEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmSwapConflict () {
        this.setState({
            showConfirmSwapConflict: false,
            conflictEntry: "",
            modalChecked: false,
            modalSpinning: false,
        });
    }

    confirmDeleteConflict (dn) {
        this.setState({
            showConfirmDeleteConflict: true,
            conflictEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmDeleteConflict () {
        this.setState({
            showConfirmDeleteConflict: false,
            conflictEntry: "",
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeCompareModal () {
        this.setState({
            showCompareModal: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    handleNavSelect(key) {
        this.setState({
            activeKey: key
        });
    }

    handleReportNavSelect(key) {
        this.setState({
            activeReportKey: key
        });
    }

    closeLogModal() {
        this.setState({
            showLogModal: false
        });
    }

    viewCleanLog (name) {
        let logData = "";
        for (let task of this.props.data.cleanTasks) {
            if (task.attrs.cn[0] == name) {
                logData = task.attrs.nstasklog[0];
                break;
            }
        }
        this.setState({
            showLogModal: true,
            logData: logData
        });
    }

    viewAbortLog (name) {
        let logData = "";
        for (let task of this.props.data.abortTasks) {
            if (task.attrs.cn[0] == name) {
                logData = task.attrs.nstasklog[0];
                break;
            }
        }
        this.setState({
            showLogModal: true,
            logData: logData
        });
    }

    pokeAgmt (evt) {
        let agmt_name = evt.target.id;
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-agmt", "poke", "--suffix=" + this.props.suffix, agmt_name];
        log_cmd("pokeAgmt", "Awaken the agreement", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Replication agreement has been poked`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to poke replication agreement ${agmt_name} - ${errMsg.desc}`
                    );
                });
    }

    pokeWinsyncAgmt(name) {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-winsync-agmt", "poke", "--suffix=" + this.props.suffix, name];
        log_cmd("pokeAgmt", "Awaken the agreement", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Replication winsync agreement has been poked`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to poke replication winsync agreement ${name} - ${errMsg.desc}`
                    );
                });
    }

    showAgmtModalRemote (supplierName, replicaName, agmtName) {
        if (!agmtName) {
            this.props.addNotification(
                "error",
                `The agreement doesn't exist!`
            );
        } else {
            for (let supplier of this.state.reportData) {
                if (supplier.name == supplierName) {
                    for (let replica of supplier.data) {
                        if (`${replica.replica_root}:${replica.replica_id}` == replicaName) {
                            for (let agmt of replica.agmts_status) {
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
        const { credentialsList, oldCredsHostname, oldCredsPort, credsHostname,
                credsPort, credsBinddn, credsBindpw, pwInputInterractive } = this.state;

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
            credsPort: "",
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
            credsPort: connData.split(':')[1],
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
        let sorted_creds = [];
        let rows = [];

        // Convert the aliases into a sortable array based on the column indexes
        for (let row of this.state.credentialsList) {
            sorted_creds.push({
                '1': row.connData,
                '2': row.credsBinddn,
                '3': row.credsBindpw,
                '4': row.pwInputInterractive,
            });
        }

        // Sort the connections and build the new rows
        sorted_creds.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_creds.reverse();
        }
        for (let cred of sorted_creds) {
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
        let sorted_alias = [];
        let rows = [];

        // Convert the aliases into a sortable array based on the column indexes
        for (let row of this.state.aliasesList) {
            sorted_alias.push({
                '1': row[0],
                '2': row[1],
            });
        }

        // Sort the connections and build the new rows
        sorted_alias.sort((a, b) => (a[index] > b[index]) ? 1 : -1);
        if (direction !== SortByDirection.asc) {
            sorted_alias.reverse();
        }
        for (let alias of sorted_alias) {
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
                let connData = `${aliasHostname}:${aliasPort}`;
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

    refreshFullReport() {
        this.doFullReport();
        this.setState({
            reportRefreshing: true
        });
    }

    doFullReport() {
        // Initiate the report and continue the processing in the input window
        this.setState({
            reportLoading: true,
            activeReportKey: 2
        });

        let password = "";
        let credentials = [];
        let printCredentials = [];
        for (let row of this.state.credentialsList) {
            if (row.pwInputInterractive) {
                password = "*";
            } else {
                password = `${row.credsBindpw}`;
            }
            credentials.push(`${row.connData}:${row.credsBinddn}:${password}`);
            printCredentials.push(`${row.connData}:${row.credsBinddn}:********`);
        }

        let aliases = [];
        for (let row of this.state.aliasesList) {
            aliases.push(`${row[0]}=${row[1]}`);
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
            for (let value of aliases) {
                cmd = [...cmd, value];
            }
        }

        // We should not print the passwords to console.log
        let printCmd = cmd;
        if (credentials.length != 0) {
            cmd = [...cmd, "-c"];
            for (let value of credentials) {
                cmd = [...cmd, value];
            }
            printCmd = [...printCmd, "-c"];
            for (let value of printCredentials) {
                printCmd = [...printCmd, value];
            }
        }

        log_cmd("doFullReport", "Get the report for the current instance topology", printCmd);
        // We need to set it here because 'input' will be run from inside
        let proc = cockpit.spawn(cmd, { pty: true, environ: ["LC_ALL=C"], superuser: true, err: "message", directory: self.path });
        // We use it in processCredsInput
        this.setState({
            fullReportProcess: proc
        });
        proc
                .done(data => {
                    // Use the buffer from stream. 'data' is empty
                    let report = JSON.parse(buffer);
                    // We need to reparse the report data because agmts json wasn't parsed correctly because it was too nested
                    let agmts_reparsed = [];
                    let replica_reparsed = [];
                    let supplier_reparsed = [];
                    for (let supplier of report.items) {
                        replica_reparsed = [];
                        for (let replica of supplier.data) {
                            agmts_reparsed = [];
                            let agmts_done = false;
                            if (replica.hasOwnProperty("agmts_status")) {
                                for (let agmt of replica.agmts_status) {
                                    // We need this for Agreement View Modal
                                    agmt["supplierName"] = [supplier.name];
                                    agmt["replicaName"] = [`${replica.replica_root}:${replica.replica_id}`];
                                    agmt["replicaStatus"] = [`${replica.replica_status}`];
                                    agmt["rowKey"] = [`${supplier.name}:${replica.replica_root}:${replica.replica_id}:${agmt["agmt-name"]}`];
                                    agmts_reparsed.push(agmt);
                                    agmts_done = true;
                                }
                            }
                            if (!agmts_done) {
                                let agmt_empty = {};
                                agmt_empty["supplierName"] = [supplier.name];
                                if (replica.replica_root || replica.replica_id) {
                                    agmt_empty["replicaName"] = [`${replica.replica_root || ""}:${replica.replica_id || ""}`];
                                } else {
                                    agmt_empty["replicaName"] = [""];
                                }
                                agmt_empty["replicaStatus"] = [`${replica.replica_status}`];
                                agmt_empty["rowKey"] = [`${supplier.name}:${replica.replica_root}:${replica.replica_id}:None`];
                                agmts_reparsed.push(agmt_empty);
                            }
                            replica_reparsed.push({...replica, agmts_status: agmts_reparsed});
                        }
                        supplier_reparsed.push({...supplier, data: replica_reparsed});
                    }
                    const report_reparsed = {...report, items: supplier_reparsed};
                    this.setState({
                        reportData: report_reparsed.items,
                        showFullReportModal: true,
                        reportLoading: false,
                        doFullReportCleanup: true
                    });
                })
                .fail(_ => {
                    let errMsg = JSON.parse(buffer);
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
                    let lines = buffer.split("\n");
                    let last_line = lines[lines.length - 1];
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
                        for (let creds of this.state.dynamicCredentialsList) {
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
                    } else if (last_line.startsWith("Enter a password") && last_line.endsWith(": ")) {
                        buffer = "";
                        // Do the same logic for password but the string parsing is different
                        this.setState({
                            credsInstanceName: data.split(" on ")[1].split(": ")[0]
                        });
                        for (let creds of this.state.dynamicCredentialsList) {
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
                                loginBinddn: data.split("Enter a password for ")[1].split(" on")[0],
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

    render() {
        let reportData = this.state.reportData;
        let credentialsList = this.state.credentialsList;
        let aliasesList = this.state.aliasesList;
        let replAgmts = this.props.data.replAgmts;
        let replWinsyncAgmts = this.props.data.replWinsyncAgmts;
        let cleanTasks = this.props.data.cleanTasks;
        let abortTasks = this.props.data.abortTasks;
        let conflictEntries = this.props.data.conflicts;
        let glueEntries = this.props.data.glues;
        let fullReportModal = "";
        let reportLoginModal = "";
        let reportCredentialsModal = "";
        let reportAliasesModal = "";
        let agmtDetailModal = "";
        let winsyncAgmtDetailModal = "";

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

        let reportContent =
            <div>
                <Nav bsClass="nav nav-tabs nav-tabs-pf">
                    <NavItem className="ds-nav-med" eventKey={1}>
                        {_("Prepare Report")}
                    </NavItem>
                    <NavItem className="ds-nav-med" eventKey={2}>
                        {_("Report Result")}
                    </NavItem>
                </Nav>
                <TabContent>
                    <TabPane eventKey={1}>
                        <div className="ds-indent ds-margin-top-lg">
                            <ExpandableSection
                                className="h5Z"
                                toggleText={this.state.isExpanded ? 'Hide Help' : 'Show Help'}
                                onToggle={this.onToggle}
                                isExpanded={this.state.isExpanded}
                            >
                                <div className="ds-left-indent-md">
                                    <h4>How To Use Replication Sync Report</h4>
                                    <ol className="ds-left-indent-md ds-margin-top">
                                        <li>
                                            Update The <b>Replication Credentials</b>
                                            <ul>
                                                <li>• Initially, the table is populated with the local instance's replication
                                                    agreements, which includes the local instance.</li>
                                                <li>• Add the remaining replica server credentials from your replication topology.</li>
                                                <li>• It is advised to use an <b>Interactive Input</b> option for the
                                                    password because it's more secure.</li>
                                            </ul>
                                        </li>
                                        <li>
                                            Add <b>Instance Aliases</b> (if desired)
                                            <ul>
                                                <li>• Adding the aliases will make the report more readable.</li>
                                                <li>• Each instance can have one alias. For example, you can give names like this:
                                                    <b> Alias</b>=Main Supplier, <b>Hostname</b>=192.168.122.01, <b>Port</b>=38901</li>
                                                <li>• In the report result, the report will have an entry like this:
                                                    <b> Supplier: Main Supplier (192.168.122.01:38901)</b>.</li>
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
                            >
                                Generate Report
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
                        </div>
                    </TabPane>
                    <TabPane eventKey={2}>
                        <div className="ds-indent ds-margin-top-lg">
                            <FullReportContent
                                reportData={reportData}
                                viewAgmt={this.showAgmtModalRemote}
                                handleRefresh={this.refreshFullReport}
                                reportRefreshing={this.state.reportRefreshing}
                                reportLoading={this.state.reportLoading}
                            />
                        </div>
                    </TabPane>
                </TabContent>
            </div>;
        let cleanNavTitle = 'CleanAllRUV Tasks <font size="2">(' + cleanTasks.length + ')</font>';
        let abortNavTitle = 'Abort CleanAllRUV Tasks <font size="2">(' + abortTasks.length + ')</font>';
        let taskContent =
            <div>
                <Nav bsClass="nav nav-tabs nav-tabs-pf">
                    <NavItem className="ds-nav-med" eventKey={1}>
                        <div dangerouslySetInnerHTML={{__html: cleanNavTitle}} />
                    </NavItem>
                    <NavItem className="ds-nav-med" eventKey={2}>
                        <div dangerouslySetInnerHTML={{__html: abortNavTitle}} />
                    </NavItem>
                </Nav>
                <TabContent>
                    <TabPane eventKey={1}>
                        <div className="ds-indent ds-margin-top-lg">
                            <CleanALLRUVTable
                                tasks={cleanTasks}
                                viewLog={this.viewCleanLog}
                            />
                        </div>
                    </TabPane>
                    <TabPane eventKey={2}>
                        <div className="ds-indent ds-margin-top-lg">
                            <AbortCleanALLRUVTable
                                tasks={abortTasks}
                                viewLog={this.viewAbortLog}
                            />
                        </div>
                    </TabPane>
                </TabContent>
            </div>;

        let conflictNavTitle = 'Conflict Entries <font size="2">(' + conflictEntries.length + ')</font>';
        let glueNavTitle = 'Glue Entries <font size="2">(' + glueEntries.length + ')</font>';
        let conflictContent =
            <div>
                <Nav bsClass="nav nav-tabs nav-tabs-pf">
                    <NavItem className="ds-nav-med" eventKey={1}>
                        <div dangerouslySetInnerHTML={{__html: conflictNavTitle}} />
                    </NavItem>
                    <NavItem className="ds-nav-med" eventKey={2}>
                        <div dangerouslySetInnerHTML={{__html: glueNavTitle}} />
                    </NavItem>
                </Nav>
                <TabContent>
                    <TabPane eventKey={1}>
                        <div className="ds-indent ds-margin-top-lg">
                            <Tooltip
                                content={
                                    <div>
                                        Replication conflict entries occur when two entries are created with the
                                        same DN (or name) on different servers at about the same time.  The automatic conflict
                                        resolution procedure renames the entry created last.  Its RDN is changed
                                        into a multi-valued RDN that includes the entry's original RDN and it's unique
                                        identifier (nsUniqueId).  There are several ways to resolve a conflict,
                                        but choosing which option to use is up to you.
                                    </div>
                                }
                            >
                                <a className="ds-indent ds-font-size-sm">What Is A Replication Conflict Entry?</a>
                            </Tooltip>
                            <ConflictTable
                                conflicts={conflictEntries}
                                resolveConflict={this.resolveConflict}
                                key={conflictEntries}
                            />
                        </div>
                    </TabPane>
                    <TabPane eventKey={2}>
                        <div className="ds-indent ds-margin-top-lg">
                            <Tooltip
                                content={
                                    <div>
                                        When a <b>Delete</b> operation is replicated and the consumer server finds that the entry to be
                                        deleted has child entries, the conflict resolution procedure creates a "<i>glue entry</i>" to
                                        avoid having orphaned entries in the database.  In the same way, when an <b>Add</b> operation is
                                        replicated and the consumer server cannot find the parent entry, the conflict resolution
                                        procedure creates a "<i>glue entry</i>", representing the "parent entry", so that the new entry is
                                        not an orphaned entry.  You can choose to convert the glue entry, or remove the glue entry and
                                        all its child entries.
                                    </div>
                                }
                            >
                                <a className="ds-indent ds-font-size-sm">What Is A Replication Glue Entry?</a>
                            </Tooltip>
                            <GlueTable
                                glues={glueEntries}
                                convertGlue={this.confirmConvertGlue}
                                deleteGlue={this.confirmDeleteGlue}
                                key={glueEntries}
                            />
                        </div>
                    </TabPane>
                </TabContent>
            </div>;

        let fullReportTitle = 'Synchronization Report';
        let replAgmtNavTitle = 'Agreements <font size="2">(' + replAgmts.length + ')</font>';
        let winsyncNavTitle = 'Winsync <font size="2">(' + replWinsyncAgmts.length + ')</font>';
        let tasksNavTitle = 'Tasks <font size="2">(' + (cleanTasks.length + abortTasks.length) + ')</font>';
        let conflictsNavTitle = 'Conflict Entries <font size="2">(' + (conflictEntries.length + glueEntries.length) + ')</font>';

        return (
            <div>
                <div id="monitor-suffix-page" className="ds-tab-table">
                    <TabContainer className="ds-margin-top-lg" id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                        <div>
                            <Nav bsClass="nav nav-tabs nav-tabs-pf">
                                <NavItem eventKey={1}>
                                    <div dangerouslySetInnerHTML={{__html: fullReportTitle}} />
                                </NavItem>
                                <NavItem eventKey={2}>
                                    <div dangerouslySetInnerHTML={{__html: replAgmtNavTitle}} />
                                </NavItem>
                                <NavItem eventKey={3}>
                                    <div dangerouslySetInnerHTML={{__html: winsyncNavTitle}} />
                                </NavItem>
                                <NavItem eventKey={4}>
                                    <div dangerouslySetInnerHTML={{__html: tasksNavTitle}} />
                                </NavItem>
                                <NavItem eventKey={5}>
                                    <div dangerouslySetInnerHTML={{__html: conflictsNavTitle}} />
                                </NavItem>
                            </Nav>
                            <TabContent>
                                <TabPane eventKey={1}>
                                    <div className="ds-indent ds-tab-table">
                                        <TabContainer
                                            id="task-tabs"
                                            defaultActiveKey={1}
                                            onSelect={this.handleReportNavSelect}
                                            activeKey={this.state.activeReportKey}
                                        >
                                            {reportContent}
                                        </TabContainer>
                                    </div>
                                </TabPane>
                                <TabPane eventKey={2}>
                                    <div className="ds-indent ds-tab-table">
                                        <AgmtTable
                                            agmts={replAgmts}
                                            pokeAgmt={this.pokeAgmt}
                                        />
                                    </div>
                                </TabPane>
                                <TabPane eventKey={3}>
                                    <div className="dds-indent ds-tab-table">
                                        <WinsyncAgmtTable
                                            agmts={replWinsyncAgmts}
                                            pokeAgmt={this.pokeWinsyncAgmt}
                                        />
                                    </div>
                                </TabPane>
                                <TabPane eventKey={4}>
                                    <div className="ds-indent ds-tab-table">
                                        <TabContainer id="task-tabs" defaultActiveKey={1}>
                                            {taskContent}
                                        </TabContainer>
                                    </div>
                                </TabPane>
                                <TabPane eventKey={5}>
                                    <div className="ds-indent ds-tab-table">
                                        <TabContainer id="task-tabs" defaultActiveKey={1}>
                                            {conflictContent}
                                        </TabContainer>
                                    </div>
                                </TabPane>
                            </TabContent>
                        </div>
                    </TabContainer>
                    <TaskLogModal
                        showModal={this.state.showLogModal}
                        closeHandler={this.closeLogModal}
                        logData={this.state.logData}
                    />
                    {fullReportModal}
                    {reportLoginModal}
                    {reportCredentialsModal}
                    {reportAliasesModal}
                    {agmtDetailModal}
                    {winsyncAgmtDetailModal}
                    <ConflictCompareModal
                        showModal={this.state.showCompareModal}
                        conflictEntry={this.state.cmpConflictEntry}
                        validEntry={this.state.cmpValidEntry}
                        swapConflictRadio={this.state.swapConflictRadio}
                        convertConflictRadio={this.state.convertConflictRadio}
                        deleteConflictRadio={this.state.deleteConflictRadio}
                        newRDN={this.state.convertRDN}
                        closeHandler={this.closeCompareModal}
                        saveHandler={this.handleConflictConversion}
                        handleChange={this.handleChange}
                        handleRadioChange={this.handleRadioChange}
                    />
                </div>

                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteGlue}
                    closeHandler={this.closeConfirmDeleteGlue}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.deleteGlue}
                    spinning={this.state.modalSpinning}
                    item={this.state.glueEntry}
                    checked={this.state.modalChecked}
                    mTitle="Delete Glue Entry"
                    mMsg="Are you really sure you want to delete this glue entry and its child entries?"
                    mSpinningMsg="Deleting Glue Entry ..."
                    mBtnName="Delete Glue"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmConvertGlue}
                    closeHandler={this.closeConfirmConvertGlue}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.convertGlue}
                    spinning={this.state.modalSpinning}
                    item={this.state.glueEntry}
                    checked={this.state.modalChecked}
                    mTitle="Convert Glue Entry"
                    mMsg="Are you really sure you want to convert this glue entry to a regular entry?"
                    mSpinningMsg="Converting Glue Entry ..."
                    mBtnName="Convert Glue"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmConvertConflict}
                    closeHandler={this.closeConfirmConvertConflict}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.convertConflict}
                    spinning={this.state.modalSpinning}
                    item={this.state.conflictEntry}
                    checked={this.state.modalChecked}
                    mTitle="Convert Conflict Entry Into New Entry"
                    mMsg="Are you really sure you want to convert this conflict entry?"
                    mSpinningMsg="Converting Conflict Entry ..."
                    mBtnName="Convert Conflict"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmSwapConflict}
                    closeHandler={this.closeConfirmSwapConflict}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.swapConflict}
                    spinning={this.state.modalSpinning}
                    item={this.state.conflictEntry}
                    checked={this.state.modalChecked}
                    mTitle="Swap Conflict Entry"
                    mMsg="Are you really sure you want to swap this conflict entry with the valid entry?"
                    mSpinningMsg="Swapping Conflict Entry ..."
                    mBtnName="Swap Conflict"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteConflict}
                    closeHandler={this.closeConfirmDeleteConflict}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.deleteConflict}
                    spinning={this.state.modalSpinning}
                    item={this.state.conflictEntry}
                    checked={this.state.modalChecked}
                    mTitle="Delete Replication Conflict Entry"
                    mMsg="Are you really sure you want to delete this conflict entry?"
                    mSpinningMsg="Deleting Conflict Entry ..."
                    mBtnName="Delete Conflict"
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
    addNotification: PropTypes.func,
    reloadConflicts: PropTypes.func,
    enableTree: PropTypes.func,
};

ReplMonitor.defaultProps = {
    data: {},
    suffix: "",
    serverId: "",
    addNotification: noop,
    reloadConflicts: noop,
    enableTree: noop,
};

export default ReplMonitor;
