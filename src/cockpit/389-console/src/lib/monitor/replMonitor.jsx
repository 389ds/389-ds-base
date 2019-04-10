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
    AbortCleanALLRUVTable
} from "./monitorTables.jsx";
import {
    TaskLogModal,
    AgmtDetailsModal,
    WinsyncAgmtDetailsModal,
    ReplLagReportModal,
    ReplLoginModal,
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
            reportLoading: false,
            lagAgmts: [],
            agmt: "",
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

    render() {
        let replAgmts = this.props.data.replAgmts;
        let replWinsyncAgmts = this.props.data.replWinsyncAgmts;
        let cleanTasks = this.props.data.cleanTasks;
        let abortTasks = this.props.data.abortTasks;
        let agmtDetailModal = "";
        let winsyncAgmtDetailModal = "";

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

        let AgmtNavTitle = 'Agreements <font size="1">(' + replAgmts.length + ')</font>';
        let WinsyncAgmtNavTitle = 'Winsync Agreements <font size="1">(' + replWinsyncAgmts.length + ')</font>';
        let TasksNavTitle = 'Tasks <font size="1">(' + (cleanTasks.length + abortTasks.length) + ')</font>';

        return (
            <div id="monitor-suffix-page" className="container-fluid ds-tab-table">
                <TabContainer id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div>
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: AgmtNavTitle}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: WinsyncAgmtNavTitle}} />
                            </NavItem>
                            <NavItem eventKey={3}>
                                <div dangerouslySetInnerHTML={{__html: TasksNavTitle}} />
                            </NavItem>
                        </Nav>
                        <TabContent>
                            <TabPane eventKey={1}>
                                <div className="ds-margin-top-lg">
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
                                <div className="ds-margin-top-lg">
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
                {agmtDetailModal}
                {winsyncAgmtDetailModal}
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
};

ReplMonitor.defaultProps = {
    data: {},
    suffix: "",
    serverId: "",
    addNotification: noop,
    reloadAgmts: noop,
    reloadWinsyncAgmts: noop,
};

export default ReplMonitor;
