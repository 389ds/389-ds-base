import cockpit from "cockpit";
import React from "react";
import { NotificationController } from "./lib/notifications.jsx";
import { log_cmd } from "./lib/tools.jsx";
import {
    ChainingConfig,
    ChainingDatabaseConfig
} from "./lib/database/chaining.jsx";
import { GlobalDatabaseConfig } from "./lib/database/databaseConfig.jsx";
import { Suffix } from "./lib/database/suffix.jsx";
import { Backups } from "./lib/database/backups.jsx";
import { GlobalPwPolicy } from "./lib/database/globalPwp.jsx";
import { LocalPwPolicy } from "./lib/database/localPwp.jsx";
import {
    Modal,
    Icon,
    Form,
    Row,
    Col,
    ControlLabel,
    Button,
    noop,
    TreeView,
    Radio,
    Spinner
} from "patternfly-react";
import PropTypes from "prop-types";
import "./css/ds.css";

const DB_CONFIG = "dbconfig";
const CHAINING_CONFIG = "chaining-config";
const BACKUP_CONFIG = "backups";
const PWP_CONFIG = "pwpolicy";
const LOCAL_PWP_CONFIG = "localpwpolicy";
const treeViewContainerStyles = {
    width: '295px',
};

export class Database extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            notifications: [],
            errObj: {},
            nodes: [],
            node_name: "",
            node_text: "",
            dbtype: "",
            showSuffixModal: false,
            createSuffix: "",
            createBeName: "",
            createSuffixEntry: false,
            createSampleEntries: false,
            noSuffixInit: true,
            disableTree: false,

            // DB config
            globalDBConfig: {},
            configUpdated: 0,
            // Chaining Config
            chainingConfig: {},
            chainingUpdated: 0,
            // Chaining Link
            chainingLoading: false,
            // Suffix
            suffixLoading: false,
            attributes: [],
            // Loaded suffix configurations
            suffix: {},
            // Other
            LDIFRows: [],
            BackupRows: [],
            suffixList: [],
            loaded: false,
        };

        // General
        this.selectNode = this.selectNode.bind(this);
        this.removeNotification = this.removeNotification.bind(this);
        this.addNotification = this.addNotification.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleRadioChange = this.handleRadioChange.bind(this);
        this.loadGlobalConfig = this.loadGlobalConfig.bind(this);
        this.loadLDIFs = this.loadLDIFs.bind(this);
        this.loadBackups = this.loadBackups.bind(this);
        this.loadSuffixList = this.loadSuffixList.bind(this);

        // Suffix
        this.showSuffixModal = this.showSuffixModal.bind(this);
        this.closeSuffixModal = this.closeSuffixModal.bind(this);
        this.createSuffix = this.createSuffix.bind(this);
        this.loadSuffix = this.loadSuffix.bind(this);
        this.loadSuffixConfig = this.loadSuffixConfig.bind(this);
        this.loadIndexes = this.loadIndexes.bind(this);
        this.loadVLV = this.loadVLV.bind(this);
        this.loadAttrEncrypt = this.loadAttrEncrypt.bind(this);
        this.loadReferrals = this.loadReferrals.bind(this);
        this.getAutoTuning = this.getAutoTuning.bind(this);
        this.loadAttrs = this.loadAttrs.bind(this);

        // ChainingConfig
        this.loadChainingLink = this.loadChainingLink.bind(this);
        this.loadChainingConfig = this.loadChainingConfig.bind(this);
        this.loadAvailableControls = this.loadAvailableControls.bind(this);
        this.loadDefaultConfig = this.loadDefaultConfig.bind(this);

        // Other
        this.loadSuffixTree = this.loadSuffixTree.bind(this);
        this.enableTree = this.enableTree.bind(this);
    }

    componentWillMount () {
        if (!this.state.loaded) {
            this.loadGlobalConfig();
            this.loadChainingConfig();
            this.loadLDIFs();
            this.loadBackups();
            this.loadSuffixList();
        }
    }

    componentDidMount() {
        this.loadSuffixTree(false);
    }

    componentDidUpdate(prevProps) {
        if (this.props.serverId !== prevProps.serverId) {
            this.loadSuffixTree(false);
        }
    }

    loadSuffixList () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "suffix", "list", "--suffix"
        ];
        log_cmd("loadSuffixList", "Get a list of all the suffixes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const suffixList = JSON.parse(content);
                    this.setState(() => (
                        {suffixList: suffixList.items}
                    ));
                });
    }

    loadGlobalConfig () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "config", "get"
        ];
        log_cmd("loadGlobalConfig", "Load the database global configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    let db_cache_auto = false;
                    let import_cache_auto = false;
                    let dbhome = "";

                    if ('nsslapd-db-home-directory' in attrs) {
                        dbhome = attrs['nsslapd-db-home-directory'];
                    }
                    if (attrs['nsslapd-cache-autosize'] != "0") {
                        db_cache_auto = true;
                    }
                    if (attrs['nsslapd-import-cache-autosize'] != "0") {
                        import_cache_auto = true;
                    }

                    this.setState(() => (
                        {
                            globalDBConfig:
                                {
                                    loading: false,
                                    db_cache_auto: db_cache_auto,
                                    import_cache_auto: import_cache_auto,
                                    looklimit: attrs['nsslapd-lookthroughlimit'],
                                    idscanlimit: attrs['nsslapd-idlistscanlimit'],
                                    pagelooklimit: attrs['nsslapd-pagedlookthroughlimit'],
                                    pagescanlimit: attrs['nsslapd-pagedidlistscanlimit'],
                                    rangelooklimit: attrs['nsslapd-rangelookthroughlimit'],
                                    autosize: attrs['nsslapd-cache-autosize'],
                                    autosizesplit: attrs['nsslapd-cache-autosize-split'],
                                    dbcachesize: attrs['nsslapd-dbcachesize'],
                                    txnlogdir: attrs['nsslapd-db-logdirectory'],
                                    dbhomedir: dbhome,
                                    dblocks: attrs['nsslapd-db-locks'],
                                    chxpoint: attrs['nsslapd-db-checkpoint-interval'],
                                    compactinterval: attrs['nsslapd-db-compactdb-interval'],
                                    importcacheauto: attrs['nsslapd-import-cache-autosize'],
                                    importcachesize: attrs['nsslapd-import-cachesize'],
                                },
                            configUpdated: 1
                        }), this.setState({configUpdated: 0}));
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.addNotification(
                        "error",
                        `Error loading database configuration - ${errMsg.desc}`
                    );
                });
    }

    loadAvailableControls () {
        // Get the available control oids from rootdse
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-get", "--avail-controls"
        ];
        log_cmd("loadChainingConfig", "Get available controls", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let availableOids = config.items.filter((el) => !this.state.chainingConfig.oidList.includes(el));
                    this.setState((prevState) => (
                        {
                            chainingConfig: {
                                ...this.state.chainingConfig,
                                availableOids: availableOids,
                            },
                            chainingUpdated: 1
                        }
                    ), this.setState({chainingUpdated: 0})
                    );
                });
    }

    loadDefaultConfig() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-get-def"
        ];
        log_cmd("loadChainingConfig", "Load chaining default configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attr = config.attrs;
                    let checkAci = false;
                    let proxy = false;
                    let refOnScope = false;
                    let useStartTLS = false;

                    if (attr['nschecklocalaci'] == "on") {
                        checkAci = true;
                    }
                    if (attr['nsproxiedauthorization'] == "on") {
                        proxy = true;
                    }
                    if (attr['nsreferralonscopedsearch'] == "on") {
                        refOnScope = true;
                    }
                    if (attr['nsusestarttls'] == "on") {
                        useStartTLS = true;
                    }
                    this.setState(() => (
                        {
                            chainingConfig: {
                                ...this.state.chainingConfig,
                                defSearchCheck: attr['nsabandonedsearchcheckinterval'],
                                defBindConnLimit: attr['nsbindconnectionslimit'],
                                defBindTimeout: attr['nsbindtimeout'],
                                defBindRetryLimit: attr['nsbindretrylimit'],
                                defConcurLimit: attr['nsconcurrentbindlimit'],
                                defConcurOpLimit: attr['nsconcurrentoperationslimit'],
                                defConnLife: attr['nsconnectionlife'],
                                defHopLimit: attr['nshoplimit'],
                                defDelay: attr['nsmaxresponsedelay'],
                                defTestDelay: attr['nsmaxtestresponsedelay'],
                                defOpConnLimit: attr['nsoperationconnectionslimit'],
                                defSizeLimit: attr['nsslapd-sizelimit'],
                                defTimeLimit: attr['nsslapd-timelimit'],
                                defProxy: proxy,
                                defRefOnScoped: refOnScope,
                                defCheckAci: checkAci,
                                defUseStartTLS: useStartTLS,
                            }
                        }
                    ), this.loadAvailableControls());
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.addNotification(
                        "error",
                        `Error loading default chaining configuration - ${errMsg.desc}`
                    );
                    this.setState({
                        loading: false
                    });
                });
    }

    loadChainingConfig() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-get"
        ];
        log_cmd("loadChainingConfig", "Load chaining OIDs and Controls", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let availableComps = config.attrs.nspossiblechainingcomponents;
                    let compList = [];
                    let oidList = [];
                    if ('nsactivechainingcomponents' in config.attrs) {
                        availableComps = config.attrs.nspossiblechainingcomponents.filter((el) => !config.attrs.nsactivechainingcomponents.includes(el));
                        compList = config.attrs.nsactivechainingcomponents;
                    }
                    if ('nstransmittedcontrols' in config.attrs) {
                        oidList = config.attrs.nstransmittedcontrols;
                    }
                    this.setState(() => (
                        {
                            chainingConfig: {
                                ...this.state.chainingConfig,
                                oidList: oidList,
                                compList: compList,
                                availableComps: availableComps
                            }
                        }
                    ), this.loadDefaultConfig());
                });
    }

    loadSuffixTree(fullReset) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "get-tree",
        ];
        log_cmd("loadSuffixTree", "Start building the suffix tree", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let treeData = [];
                    if (content != "") {
                        treeData = JSON.parse(content);
                    }
                    let basicData = [
                        {
                            text: "Global Database Configuration",
                            selectable: true,
                            selected: true,
                            icon: "pficon-settings",
                            id: "dbconfig",

                        },
                        {
                            text: "Chaining Configuration",
                            icon: "glyphicon glyphicon-link",
                            selectable: true,
                            id: "chaining-config",
                        },
                        {
                            text: "Backups & LDIFS",
                            icon: "glyphicon glyphicon-duplicate",
                            selectable: true,
                            id: "backups",
                        },
                        {
                            text: "Password Policies",
                            icon: "pficon-key",
                            selectable: false,
                            state: {"expanded": true},
                            "nodes": [
                                {
                                    text: "Global Policy",
                                    icon: "glyphicon glyphicon-globe",
                                    selectable: true,
                                    id: "pwpolicy",
                                },
                                {
                                    text: "Local Policies",
                                    icon: "pficon-home",
                                    selectable: true,
                                    id: "localpwpolicy",
                                },
                            ]
                        },
                        {
                            text: "Suffixes",
                            icon: "pficon-catalog",
                            state: {"expanded": true},
                            selectable: false,
                            nodes: []
                        }
                    ];
                    let current_node = this.state.node_name;
                    if (fullReset) {
                        current_node = DB_CONFIG;
                    }
                    basicData[4].nodes = treeData;

                    this.setState(() => ({
                        nodes: basicData,
                        node_name: current_node,
                    }), this.update_tree_nodes);
                });
    }

    loadChainingLink(suffix) {
        this.setState({
            chainingLoading: true,
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "link-get", suffix
        ];
        log_cmd("loadChainingLink", "Load chaining link configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    let bindmech = "Simple";
                    let usestarttls = false;
                    let refOnScope = false;
                    let proxiedAuth = false;
                    let checkLocalAci = false;

                    // Handler checkboxes, need to convert "on" to true
                    if (config.attrs.nsreferralonscopedsearch[0] == "on") {
                        refOnScope = true;
                    }
                    if (config.attrs.nsproxiedauthorization[0] == "on") {
                        proxiedAuth = true;
                    }
                    if (config.attrs.nschecklocalaci[0] == "on") {
                        checkLocalAci = true;
                    }
                    if (config.attrs.nsusestarttls[0] == "on") {
                        usestarttls = true;
                    }
                    if (config.attrs.nsbindmechanism !== undefined) {
                        bindmech = config.attrs.nsbindmechanism[0];
                    }

                    this.setState({
                        [suffix]: {
                            nsfarmserverurl: attrs.nsfarmserverurl[0],
                            nsmultiplexorbinddn: attrs.nsmultiplexorbinddn[0],
                            nsmultiplexorcredentials: attrs.nsmultiplexorcredentials[0],
                            nsmultiplexorcredentials_confirm: attrs.nsmultiplexorcredentials[0],
                            sizelimit: attrs['nsslapd-sizelimit'][0],
                            timelimit: attrs['nsslapd-timelimit'][0],
                            bindconnlimit: attrs.nsbindconnectionslimit[0],
                            opconnlimit: attrs.nsoperationconnectionslimit[0],
                            concurrbindlimit: attrs.nsconcurrentbindlimit[0],
                            bindtimeout: attrs.nsbindtimeout[0],
                            bindretrylimit: attrs.nsbindretrylimit[0],
                            concurroplimit: attrs.nsconcurrentoperationslimit[0],
                            connlifetime: attrs.nsconnectionlife[0],
                            searchcheckinterval: attrs.nsabandonedsearchcheckinterval[0],
                            hoplimit: attrs.nshoplimit[0],
                            nsbindmechanism: bindmech,
                            nsusestarttls: usestarttls,
                            nsreferralonscopedsearch: refOnScope,
                            nsproxiedauthorization: proxiedAuth,
                            nschecklocalaci: checkLocalAci,
                        },
                        chainingLoading: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.addNotification(
                        "error",
                        `Error getting chaining link configuration - ${errMsg.desc}`
                    );
                });
    }

    selectNode(selectedNode) {
        if (selectedNode.selected) {
            return;
        }
        this.setState({
            disableTree: true // Disable the tree to allow node to be fully loaded
        });

        if (selectedNode.id == "dbconfig" ||
            selectedNode.id == "chaining-config" ||
            selectedNode.id == "pwpolicy" ||
            selectedNode.id == "localpwpolicy" ||
            selectedNode.id == "backups") {
            // Nothing special to do, these configurations have already been loaded
            this.setState(prevState => {
                return {
                    nodes: this.nodeSelector(prevState.nodes, selectedNode),
                    node_name: selectedNode.id,
                    node_text: selectedNode.text,
                    dbtype: selectedNode.type,
                    bename: "",
                };
            });
        } else {
            if (selectedNode.id in this.state) {
                // This suffix is already cached, just use what we have...
                this.setState(prevState => {
                    return {
                        nodes: this.nodeSelector(prevState.nodes, selectedNode),
                        node_name: selectedNode.id,
                        node_text: selectedNode.text,
                        dbtype: selectedNode.type,
                        bename: selectedNode.be,
                    };
                });
            } else {
                // Load this suffix whatever it is...
                if (selectedNode.type == "dblink") {
                    // Chained suffix
                    this.loadChainingLink(selectedNode.id);
                } else {
                    // Suffix/subsuffix
                    this.loadSuffix(selectedNode.id);
                }
                this.setState(prevState => {
                    return {
                        nodes: this.nodeSelector(prevState.nodes, selectedNode),
                        node_name: selectedNode.id,
                        node_text: selectedNode.text,
                        dbtype: selectedNode.type,
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

    update_tree_nodes() {
        // Set title to the text value of each suffix node.  We need to do this
        // so we can read long suffixes in the UI tree div
        let elements = document.getElementsByClassName('treeitem-row');
        for (let el of elements) {
            el.setAttribute('title', el.innerText);
        }
        this.setState({
            disableTree: false,
        }, this.loadAttrs());
    }

    showSuffixModal () {
        this.setState({
            showSuffixModal: true,
            createSuffixEntry: false,
            createSampleEntries: false,
            noSuffixInit: true,
            errObj: {},
        });
    }

    handleRadioChange(e) {
        // Handle the create suffix init option radio button group
        let noInit = false;
        let addSuffix = false;
        let addSample = false;
        if (e.target.id == "noSuffixInit") {
            noInit = true;
        } else if (e.target.id == "createSuffixEntry") {
            addSuffix = true;
        } else { // createSampleEntries
            addSample = true;
        }
        this.setState({
            noSuffixInit: noInit,
            createSuffixEntry: addSuffix,
            createSampleEntries: addSample
        });
    }

    handleChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
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

    closeSuffixModal() {
        this.setState({
            showSuffixModal: false
        });
    }

    createSuffix() {
        // validate
        let errors = false;
        let missingArgs = {
            createSuffix: false,
            createBeName: false,
        };

        if (this.state.createSuffix == "") {
            this.addNotification(
                "warning",
                `Missing the suffix DN`
            );
            missingArgs.createSuffix = true;
            errors = true;
        }
        if (this.state.createBeName == "") {
            this.addNotification(
                "warning",
                `Missing the suffix backend name`
            );
            missingArgs.createBeName = true;
            errors = true;
        }
        if (errors) {
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        // Create a new suffix
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "create", "--be-name", this.state.createBeName, '--suffix', this.state.createSuffix,
        ];
        if (this.state.createSampleEntries) {
            cmd.push('--create-entries');
        }
        if (this.state.createSuffixEntry) {
            cmd.push('--create-suffix');
        }

        log_cmd("createSuffix", "Create a new backend", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeSuffixModal();
                    this.addNotification(
                        "success",
                        `Successfully create new suffix`
                    );
                    // Refresh tree
                    this.loadSuffixTree(false);
                    this.loadSuffixList();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.addNotification(
                        "error",
                        `Error creating suffix - ${errMsg.desc}`
                    );
                    this.closeSuffixModal();
                });
    }

    getAutoTuning(suffix) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "config", "get"
        ];
        log_cmd("getAutoTuning", "Check cache auto tuning", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    if ('nsslapd-cache-autosize' in config.attrs &&
                        config.attrs['nsslapd-cache-autosize'] != "0") {
                        this.setState({
                            [suffix]: {
                                ...this.state[suffix],
                                autoTuning: true,
                            },
                        });
                    }
                });
    }

    loadSuffixConfig(suffix) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "suffix", "get", suffix
        ];
        log_cmd("loadSuffixConfig", "Load suffix config", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let refs = [];
                    let readonly = false;
                    let requireindex = false;
                    if ('nsslapd-referral' in config.attrs) {
                        refs = config.attrs['nsslapd-referral'];
                    }
                    if ('nsslapd-readonly' in config.attrs) {
                        if (config.attrs['nsslapd-readonly'] == "on") {
                            readonly = true;
                        }
                    }
                    if ('nsslapd-require-index' in config.attrs) {
                        if (config.attrs['nsslapd-require-index'] == "on") {
                            requireindex = true;
                        }
                    }
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            refRows: refs,
                            cachememsize: config.attrs['nsslapd-cachememsize'][0],
                            cachesize: config.attrs['nsslapd-cachesize'][0],
                            dncachememsize: config.attrs['nsslapd-dncachememsize'][0],
                            readOnly: readonly,
                            requireIndex: requireindex,
                        }
                    }, this.getAutoTuning(suffix));
                });
    }

    loadVLV(suffix) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "list", suffix
        ];
        log_cmd("loadVLV", "Load VLV indexes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            vlvItems: config.items,
                        }
                    });
                });
    }

    loadAttrEncrypt(suffix) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "attr-encrypt", "--list", "--just-names", suffix
        ];
        log_cmd("loadAttrEncrypt", "Load encrypted attrs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let rows = [];
                    for (let row of config.items) {
                        rows.push({"name": row});
                    }
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            encAttrsRows: rows
                        }
                    });
                });
    }

    loadIndexes(suffix) {
        // Load the suffix configurations.  Start with indexes which is the
        // most time consuming (and which controls the spinner), and spawn new
        // commands for the other areas.
        const index_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "list", suffix
        ];
        log_cmd("loadIndexes", "Load backend indexes", index_cmd);
        cockpit
                .spawn(index_cmd, { superuser: true, err: "message" })
                .done(content => {
                    // Now do the Indexes
                    const config = JSON.parse(content);
                    let rows = [];
                    let systemRows = [];
                    for (let item of config.items) {
                        let index = item.attrs;
                        let types = [];
                        let mrs = [];
                        if (index.nsindextype.length > 1) {
                            types = index.nsindextype.join(', ');
                        } else {
                            types = index.nsindextype[0];
                        }
                        if ('nsmatchingrule' in index) {
                            if (index.nsmatchingrule.length > 1) {
                                mrs = index.nsmatchingrule.join(', ');
                            } else {
                                mrs = index.nsmatchingrule[0];
                            }
                        }
                        if (index.nssystemindex[0] == 'true') {
                            systemRows.push({'name': index.cn, 'types': [types], 'matchingrules': [mrs]});
                        } else {
                            rows.push({'name': index.cn, 'types': [types], 'matchingrules': [mrs]});
                        }
                    }
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            indexRows: rows,
                            systemIndexRows: systemRows
                        }
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.addNotification(
                        "error",
                        `Error loading indexes for ${suffix} - ${errMsg.desc}`
                    );
                });
    }

    loadReferrals(suffix) {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "suffix", "get", suffix
        ];
        log_cmd("loadReferrals", "get referrals", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let refs = [];
                    if ('nsslapd-referral' in config.attrs) {
                        refs = config.attrs['nsslapd-referral'];
                    }
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            refRows: refs,
                        }
                    });
                });
    }

    loadSuffix(suffix) {
        // Load everything, we must nest cockpit promise so we can proper set
        // the loading is finished
        this.setState({
            activeKey: 1,
            suffixLoading: true
        }, this.loadAttrs());

        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "suffix", "get", suffix
        ];
        log_cmd("loadSuffix", "Load suffix config", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let refs = [];
                    let readonly = false;
                    let requireindex = false;
                    if ('nsslapd-referral' in config.attrs) {
                        refs = config.attrs['nsslapd-referral'];
                    }
                    if ('nsslapd-readonly' in config.attrs) {
                        if (config.attrs['nsslapd-readonly'] == "on") {
                            readonly = true;
                        }
                    }
                    if ('nsslapd-require-index' in config.attrs) {
                        if (config.attrs['nsslapd-require-index'] == "on") {
                            requireindex = true;
                        }
                    }
                    this.setState({
                        [suffix]: {
                            refRows: refs,
                            cachememsize: config.attrs['nsslapd-cachememsize'][0],
                            cachesize: config.attrs['nsslapd-cachesize'][0],
                            dncachememsize: config.attrs['nsslapd-dncachememsize'][0],
                            readOnly: readonly,
                            requireIndex: requireindex,
                        }
                    }, this.getAutoTuning(suffix));

                    // Now load VLV indexes
                    let cmd = [
                        "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                        "backend", "vlv-index", "list", suffix
                    ];
                    log_cmd("loadVLV", "Load VLV indexes", cmd);
                    cockpit
                            .spawn(cmd, { superuser: true, err: "message" })
                            .done(content => {
                                const config = JSON.parse(content);
                                this.setState({
                                    [suffix]: {
                                        ...this.state[suffix],
                                        vlvItems: config.items,
                                    }
                                });

                                let cmd = [
                                    "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                                    "backend", "attr-encrypt", "--list", "--just-names", suffix
                                ];
                                log_cmd("loadAttrEncrypt", "Load encrypted attrs", cmd);
                                cockpit
                                        .spawn(cmd, { superuser: true, err: "message" })
                                        .done(content => {
                                            const config = JSON.parse(content);
                                            let rows = [];
                                            for (let row of config.items) {
                                                rows.push({"name": row});
                                            }
                                            this.setState({
                                                [suffix]: {
                                                    ...this.state[suffix],
                                                    encAttrsRows: rows
                                                }
                                            });
                                            const index_cmd = [
                                                "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                                                "backend", "index", "list", suffix
                                            ];
                                            log_cmd("loadIndexes", "Load backend indexes", index_cmd);
                                            cockpit
                                                    .spawn(index_cmd, { superuser: true, err: "message" })
                                                    .done(content => {
                                                        // Now do the Indexes
                                                        const config = JSON.parse(content);
                                                        let rows = [];
                                                        let systemRows = [];
                                                        for (let item of config.items) {
                                                            let index = item.attrs;
                                                            let types = [];
                                                            let mrs = [];
                                                            if (index.nsindextype.length > 1) {
                                                                types = index.nsindextype.join(', ');
                                                            } else {
                                                                types = index.nsindextype[0];
                                                            }
                                                            if ('nsmatchingrule' in index) {
                                                                if (index.nsmatchingrule.length > 1) {
                                                                    mrs = index.nsmatchingrule.join(', ');
                                                                } else {
                                                                    mrs = index.nsmatchingrule[0];
                                                                }
                                                            }
                                                            if (index.nssystemindex[0] == 'true') {
                                                                systemRows.push({'name': index.cn, 'types': [types], 'matchingrules': [mrs]});
                                                            } else {
                                                                rows.push({'name': index.cn, 'types': [types], 'matchingrules': [mrs]});
                                                            }
                                                        }
                                                        this.setState({
                                                            [suffix]: {
                                                                ...this.state[suffix],
                                                                indexRows: rows,
                                                                systemIndexRows: systemRows,
                                                            },
                                                            suffixLoading: false
                                                        });
                                                    })
                                                    .fail(err => {
                                                        let errMsg = JSON.parse(err);
                                                        this.addNotification(
                                                            "error",
                                                            `Error loading indexes for ${suffix} - ${errMsg.desc}`
                                                        );
                                                        this.setState({
                                                            suffixLoading: false
                                                        });
                                                    });
                                        })
                                        .fail(err => {
                                            let errMsg = JSON.parse(err);
                                            this.addNotification(
                                                "error",
                                                `Error attribute encryption for ${suffix} - ${errMsg.desc}`
                                            );
                                            this.setState({
                                                suffixLoading: false
                                            });
                                        });
                            })
                            .fail(err => {
                                let errMsg = JSON.parse(err);
                                this.addNotification(
                                    "error",
                                    `Error loading VLV indexes for ${suffix} - ${errMsg.desc}`
                                );
                                this.setState({
                                    suffixLoading: false
                                });
                            });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.addNotification(
                        "error",
                        `Error loading config for ${suffix} - ${errMsg.desc}`
                    );
                    this.setState({
                        suffixLoading: false
                    });
                });
    }

    loadLDIFs() {
        const cmd = [
            "dsctl", "-j", this.props.serverId, "ldifs"
        ];
        log_cmd("loadLDIFs", "Load LDIF Files", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let rows = [];
                    for (let row of config.items) {
                        rows.push({'name': row[0], 'date': [row[1]], 'size': [row[2]], 'suffix': [row[3]]});
                    }
                    this.setState({
                        LDIFRows: rows,
                    });
                });
    }

    loadBackups() {
        const cmd = [
            "dsctl", "-j", this.props.serverId, "backups"
        ];
        log_cmd("loadBackups", "Load Backups", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let rows = [];
                    for (let row of config.items) {
                        rows.push({'name': row[0], 'date': [row[1]], 'size': [row[2]]});
                    }
                    this.setState({
                        BackupRows: rows
                    }, this.loadLDIFs());
                });
    }

    loadAttrs() {
        // Now get the schema that various tabs use
        const attr_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema", "attributetypes", "list"
        ];
        log_cmd("loadAttrs", "Get attrs", attr_cmd);
        cockpit
                .spawn(attr_cmd, { superuser: true, err: "message" })
                .done(content => {
                    let attrContent = JSON.parse(content);
                    let attrs = [];
                    for (let content of attrContent['items']) {
                        attrs.push(content.name[0]);
                    }
                    this.setState({
                        attributes: attrs,
                        loaded: true
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.addNotification(
                        "error",
                        `Failed to get attributes - ${errMsg.desc}`
                    );
                });
    }

    enableTree () {
        this.setState({
            disableTree: false
        });
    }

    render() {
        const { nodes } = this.state;
        let db_element = "";
        let body = "";
        let disabled = "tree-view-container";
        if (this.state.disableTree) {
            disabled = "tree-view-container ds-disabled";
        }

        if (this.state.loaded) {
            if (this.state.node_name == DB_CONFIG || this.state.node_name == "") {
                db_element =
                    <GlobalDatabaseConfig
                        serverId={this.props.serverId}
                        addNotification={this.addNotification}
                        reload={this.loadGlobalConfig}
                        data={this.state.globalDBConfig}
                        enableTree={this.enableTree}
                        key={this.state.configUpdated}
                    />;
            } else if (this.state.node_name == CHAINING_CONFIG) {
                db_element =
                    <ChainingDatabaseConfig
                        serverId={this.props.serverId}
                        addNotification={this.addNotification}
                        reload={this.loadChainingConfig}
                        data={this.state.chainingConfig}
                        enableTree={this.enableTree}
                        key={this.state.chainingUpdated}
                    />;
            } else if (this.state.node_name == PWP_CONFIG) {
                db_element =
                    <GlobalPwPolicy
                        serverId={this.props.serverId}
                        addNotification={this.addNotification}
                        attrs={this.state.attributes}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name == LOCAL_PWP_CONFIG) {
                db_element =
                    <LocalPwPolicy
                        serverId={this.props.serverId}
                        addNotification={this.addNotification}
                        attrs={this.state.attributes}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name == BACKUP_CONFIG) {
                db_element =
                    <Backups
                        serverId={this.props.serverId}
                        addNotification={this.addNotification}
                        backups={this.state.BackupRows}
                        suffixes={this.state.suffixList}
                        ldifs={this.state.LDIFRows}
                        enableTree={this.enableTree}
                        reload={this.loadBackups}
                    />;
            } else if (this.state.node_name != "") {
                // We have a suffix, or database link
                if (this.state.dbtype == "suffix" || this.state.dbtype == "subsuffix") {
                    if (this.state.suffixLoading) {
                        db_element =
                            <div className="ds-margin-top ds-loading-spinner ds-center">
                                <h4>Loading suffix configuration for <b>{this.state.node_text} ...</b></h4>
                                <Spinner className="ds-margin-top-lg" loading size="md" />
                            </div>;
                    } else {
                        db_element =
                            <Suffix
                                serverId={this.props.serverId}
                                suffix={this.state.node_text}
                                bename={this.state.bename}
                                loadSuffixTree={this.loadSuffixTree}
                                reload={this.loadSuffix}
                                reloadRefs={this.loadReferrals}
                                reloadIndexes={this.loadIndexes}
                                reloadVLV={this.loadVLV}
                                reloadAttrEnc={this.loadAttrEncrypt}
                                addNotification={this.addNotification}
                                reloadLDIFs={this.loadLDIFs}
                                LDIFRows={this.state.LDIFRows}
                                dbtype={this.state.dbtype}
                                data={this.state[this.state.node_text]}
                                attrs={this.state.attributes}
                                enableTree={this.enableTree}
                                key={this.state.node_text}
                            />;
                    }
                } else {
                    // Chaining
                    if (this.state.chainingLoading) {
                        db_element =
                            <div className="ds-margin-top ds-loading-spinner ds-center">
                                <h4>Loading chaining configuration for <b>{this.state.node_text} ...</b></h4>
                                <Spinner className="ds-margin-top-lg" loading size="md" />
                            </div>;
                    } else {
                        db_element =
                            <ChainingConfig
                                serverId={this.props.serverId}
                                suffix={this.state.node_text}
                                bename={this.state.bename}
                                loadSuffixTree={this.loadSuffixTree}
                                addNotification={this.addNotification}
                                data={this.state[this.state.node_text]}
                                enableTree={this.enableTree}
                                reload={this.loadChainingLink}
                            />;
                    }
                }
            }
            body =
                <div className="ds-container">
                    <div>
                        <div className="ds-tree">
                            <div className={disabled} id="db-tree"
                                style={treeViewContainerStyles}>
                                <TreeView
                                    nodes={nodes}
                                    highlightOnHover
                                    highlightOnSelect
                                    selectNode={this.selectNode}
                                />
                            </div>
                        </div>
                        <div>
                            <button className="btn btn-primary save-button"
                                onClick={this.showSuffixModal}>Create Suffix</button>
                        </div>
                    </div>
                    <div className="ds-tree-content">
                        {db_element}
                    </div>
                </div>;
        } else {
            body =
                <div className="ds-loading-spinner ds-margin-top ds-center">
                    <h4>Loading database configuration ...</h4>
                    <Spinner className="ds-margin-top" loading size="md" />
                </div>;
        }

        return (
            <div className="container-fluid">
                <NotificationController
                    notifications={this.state.notifications}
                    removeNotificationAction={this.removeNotification}
                />
                {body}
                <CreateSuffixModal
                    showModal={this.state.showSuffixModal}
                    closeHandler={this.closeSuffixModal}
                    handleChange={this.handleChange}
                    handleRadioChange={this.handleRadioChange}
                    saveHandler={this.createSuffix}
                    noInit={this.state.noSuffixInit}
                    addSuffix={this.state.createSuffixEntry}
                    addSample={this.state.createSampleEntries}
                    error={this.state.errObj}
                />
            </div>
        );
    }
}

class CreateSuffixModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            handleRadioChange,
            saveHandler,
            noInit,
            addSuffix,
            addSample,
            error
        } = this.props;

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Create New Suffix
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <Row title="Database suffix, like 'dc=example,dc=com'.  The suffix must be a valid LDAP Distiguished Name (DN)">
                                <Col sm={3}>
                                    <ControlLabel>Suffix DN</ControlLabel>
                                </Col>
                                <Col sm={5}>
                                    <input onChange={handleChange} className={error.createSuffix ? "ds-input-bad" : "ds-input"} type="text" id="createSuffix" size="40" />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The name for the backend database, like 'userroot'.  The name can be a combination of alphanumeric characters, dashes (-), and underscores (_). No other characters are allowed, and the name must be unique across all backends.">
                                <Col sm={3}>
                                    <ControlLabel>Database Name</ControlLabel>
                                </Col>
                                <Col sm={5}>
                                    <input onChange={handleChange} className={error.createBeName ? "ds-input-bad" : "ds-input"} type="text" id="createBeName" size="40" />
                                </Col>
                            </Row>
                            <hr />
                            <div>
                                <Row className="ds-indent">
                                    <Radio name="radioGroup" id="noSuffixInit" onChange={handleRadioChange} checked={noInit} inline>
                                        Do Not Initialize Database
                                    </Radio>
                                </Row>
                                <Row className="ds-indent">
                                    <Radio name="radioGroup" id="createSuffixEntry" onChange={handleRadioChange} checked={addSuffix} inline>
                                        Create The Top Suffix Entry
                                    </Radio>
                                </Row>
                                <Row className="ds-indent">
                                    <Radio name="radioGroup" id="createSampleEntries" onChange={handleRadioChange} checked={addSample} inline>
                                        Add Sample Entries
                                    </Radio>
                                </Row>
                            </div>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Create Suffix
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

// Property types and defaults

Database.propTypes = {
    serverId: PropTypes.string
};

Database.defaultProps = {
    serverId: ""
};

CreateSuffixModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    handleRadioChange: PropTypes.func,
    saveHandler: PropTypes.func,
    noInit: PropTypes.bool,
    addSuffix: PropTypes.bool,
    addSample: PropTypes.bool,
    error: PropTypes.object,
};

CreateSuffixModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    handleRadioChange: noop,
    saveHandler: noop,
    noInit: true,
    addSuffix: false,
    addSample: false,
    error: {},
};
