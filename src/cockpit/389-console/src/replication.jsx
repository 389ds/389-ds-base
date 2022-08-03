import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "./lib/tools.jsx";
import { ReplSuffix } from "./lib/replication/replSuffix.jsx";
import PropTypes from "prop-types";
import {
    Spinner,
    TreeView,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faClone,
    faTree,
    faLeaf,
} from '@fortawesome/free-solid-svg-icons';
import {
    TopologyIcon
} from '@patternfly/react-icons';

export class Replication extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            errObj: {},
            nodes: [],
            node_name: "",
            node_type: "",
            node_replicated: false,
            disableTree: true,
            activeItems: [],

            // Suffix
            suffixLoading: false,
            attributes: [],
            role: "",
            rid: "0",
            bindDNs: [],
            bindDNGroup: "",
            agmtRows: [],
            winsyncRows: [],
            ruvRows: [],
            suffixSpinning: false,
            disabled: false,
            clLoading: false,
            clMaxEntries: "",
            clMaxAge: "",
            clTrimInt: "",
            clEncrypt: false,
            suffixKey: 0,
            ldifRows: [],

            showDisableConfirm: false,
            loaded: false,
        };

        // General
        this.handleTreeClick = this.handleTreeClick.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.disableTree = this.disableTree.bind(this);
        this.enableTree = this.enableTree.bind(this);
        this.update_tree_nodes = this.update_tree_nodes.bind(this);

        this.reloadConfig = this.reloadConfig.bind(this);
        this.reloadAgmts = this.reloadAgmts.bind(this);
        this.reloadWinsyncAgmts = this.reloadWinsyncAgmts.bind(this);
        this.reloadRUV = this.reloadRUV.bind(this);
        this.loadAttrs = this.loadAttrs.bind(this);
        this.loadReplSuffix = this.loadReplSuffix.bind(this);
        this.reloadChangelog = this.reloadChangelog.bind(this);
        this.loadSuffixTree = this.loadSuffixTree.bind(this);
        this.loadLDIFs = this.loadLDIFs.bind(this);
    }

    componentDidUpdate(prevProps) {
        if (this.props.wasActiveList.includes(3)) {
            if (this.state.firstLoad) {
                this.loadSuffixTree(true);
                if (!this.state.loaded) {
                    this.loadAttrs();
                }
            } else {
                if (this.props.serverId !== prevProps.serverId) {
                    this.loadSuffixTree(true);
                }
            }
        }
    }

    reloadChangelog () {
        // Refresh the changelog
        this.setState({
            clLoading: true
        });
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket', 'replication', 'get-changelog'];
        log_cmd("reloadChangelog", "Reload the changelog", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let clDir = "";
                    let clMaxEntries = "";
                    let clMaxAge = "";
                    let clTrimInt = "";
                    let clEncrypt = false;
                    for (const attr in config.attrs) {
                        const val = config.attrs[attr][0];
                        if (attr === "nsslapd-changelogdir") {
                            clDir = val;
                        }
                        if (attr === "nsslapd-changelogmaxentries") {
                            clMaxEntries = val;
                        }
                        if (attr === "nsslapd-changelogmaxage") {
                            clMaxAge = val;
                        }
                        if (attr === "nsslapd-changelogtrim-interval") {
                            clTrimInt = val;
                        }
                        if (attr === "nsslapd-encryptionalgorithm") {
                            clEncrypt = true;
                        }
                    }
                    this.setState({
                        clDir: clDir,
                        clMaxEntries: clMaxEntries,
                        clMaxAge: clMaxAge,
                        clTrimInt: clTrimInt,
                        clEncrypt: clEncrypt,
                        clLoading: false
                    });
                })
                .fail(() => {
                    this.setState({
                        clDir: "",
                        clMaxEntries: "",
                        clMaxAge: "",
                        clTrimInt: "",
                        clEncrypt: false,
                        clLoading: false
                    });
                });
    }

    processBranch(treeBranch) {
        if (treeBranch === undefined || treeBranch.length === 0) {
            return;
        }
        for (const sub in treeBranch) {
            if (!treeBranch[sub].type.endsWith("suffix")) {
                // Not a suffix, skip it
                treeBranch.splice(sub, 1);
                continue;
            } else if (treeBranch[sub].replicated) {
                treeBranch[sub].icon = <FontAwesomeIcon size="sm" icon={faClone} />;
                treeBranch[sub].replicated = true;
            }
            if (treeBranch[sub].children.length === 0) {
                delete treeBranch[sub].children;
            }
            this.processBranch(treeBranch[sub].children);
        }
    }

    loadSuffixTree(fullReset) {
        if (this.state.firstLoad) {
            this.setState({
                firstLoad: false
            });
        }

        this.setState({
            loaded: false
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "get-tree",
        ];
        log_cmd("loadSuffixTree", "Start building the suffix tree", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let treeData = [];
                    if (content !== "") {
                        treeData = JSON.parse(content);
                        for (const suffix of treeData) {
                            if (suffix.type === "suffix") {
                                suffix.icon = <FontAwesomeIcon size="sm" icon={faTree} />;
                            } else if (suffix.type === "subsuffix") {
                                suffix.icon = <FontAwesomeIcon size="sm" icon={faLeaf} />;
                            }
                            if (suffix.children.length === 0) {
                                delete suffix.children;
                            }
                        }
                    }
                    const basicData = [
                        {
                            name: "Suffixes",
                            icon: <TopologyIcon />,
                            id: "repl-suffixes",
                            children: [],
                            defaultExpanded: true
                        }
                    ];
                    let current_node = this.state.node_name;
                    let current_type = this.state.node_type;
                    let replicated = this.state.node_replicated;
                    if (fullReset && treeData.length > 0) {
                        let found = false;
                        for (let i = 0; i < treeData.length; i++) {
                            if (treeData[i].replicated) {
                                treeData[i].icon = <FontAwesomeIcon size="sm" icon={faClone} />;
                                replicated = true;
                                if (!found) {
                                    // Load the first replicated suffix we find
                                    current_node = treeData[i].id;
                                    current_type = treeData[i].type;
                                    this.setState({
                                        activeItems: [treeData[i], basicData[0]]
                                    });
                                    this.loadReplSuffix(treeData[i].id);
                                    found = true;
                                }
                            }
                            this.processBranch(treeData[i].children);
                            if (treeData[i].children && treeData[i].children.length === 0) {
                                // Clean up tree
                                delete treeData[i].children;
                            }
                        }
                        if (!found) {
                            // No replicated suffixes, load the first one
                            current_node = treeData[0].id;
                            current_type = treeData[0].type;
                            this.setState({
                                activeItems: [treeData[0], basicData[0]]
                            });
                            this.loadReplSuffix(treeData[0].id);
                        }
                    } else if (treeData.length > 0) {
                        // Reset current suffix
                        for (const suffix of treeData) {
                            this.processBranch(suffix.children);
                            if (suffix.id === current_node) {
                                replicated = suffix.replicated;
                            }
                            if (suffix.replicated) {
                                suffix.icon = <FontAwesomeIcon size="sm" icon={faClone} />;
                            }
                        }
                        this.loadReplSuffix(current_node);
                    }

                    basicData[0].children = treeData;
                    this.setState({
                        nodes: basicData,
                        node_name: current_node,
                        node_type: current_type,
                        node_replicated: replicated,
                    }, () => { this.update_tree_nodes() });
                });
    }

    handleTreeClick(evt, treeViewItem, parentItem) {
        if (treeViewItem.id === "repl-suffixes") {
            return;
        }

        this.setState({
            activeItems: [treeViewItem],
            node_name: treeViewItem.id,
            node_type: treeViewItem.type,
            node_replicated: treeViewItem.replicated,
            disableTree: true // Disable the tree to allow node to be fully loaded
        });

        if (treeViewItem.id in this.state) {
            // This suffix is already cached, just use what we have...
            this.setState({
                disableTree: false,
                suffixKey: new Date(),
            });
        } else {
            // Suffix/subsuffix
            this.loadReplSuffix(treeViewItem.id);
            this.setState(prevState => {
                return {
                    suffixKey: new Date(),
                };
            });
        }
    }

    update_tree_nodes() {
        // Enable the tree, and update the titles
        this.setState({
            loaded: true,
            disableTree: false,
        }, () => {
            const className = 'pf-c-tree-view__list-item';
            const element = document.getElementById("repl-tree");
            if (element) {
                const elements = element.getElementsByClassName(className);
                for (const el of elements) {
                    if (el.id === "repl-suffixes") {
                        continue;
                    }
                    el.setAttribute('title', el.innerText);
                }
            }
        });
    }

    handleChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        if (value === "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        });
    }

    closeSuffixModal() {
        this.setState({
            showSuffixModal: false
        });
    }

    reloadAgmts (suffix) {
        this.setState({
            suffixSpinning: true,
            disabled: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-agmt", "list", "--suffix", suffix
        ];
        log_cmd("reloadAgmts", "get repl agreements", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const obj = JSON.parse(content);
                    const rows = [];
                    for (const idx in obj.items) {
                        const agmt_attrs = obj.items[idx].attrs;
                        let state = "Enabled";
                        let update_status = "";
                        let agmt_init_status = "";

                        // Compute state (enabled by default)
                        if ('nsds5replicaenabled' in agmt_attrs) {
                            if (agmt_attrs.nsds5replicaenabled[0].toLowerCase() === 'off') {
                                state = "Disabled";
                            }
                        }

                        // Check for status msgs
                        if ('nsds5replicalastupdatestatus' in agmt_attrs) {
                            update_status = agmt_attrs.nsds5replicalastupdatestatus[0];
                        }
                        if ('nsds5replicalastinitstatus' in agmt_attrs &&
                            agmt_attrs.nsds5replicalastinitstatus[0] !== "") {
                            agmt_init_status = agmt_attrs.nsds5replicalastinitstatus[0];
                            if (agmt_init_status === "Error (0) Total update in progress" ||
                                agmt_init_status === "Error (0)") {
                                agmt_init_status = "Initializing";
                            } else if (agmt_init_status === "Error (0) Total update succeeded") {
                                agmt_init_status = "Initialized";
                            }
                        } else if (agmt_attrs.nsds5replicalastinitstart[0] === "19700101000000Z") {
                            agmt_init_status = "Not Initialized";
                        } else if ('nsds5beginreplicarefresh' in agmt_attrs) {
                            agmt_init_status = "Initializing";
                        }

                        // Update table
                        rows.push([
                            agmt_attrs.cn[0],
                            agmt_attrs.nsds5replicahost[0],
                            agmt_attrs.nsds5replicaport[0],
                            state,
                            update_status,
                            agmt_init_status
                        ]);
                    }

                    // Set agmt
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            agmtRows: rows,
                        },
                        suffixSpinning: false,
                        disabled: false,
                    });
                })
                .fail(() => {
                    this.setState({
                        suffixSpinning: false,
                        disabled: false,
                    });
                });
    }

    reloadWinsyncAgmts (suffix) {
        this.setState({
            suffixSpinning: true,
            disabled: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-winsync-agmt", "list", "--suffix", suffix
        ];
        log_cmd("reloadWinsyncAgmts", "Get Winsync Agreements", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const obj = JSON.parse(content);
                    const ws_rows = [];
                    for (var idx in obj.items) {
                        let state = "Enabled";
                        let update_status = "";
                        let ws_agmt_init_status = "Initialized";
                        const agmt_attrs = obj.items[idx].attrs;
                        // let agmt_name = agmt_attrs['cn'][0];

                        // Compute state (enabled by default)
                        if ('nsds5replicaenabled' in agmt_attrs) {
                            if (agmt_attrs.nsds5replicaenabled[0].toLowerCase() === 'off') {
                                state = "Disabled";
                            }
                        }

                        if ('nsds5replicalastupdatestatus' in agmt_attrs) {
                            update_status = agmt_attrs.nsds5replicalastupdatestatus[0];
                        }

                        if ('nsds5replicalastinitstatus' in agmt_attrs &&
                            agmt_attrs.nsds5replicalastinitstatus[0] !== "") {
                            ws_agmt_init_status = agmt_attrs.nsds5replicalastinitstatus[0];
                            if (ws_agmt_init_status === "Error (0) Total update in progress" ||
                                ws_agmt_init_status === "Error (0)") {
                                ws_agmt_init_status = "Initializing";
                            } else if (ws_agmt_init_status === "Error (0) Total update succeeded") {
                                ws_agmt_init_status = "Initialized";
                            }
                        } else if ('nsds5replicalastinitstart' in agmt_attrs && agmt_attrs.nsds5replicalastinitstart[0] === "19700101000000Z") {
                            ws_agmt_init_status = "Not Initialized";
                        } else if ('nsds5beginreplicarefresh' in agmt_attrs) {
                            ws_agmt_init_status = "Initializing";
                        }

                        // Update table
                        ws_rows.push([
                            agmt_attrs.cn[0],
                            agmt_attrs.nsds5replicahost[0],
                            agmt_attrs.nsds5replicaport[0],
                            state,
                            update_status,
                            ws_agmt_init_status
                        ]);
                    }
                    // Set winsync agmts
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            winsyncRows: ws_rows,
                        },
                        suffixSpinning: false,
                        disabled: false,
                    });
                })
                .fail(() => {
                    this.setState({
                        suffixSpinning: false,
                        disabled: false,
                    });
                });
    }

    reloadConfig (suffix) {
        this.setState({
            suffixSpinning: true,
            disabled: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "get", "--suffix", suffix
        ];
        log_cmd("reloadConfig", "Reload suffix repl config", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let current_role = "";
                    let nsds5replicaprecisetombstonepurging = false;
                    if ('nsds5replicaprecisetombstonepurging' in config.attrs) {
                        if (config.attrs.nsds5replicaprecisetombstonepurging[0].toLowerCase() === "on") {
                            nsds5replicaprecisetombstonepurging = true;
                        }
                    }
                    // Set the replica role
                    if (config.attrs.nsds5replicatype[0] === "3") {
                        current_role = "Supplier";
                    } else {
                        if (config.attrs.nsds5flags[0] === "1") {
                            current_role = "Hub";
                        } else {
                            current_role = "Consumer";
                        }
                    }

                    this.setState({
                        [suffix]: {
                            role: current_role,
                            nsds5flags: config.attrs.nsds5flags[0],
                            nsds5replicatype: config.attrs.nsds5replicatype[0],
                            nsds5replicaid: 'nsds5replicaid' in config.attrs ? config.attrs.nsds5replicaid[0] : "",
                            nsds5replicabinddn: 'nsds5replicabinddn' in config.attrs ? config.attrs.nsds5replicabinddn : "",
                            nsds5replicabinddngroup: 'nsds5replicabinddngroup' in config.attrs ? config.attrs.nsds5replicabinddngroup[0] : "",
                            nsds5replicabinddngroupcheckinterval: 'nsds5replicabinddngroupcheckinterval' in config.attrs ? config.attrs.nsds5replicabinddngroupcheckinterval[0] : "",
                            nsds5replicareleasetimeout: 'nsds5replicareleasetimeout' in config.attrs ? config.attrs.nsds5replicareleasetimeout[0] : "",
                            nsds5replicapurgedelay: 'nsds5replicapurgedelay' in config.attrs ? config.attrs.nsds5replicapurgedelay[0] : "",
                            nsds5replicatombstonepurgeinterval: 'nsds5replicatombstonepurgeinterval' in config.attrs ? config.attrs.nsds5replicatombstonepurgeinterval[0] : "",
                            nsds5replicaprecisetombstonepurging: nsds5replicaprecisetombstonepurging,
                            nsds5replicaprotocoltimeout: 'nsds5replicaprotocoltimeout' in config.attrs ? config.attrs.nsds5replicaprotocoltimeout[0] : "",
                            nsds5replicabackoffmin: 'nsds5replicabackoffmin' in config.attrs ? config.attrs.nsds5replicabackoffmin[0] : "",
                            nsds5replicabackoffmax: 'nsds5replicabackoffmax' in config.attrs ? config.attrs.nsds5replicabackoffmax[0] : "",
                            nsds5replicakeepaliveupdateinterval: 'nsds5replicakeepaliveupdateinterval' in config.attrs ? config.attrs.nsds5replicakeepaliveupdateinterval[0] : "3600",
                        },
                        suffixSpinning: false,
                        disabled: false,
                        suffixKey: new Date(),
                    }, this.loadLDIFs);
                })
                .fail(() => {
                    this.setState({
                        suffixSpinning: false,
                        disabled: false,
                    });
                });
    }

    reloadRUV (suffix) {
        this.setState({
            suffixSpinning: true,
            disabled: true,
        });
        // Load suffix RUV
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'get-ruv', '--suffix=' + suffix];
        log_cmd('reloadRUV', 'Get the suffix RUV', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const ruvs = JSON.parse(content);
                    const ruv_rows = [];
                    for (const idx in ruvs.items) {
                        const ruv = ruvs.items[idx];
                        // Update table
                        ruv_rows.push({
                            rid: ruv.rid,
                            url: ruv.url,
                            csn: ruv.csn,
                            raw_csn: ruv.raw_csn,
                            maxcsn: ruv.maxcsn,
                            raw_maxcsn: ruv.raw_maxcsn,
                        });
                    }
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            ruvRows: ruv_rows,
                        },
                        suffixSpinning: false,
                        disabled: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (errMsg.desc !== "No such object") {
                        this.props.addNotification(
                            "error",
                            `Error loading suffix RUV - ${errMsg.desc}`
                        );
                    }
                    this.setState({
                        suffixSpinning: false,
                        disabled: false
                    });
                });
    }

    loadLDIFs() {
        const cmd = [
            "dsctl", "-j", this.props.serverId, "ldifs"
        ];
        log_cmd("loadLDIFs", "Load replication LDIF Files", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const rows = [];
                    for (const row of config.items) {
                        if (row[3].toLowerCase() === this.state.node_name.toLowerCase()) {
                            rows.push([row[0], row[1], row[2]]);
                        }
                    }
                    this.setState({
                        ldifRows: rows,
                    }, () => { this.update_tree_nodes() });
                });
    }

    loadReplSuffix(suffix) {
        // Load everything, we must nest cockpit promise so we can proper set
        // the loading is finished.
        // - Get Suffix config
        // - Get Changelog Settings
        // - Get Repl agmts
        // - Get Winsync Agmts
        // - Get RUV's
        this.setState({
            activeKey: 1,
            suffixLoading: true,
            [suffix]: {},
        });

        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "get", "--suffix", suffix
        ];
        log_cmd("loadReplSuffix", "Load suffix repl config", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let current_role = "";
                    let nsds5replicaprecisetombstonepurging = false;
                    if ('nsds5replicaprecisetombstonepurging' in config.attrs) {
                        if (config.attrs.nsds5replicaprecisetombstonepurging[0].toLowerCase() === "on") {
                            nsds5replicaprecisetombstonepurging = true;
                        }
                    }
                    // Set the replica role
                    if (config.attrs.nsds5replicatype[0] === "3") {
                        current_role = "Supplier";
                    } else {
                        if (config.attrs.nsds5flags[0] === "1") {
                            current_role = "Hub";
                        } else {
                            current_role = "Consumer";
                        }
                    }

                    this.setState({
                        [suffix]: {
                            role: current_role,
                            nsds5flags: config.attrs.nsds5flags[0],
                            nsds5replicatype: config.attrs.nsds5replicatype[0],
                            nsds5replicaid: 'nsds5replicaid' in config.attrs ? config.attrs.nsds5replicaid[0] : "",
                            nsds5replicabinddn: 'nsds5replicabinddn' in config.attrs ? config.attrs.nsds5replicabinddn : "",
                            nsds5replicabinddngroup: 'nsds5replicabinddngroup' in config.attrs ? config.attrs.nsds5replicabinddngroup[0] : "",
                            nsds5replicabinddngroupcheckinterval: 'nsds5replicabinddngroupcheckinterval' in config.attrs ? config.attrs.nsds5replicabinddngroupcheckinterval[0] : "",
                            nsds5replicareleasetimeout: 'nsds5replicareleasetimeout' in config.attrs ? config.attrs.nsds5replicareleasetimeout[0] : "",
                            nsds5replicapurgedelay: 'nsds5replicapurgedelay' in config.attrs ? config.attrs.nsds5replicapurgedelay[0] : "",
                            nsds5replicatombstonepurgeinterval: 'nsds5replicatombstonepurgeinterval' in config.attrs ? config.attrs.nsds5replicatombstonepurgeinterval[0] : "",
                            nsds5replicaprecisetombstonepurging: nsds5replicaprecisetombstonepurging,
                            nsds5replicaprotocoltimeout: 'nsds5replicaprotocoltimeout' in config.attrs ? config.attrs.nsds5replicaprotocoltimeout[0] : "",
                            nsds5replicabackoffmin: 'nsds5replicabackoffmin' in config.attrs ? config.attrs.nsds5replicabackoffmin[0] : "",
                            nsds5replicabackoffmax: 'nsds5replicabackoffmax' in config.attrs ? config.attrs.nsds5replicabackoffmax[0] : "",
                            nsds5replicakeepaliveupdateinterval: 'nsds5replicakeepaliveupdateinterval' in config.attrs ? config.attrs.nsds5replicakeepaliveupdateinterval[0] : "3600",
                            clMaxEntries: "",
                            clMaxAge: "",
                            clTrimInt: "",
                            clEncrypt: false,
                        }
                    }, this.loadLDIFs);

                    cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
                        'replication', 'get-changelog', '--suffix', suffix];
                    log_cmd("loadReplSuffix", "Load the replication info", cmd);
                    cockpit
                            .spawn(cmd, { superuser: true, err: "message" })
                            .done(content => {
                                const config = JSON.parse(content);
                                let clMaxEntries = "";
                                let clMaxAge = "";
                                let clTrimInt = "";
                                let clEncrypt = false;
                                for (const attr in config.attrs) {
                                    const val = config.attrs[attr][0];
                                    if (attr === "nsslapd-changelogmaxentries") {
                                        clMaxEntries = val;
                                    }
                                    if (attr === "nsslapd-changelogmaxage") {
                                        clMaxAge = val;
                                    }
                                    if (attr === "nsslapd-changelogtrim-interval") {
                                        clTrimInt = val;
                                    }
                                    if (attr === "nsslapd-encryptionalgorithm") {
                                        clEncrypt = true;
                                    }
                                }
                                this.setState({
                                    [suffix]: {
                                        ...this.state[suffix],
                                        clMaxEntries: clMaxEntries,
                                        clMaxAge: clMaxAge,
                                        clTrimInt: clTrimInt,
                                        clEncrypt: clEncrypt,
                                    }
                                });

                                // Now load agmts, then the winsync agreement, and finally the RUV
                                cmd = [
                                    "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                                    "repl-agmt", "list", "--suffix", suffix
                                ];
                                log_cmd("loadReplSuffix", "get repl agreements", cmd);
                                cockpit
                                        .spawn(cmd, { superuser: true, err: "message" })
                                        .done(content => {
                                            const obj = JSON.parse(content);
                                            const rows = [];
                                            for (const idx in obj.items) {
                                                const agmt_attrs = obj.items[idx].attrs;
                                                let state = "Enabled";
                                                let update_status = "";
                                                let agmt_init_status = "";

                                                // Compute state (enabled by default)
                                                if ('nsds5replicaenabled' in agmt_attrs) {
                                                    if (agmt_attrs.nsds5replicaenabled[0].toLowerCase() === 'off') {
                                                        state = "Disabled";
                                                    }
                                                }

                                                // Check for status msgs
                                                if ('nsds5replicalastupdatestatus' in agmt_attrs) {
                                                    update_status = agmt_attrs.nsds5replicalastupdatestatus[0];
                                                }
                                                if ('nsds5replicalastinitstatus' in agmt_attrs &&
                                                    agmt_attrs.nsds5replicalastinitstatus[0] !== "") {
                                                    agmt_init_status = agmt_attrs.nsds5replicalastinitstatus[0];
                                                    if (agmt_init_status === "Error (0) Total update in progress" ||
                                                        agmt_init_status === "Error (0)") {
                                                        agmt_init_status = "Initializing";
                                                    } else if (agmt_init_status === "Error (0) Total update succeeded") {
                                                        agmt_init_status = "Initialized";
                                                    }
                                                } else if ('nsds5replicalastinitstart' in agmt_attrs && agmt_attrs.nsds5replicalastinitstart[0] === "19700101000000Z") {
                                                    agmt_init_status = "Not Initialized";
                                                } else if ('nsds5beginreplicarefresh' in agmt_attrs) {
                                                    agmt_init_status = "Initializing";
                                                }

                                                // Update table
                                                rows.push([
                                                    agmt_attrs.cn[0],
                                                    agmt_attrs.nsds5replicahost[0],
                                                    agmt_attrs.nsds5replicaport[0],
                                                    state,
                                                    update_status,
                                                    agmt_init_status
                                                ]);
                                            }

                                            // Set agmt
                                            this.setState({
                                                [suffix]: {
                                                    ...this.state[suffix],
                                                    agmtRows: rows,
                                                }
                                            });

                                            // Load winsync agreements
                                            cmd = [
                                                "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                                                "repl-winsync-agmt", "list", "--suffix", suffix
                                            ];
                                            log_cmd("loadReplSuffix", "Get Winsync Agreements", cmd);
                                            cockpit
                                                    .spawn(cmd, { superuser: true, err: "message" })
                                                    .done(content => {
                                                        const obj = JSON.parse(content);
                                                        const ws_rows = [];
                                                        for (var idx in obj.items) {
                                                            let state = "Enabled";
                                                            let update_status = "";
                                                            let ws_agmt_init_status = "Initialized";
                                                            const agmt_attrs = obj.items[idx].attrs;
                                                            // let agmt_name = agmt_attrs['cn'][0];

                                                            // Compute state (enabled by default)
                                                            if ('nsds5replicaenabled' in agmt_attrs) {
                                                                if (agmt_attrs.nsds5replicaenabled[0].toLowerCase() === 'off') {
                                                                    state = "Disabled";
                                                                }
                                                            }

                                                            if ('nsds5replicalastupdatestatus' in agmt_attrs) {
                                                                update_status = agmt_attrs.nsds5replicalastupdatestatus[0];
                                                            }

                                                            if ('nsds5replicalastinitstatus' in agmt_attrs &&
                                                                agmt_attrs.nsds5replicalastinitstatus[0] !== "") {
                                                                ws_agmt_init_status = agmt_attrs.nsds5replicalastinitstatus[0];
                                                                if (ws_agmt_init_status === "Error (0) Total update in progress" ||
                                                                    ws_agmt_init_status === "Error (0)") {
                                                                    ws_agmt_init_status = "Initializing";
                                                                } else if (ws_agmt_init_status === "Error (0) Total update succeeded") {
                                                                    ws_agmt_init_status = "Initialized";
                                                                }
                                                            } else if ('nsds5replicalastinitstart' in agmt_attrs && agmt_attrs.nsds5replicalastinitstart[0] === "19700101000000Z") {
                                                                ws_agmt_init_status = "Not Initialized";
                                                            } else if ('nsds5beginreplicarefresh' in agmt_attrs) {
                                                                ws_agmt_init_status = "Initializing";
                                                            }

                                                            // Update table
                                                            ws_rows.push([
                                                                agmt_attrs.cn[0],
                                                                agmt_attrs.nsds5replicahost[0],
                                                                agmt_attrs.nsds5replicaport[0],
                                                                state,
                                                                update_status,
                                                                ws_agmt_init_status
                                                            ]);
                                                        }
                                                        // Set winsync agmts
                                                        this.setState({
                                                            [suffix]: {
                                                                ...this.state[suffix],
                                                                winsyncRows: ws_rows,
                                                            }
                                                        });

                                                        // Load suffix RUV
                                                        cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
                                                            'replication', 'get-ruv', '--suffix=' + suffix];
                                                        log_cmd('loadReplSuffix', 'Get the suffix RUV', cmd);
                                                        cockpit
                                                                .spawn(cmd, { superuser: true, err: "message" })
                                                                .done(content => {
                                                                    const ruvs = JSON.parse(content);
                                                                    const ruv_rows = [];
                                                                    for (const idx in ruvs.items) {
                                                                        const ruv = ruvs.items[idx];
                                                                        // Update table
                                                                        ruv_rows.push({
                                                                            rid: ruv.rid,
                                                                            url: ruv.url,
                                                                            csn: ruv.csn,
                                                                            raw_csn: ruv.raw_csn,
                                                                            maxcsn: ruv.maxcsn,
                                                                            raw_maxcsn: ruv.raw_maxcsn,
                                                                        });
                                                                    }

                                                                    this.setState({
                                                                        [suffix]: {
                                                                            ...this.state[suffix],
                                                                            ruvRows: ruv_rows,
                                                                        },
                                                                        suffixLoading: false,
                                                                        disableTree: false
                                                                    });
                                                                })
                                                                .fail(err => {
                                                                    const errMsg = JSON.parse(err);
                                                                    if (errMsg.desc !== "No such object") {
                                                                        this.props.addNotification(
                                                                            "error",
                                                                            `Error loading suffix RUV - ${errMsg.desc}`
                                                                        );
                                                                    }
                                                                    this.setState({
                                                                        suffixLoading: false,
                                                                        disableTree: false
                                                                    });
                                                                });
                                                    })
                                                    .fail(err => {
                                                        const errMsg = JSON.parse(err);
                                                        this.props.addNotification(
                                                            "error",
                                                            `Error loading winsync agreements - ${errMsg.desc}`
                                                        );
                                                        this.setState({
                                                            suffixLoading: false,
                                                            disableTree: false
                                                        });
                                                    });
                                        })
                                        .fail(err => {
                                            const errMsg = JSON.parse(err);
                                            this.props.addNotification(
                                                "error",
                                                `Error loading replication agreements configuration - ${errMsg.desc}`
                                            );
                                            this.setState({
                                                suffixLoading: false,
                                                disableTree: false
                                            });
                                        });
                            })
                            .fail(err => {
                                // changelog failure
                                const errMsg = JSON.parse(err);
                                this.props.addNotification(
                                    "error",
                                    `Error loading replication changelog configuration - ${errMsg.desc}`
                                );
                                this.setState({
                                    suffixLoading: false,
                                    disableTree: false
                                });
                            });
                })
                .fail(() => {
                    this.setState({
                        suffixLoading: false,
                        disableTree: false,
                        node_replicated: false
                    });
                });
    }

    loadAttrs() {
        // Now get the schema that various tabs use
        const attr_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema", "attributetypes", "list"
        ];
        log_cmd("Suffixes", "Get attrs", attr_cmd);
        cockpit
                .spawn(attr_cmd, { superuser: true, err: "message" })
                .done(content => {
                    const attrContent = JSON.parse(content);
                    const attrs = [];
                    for (const content of attrContent.items) {
                        attrs.push(content.name[0]);
                    }
                    this.setState({
                        attributes: attrs,
                    });
                });
    }

    enableTree () {
        this.setState({
            disableTree: false
        });
    }

    disableTree () {
        this.setState({
            disableTree: true
        });
    }

    render() {
        const { nodes } = this.state;
        let repl_page = "";
        let repl_element = "";
        let disabled = "tree-view-container";
        if (this.state.disableTree) {
            disabled = "tree-view-container ds-disabled";
        }
        if (!this.state.loaded) {
            repl_page =
                <div className="ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>Loading Replication Information ...</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top-lg" size="xl" />
                </div>;
        } else {
            if (this.state.suffixLoading) {
                repl_element =
                    <div className="ds-margin-top-xlg ds-center">
                        <TextContent>
                            <Text component={TextVariants.h3}>Loading Replication Configuration For <b>{this.state.node_name} ...</b></Text>
                        </TextContent>
                        <Spinner className="ds-margin-top-lg" size="xl" />
                    </div>;
            } else {
                if (this.state.node_name in this.state) {
                    repl_element =
                        <div>
                            <ReplSuffix
                                serverId={this.props.serverId}
                                suffix={this.state.node_name}
                                role={this.state[this.state.node_name].role}
                                data={this.state[this.state.node_name]}
                                addNotification={this.props.addNotification}
                                agmtRows={this.state[this.state.node_name].agmtRows}
                                winsyncRows={this.state[this.state.node_name].winsyncRows}
                                ruvRows={this.state[this.state.node_name].ruvRows}
                                ldifRows={this.state.ldifRows}
                                reloadAgmts={this.reloadAgmts}
                                reloadWinsyncAgmts={this.reloadWinsyncAgmts}
                                reloadRUV={this.reloadRUV}
                                reloadConfig={this.reloadConfig}
                                reloadLDIF={this.loadLDIFs}
                                reload={this.loadSuffixTree}
                                attrs={this.state.attributes}
                                replicated={this.state.node_replicated}
                                enableTree={this.enableTree}
                                disableTree={this.disableTree}
                                key={this.state.suffixKey}
                                disabled={this.state.disabled}
                                spinning={this.state.suffixSpinning}
                            />
                        </div>;
                } else {
                    // Suffix is not replicated
                    repl_element =
                        <ReplSuffix
                            serverId={this.props.serverId}
                            suffix={this.state.node_name}
                            role=""
                            data=""
                            addNotification={this.props.addNotification}
                            disableWSAgmtTable={this.state.disableWSAgmtTable}
                            disableAgmtTable={this.state.disableAgmtTable}
                            reloadAgmts={this.reloadAgmts}
                            reloadWinsyncAgmts={this.reloadWinsyncAgmts}
                            reloadRUV={this.reloadRUV}
                            reloadLDIF={this.loadLDIFs}
                            reloadConfig={this.reloadConfig}
                            reload={this.loadSuffixTree}
                            attrs={this.state.attributes}
                            replicated={this.state.node_replicated}
                            enableTree={this.enableTree}
                            disableTree={this.disableTree}
                            spinning={this.state.suffixSpinning}
                            disabled={this.state.disabled}
                            ldifRows={this.state.ldifRows}
                            key={this.state.node_name}
                        />;
                }
            }
            repl_page =
                <div className="container-fluid">
                    <div className="ds-container">
                        <div>
                            <div className="ds-tree">
                                <div className={disabled} id="repl-tree">
                                    <TreeView
                                        data={nodes}
                                        activeItems={this.state.activeItems}
                                        onSelect={this.handleTreeClick}
                                    />
                                </div>
                            </div>
                        </div>
                        <div className="ds-tree-content">
                            {repl_element}
                        </div>
                    </div>
                </div>;
        }

        return (
            <div>
                {repl_page}
            </div>
        );
    }
}

// Property types and defaults

Replication.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string
};

Replication.defaultProps = {
    serverId: ""
};

export default Replication;
