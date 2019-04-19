import React from "react";
import cockpit from "cockpit";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import "../../css/ds.css";
import { ConfirmPopup } from "../notifications.jsx";
import {
    AgmtTable,
    WinsyncAgmtTable,
    CleanALLRUVTable,
    AbortCleanALLRUVTable,
    ConflictTable,
    GlueTable,
} from "./monitorTables.jsx";
import {
    TaskLogModal,
    AgmtDetailsModal,
    WinsyncAgmtDetailsModal,
    ReplLagReportModal,
    ReplLoginModal,
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

export class ReplMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeKey: 1,
            logData: "",
            showBindModal: false,
            showLogModal: false,
            showAgmtModal: false,
            showWinsyncAgmtModal: false,
            showInitWinsyncConfirm: false,
            showInitConfirm: false,
            showLoginModal: false,
            showLagReport: false,
            showCompareModal: false,
            showConfirmDeleteGlue: false,
            showConfirmConvertGlue: false,
            showConfirmSwapConflict: false,
            showConfirmConvertConflict: false,
            showConfirmDeleteConflict: false,
            reportLoading: false,
            lagAgmts: [],
            agmt: "",
            convertRDN: "",
            glueEntry: "",
            conflictEntry: "",
            binddn: "cn=Directory Manager",
            bindpw: "",
            errObj: {}
        };

        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.pokeAgmt = this.pokeAgmt.bind(this);
        this.initAgmt = this.initAgmt.bind(this);
        this.initWinsyncAgmt = this.initWinsyncAgmt.bind(this);
        this.confirmInit = this.confirmInit.bind(this);
        this.confirmWinsyncInit = this.confirmWinsyncInit.bind(this);
        this.closeInitConfirm = this.closeInitConfirm.bind(this);
        this.closeInitWinsyncConfirm = this.closeInitWinsyncConfirm.bind(this);
        this.pokeWinsyncAgmt = this.pokeWinsyncAgmt.bind(this);
        this.showAgmtModal = this.showAgmtModal.bind(this);
        this.closeAgmtModal = this.closeAgmtModal.bind(this);
        this.showWinsyncAgmtModal = this.showWinsyncAgmtModal.bind(this);
        this.closeWinsyncAgmtModal = this.closeWinsyncAgmtModal.bind(this);
        this.getLagReportCreds = this.getLagReportCreds.bind(this);
        this.doLagReport = this.doLagReport.bind(this);
        this.closeLagReport = this.closeLagReport.bind(this);
        this.viewCleanLog = this.viewCleanLog.bind(this);
        this.viewAbortLog = this.viewAbortLog.bind(this);
        this.closeLogModal = this.closeLogModal.bind(this);
        this.handleLoginModal = this.handleLoginModal.bind(this);
        this.closeLoginModal = this.closeLoginModal.bind(this);
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
                    agmt: agmt
                });
                break;
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

    getLagReportCreds () {
        if (this.props.data.replAgmts.length == 0) {
            // No agreements, don't proceed...
            this.props.addNotification(
                "error", "There are no replication agreements to report on"
            );
        } else {
            this.setState({
                showLoginModal: true,
                errObj: {
                    bindpw: true
                }
            });
        }
    }

    closeLoginModal () {
        this.setState({
            showLoginModal: false,
        });
    }

    handleLoginModal(e) {
        const value = e.target.value.trim();
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

    closeLagReport() {
        this.setState({
            showLagReport: false
        });
    }

    doLagReport() {
        // Get agmts but this time with with bind credentials, then clear
        // out bind credentials after we use them

        if (this.state.binddn == "" || this.state.bindpw == "") {
            return;
        }

        this.setState({
            loginSpinning: true,
        });

        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "status", "--suffix=" + this.props.suffix,
            "--bind-dn=" + this.state.binddn, "--bind-passwd=" + this.state.bindpw];
        log_cmd("doLagReport", "Get agmts for lag report", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        lagAgmts: config.items,
                        showLagReport: true,
                        showLoginModal: false,
                        loginSpinning: false,
                        bindpw: ""
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to get replication status - ${errMsg.desc}`
                    );
                    this.setState({
                        showLoginModal: false,
                        loginSpinning: false,
                        bindpw: ""
                    });
                });
    }

    handleConvertChange(e) {
        const value = e.target.value;
        this.setState({
            convertRDN: value,
        });
    }

    render() {
        let replAgmts = this.props.data.replAgmts;
        let replWinsyncAgmts = this.props.data.replWinsyncAgmts;
        let cleanTasks = this.props.data.cleanTasks;
        let abortTasks = this.props.data.abortTasks;
        let conflictEntries = this.props.data.conflicts;
        let glueEntries = this.props.data.glues;
        let agmtDetailModal = "";
        let winsyncAgmtDetailModal = "";
        let compareConflictModal = "";

        if (this.state.showAgmtModal) {
            agmtDetailModal =
                <AgmtDetailsModal
                    showModal={this.state.showAgmtModal}
                    closeHandler={this.closeAgmtModal}
                    agmt={this.state.agmt}
                    initAgmt={this.confirmInit}
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

        let cleanNavTitle = 'CleanAllRUV Tasks <font size="1">(' + cleanTasks.length + ')</font>';
        let abortNavTitle = 'Abort CleanAllRUV Tasks <font size="1">(' + abortTasks.length + ')</font>';
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

        let conflictNavTitle = 'Conflict Entries <font size="1">(' + conflictEntries.length + ')</font>';
        let glueNavTitle = 'Glue Entries <font size="1">(' + glueEntries.length + ')</font>';
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

        let replAgmtNavTitle = 'Replication Agreements <font size="1">(' + replAgmts.length + ')</font>';
        let winsyncNavTitle = 'Winsync Agreements <font size="1">(' + replWinsyncAgmts.length + ')</font>';
        let tasksNavTitle = 'Tasks <font size="1">(' + (cleanTasks.length + abortTasks.length) + ')</font>';
        let conflictsNavTitle = 'Conflicts <font size="1">(' + (conflictEntries.length + glueEntries.length) + ')</font>';

        return (
            <div id="monitor-suffix-page" className="container-fluid ds-tab-table">
                <TabContainer id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div>
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: replAgmtNavTitle}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: winsyncNavTitle}} />
                            </NavItem>
                            <NavItem eventKey={3}>
                                <div dangerouslySetInnerHTML={{__html: tasksNavTitle}} />
                            </NavItem>
                            <NavItem eventKey={4}>
                                <div dangerouslySetInnerHTML={{__html: conflictsNavTitle}} />
                            </NavItem>
                        </Nav>
                        <TabContent>
                            <TabPane eventKey={1}>
                                <div className="ds-indent ds-tab-table">
                                    <AgmtTable
                                        agmts={replAgmts}
                                        pokeAgmt={this.pokeAgmt}
                                        viewAgmt={this.showAgmtModal}
                                    />
                                    <p />
                                    <Button
                                        bsStyle="primary"
                                        onClick={this.getLagReportCreds}
                                        title="Display report that shows the lag time and replication status of each agreement in relationship to its replica"
                                    >
                                        Get Lag Report
                                    </Button>
                                </div>
                            </TabPane>
                            <TabPane eventKey={2}>
                                <div className="dds-indent ds-tab-table">
                                    <WinsyncAgmtTable
                                        agmts={replWinsyncAgmts}
                                        pokeAgmt={this.pokeWinsyncAgmt}
                                        viewAgmt={this.showWinsyncAgmtModal}
                                    />
                                </div>
                            </TabPane>
                            <TabPane eventKey={3}>
                                <div className="ds-indent ds-tab-table">
                                    <TabContainer id="task-tabs" defaultActiveKey={1}>
                                        {taskContent}
                                    </TabContainer>
                                </div>
                            </TabPane>
                            <TabPane eventKey={4}>
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
                <ReplLoginModal
                    showModal={this.state.showLoginModal}
                    closeHandler={this.closeLoginModal}
                    handleChange={this.handleLoginModal}
                    doReport={this.doLagReport}
                    spinning={this.state.loginSpinning}
                    error={this.state.errObj}
                />
                <ReplLagReportModal
                    showModal={this.state.showLagReport}
                    closeHandler={this.closeLagReport}
                    agmts={this.state.lagAgmts}
                    pokeAgmt={this.pokeAgmt}
                    viewAgmt={this.showAgmtModal}
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
};

ReplMonitor.defaultProps = {
    data: {},
    suffix: "",
    serverId: "",
    addNotification: noop,
    reloadAgmts: noop,
    reloadWinsyncAgmts: noop,
    reloadConflicts: noop,
};

export default ReplMonitor;
