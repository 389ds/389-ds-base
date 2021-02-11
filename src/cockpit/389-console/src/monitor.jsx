import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "./lib/tools.jsx";
import {
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
import {
    Spinner,
    TreeView,
    noop
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
    ListIcon,
    TopologyIcon,
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
                id: "database-monitor",
                type: "database",
                children: [],
                defaultExpanded: true,
            }],
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
            accesslogLocation: "",
            errorlogLocation: "",
            auditlogLocation: "",
            auditfaillogLocation: "",
        };

        // Bindings
        this.loadSuffixTree = this.loadSuffixTree.bind(this);
        this.enableTree = this.enableTree.bind(this);
        this.update_tree_nodes = this.update_tree_nodes.bind(this);
        this.onTreeClick = this.onTreeClick.bind(this);
        this.loadMonitorSuffix = this.loadMonitorSuffix.bind(this);
        this.disableSuffixLoading = this.disableSuffixLoading.bind(this);
        this.loadMonitorLDBM = this.loadMonitorLDBM.bind(this);
        this.reloadLDBM = this.reloadLDBM.bind(this);
        this.loadMonitorSNMP = this.loadMonitorSNMP.bind(this);
        this.reloadSNMP = this.reloadSNMP.bind(this);
        this.loadMonitorServer = this.loadMonitorServer.bind(this);
        this.reloadServer = this.reloadServer.bind(this);
        this.loadMonitorChaining = this.loadMonitorChaining.bind(this);
        this.loadDiskSpace = this.loadDiskSpace.bind(this);
        this.reloadDisks = this.reloadDisks.bind(this);
        // Replication
        this.loadMonitorReplication = this.loadMonitorReplication.bind(this);
        this.loadCleanTasks = this.loadCleanTasks.bind(this);
        this.loadAbortTasks = this.loadAbortTasks.bind(this);
        this.loadReplicatedSuffixes = this.loadReplicatedSuffixes.bind(this);
        this.loadWinsyncAgmts = this.loadWinsyncAgmts.bind(this);
        this.replSuffixChange = this.replSuffixChange.bind(this);
        this.reloadReplAgmts = this.reloadReplAgmts.bind(this);
        this.reloadReplWinsyncAgmts = this.reloadReplWinsyncAgmts.bind(this);
        this.loadConflicts = this.loadConflicts.bind(this);
        this.loadGlues = this.loadGlues.bind(this);
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
                    for (let suffix of treeData) {
                        if (suffix['type'] == "suffix") {
                            suffix['icon'] = <FontAwesomeIcon size="sm" icon={faTree} />;
                        } else if (suffix['type'] == "subsuffix") {
                            suffix['icon'] = <FontAwesomeIcon size="sm" icon={faLeaf} />;
                        } else {
                            suffix['icon'] = <FontAwesomeIcon size="sm" icon={faLink} />;
                        }
                        if (suffix['children'].length == 0) {
                            delete suffix.children;
                        }
                    }
                    let basicData = [
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
                            ]
                        },
                        {
                            name: "Replication",
                            icon: <TopologyIcon />,
                            id: "replication-monitor",
                            type: "replication",
                        },
                        {
                            name: "Server Statistics",
                            icon: <ClusterIcon />,
                            id: "server-monitor",
                            type: "server",
                        },
                        {
                            name: "SNMP Counters",
                            icon: <ListIcon />,
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
                    basicData[0].children = treeData;
                    this.setState(() => ({
                        nodes: basicData,
                        node_name: current_node,
                        node_type: type,
                    }), this.update_tree_nodes);
                });
    }

    onTreeClick(evt, treeViewItem, parentItem) {
        if (treeViewItem.id == "log-monitor") {
            return;
        }
        if (this.state.activeItems.length == 0 || treeViewItem == this.state.activeItems[0]) {
            this.setState({
                activeItems: [treeViewItem, parentItem]
            });
            return;
        }
        this.setState({
            disableTree: true, // Disable the tree to allow node to be fully loaded
        });

        if (treeViewItem.id == "database-monitor" ||
            treeViewItem.id == "server-monitor" ||
            treeViewItem.id == "snmp-monitor") {
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
        } else if (treeViewItem.id == "access-log-monitor") {
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    bename: "",
                };
            });
        } else if (treeViewItem.id == "audit-log-monitor") {
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    bename: "",
                };
            });
        } else if (treeViewItem.id == "auditfail-log-monitor") {
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    bename: "",
                };
            });
        } else if (treeViewItem.id == "error-log-monitor") {
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    bename: "",
                };
            });
        } else if (treeViewItem.id == "replication-monitor") {
            if (!this.state.replInitLoaded) {
                this.loadMonitorReplication();
            }
            this.setState(prevState => {
                return {
                    activeItems: [treeViewItem, parentItem],
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    node_type: treeViewItem.type,
                    bename: "",
                };
            });
        } else {
            if (treeViewItem.id in this.state &&
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
                if (treeViewItem.type == "dblink") {
                    // Chaining
                    this.loadMonitorChaining(treeViewItem.id);
                } else {
                    // Suffix
                    this.loadMonitorSuffix(treeViewItem.id);
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
            let className = 'pf-c-tree-view__list-item';
            let element = document.getElementById("monitor-tree");
            if (element) {
                let elements = element.getElementsByClassName(className);
                for (let el of elements) {
                    el.setAttribute('title', el.innerText);
                }
            }
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
                }, this.loadDiskSpace());
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
                    }, this.reloadDisks());
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

    loadDiskSpace() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "disk"
        ];
        log_cmd("loadDiskSpace", "Load disk space info", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let disks = JSON.parse(content);
                    for (let disk of disks.items) {
                        disk.used = disk.used + " (" + disk.percent + "%)";
                    }
                    this.setState({
                        disks: disks.items
                    });
                }, this.loadMonitorLDBM());
    }

    reloadDisks () {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "disk"
        ];
        log_cmd("reloadDisks", "Reload disk stats", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let disks = JSON.parse(content);
                    for (let disk of disks.items) {
                        disk.used = disk.used + " (" + disk.percent + "%)";
                    }
                    this.setState({
                        disks: disks.items,
                    });
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

    disableSuffixLoading () {
        this.setState({
            suffixLoading: false
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
                    }, this.disableSuffixLoading);
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
                    }, this.loadConflicts());
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadConflicts() {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "list", this.state.replSuffix];
        log_cmd("loadConflicts", "Load conflict entries", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        [this.state.replSuffix]: {
                            ...this.state[this.state.replSuffix],
                            conflicts: config.items,
                            glues: []
                        },
                    }, this.loadGlues());
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        replLoading: false,
                    });
                });
    }

    loadGlues() {
        let cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "list-glue", this.state.replSuffix];
        log_cmd("loadGlues", "Load glue entries", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        [this.state.replSuffix]: {
                            ...this.state[this.state.replSuffix],
                            glues: config.items,
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
        } else {
            // We should enable it here because ReplMonitor never will be mounted
            this.enableTree();
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

    replSuffixChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                replSuffix: value,
                replLoading: true
            }
        ), this.loadMonitorReplication);
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
            if (this.state.node_name == "database-monitor" || this.state.node_name == "") {
                if (this.state.ldbmLoading) {
                    monitor_element =
                        <div className="ds-margin-top-xlg ds-center">
                            <h4>Loading database monitor information ...</h4>
                            <Spinner className="ds-margin-top-lg" size="xl" />
                        </div>;
                } else {
                    monitor_element =
                        <DatabaseMonitor
                            data={this.state.ldbmData}
                            reload={this.reloadLDBM}
                            enableTree={this.enableTree}
                        />;
                }
            } else if (this.state.node_name == "server-monitor") {
                if (this.state.serverLoading) {
                    monitor_element =
                        <div className="ds-margin-top-xlg ds-center">
                            <h4>Loading server monitor information ...</h4>
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
                            enableTree={this.enableTree}
                        />;
                }
            } else if (this.state.node_name == "snmp-monitor") {
                if (this.state.snmpLoading) {
                    monitor_element =
                        <div className="ds-margin-top-xlg ds-center">
                            <h4>Loading SNMP monitor information ...</h4>
                            <Spinner className="ds-margin-top-lg" size="xl" />
                        </div>;
                } else {
                    monitor_element =
                        <SNMPMonitor
                            data={this.state.snmpData}
                            reload={this.reloadSNMP}
                            enableTree={this.enableTree}
                        />;
                }
            } else if (this.state.node_name == "access-log-monitor") {
                monitor_element =
                    <AccessLogMonitor
                        logLocation={this.state.accesslogLocation}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name == "audit-log-monitor") {
                monitor_element =
                    <AuditLogMonitor
                        logLocation={this.state.auditlogLocation}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name == "auditfail-log-monitor") {
                monitor_element =
                    <AuditFailLogMonitor
                        logLocation={this.state.auditfaillogLocation}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name == "error-log-monitor") {
                monitor_element =
                    <ErrorLogMonitor
                        logLocation={this.state.errorlogLocation}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name == "replication-monitor") {
                if (this.state.replLoading) {
                    monitor_element =
                        <div className="ds-margin-top-xlg ds-center">
                            <h4>Loading replication monitor information ...</h4>
                            <Spinner className="ds-margin-top-lg" size="xl" />
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
                                        addNotification={this.props.addNotification}
                                        reloadAgmts={this.reloadReplAgmts}
                                        reloadWinsyncAgmts={this.reloadReplWinsyncAgmts}
                                        reloadConflicts={this.loadConflicts}
                                        enableTree={this.enableTree}
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
                        <div className="ds-margin-top-xlg ds-center">
                            <h4>Loading suffix monitor information for <b>{this.state.node_text} ...</b></h4>
                            <Spinner className="ds-margin-top-lg" size="xl" />
                        </div>;
                } else if (this.state.chainingLoading) {
                    monitor_element =
                        <div className="ds-margin-top-xlg ds-center">
                            <h4>Loading chaining monitor information for <b>{this.state.node_text} ...</b></h4>
                            <Spinner className="ds-margin-top-lg" size="xl" />
                        </div>;
                } else {
                    if (this.state.node_type == "dblink") {
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
                                suffix={this.state.node_text}
                                bename={this.state.bename}
                                reload={this.loadMonitorSuffix}
                                data={this.state[this.state.node_text].suffixData}
                                enableTree={this.enableTree}
                                key={this.state.node_text}
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
                                        data={nodes}
                                        activeItems={this.state.activeItems}
                                        onSelect={this.onTreeClick}
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
                    <h4>Loading monitor information ...</h4>
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
    addNotification: noop,
    serverId: ""
};
