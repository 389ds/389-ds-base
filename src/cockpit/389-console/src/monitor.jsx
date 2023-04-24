import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "./lib/tools.jsx";
import PropTypes from "prop-types";
import ServerMonitor from "./lib/monitor/serverMonitor.jsx";
import DatabaseMonitor from "./lib/monitor/dbMonitor.jsx";
import SuffixMonitor from "./lib/monitor/suffixMonitor.jsx";
import ChainingMonitor from "./lib/monitor/chainingMonitor.jsx";
import AccessLogMonitor from "./lib/monitor/accesslog.jsx";
import AuditLogMonitor from "./lib/monitor/auditlog.jsx";
import AuditFailLogMonitor from "./lib/monitor/auditfaillog.jsx";
import ErrorLogMonitor from "./lib/monitor/errorlog.jsx";
import SecurityLogMonitor from "./lib/monitor/securitylog.jsx";
import ReplMonitor from "./lib/monitor/replMonitor.jsx";
import ReplAgmtMonitor from "./lib/monitor/replMonAgmts.jsx";
import ReplAgmtWinsync from "./lib/monitor/replMonWinsync.jsx";
import ReplMonTasks from "./lib/monitor/replMonTasks.jsx";
import ReplMonConflict from "./lib/monitor/replMonConflict.jsx";
import {
    Spinner,
    TreeView,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faBook,
    faLeaf,
    faLink,
    faTree,
} from '@fortawesome/free-solid-svg-icons';
import {
    CatalogIcon,
    ClusterIcon,
    DatabaseIcon,
    TopologyIcon,
    MonitoringIcon,
} from '@patternfly/react-icons';

export class Monitor extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            nodes: [],
            node_name: "",
            node_text: "",
            node_type: "",
            node_item: "",
            loaded: false,
            snmpData: {},
            ldbmData: {},
            serverData: {},
            disks: [],
            loadingMsg: "",
            disableTree: false,
            activeItems: [{
                name: "Database",
                icon: <DatabaseIcon />,
                id: "server-monitor",
                type: "server",
                children: [],
                defaultExpanded: true,
            }],
            // Suffix
            suffixLoading: false,
            serverLoading: false,
            ldbmLoading: false,
            chainingLoading: false,
            // replication
            replLoading: false,
            replInitLoaded: false,
            replSuffix: "",
            replRole: "",
            replRid: "",
            replicatedSuffixes: [],
            credRows: [],
            aliasRows: [],
            // Logging
            accesslogLocation: "",
            errorlogLocation: "",
            auditlogLocation: "",
            auditfaillogLocation: "",
            securitylogLocation: "",
        };

        // Bindings
        this.loadSuffixTree = this.loadSuffixTree.bind(this);
        this.enableTree = this.enableTree.bind(this);
        this.update_tree_nodes = this.update_tree_nodes.bind(this);
        this.handleTreeClick = this.handleTreeClick.bind(this);
        this.disableSuffixLoading = this.disableSuffixLoading.bind(this);
        this.loadMonitorLDBM = this.loadMonitorLDBM.bind(this);
        this.loadMonitorSNMP = this.loadMonitorSNMP.bind(this);
        this.reloadSNMP = this.reloadSNMP.bind(this);
        this.loadMonitorServer = this.loadMonitorServer.bind(this);
        this.reloadServer = this.reloadServer.bind(this);
        this.loadMonitorChaining = this.loadMonitorChaining.bind(this);
        this.loadDiskSpace = this.loadDiskSpace.bind(this);
        this.reloadDisks = this.reloadDisks.bind(this);
        // Replication
        this.onHandleLoadMonitorReplication = this.onHandleLoadMonitorReplication.bind(this);
        this.loadCleanTasks = this.loadCleanTasks.bind(this);
        this.loadAbortTasks = this.loadAbortTasks.bind(this);
        this.loadReplicatedSuffixes = this.loadReplicatedSuffixes.bind(this);
        this.loadWinsyncAgmts = this.loadWinsyncAgmts.bind(this);
        this.reloadReplAgmts = this.reloadReplAgmts.bind(this);
        this.reloadReplWinsyncAgmts = this.reloadReplWinsyncAgmts.bind(this);
        this.loadConflicts = this.loadConflicts.bind(this);
        this.loadGlues = this.loadGlues.bind(this);
        this.gatherAllReplicaHosts = this.gatherAllReplicaHosts.bind(this);
        this.getAgmts = this.getAgmts.bind(this);
        // Logging
        this.loadMonitor = this.loadMonitor.bind(this);
    }

    componentDidUpdate(prevProps) {
        if (this.props.wasActiveList.includes(6)) {
            if (this.state.firstLoad) {
                this.loadMonitor();
            } else {
                if (this.props.serverId !== prevProps.serverId) {
                    this.loadSuffixTree(false);
                }
            }
        }
    }

    processTree(suffixData) {
        for (const suffix of suffixData) {
            if (suffix.type === "suffix") {
                suffix.icon = <FontAwesomeIcon size="sm" icon={faTree} />;
            } else if (suffix.type === "subsuffix") {
                suffix.icon = <FontAwesomeIcon size="sm" icon={faLeaf} />;
            } else {
                suffix.icon = <FontAwesomeIcon size="sm" icon={faLink} />;
            }
            if (suffix.children.length === 0) {
                delete suffix.children;
            } else {
                this.processTree(suffix.children);
            }
        }
    }

    processReplSuffixes(suffixTree) {
        for (const suffix of this.state.replicatedSuffixes) {
            suffixTree.push({
                name: suffix,
                icon: <FontAwesomeIcon size="sm" icon={faTree} />,
                id: "replication-suffix-" + suffix,
                type: "replication-suffix",
                defaultExpanded: false,
                children: [
                    {
                        name: "Agreements",
                        icon: <MonitoringIcon />,
                        id: suffix + "-agmts",
                        item: "agmt-mon",
                        type: "repl-mon",
                        suffix: suffix
                    },
                    {
                        name: "Winsync Agreements",
                        icon: <MonitoringIcon />,
                        id: suffix + "-winsync",
                        item: "winsync-mon",
                        type: "repl-mon",
                        suffix: suffix
                    },
                    {
                        name: "Tasks",
                        icon: <MonitoringIcon />,
                        id: suffix + "-tasks",
                        item: "task-mon",
                        type: "repl-mon",
                        suffix: suffix
                    },
                    {
                        name: "Conflict Entries",
                        icon: <MonitoringIcon />,
                        id: suffix + "-conflict",
                        item: "conflict-mon",
                        type: "repl-mon",
                        suffix: suffix
                    },
                ],
            });
        }
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
                    const treeData = JSON.parse(content);
                    this.processTree(treeData);
                    const basicData = [
                        {
                            name: "Server Statistics",
                            icon: <ClusterIcon />,
                            id: "server-monitor",
                            type: "server",
                        },
                        {
                            name: "Replication",
                            icon: <TopologyIcon />,
                            id: "replication-monitor",
                            type: "replication",
                            defaultExpanded: true,
                            children: [
                                {
                                    name: "Synchronization Report",
                                    icon: <MonitoringIcon />,
                                    id: "sync-report",
                                    item: "sync-report",
                                    type: "repl-mon",
                                },
                            ],
                        },
                        {
                            name: "Database",
                            icon: <DatabaseIcon />,
                            id: "database-monitor",
                            type: "database",
                            children: [],
                            defaultExpanded: true,
                        },
                        {
                            name: "Logging",
                            icon: <CatalogIcon />,
                            id: "log-monitor",
                            defaultExpanded: true,
                            children: [
                                {
                                    name: "Access Log",
                                    icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                                    id: "access-log-monitor",
                                    type: "log",
                                },
                                {
                                    name: "Audit Log",
                                    icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                                    id: "audit-log-monitor",
                                    type: "log",
                                },
                                {
                                    name: "Audit Failure Log",
                                    icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                                    id: "auditfail-log-monitor",
                                    type: "log",
                                },
                                {
                                    name: "Errors Log",
                                    icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                                    id: "error-log-monitor",
                                    type: "log",
                                },
                                {
                                    name: "Security Log",
                                    icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                                    id: "security-log-monitor",
                                    type: "log",
                                },
                            ]
                        },
                    ];
                    let current_node = this.state.node_name;
                    let type = this.state.node_type;
                    if (fullReset) {
                        current_node = "server-monitor";
                        type = "server";
                    }
                    basicData[2].children = treeData; // database node
                    this.processReplSuffixes(basicData[1].children);

                    this.setState(() => ({
                        nodes: basicData,
                        node_name: current_node,
                        node_type: type,
                    }), this.update_tree_nodes);
                });
    }

    handleTreeClick(evt, treeViewItem, parentItem) {
        if (treeViewItem.id === "log-monitor" ||
            treeViewItem.id === "replication-monitor" ||
            treeViewItem.id.startsWith("replication-suffix")) {
            return;
        }
        if (this.state.activeItems.length === 0 || treeViewItem === this.state.activeItems[0]) {
            this.setState({
                activeItems: [treeViewItem, parentItem]
            });
            return;
        }
        this.setState({
            disableTree: true, // Disable the tree to allow node to be fully loaded
        });

        if (treeViewItem.id === "database-monitor" ||
            treeViewItem.id === "server-monitor") {
            // Nothing special to do, these configurations have already been loaded
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    disableTree: false,
                    bename: "",
                };
            });
        } else if (treeViewItem.id === "access-log-monitor") {
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    bename: "",
                };
            });
        } else if (treeViewItem.id === "audit-log-monitor") {
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    bename: "",
                };
            });
        } else if (treeViewItem.id === "auditfail-log-monitor") {
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    bename: "",
                };
            });
        } else if (treeViewItem.id === "error-log-monitor") {
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    bename: "",
                };
            });
        } else if (treeViewItem.id === "sync-report") {
            this.gatherAllReplicaHosts(treeViewItem, parentItem);
        } else {
            if (treeViewItem.type === "repl-mon") {
                this.onHandleLoadMonitorReplication(treeViewItem, parentItem);
            } else if (treeViewItem.id in this.state &&
                ("chainingData" in this.state[treeViewItem.id] ||
                 "suffixData" in this.state[treeViewItem.id])
            ) {
                // This suffix is already cached
                this.setState(prevState => {
                    return {
                        activeItems: [treeViewItem, parentItem],
                        node_name: treeViewItem.id,
                        node_text: treeViewItem.name,
                        node_type: treeViewItem.type,
                        disableTree: false,
                        bename: treeViewItem.be,
                    };
                });
            } else {
                // Load this suffix (db, chaining & replication)
                if (treeViewItem.type === "dblink") {
                    // Chaining
                    this.loadMonitorChaining(treeViewItem.id);
                }
                this.setState(prevState => {
                    return {
                        activeItems: [treeViewItem, parentItem],
                        node_name: treeViewItem.id,
                        node_text: treeViewItem.name,
                        node_type: treeViewItem.type,
                        bename: treeViewItem.be,
                    };
                });
            }
        }
    }

    update_tree_nodes() {
        // Enable the tree, and update the titles
        this.setState({
            loaded: true,
            disableTree: false,
        }, () => {
            const className = 'pf-c-tree-view__list-item';
            const element = document.getElementById("monitor-tree");
            if (element) {
                const elements = element.getElementsByClassName(className);
                for (const el of elements) {
                    el.setAttribute('title', el.innerText);
                }
            }
            this.loadDSRC();
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
        if (this.state.firstLoad) {
            this.setState({
                firstLoad: false
            });
        }
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get", "nsslapd-auditlog", "nsslapd-accesslog", "nsslapd-errorlog",
            "nsslapd-auditfaillog", "nsslapd-securitylog"
        ];
        log_cmd("loadLogLocations", "Get log locations", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        accesslogLocation: config.attrs['nsslapd-accesslog'][0],
                        auditlogLocation: config.attrs['nsslapd-auditlog'][0],
                        auditfaillogLocation: config.attrs['nsslapd-auditfaillog'][0],
                        errorlogLocation: config.attrs['nsslapd-errorlog'][0],
                        securitylogLocation: config.attrs['nsslapd-securitylog'][0],
                    });
                }, this.loadReplicatedSuffixes());
    }

    loadReplicatedSuffixes() {
        // Load replicated suffix to populate the dropdown select list
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "list"
        ];
        log_cmd("loadReplicatedSuffixes", "Load replication suffixes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let replSuffix = "";
                    if (config.items.length > 0) {
                        replSuffix = config.items[0];
                    }
                    this.setState({
                        replicatedSuffixes: config.items,
                        replSuffix: replSuffix,
                    });
                }, this.loadDiskSpace());
    }

    loadMonitorLDBM() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "ldbm"
        ];
        log_cmd("loadMonitorLDBM", "Load database monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        ldbmData: config.attrs
                    });
                }, this.loadMonitorServer());
    }

    loadMonitorServer() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "server"
        ];
        log_cmd("loadMonitorServer", "Load server monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        serverData: config.attrs
                    });
                }, this.loadMonitorSNMP());
    }

    reloadServer() {
        this.setState({
            serverLoading: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "server"
        ];
        log_cmd("reloadServer", "Load server monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        serverLoading: false,
                        serverData: config.attrs
                    }, this.reloadDisks());
                });
    }

    loadMonitorSNMP() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "snmp"
        ];
        log_cmd("loadMonitorSNMP", "Load snmp monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        snmpData: config.attrs,
                    }, this.loadSuffixTree(true));
                });
    }

    loadDiskSpace() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "disk"
        ];
        log_cmd("loadDiskSpace", "Load disk space info", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const disks = JSON.parse(content);
                    const rows = [];
                    for (const disk of disks.items) {
                        rows.push([disk.mount, disk.size, disk.used + " (" + disk.percent + "%)", disk.avail]);
                    }
                    this.setState({
                        disks: rows,
                    });
                }, this.loadMonitorLDBM());
    }

    reloadDisks () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "disk"
        ];
        log_cmd("reloadDisks", "Reload disk stats", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const disks = JSON.parse(content);
                    const rows = [];
                    for (const disk of disks.items) {
                        rows.push([disk.mount, disk.size, disk.used + " (" + disk.percent + "%)", disk.avail]);
                    }
                    this.setState({
                        disks: rows,
                    });
                });
    }

    reloadSNMP() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "snmp"
        ];
        log_cmd("reloadSNMP", "Load snmp monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        snmpData: config.attrs,
                    });
                });
    }

    loadMonitorChaining(suffix) {
        this.setState({
            chainingLoading: true
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "chaining", suffix
        ];
        log_cmd("loadMonitorChaining", "Load suffix monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
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

    disableSuffixLoading () {
        this.setState({
            suffixLoading: false
        });
    }

    loadCleanTasks(replSuffix) {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-tasks", "list-cleanruv-tasks", "--suffix=" + replSuffix];
        log_cmd("loadCleanTasks", "Load clean tasks", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [replSuffix]: {
                            ...this.state[replSuffix],
                            cleanTasks: config.items,
                        },
                    }, this.loadAbortTasks(replSuffix));
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadAbortTasks(replSuffix) {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-tasks", "list-abortruv-tasks", "--suffix=" + replSuffix];
        log_cmd("loadAbortCleanTasks", "Load abort tasks", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [replSuffix]: {
                            ...this.state[replSuffix],
                            abortTasks: config.items,
                        },
                    }, this.loadConflicts(replSuffix));
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadConflicts(replSuffix) {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "list", replSuffix];
        log_cmd("loadConflicts", "Load conflict entries", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [replSuffix]: {
                            ...this.state[replSuffix],
                            conflicts: config.items,
                            glues: []
                        },
                    }, this.loadGlues(replSuffix));
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadGlues(replSuffix) {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "list-glue", replSuffix];
        log_cmd("loadGlues", "Load glue entries", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [replSuffix]: {
                            ...this.state[replSuffix],
                            glues: config.items,

                        },
                        replLoading: false,
                        replInitLoaded: true
                    });
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadDSRC() {
        // Load dsrc replication report configuration
        const dsrc_cmd = ["dsctl", "-j", this.props.serverId, "dsrc", "display"];
        log_cmd("loadDSRC", "Check for replication monitor configurations in the .dsrc file", dsrc_cmd);
        cockpit
                .spawn(dsrc_cmd, { superuser: true, err: "message" })
                .done(dsrc_content => {
                    const content = JSON.parse(dsrc_content);
                    const credRows = [];
                    const aliasRows = [];
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
                        replLoading: false,
                        replInitLoaded: true
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to get .dsrc information: ${errMsg.desc}`
                    );
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadWinsyncAgmts(replSuffix) {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "winsync-status", "--suffix=" + replSuffix];
        log_cmd("loadWinsyncAgmts", "Load winsync agmt status", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [replSuffix]: {
                            ...this.state[replSuffix],
                            replWinsyncAgmts: config.items,
                        },
                    }, this.loadCleanTasks(replSuffix));
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    getAgmts(suffixList, idx) {
        const new_idx = idx + 1;
        if (new_idx > suffixList.length) {
            this.setState({
                replLoading: false,
                disableTree: false,
            });
            return;
        }

        const suffix = suffixList[idx];
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "status", "--suffix=" + suffix];
        log_cmd("gatherAllReplicaHosts", "Get replication hosts for repl report", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            replAgmts: config.items,
                            abortTasks: [],
                            cleanTasks: [],
                            replWinsyncAgmts: [],
                        },
                    }, () => { this.getAgmts(suffixList, new_idx) });
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                        disableTree: false,
                    });
                });
    }

    gatherAllReplicaHosts(treeViewItem, parentItem) {
        if (treeViewItem.name !== "") {
            this.setState({
                replLoading: true,
                activeItems: [treeViewItem, parentItem],
                node_name: treeViewItem.id,
                node_text: treeViewItem.name,
                node_type: treeViewItem.type,
                node_item: treeViewItem.item,
            }, () => { this.getAgmts(this.state.replicatedSuffixes, 0) });
        } else {
            // We should enable it here because ReplMonitor never will be mounted
            this.enableTree();
        }
    }

    onHandleLoadMonitorReplication(treeViewItem, parentItem) {
        if (treeViewItem.name !== "") {
            this.setState({
                replLoading: true
            });

            // Now load the agmts
            const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "replication", "status", "--suffix=" + treeViewItem.suffix];
            log_cmd("onHandleLoadMonitorReplication", "Load replication suffix info", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        const config = JSON.parse(content);
                        this.setState({
                            [treeViewItem.suffix]: {
                                ...this.state[treeViewItem.suffix],
                                replAgmts: config.items,
                                abortTasks: [],
                                cleanTasks: [],
                                replWinsyncAgmts: [],
                            },
                            activeItems: [treeViewItem, parentItem],
                            node_name: treeViewItem.id,
                            node_text: treeViewItem.name,
                            node_type: treeViewItem.type,
                            node_item: treeViewItem.item,
                            replSuffix: treeViewItem.suffix,
                            disableTree: false,
                        }, this.loadWinsyncAgmts(treeViewItem.suffix));
                    })
                    .fail(() => {
                        // Notification of failure (could only be server down)
                        this.setState({
                            replLoading: false,
                        });
                    });
        } else {
            // We should enable it here because ReplMonitor never will be mounted
            this.enableTree();
        }
    }

    reloadReplAgmts(replSuffix) {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "status", "--suffix=" + replSuffix];
        log_cmd("reloadReplAgmts", "Load replication agmts", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [replSuffix]: {
                            ...this.state[replSuffix],
                            replAgmts: config.items,
                        },
                    });
                });
    }

    reloadReplWinsyncAgmts(replSuffix) {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "winsync-status", "--suffix=" + replSuffix];
        log_cmd("reloadReplWinsyncAgmts", "Load winysnc agmts", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [replSuffix]: {
                            ...this.state[replSuffix],
                            replWinsyncAgmts: config.items,
                        },
                    });
                });
    }

    enableTree () {
        this.setState({
            disableTree: false
        });
    }

    render() {
        const { nodes } = this.state;
        let monitorPage = "";
        let monitor_element = "";
        let disabled = "tree-view-container";
        if (this.state.disableTree) {
            disabled = "tree-view-container ds-disabled";
        }

        if (this.state.loaded) {
            if (this.state.node_name === "database-monitor" || this.state.node_name === "") {
                if (this.state.ldbmLoading) {
                    monitor_element =
                        <div className="ds-margin-top-xlg ds-center">
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Loading Database Monitor Information ...
                                </Text>
                            </TextContent>
                            <Spinner className="ds-margin-top-lg" size="xl" />
                        </div>;
                } else {
                    monitor_element =
                        <DatabaseMonitor
                            data={this.state.ldbmData}
                            enableTree={this.enableTree}
                            serverId={this.props.serverId}
                        />;
                }
            } else if (this.state.node_name === "server-monitor") {
                if (this.state.serverLoading) {
                    monitor_element =
                        <div className="ds-margin-top-xlg ds-center">
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Loading Server Monitor Information ...
                                </Text>
                            </TextContent>
                            <Spinner className="ds-margin-top-lg" size="xl" />
                        </div>;
                } else {
                    monitor_element =
                        <ServerMonitor
                            data={this.state.serverData}
                            reload={this.reloadServer}
                            serverId={this.props.serverId}
                            disks={this.state.disks}
                            reloadDisks={this.reloadDisks}
                            snmpData={this.state.snmpData}
                            snmpReload={this.reloadSNMP}
                            enableTree={this.enableTree}
                        />;
                }
            } else if (this.state.node_name === "access-log-monitor") {
                monitor_element =
                    <AccessLogMonitor
                        logLocation={this.state.accesslogLocation}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name === "audit-log-monitor") {
                monitor_element =
                    <AuditLogMonitor
                        logLocation={this.state.auditlogLocation}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name === "auditfail-log-monitor") {
                monitor_element =
                    <AuditFailLogMonitor
                        logLocation={this.state.auditfaillogLocation}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name === "error-log-monitor") {
                monitor_element =
                    <ErrorLogMonitor
                        logLocation={this.state.errorlogLocation}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name === "security-log-monitor") {
                monitor_element =
                    <SecurityLogMonitor
                        logLocation={this.state.securitylogLocation}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_type === "repl-mon") {
                if (this.state.replLoading) {
                    monitor_element =
                        <div className="ds-margin-top-xlg ds-center">
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Loading Replication Monitor Information ...
                                </Text>
                            </TextContent>
                            <Spinner className="ds-margin-top-lg" size="xl" />
                        </div>;
                } else {
                    if (this.state.node_name === "sync-report") {
                        monitor_element =
                            <div>
                                <ReplMonitor
                                    serverId={this.props.serverId}
                                    data={this.state[this.state.replSuffix]}
                                    credRows={this.state.credRows}
                                    aliasRows={this.state.aliasRows}
                                    addNotification={this.props.addNotification}
                                    enableTree={this.enableTree}
                                    reload={this.onHandleLoadMonitorReplication}
                                    key={this.state.node_name}
                                />
                            </div>;
                    } else if (this.state.node_item === "agmt-mon") {
                        monitor_element =
                            <div>
                                <ReplAgmtMonitor
                                    suffix={this.state.replSuffix}
                                    serverId={this.props.serverId}
                                    data={this.state[this.state.replSuffix]}
                                    addNotification={this.props.addNotification}
                                    reloadAgmts={this.reloadReplAgmts}
                                    enableTree={this.enableTree}
                                    reload={this.onHandleLoadMonitorReplication}
                                    key={this.state.node_name}
                                />
                            </div>;
                    } else if (this.state.node_item === "winsync-mon") {
                        monitor_element =
                            <div>
                                <ReplAgmtWinsync
                                    suffix={this.state.replSuffix}
                                    serverId={this.props.serverId}
                                    data={this.state[this.state.replSuffix]}
                                    addNotification={this.props.addNotification}
                                    reloadAgmts={this.reloadReplWinsyncAgmts}
                                    enableTree={this.enableTree}
                                    reload={this.onHandleLoadMonitorReplication}
                                    key={this.state.node_name}
                                />
                            </div>;
                    } else if (this.state.node_item === "task-mon") {
                        monitor_element =
                            <div>
                                <ReplMonTasks
                                    suffix={this.state.replSuffix}
                                    serverId={this.props.serverId}
                                    data={this.state[this.state.replSuffix]}
                                    addNotification={this.props.addNotification}
                                    enableTree={this.enableTree}
                                    reload={this.onHandleLoadMonitorReplication}
                                    key={this.state.node_name}
                                />
                            </div>;
                    } else if (this.state.node_item === "conflict-mon") {
                        monitor_element =
                            <div>
                                <ReplMonConflict
                                    suffix={this.state.replSuffix}
                                    serverId={this.props.serverId}
                                    data={this.state[this.state.replSuffix]}
                                    addNotification={this.props.addNotification}
                                    reloadConflicts={this.loadConflicts}
                                    enableTree={this.enableTree}
                                    reload={this.onHandleLoadMonitorReplication}
                                    key={this.state.node_name}
                                />
                            </div>;
                    }
                }
            } else if (this.state.node_name !== "") {
                // suffixes (example)
                if (this.state.chainingLoading) {
                    monitor_element =
                        <div className="ds-margin-top-xlg ds-center">
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Loading Chaining Monitor Information For <b>{this.state.node_text} ...</b>
                                </Text>
                            </TextContent>
                            <Spinner className="ds-margin-top-lg" size="xl" />
                        </div>;
                } else {
                    if (this.state.node_type === "dblink") {
                        monitor_element =
                            <ChainingMonitor
                                suffix={this.state.node_text}
                                bename={this.state.bename}
                                reload={this.loadMonitorChaining}
                                data={this.state[this.state.node_text].chainingData}
                                enableTree={this.enableTree}
                                key={this.state.node_text}
                            />;
                    } else {
                        // Suffix
                        monitor_element =
                            <SuffixMonitor
                                serverId={this.props.serverId}
                                suffix={this.state.node_text}
                                bename={this.state.bename}
                                enableTree={this.enableTree}
                                key={this.state.node_text}
                                addNotification={this.props.addNotification}
                            />;
                    }
                }
            }
            monitorPage =
                <div className="container-fluid">
                    <div className="ds-container">
                        <div>
                            <div className="ds-tree">
                                <div className={disabled} id="monitor-tree">
                                    <TreeView
                                        hasSelectableNodes
                                        data={nodes}
                                        activeItems={this.state.activeItems}
                                        onSelect={this.handleTreeClick}
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
                <div className="ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Loading Monitor Information ...
                        </Text>
                    </TextContent>
                    <Spinner className="ds-margin-top-lg" size="xl" />
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
    addNotification: PropTypes.func,
    serverId: PropTypes.string
};

Monitor.defaultProps = {
    serverId: ""
};
