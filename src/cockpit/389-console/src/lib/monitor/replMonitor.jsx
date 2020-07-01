import React from "react";
import cockpit from "cockpit";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import { ConfirmPopup } from "../notifications.jsx";
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
    WinsyncAgmtDetailsModal,
    ConflictCompareModal,
} from "./monitorModals.jsx";
import {
    Nav,
    NavItem,
    TabContent,
    TabPane,
    TabContainer,
    Button,
    noop
} from "patternfly-react";
import CustomCollapse from "../customCollapse.jsx";

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
                                    connData: `${config.attrs["nsslapd-localhost"]}:${config.attrs["nsslapd-port"]}`,
                                    credsBinddn: config.attrs["nsslapd-rootdn"],
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
                                        credsBinddn: config.attrs["nsslapd-rootdn"],
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
            showAgmtModal: false,
            isRemoteAgmt: false,
            showFullReportModal: false,
            showReportLoginModal: false,
            showCredentialsModal: false,
            showAliasesModal: false,
            showWinsyncAgmtModal: false,
            showInitWinsyncConfirm: false,
            showInitConfirm: false,
            showCompareModal: false,
            showConfirmDeleteGlue: false,
            showConfirmConvertGlue: false,
            showConfirmSwapConflict: false,
            showConfirmConvertConflict: false,
            showConfirmDeleteConflict: false,
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
            aliasesList: []
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleReportNavSelect = this.handleReportNavSelect.bind(this);
        this.pokeAgmt = this.pokeAgmt.bind(this);
        this.initAgmt = this.initAgmt.bind(this);
        this.initWinsyncAgmt = this.initWinsyncAgmt.bind(this);
        this.confirmInit = this.confirmInit.bind(this);
        this.confirmWinsyncInit = this.confirmWinsyncInit.bind(this);
        this.closeInitConfirm = this.closeInitConfirm.bind(this);
        this.closeInitWinsyncConfirm = this.closeInitWinsyncConfirm.bind(this);
        this.pokeWinsyncAgmt = this.pokeWinsyncAgmt.bind(this);
        this.showAgmtModal = this.showAgmtModal.bind(this);
        this.showAgmtModalRemote = this.showAgmtModalRemote.bind(this);
        this.closeAgmtModal = this.closeAgmtModal.bind(this);
        this.showWinsyncAgmtModal = this.showWinsyncAgmtModal.bind(this);
        this.closeWinsyncAgmtModal = this.closeWinsyncAgmtModal.bind(this);
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

        this.addAliases = this.addAliases.bind(this);
        this.editAliases = this.editAliases.bind(this);
        this.removeAliases = this.removeAliases.bind(this);
        this.openAliasesModal = this.openAliasesModal.bind(this);
        this.showAddAliasesModal = this.showAddAliasesModal.bind(this);
        this.showEditAliasesModal = this.showEditAliasesModal.bind(this);
        this.closeAliasesModal = this.closeAliasesModal.bind(this);

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
        this.handleConvertChange = this.handleConvertChange.bind(this);

        this.confirmDeleteConflict = this.confirmDeleteConflict.bind(this);
        this.confirmConvertConflict = this.confirmConvertConflict.bind(this);
        this.confirmSwapConflict = this.confirmSwapConflict.bind(this);

        this.closeConfirmDeleteConflict = this.closeConfirmDeleteConflict.bind(this);
        this.closeConfirmConvertConflict = this.closeConfirmConvertConflict.bind(this);
        this.closeConfirmSwapConflict = this.closeConfirmSwapConflict.bind(this);
    }

    handleFieldChange(e) {
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

    convertConflict (dn) {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "convert", dn, "--new-rdn=" + this.state.convertRDN];
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
                        convertRDN: ""
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to convert conflict entry entry: ${dn} - ${errMsg.desc}`
                    );
                });
    }

    swapConflict (dn) {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "swap", dn];
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
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to swap in conflict entry: ${dn} - ${errMsg.desc}`
                    );
                });
    }

    deleteConflict (dn) {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "delete", dn];
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
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete conflict entry: ${dn} - ${errMsg.desc}`
                    );
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
            glueEntry: dn
        });
    }

    closeConfirmConvertGlue () {
        this.setState({
            showConfirmConvertGlue: false,
            glueEntry: ""
        });
    }

    convertGlue (dn) {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "convert-glue", dn];
        log_cmd("convertGlue", "Convert glue entry to normal entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication glue entry was converted`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to convert glue entry: ${dn} - ${errMsg.desc}`
                    );
                });
    }

    confirmDeleteGlue (dn) {
        this.setState({
            showConfirmDeleteGlue: true,
            glueEntry: dn
        });
    }

    deleteGlue (dn) {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "delete-glue", dn];
        log_cmd("deleteGlue", "Delete glue entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication glue entry was deleted`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete glue entry: ${dn} - ${errMsg.desc}`
                    );
                });
    }

    closeConfirmDeleteGlue () {
        this.setState({
            showConfirmDeleteGlue: false,
            glueEntry: ""
        });
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
            conflictEntry: dn
        });
    }

    closeConfirmConvertConflict () {
        this.setState({
            showConfirmConvertConflict: false,
            conflictEntry: ""
        });
    }

    confirmSwapConflict (dn) {
        this.setState({
            showConfirmSwapConflict: true,
            conflictEntry: dn
        });
    }

    closeConfirmSwapConflict () {
        this.setState({
            showConfirmSwapConflict: false,
            conflictEntry: ""
        });
    }

    confirmDeleteConflict (dn) {
        this.setState({
            showConfirmDeleteConflict: true,
            conflictEntry: dn
        });
    }

    closeConfirmDeleteConflict () {
        this.setState({
            showConfirmDeleteConflict: false,
            conflictEntry: ""
        });
    }

    closeCompareModal () {
        this.setState({
            showCompareModal: false
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

    pokeAgmt (name) {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-agmt", "poke", "--suffix=" + this.props.suffix, name];
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
                        `Failed to poke replication agreement ${name} - ${errMsg.desc}`
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

    showAgmtModal (name) {
        for (let agmt of this.props.data.replAgmts) {
            if (agmt['agmt-name'] == name) {
                this.setState({
                    showAgmtModal: true,
                    isRemoteAgmt: false,
                    agmt: agmt
                });
                break;
            }
        }
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
                                        isRemoteAgmt: true,
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

    showWinsyncAgmtModal(name) {
        for (let agmt of this.props.data.replWinsyncAgmts) {
            if (agmt['agmt-name'] == name) {
                this.setState({
                    showWinsyncAgmtModal: true,
                    agmt: agmt
                });
                break;
            }
        }
    }

    closeWinsyncAgmtModal() {
        this.setState({
            showWinsyncAgmtModal: false,
        });
    }

    confirmInit() {
        this.setState({
            showInitConfirm: true,
        });
    }

    closeInitConfirm() {
        this.setState({
            showInitConfirm: false
        });
    }

    confirmWinsyncInit() {
        this.setState({
            showInitWinsyncConfirm: true
        });
    }

    closeInitWinsyncConfirm() {
        this.setState({
            showInitWinsyncConfirm: false
        });
    }

    initAgmt() {
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'init', '--suffix=' + this.props.suffix, this.state.agmt['agmt-name'] ];
        log_cmd('initAgmt', 'Initialize agreement', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadAgmts();
                    this.props.addNotification(
                        "success",
                        `Replication agreement initialization has started ...`
                    );
                    this.setState({
                        showAgmtModal: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to start agreement initialization - ${errMsg.desc}`
                    );
                    this.setState({
                        showAgmtModal: false
                    });
                });
    }

    initWinsyncAgmt() {
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'init', '--suffix=' + this.props.suffix, this.state.agmt['agmt-name'] ];
        log_cmd('initWinsyncAgmt', 'Initialize winsync agreement', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadWinsyncAgmts();
                    this.props.addNotification(
                        "success",
                        `Replication winsync agreement initialization has started ...`
                    );
                    this.setState({
                        showInitWinsyncConfirm: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to start winsync agreement initialization - ${errMsg.desc}`
                    );
                    this.setState({
                        showInitWinsyncConfirm: false
                    });
                });
    }

    handleConvertChange(e) {
        const value = e.target.value;
        this.setState({
            convertRDN: value,
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

    removeCreds(rowData) {
        this.setState({
            credentialsList: this.state.credentialsList.filter(
                row => row.connData !== rowData.connData
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

    showEditCredsModal(rowData) {
        this.openCredsModal();
        this.setState({
            newEntry: false,
            oldCredsHostname: rowData.connData.split(':')[0],
            oldCredsPort: rowData.connData.split(':')[1],
            credsHostname: rowData.connData.split(':')[0],
            credsPort: rowData.connData.split(':')[1],
            credsBinddn: rowData.credsBinddn,
            credsBindpw: rowData.credsBindpw,
            pwInputInterractive: rowData.pwInputInterractive
        });
    }

    closeCredsModal() {
        this.setState({
            showCredentialsModal: false
        });
    }

    changeAlias(action) {
        const { aliasesList, aliasHostname, aliasPort, oldAliasName, aliasName } = this.state;

        if (aliasPort === "" || aliasHostname === "" || aliasName === "") {
            this.props.addNotification("warning", "Host, Port, and Alias are required.");
        } else {
            let aliasExists = false;
            if ((action == "add") && (aliasesList.some(row => row.alias === aliasName))) {
                aliasExists = true;
            }
            if ((action == "edit") && (aliasesList.some(row => row.alias === oldAliasName))) {
                this.setState({
                    aliasesList: aliasesList.filter(row => row.alias !== oldAliasName)
                });
            }

            if (!aliasExists) {
                this.setState(prevState => ({
                    aliasesList: [
                        ...prevState.aliasesList,
                        {
                            connData: `${aliasHostname}:${aliasPort}`,
                            alias: aliasName
                        }
                    ]
                }));
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

    removeAliases(rowData) {
        this.setState({
            aliasesList: this.state.aliasesList.filter(row => row.alias !== rowData.alias)
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

    showEditAliasesModal(rowData) {
        this.openAliasesModal();
        this.setState({
            newEntry: false,
            aliasHostname: rowData.connData.split(':')[0],
            aliasPort: parseInt(rowData.connData.split(':')[1]),
            oldAliasName: rowData.alias,
            aliasName: rowData.alias
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
            aliases.push(`${row.alias}=${row.connData}`);
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
                // Stream is run each time as a new character arriving
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
                fullReportProcess.input(`${loginBindpw}\n`, true);
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
        let compareConflictModal = "";

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
                    initAgmt={this.confirmInit}
                    isRemoteAgmt={this.state.isRemoteAgmt}
                />;
        }

        if (this.state.showWinsyncAgmtModal) {
            winsyncAgmtDetailModal =
                <WinsyncAgmtDetailsModal
                    showModal={this.state.showWinsyncAgmtModal}
                    closeHandler={this.closeWinsyncAgmtModal}
                    agmt={this.state.agmt}
                    initAgmt={this.confirmWinsyncInit}
                />;
        }
        if (this.state.showCompareModal) {
            compareConflictModal =
                <ConflictCompareModal
                    showModal
                    conflictEntry={this.state.cmpConflictEntry}
                    validEntry={this.state.cmpValidEntry}
                    swapFunc={this.confirmSwapConflict}
                    convertFunc={this.confirmConvertConflict}
                    deleteFunc={this.confirmDeleteConflict}
                    handleConvertChange={this.handleConvertChange}
                    closeHandler={this.closeCompareModal}
                />;
        }

        let reportContent =
            <div>
                <Nav bsClass="nav nav-tabs nav-tabs-pf">
                    <NavItem className="ds-nav-med" eventKey={1}>
                        {_("Prepare")}
                    </NavItem>
                    <NavItem className="ds-nav-med" eventKey={2}>
                        {_("Result")}
                    </NavItem>
                </Nav>
                <TabContent>
                    <TabPane eventKey={1}>
                        <div className="ds-indent ds-margin-top-lg">
                            <CustomCollapse textClosed="Show Help" textOpened="Hide Help" className="h3">
                                <h3>How To Use Replication Sync Report</h3>
                                <ol className="ds-left-indent">
                                    <li>
                                        Fill in <b>Replica Credentials</b>;
                                        <ul>
                                            <li>• Initially, the list is populated with existing instance agreements and the active instance itself;</li>
                                            <li>• You can use regular expressions for the <b>Connection Data</b> field;</li>
                                            <li>• It is advised to use an <b>Interactive Input</b> option for a password because it's more secure.</li>
                                        </ul>
                                    </li>
                                    <li>
                                        Add <b>Instance Aliases</b> if needed;
                                        <ul>
                                            <li>• Adding the aliases will make the report more readable;</li>
                                            <li>• Each instance can have one alias. For example, you can give names like this:
                                                <b> Alias</b>=Main Master, <b>Hostname</b>=192.168.122.01, <b>Port</b>=38901;</li>
                                            <li>• In a result, the report will have an entry like this:
                                                <b> Supplier: Main Master (192.168.122.01:38901)</b>.</li>
                                        </ul>
                                    </li>
                                    <li>
                                        Press <b>Generate Report</b> button;
                                        <ul>
                                            <li>• It will initiate the report creation;</li>
                                            <li>• You may be asked for the credentials while the process is running through the agreements.</li>
                                        </ul>
                                    </li>
                                    <li>
                                        Once report is generated you can review it and enable continuous refreshing.
                                        <ul>
                                            <li>• More consumer replication data is available under the 'View Data' button;</li>
                                            <li>• You can set the timeout and the new report will be created by that;</li>
                                            <li>• It will use the specified credentials (both preset and from interactive input).</li>
                                        </ul>
                                    </li>
                                </ol>
                            </CustomCollapse>
                            <ReportCredentialsTable
                                rows={credentialsList}
                                deleteConfig={this.removeCreds}
                                editConfig={this.showEditCredsModal}
                            />
                            <Button
                                className="ds-margin-top"
                                bsStyle="default"
                                onClick={this.showAddCredsModal}
                            >
                                Add Credentials
                            </Button>
                            <ReportAliasesTable
                                rows={aliasesList}
                                deleteConfig={this.removeAliases}
                                editConfig={this.showEditAliasesModal}
                            />
                            <Button
                                className="ds-margin-top"
                                bsStyle="default"
                                onClick={this.showAddAliasesModal}
                            >
                                Add Alias
                            </Button>
                            <p />
                            <Button
                                className="ds-margin-top"
                                bsStyle="primary"
                                onClick={this.doFullReport}
                                title="Use the specified credentials and display full topology report"
                            >
                                Generate Report
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
                            <p>
                                Replication conflict entries occur when two entries are created with the same
                                DN on different servers.  The automatic conflict resolution procedure renames
                                the last entry created to include the entry's unique identifier (nsuniqueid)
                                in the DN.  There are several ways to resolve a conflict, but that is up to
                                you on which option to use.
                            </p>
                            <ConflictTable
                                conflicts={conflictEntries}
                                resolveConflict={this.resolveConflict}
                                key={conflictEntries}
                            />
                        </div>
                    </TabPane>
                    <TabPane eventKey={2}>
                        <div className="ds-indent ds-margin-top-lg">
                            <p>
                                When a <b>Delete</b> operation is replicated and the consumer server finds that the entry to be
                                deleted has child entries, the conflict resolution procedure creates a "<i>glue entry</i>" to
                                avoid having orphaned entries in the database.  In the same way, when an <b>Add</b> operation is
                                replicated and the consumer server cannot find the parent entry, the conflict resolution
                                procedure creates a "<i>glue entry</i>" representing the parent so that the new entry is not an
                                orphan entry.
                            </p>
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

        let fullReportTitle = 'Sync Report';
        let replAgmtNavTitle = 'Agreements <font size="2">(' + replAgmts.length + ')</font>';
        let winsyncNavTitle = 'Winsync <font size="2">(' + replWinsyncAgmts.length + ')</font>';
        let tasksNavTitle = 'Tasks <font size="2">(' + (cleanTasks.length + abortTasks.length) + ')</font>';
        let conflictsNavTitle = 'Conflicts <font size="2">(' + (conflictEntries.length + glueEntries.length) + ')</font>';

        return (
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
                                        viewAgmt={this.showAgmtModal}
                                    />
                                </div>
                            </TabPane>
                            <TabPane eventKey={3}>
                                <div className="dds-indent ds-tab-table">
                                    <WinsyncAgmtTable
                                        agmts={replWinsyncAgmts}
                                        pokeAgmt={this.pokeWinsyncAgmt}
                                        viewAgmt={this.showWinsyncAgmtModal}
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
                <ConfirmPopup
                    showModal={this.state.showInitConfirm}
                    closeHandler={this.closeInitConfirm}
                    actionFunc={this.initAgmt}
                    actionParam={this.state.agmt['agmt-name']}
                    msg="Are you really sure you want to reinitialize this replication agreement?"
                    msgContent={this.state.agmt['agmt-name']}
                />
                <ConfirmPopup
                    showModal={this.state.showInitWinsyncConfirm}
                    closeHandler={this.closeInitWinsyncConfirm}
                    actionFunc={this.initWinsyncAgmt}
                    actionParam={this.state.agmt['agmt-name']}
                    msg="Are you really sure you want to reinitialize this replication winsync agreement?"
                    msgContent={this.state.agmt['agmt-name']}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmDeleteGlue}
                    closeHandler={this.closeConfirmDeleteGlue}
                    actionFunc={this.deleteGlue}
                    actionParam={this.state.glueEntry}
                    msg="Are you really sure you want to delete this glue entry and its child entries?"
                    msgContent={this.state.glueEntry}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmConvertGlue}
                    closeHandler={this.closeConfirmConvertGlue}
                    actionFunc={this.convertGlue}
                    actionParam={this.state.glueEntry}
                    msg="Are you really sure you want to convert this glue entry to a regular entry?"
                    msgContent={this.state.glueEntry}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmConvertConflict}
                    closeHandler={this.closeConfirmConvertConflict}
                    actionFunc={this.convertConflict}
                    actionParam={this.state.conflictEntry}
                    msg="Are you really sure you want to convert this conflict entry to a regular entry?"
                    msgContent={this.state.conflictEntry}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmSwapConflict}
                    closeHandler={this.closeConfirmSwapConflict}
                    actionFunc={this.swapConflict}
                    actionParam={this.state.conflictEntry}
                    msg="Are you really sure you want to swap this conflict entry with the valid entry (this would remove the valid entry and any child entries it might have)?"
                    msgContent={this.state.conflictEntry}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmDeleteConflict}
                    closeHandler={this.closeConfirmDeleteConflict}
                    actionFunc={this.deleteConflict}
                    actionParam={this.state.conflictEntry}
                    msg="Are you really sure you want to delete this conflict entry?"
                    msgContent={this.state.conflictEntry}
                />
                {fullReportModal}
                {reportLoginModal}
                {reportCredentialsModal}
                {reportAliasesModal}
                {agmtDetailModal}
                {winsyncAgmtDetailModal}
                {compareConflictModal}
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
    reloadAgmts: PropTypes.func,
    reloadWinsyncAgmts: PropTypes.func,
    reloadConflicts: PropTypes.func,
    enableTree: PropTypes.func,
};

ReplMonitor.defaultProps = {
    data: {},
    suffix: "",
    serverId: "",
    addNotification: noop,
    reloadAgmts: noop,
    reloadWinsyncAgmts: noop,
    reloadConflicts: noop,
    enableTree: noop,
};

export default ReplMonitor;
