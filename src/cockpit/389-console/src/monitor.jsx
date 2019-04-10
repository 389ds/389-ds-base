import cockpit from "cockpit";
import React from "react";
import { NotificationController } from "./lib/notifications.jsx";
import { log_cmd } from "./lib/tools.jsx";
import {
    TreeView,
    Spinner,
    Row,
    Col,
    Icon,
    ControlLabel
} from "patternfly-react";
import PropTypes from "prop-types";
import SNMPMonitor from "./lib/monitor/snmpMonitor.jsx";
import ServerMonitor from "./lib/monitor/serverMonitor.jsx";
import DatabaseMonitor from "./lib/monitor/dbMonitor.jsx";
import SuffixMonitor from "./lib/monitor/suffixMonitor.jsx";
import ChainingMonitor from "./lib/monitor/chainingMonitor.jsx";
import AccessLogMonitor from "./lib/monitor/accesslog.jsx";
import AuditLogMonitor from "./lib/monitor/auditlog.jsx";
import AuditFailLogMonitor from "./lib/monitor/auditfaillog.jsx";
import ErrorLogMonitor from "./lib/monitor/errorlog.jsx";
import ReplMonitor from "./lib/monitor/replMonitor.jsx";
import "./css/ds.css";

const treeViewContainerStyles = {
    width: '295px',
};

export class Monitor extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            nodes: [],
            node_name: "",
            node_text: "",
            node_type: "",
            loaded: false,
            snmpData: {},
            ldbmData: {},
            serverData: {},
            showLoading: false,
            loadingMsg: "",
            notifications: [],
            // Suffix
            suffixLoading: false,
            serverLoading: false,
            ldbmLoading: false,
            snmpLoading: false,
            chainingLoading: false,
            // replication
            replLoading: false,
            replInitLoaded: false,
            replSuffix: "",
            replRole: "",
            replRid: "",
            replicatedSuffixes: [],
            // Access log
            accesslogLocation: "",
            accesslogData: "",
            accessReloading: false,
            accesslog_cont_refresh: "",
            accessRefreshing: false,
            accessLines: "50",
            // Audit log
            auditlogLocation: "",
            auditlogData: "",
            auditReloading: false,
            auditlog_cont_refresh: "",
            auditRefreshing: false,
            auditLines: "50",
            // Audit Fail log
            auditfaillogLocation: "",
            auditfaillogData: "",
            auditfailReloading: false,
            auditfaillog_cont_refresh: "",
            auditfailRefreshing: false,
            auditfailLines: "50",
            // Error log
            errorlogLocation: "",
            errorlogData: "",
            errorReloading: false,
            errorlog_cont_refresh: "",
            errorRefreshing: false,
            errorSevLevel: "Everything",
            errorLines: "50",
        };

        // Build the log severity sev_levels
        let sev_emerg = " - EMERG - ";
        let sev_crit = " - CRIT - ";
        let sev_alert = " - ALERT - ";
        let sev_err = " - ERR - ";
        let sev_warn = " - WARN - ";
        let sev_notice = " - NOTICE - ";
        let sev_info = " - INFO - ";
        let sev_debug = " - DEBUG - ";
        this.sev_levels = {
            "Emergency": sev_emerg,
            "Critical": sev_crit,
            "Alert": sev_alert,
            "Error": sev_err,
            "Warning": sev_warn,
            "Notice": sev_notice,
            "Info": sev_info,
            "Debug": sev_debug
        };
        this.sev_all_errs = [sev_emerg, sev_crit, sev_alert, sev_err];
        this.sev_all_info = [sev_warn, sev_notice, sev_info, sev_debug];

        // Bindings
        this.addNotification = this.addNotification.bind(this);
        this.removeNotification = this.removeNotification.bind(this);
        this.loadSuffixTree = this.loadSuffixTree.bind(this);
        this.update_tree_nodes = this.update_tree_nodes.bind(this);
        this.selectNode = this.selectNode.bind(this);
        this.loadMonitorSuffix = this.loadMonitorSuffix.bind(this);
        this.loadMonitorLDBM = this.loadMonitorLDBM.bind(this);
        this.reloadLDBM = this.reloadLDBM.bind(this);
        this.loadMonitorSNMP = this.loadMonitorSNMP.bind(this);
        this.reloadSNMP = this.reloadSNMP.bind(this);
        this.loadMonitorServer = this.loadMonitorServer.bind(this);
        this.reloadServer = this.reloadServer.bind(this);
        this.loadMonitorChaining = this.loadMonitorChaining.bind(this);
        // Replication
        this.loadMonitorReplication = this.loadMonitorReplication.bind(this);
        this.loadCleanTasks = this.loadCleanTasks.bind(this);
        this.loadAbortTasks = this.loadAbortTasks.bind(this);
        this.loadReplicatedSuffixes = this.loadReplicatedSuffixes.bind(this);
        this.loadWinsyncAgmts = this.loadWinsyncAgmts.bind(this);
        this.replSuffixChange = this.replSuffixChange.bind(this);
        this.reloadReplAgmts = this.reloadReplAgmts.bind(this);
        this.reloadReplWinsyncAgmts = this.reloadReplWinsyncAgmts.bind(this);
        // Logging
        this.loadMonitor = this.loadMonitor.bind(this);
        this.refreshAccessLog = this.refreshAccessLog.bind(this);
        this.refreshAuditLog = this.refreshAuditLog.bind(this);
        this.refreshAuditFailLog = this.refreshAuditFailLog.bind(this);
        this.refreshErrorLog = this.refreshErrorLog.bind(this);
        this.handleAccessChange = this.handleAccessChange.bind(this);
        this.handleAuditChange = this.handleAuditChange.bind(this);
        this.handleAuditFailChange = this.handleAuditFailChange.bind(this);
        this.handleErrorChange = this.handleErrorChange.bind(this);
        this.accessRefreshCont = this.accessRefreshCont.bind(this);
        this.auditRefreshCont = this.auditRefreshCont.bind(this);
        this.auditFailRefreshCont = this.auditFailRefreshCont.bind(this);
        this.errorRefreshCont = this.errorRefreshCont.bind(this);
        this.handleSevChange = this.handleSevChange.bind(this);
    }

    componentDidMount() {
        this.loadMonitor();
    }

    componentDidUpdate(prevProps) {
        if (this.props.serverId !== prevProps.serverId) {
            this.loadSuffixTree(false);
        }
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
                    message: message,
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

    loadSuffixTree(fullReset) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "get-tree",
        ];
        log_cmd("getTree", "Start building the suffix tree", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let treeData = JSON.parse(content);
                    let basicData = [
                        {
                            text: "Database",
                            selectable: true,
                            selected: true,
                            icon: "fa fa-database",
                            state: {"expanded": true},
                            id: "database-monitor",
                            type: "database",
                            nodes: []
                        },
                        {
                            text: "Logging",
                            icon: "pficon-catalog",
                            selectable: false,
                            id: "log-monitor",
                            nodes: [
                                {
                                    text: "Access Log",
                                    icon: "glyphicon glyphicon-book",
                                    selectable: true,
                                    id: "access-log-monitor",
                                    type: "log",
                                },
                                {
                                    text: "Audit Log",
                                    icon: "glyphicon glyphicon-book",
                                    selectable: true,
                                    id: "audit-log-monitor",
                                    type: "log",
                                },
                                {
                                    text: "Audit Failure Log",
                                    icon: "glyphicon glyphicon-book",
                                    selectable: true,
                                    id: "auditfail-log-monitor",
                                    type: "log",
                                },
                                {
                                    text: "Errors Log",
                                    icon: "glyphicon glyphicon-book",
                                    selectable: true,
                                    id: "error-log-monitor",
                                    type: "log",
                                },
                            ]
                        },
                        {
                            text: "Replication",
                            selectable: true,
                            icon: "pficon-topology",
                            id: "replication-monitor",
                            type: "replication",
                        },
                        {
                            text: "Server Statistics",
                            icon: "pficon-server",
                            selectable: true,
                            id: "server-monitor",
                            type: "server",
                        },
                        {
                            text: "SNMP Counters",
                            icon: "glyphicon glyphicon-list-alt",
                            selectable: true,
                            id: "snmp-monitor",
                            type: "snmp",
                        },

                    ];
                    let current_node = this.state.node_name;
                    let type = this.state.node_type;
                    if (fullReset) {
                        current_node = "database-monitor";
                        type = "database";
                    }
                    basicData[0].nodes = treeData;
                    this.setState(() => ({
                        nodes: basicData,
                        node_name: current_node,
                        node_type: type,
                    }), this.update_tree_nodes);
                });
    }

    selectNode(selectedNode) {
        this.setState({
            showLoading: true
        });

        if (selectedNode.id == "database-monitor" ||
            selectedNode.id == "server-monitor" ||
            selectedNode.id == "snmp-monitor") {
            // Nothing special to do, these configurations have already been loaded
            this.setState(prevState => {
                return {
                    nodes: this.nodeSelector(prevState.nodes, selectedNode),
                    node_name: selectedNode.id,
                    node_text: selectedNode.text,
                    bename: "",
                };
            });
        } else if (selectedNode.id == "access-log-monitor") {
            this.refreshAccessLog();
            this.setState(prevState => {
                return {
                    nodes: this.nodeSelector(prevState.nodes, selectedNode),
                    node_name: selectedNode.id,
                    node_text: selectedNode.text,
                };
            });
        } else if (selectedNode.id == "audit-log-monitor") {
            this.refreshAuditLog();
            this.setState(prevState => {
                return {
                    nodes: this.nodeSelector(prevState.nodes, selectedNode),
                    node_name: selectedNode.id,
                    node_text: selectedNode.text,
                };
            });
        } else if (selectedNode.id == "auditfail-log-monitor") {
            this.refreshAuditFailLog();
            this.setState(prevState => {
                return {
                    nodes: this.nodeSelector(prevState.nodes, selectedNode),
                    node_name: selectedNode.id,
                    node_text: selectedNode.text,
                };
            });
        } else if (selectedNode.id == "error-log-monitor") {
            this.refreshErrorLog();
            this.setState(prevState => {
                return {
                    nodes: this.nodeSelector(prevState.nodes, selectedNode),
                    node_name: selectedNode.id,
                    node_text: selectedNode.text,
                };
            });
        } else if (selectedNode.id == "replication-monitor") {
            if (!this.state.replInitLoaded) {
                this.loadMonitorReplication();
            }
            this.setState(prevState => {
                return {
                    nodes: this.nodeSelector(prevState.nodes, selectedNode),
                    node_name: selectedNode.id,
                    node_text: selectedNode.text,
                };
            });
        } else {
            if (selectedNode.id in this.state) {
                // This suffix is already cached, but it might be incomplete...
                if (selectedNode.type == "dblink" && this.state.nsaddcount === undefined) {
                    this.loadMonitorChaining(selectedNode.id);
                } else if (selectedNode.type != "dblink" && this.state.entrycachehitratio === undefined) {
                    this.loadMonitorSuffix(selectedNode.id);
                }
                this.setState(prevState => {
                    return {
                        nodes: this.nodeSelector(prevState.nodes, selectedNode),
                        node_name: selectedNode.id,
                        node_text: selectedNode.text,
                        node_type: selectedNode.type,
                        bename: selectedNode.be,
                    };
                });
            } else {
                // Load this suffix (db, chaining & replication)
                if (selectedNode.type == "dblink") {
                    // Chaining
                    this.loadMonitorChaining(selectedNode.id);
                } else {
                    // Suffix
                    this.loadMonitorSuffix(selectedNode.id);
                }
                this.setState(prevState => {
                    return {
                        nodes: this.nodeSelector(prevState.nodes, selectedNode),
                        node_name: selectedNode.id,
                        node_text: selectedNode.text,
                        node_type: selectedNode.type,
                        bename: selectedNode.be,
                    };
                });
            }
        }
    }

    nodeSelector(nodes, targetNode) {
        return nodes.map(node => {
            if (node.nodes) {
                return {
                    ...node,
                    nodes: this.nodeSelector(node.nodes, targetNode),
                    selected: node.id === targetNode.id ? !node.selected : false
                };
            } else if (node.id === targetNode.id) {
                return { ...node, selected: !node.selected };
            } else if (node.id !== targetNode.id && node.selected) {
                return { ...node, selected: false };
            } else {
                return node;
            }
        });
    }

    update_tree_nodes() {
        // Set title to the text value of each suffix node.  We need to do this
        // so we can read long suffixes in the UI tree div
        let elements = document.getElementsByClassName('treeitem-row');
        for (let el of elements) {
            el.setAttribute('title', el.innerText);
        }
        this.setState({
            loaded: true
        });
    }

    loadMonitor() {
        // Load the following componets in a chained fashion:
        //  - log file locations
        //  - LDBM
        //  - Server stats
        //  - SNMP
        //  - Finally load the "tree"
        //
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get", "nsslapd-auditlog", "nsslapd-accesslog", "nsslapd-errorlog", "nsslapd-auditfaillog"
        ];
        log_cmd("loadLogLocations", "Get log locations", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        accesslogLocation: config.attrs['nsslapd-accesslog'][0],
                        auditlogLocation: config.attrs['nsslapd-auditlog'][0],
                        auditfaillogLocation: config.attrs['nsslapd-auditfaillog'][0],
                        errorlogLocation: config.attrs['nsslapd-errorlog'][0],
                    });
                }, this.loadReplicatedSuffixes());
    }

    loadReplicatedSuffixes() {
        // Load replicated suffix to populate the dropdown select list
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "list"
        ];
        log_cmd("loadReplicatedSuffixes", "Load replication suffixes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let replSuffix = "";
                    if (config.items.length > 0) {
                        replSuffix = config.items[0];
                    }
                    this.setState({
                        replicatedSuffixes: config.items,
                        replSuffix: replSuffix,
                    });
                }, this.loadMonitorLDBM());
    }

    loadMonitorLDBM() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "ldbm"
        ];
        log_cmd("loadMonitorLDBM", "Load database monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        ldbmData: config.attrs
                    });
                }, this.loadMonitorServer());
    }

    reloadLDBM() {
        this.setState({
            ldbmLoading: true
        });
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "ldbm"
        ];
        log_cmd("reloadLDBM", "Load database monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        ldbmLoading: false,
                        ldbmData: config.attrs
                    });
                });
    }

    loadMonitorServer() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "server"
        ];
        log_cmd("loadMonitorServer", "Load server monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        serverData: config.attrs
                    });
                }, this.loadMonitorSNMP());
    }

    reloadServer() {
        this.setState({
            serverLoading: true
        });
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "server"
        ];
        log_cmd("reloadServer", "Load server monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        serverLoading: false,
                        serverData: config.attrs
                    });
                });
    }

    loadMonitorSNMP() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "snmp"
        ];
        log_cmd("loadMonitorSNMP", "Load snmp monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        snmpData: config.attrs,
                    }, this.loadSuffixTree(true));
                });
    }

    reloadSNMP() {
        this.setState({
            snmpLoading: true
        });
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "snmp"
        ];
        log_cmd("reloadSNMP", "Load snmp monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        snmpLoading: false,
                        snmpData: config.attrs,
                    });
                });
    }

    loadMonitorChaining(suffix) {
        this.setState({
            chainingLoading: true
        });

        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "chaining", suffix
        ];
        log_cmd("loadMonitorChaining", "Load suffix monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            chainingData: config.attrs,
                        },
                        chainingLoading: false,
                    });
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        chainingLoading: false,
                    });
                });
    }

    loadMonitorSuffix(suffix) {
        this.setState({
            suffixLoading: true
        });

        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "backend", suffix
        ];
        log_cmd("loadMonitorSuffix", "Load suffix monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            suffixData: config.attrs,
                        },
                        suffixLoading: false,
                    });
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        suffixLoading: false,
                    });
                });
    }

    loadCleanTasks() {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-tasks", "list-cleanruv-tasks", "--suffix=" + this.state.replSuffix];
        log_cmd("loadCleanTasks", "Load clean tasks", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        [this.state.replSuffix]: {
                            ...this.state[this.state.replSuffix],
                            cleanTasks: config.items,
                        },
                    }, this.loadAbortTasks());
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadAbortTasks() {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-tasks", "list-abortruv-tasks", "--suffix=" + this.state.replSuffix];
        log_cmd("loadAbortCleanTasks", "Load abort tasks", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        [this.state.replSuffix]: {
                            ...this.state[this.state.replSuffix],
                            abortTasks: config.items,
                        },
                    }, this.setState(
                        {
                            replLoading: false,
                            replInitLoaded: true
                        }));
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadWinsyncAgmts() {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "winsync-status", "--suffix=" + this.state.replSuffix];
        log_cmd("loadWinsyncAgmts", "Load winsync agmt status", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        [this.state.replSuffix]: {
                            ...this.state[this.state.replSuffix],
                            replWinsyncAgmts: config.items,
                        },
                    }, this.loadCleanTasks());
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadMonitorReplication() {
        let replSuffix = this.state.replSuffix;
        if (replSuffix != "") {
            this.setState({
                replLoading: true
            });

            // Now load the agmts
            let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "replication", "status", "--suffix=" + replSuffix];
            log_cmd("loadMonitorReplication", "Load replication agmts", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        let config = JSON.parse(content);
                        this.setState({
                            [replSuffix]: {
                                ...this.state[replSuffix],
                                replAgmts: config.items,
                                abortTasks: [],
                                cleanTasks: [],
                                replWinsyncAgmts: [],
                            },
                        }, this.loadWinsyncAgmts());
                    })
                    .fail(() => {
                        // Notification of failure (could only be server down)
                        this.setState({
                            replLoading: false,
                        });
                    });
        }
    }

    reloadReplAgmts() {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "status", "--suffix=" + this.state.replSuffix];
        log_cmd("reloadReplAgmts", "Load replication agmts", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        [this.state.replSuffix]: {
                            ...this.state[this.state.replSuffix],
                            replAgmts: config.items,
                        },
                    });
                });
    }

    reloadReplWinsyncAgmts() {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "winsync-status", "--suffix=" + this.state.replSuffix];
        log_cmd("reloadReplWinsyncAgmts", "Load winysnc agmts", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        [this.state.replSuffix]: {
                            ...this.state[this.state.replSuffix],
                            replWinsyncAgmts: config.items,
                        },
                    });
                });
    }

    refreshAccessLog () {
        this.setState({
            accessReloading: true
        });
        let cmd = ["tail", "-" + this.state.accessLines, this.state.accesslogLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        accesslogData: content,
                        accessReloading: false
                    }));
                });
    }

    refreshAuditLog () {
        this.setState({
            auditReloading: true
        });
        let cmd = ["tail", "-" + this.state.auditLines, this.state.auditlogLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        auditlogData: content,
                        auditReloading: false
                    }));
                });
    }

    refreshAuditFailLog () {
        this.setState({
            auditfailReloading: true
        });
        let cmd = ["tail", "-" + this.state.auditfailLines, this.state.auditfaillogLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        auditfaillogData: content,
                        auditfailReloading: false
                    }));
                });
    }

    refreshErrorLog () {
        this.setState({
            errorReloading: true
        });

        let cmd = ["tail", "-" + this.state.errorLines, this.state.errorlogLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(data => {
                    if (this.state.errorSevLevel != "Everything") {
                        // Filter Data
                        let lines = data.split('\n');
                        let new_data = "";
                        for (let i = 0; i < lines.length; i++) {
                            let line = "";
                            if (this.state.errorSevLevel == "Error Messages") {
                                for (let lev of this.sev_all_errs) {
                                    if (lines[i].indexOf(lev) != -1) {
                                        line = lines[i] + "\n";
                                    }
                                }
                            } else if (this.state.errorSevLevel == "Info Messages") {
                                for (let lev of this.sev_all_info) {
                                    if (lines[i].indexOf(lev) != -1) {
                                        line = lines[i] + "\n";
                                    }
                                }
                            } else if (lines[i].indexOf(this.sev_levels[this.state.errorSevLevel]) != -1) {
                                line = lines[i] + "\n";
                            }
                            // Add the filtered line to new data
                            new_data += line;
                        }
                        data = new_data;
                    }

                    this.setState(() => ({
                        errorlogData: data,
                        errorReloading: false
                    }));
                });
    }

    accessRefreshCont(e) {
        if (e.target.checked) {
            this.state.accesslog_cont_refresh = setInterval(this.refreshAccessLog, 2000);
        } else {
            clearInterval(this.state.accesslog_cont_refresh);
        }
        this.setState({
            accessRefreshing: e.target.checked,
        });
    }

    auditRefreshCont(e) {
        if (e.target.checked) {
            this.state.auditlog_cont_refresh = setInterval(this.refreshAuditLog, 2000);
        } else {
            clearInterval(this.state.auditlog_cont_refresh);
        }
        this.setState({
            auditRefreshing: e.target.checked,
        });
    }

    auditFailRefreshCont(e) {
        if (e.target.checked) {
            this.state.auditfaillog_cont_refresh = setInterval(this.refreshAuditFailLog, 2000);
        } else {
            clearInterval(this.state.auditfaillog_cont_refresh);
        }
        this.setState({
            auditfailRefreshing: e.target.checked,
        });
    }

    errorRefreshCont(e) {
        if (e.target.checked) {
            this.state.errorlog_cont_refresh = setInterval(this.refreshErrorLog, 2000);
        } else {
            clearInterval(this.state.errorlog_cont_refresh);
        }
        this.setState({
            errorRefreshing: e.target.checked,
        });
    }

    handleAccessChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                accessLines: value
            }
        ), this.refreshAccessLog);
    }

    handleAuditChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                auditLines: value
            }
        ), this.refreshAuditLog);
    }

    handleAuditFailChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                auditfailLines: value
            }
        ), this.refreshAuditFailLog);
    }

    handleErrorChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                errorLines: value
            }
        ), this.refreshErrorLog);
    }

    handleSevChange(e) {
        const value = e.target.value;

        this.setState({
            errorSevLevel: value,
        }, this.refreshErrorLog);
    }

    replSuffixChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                replSuffix: value,
                replLoading: true
            }
        ), this.loadMonitorReplication);
    }

    render() {
        const { nodes } = this.state;
        let monitorPage = "";
        let monitor_element = "";

        if (this.state.loaded) {
            if (this.state.node_name == "database-monitor" || this.state.node_name == "") {
                if (this.state.ldbmLoading) {
                    monitor_element =
                        <div className="ds-loading-spinner ds-center">
                            <p />
                            <h4><b>Loading database monitor information ...</b></h4>
                            <Spinner loading size="md" />
                        </div>;
                } else {
                    monitor_element =
                        <DatabaseMonitor
                            data={this.state.ldbmData}
                            reload={this.reloadLDBM}
                        />;
                }
            } else if (this.state.node_name == "server-monitor") {
                if (this.state.serverLoading) {
                    monitor_element =
                        <div className="ds-loading-spinner ds-center">
                            <p />
                            <h4><b>Loading server monitor information ...</b></h4>
                            <Spinner loading size="md" />
                        </div>;
                } else {
                    monitor_element =
                        <ServerMonitor
                            data={this.state.serverData}
                            reload={this.reloadServer}
                            serverId={this.props.serverId}
                        />;
                }
            } else if (this.state.node_name == "snmp-monitor") {
                if (this.state.snmpLoading) {
                    monitor_element =
                        <div className="ds-loading-spinner ds-center">
                            <p />
                            <h4><b>Loading SNMP monitor information ...</b></h4>
                            <Spinner loading size="md" />
                        </div>;
                } else {
                    monitor_element =
                        <SNMPMonitor
                            data={this.state.snmpData}
                            reload={this.reloadSNMP}
                        />;
                }
            } else if (this.state.node_name == "access-log-monitor") {
                monitor_element =
                    <AccessLogMonitor
                        data={this.state.accesslogData}
                        handleChange={this.handleAccessChange}
                        reload={this.refreshAccessLog}
                        reloading={this.state.accessReloading}
                        refreshing={this.state.accessRefreshing}
                        handleRefresh={this.accessRefreshCont}
                        lines={this.state.accessLines}
                    />;
            } else if (this.state.node_name == "audit-log-monitor") {
                monitor_element =
                    <AuditLogMonitor
                        data={this.state.auditlogData}
                        handleChange={this.handleAuditChange}
                        reload={this.refreshAuditLog}
                        reloading={this.state.auditReloading}
                        refreshing={this.state.auditRefreshing}
                        handleRefresh={this.auditRefreshCont}
                        lines={this.state.auditLines}
                    />;
            } else if (this.state.node_name == "auditfail-log-monitor") {
                monitor_element =
                    <AuditFailLogMonitor
                        data={this.state.auditfaillogData}
                        handleChange={this.handleAuditFailChange}
                        reload={this.refreshAuditFailLog}
                        reloading={this.state.auditfailReloading}
                        refreshing={this.state.auditfailRefreshing}
                        handleRefresh={this.auditFailRefreshCont}
                        lines={this.state.auditfailLines}
                    />;
            } else if (this.state.node_name == "error-log-monitor") {
                monitor_element =
                    <ErrorLogMonitor
                        data={this.state.errorlogData}
                        handleChange={this.handleErrorChange}
                        reload={this.refreshErrorLog}
                        reloading={this.state.errorReloading}
                        refreshing={this.state.errorRefreshing}
                        handleRefresh={this.errorRefreshCont}
                        handleSevLevel={this.handleSevChange}
                        lines={this.state.errorLines}
                    />;
            } else if (this.state.node_name == "replication-monitor") {
                if (this.state.replLoading) {
                    monitor_element =
                        <div className="ds-loading-spinner ds-center">
                            <p />
                            <h4>Loading replication monitor information ...</h4>
                            <Spinner loading size="md" />
                        </div>;
                } else {
                    if (this.state.replicatedSuffixes.length < 1) {
                        monitor_element =
                            <div>
                                <p>There are no suffixes that have been configured for replication</p>
                            </div>;
                    } else {
                        let suffixList = this.state.replicatedSuffixes.map((suffix) =>
                            <option key={suffix} value={suffix}>{suffix}</option>
                        );
                        monitor_element =
                            <div>
                                <Row>
                                    <Col sm={12}>
                                        <ControlLabel className="ds-header">Replication Monitoring</ControlLabel>
                                        <select className="ds-left-indent" defaultValue={this.state.replSuffix} onChange={this.replSuffixChange}>
                                            {suffixList}
                                        </select>
                                        <Icon className="ds-left-margin ds-refresh"
                                            type="fa" name="refresh" title="Refresh replication monitor"
                                            onClick={() => this.loadMonitorReplication()}
                                        />
                                    </Col>
                                </Row>
                                <div className="ds-margin-top-med">
                                    <ReplMonitor
                                        suffix={this.state.replSuffix}
                                        serverId={this.props.serverId}
                                        data={this.state[this.state.replSuffix]}
                                        addNotification={this.addNotification}
                                        reloadAgmts={this.reloadReplAgmts}
                                        reloadWinsyncAgmts={this.reloadReplWinsyncAgmts}
                                        key={this.state.replSuffix}
                                    />
                                </div>
                            </div>;
                    }
                }
            } else if (this.state.node_name != "") {
                // suffixes (example)
                if (this.state.suffixLoading) {
                    monitor_element =
                        <div className="ds-loading-spinner ds-center">
                            <p />
                            <h4>Loading suffix monitor information for <b>{this.state.node_text} ...</b></h4>
                            <Spinner loading size="md" />
                        </div>;
                } else if (this.state.chainingLoading) {
                    monitor_element =
                        <div className="ds-loading-spinner ds-center">
                            <p />
                            <h4>Loading chaining monitor information for <b>{this.state.node_text} ...</b></h4>
                            <Spinner loading size="md" />
                        </div>;
                } else {
                    if (this.state.node_type == "dblink") {
                        monitor_element =
                            <ChainingMonitor
                                suffix={this.state.node_text}
                                bename={this.state.bename}
                                reload={this.loadMonitorChaining}
                                data={this.state[this.state.node_text].chainingData}
                                key={this.state.node_text}
                            />;
                    } else {
                        // Suffix
                        monitor_element =
                            <SuffixMonitor
                                suffix={this.state.node_text}
                                bename={this.state.bename}
                                reload={this.loadMonitorSuffix}
                                data={this.state[this.state.node_text].suffixData}
                                key={this.state.node_text}
                            />;
                    }
                }
            }
            monitorPage =
                <div className="container-fluid">
                    <NotificationController
                        notifications={this.state.notifications}
                        removeNotificationAction={this.removeNotification}
                    />
                    <div className="ds-container">
                        <div>
                            <div className="ds-tree">
                                <div className="tree-view-container" id="db-tree"
                                    style={treeViewContainerStyles}>
                                    <TreeView
                                        nodes={nodes}
                                        highlightOnHover
                                        highlightOnSelect
                                        selectNode={this.selectNode}
                                    />
                                </div>
                            </div>
                        </div>
                        <div className="ds-tree-content">
                            {monitor_element}
                        </div>
                    </div>
                </div>;
        } else {
            monitorPage =
                <div className="ds-loading-spinner ds-center">
                    <p />
                    <h4>Loading monitor information ...</h4>
                    <Spinner loading size="md" />
                </div>;
        }

        return (
            <div>
                {monitorPage}
            </div>
        );
    }
}

// Property types and defaults

Monitor.propTypes = {
    serverId: PropTypes.string
};

Monitor.defaultProps = {
    serverId: ""
};
