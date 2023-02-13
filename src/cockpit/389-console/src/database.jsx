import cockpit from "cockpit";
import React from "react";
import { log_cmd, valid_dn } from "./lib/tools.jsx";
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
    Button,
    Form,
    FormGroup,
    FormSelect,
    FormSelectOption,
    FormHelperText,
    Modal,
    ModalVariant,
    Spinner,
    TextInput,
    TreeView,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faLeaf,
    faTree,
    faLink
} from '@fortawesome/free-solid-svg-icons';
import {
    CatalogIcon,
    CogIcon,
    CopyIcon,
    HomeIcon,
    ExternalLinkAltIcon,
    KeyIcon,
    UsersIcon,
} from '@patternfly/react-icons';
import PropTypes from "prop-types";
import ExclamationCircleIcon from '@patternfly/react-icons/dist/js/icons/exclamation-circle-icon';
import PlusIcon from '@patternfly/react-icons/dist/js/icons/plus-icon';
const DB_CONFIG = "dbconfig";
const CHAINING_CONFIG = "chaining-config";
const BACKUP_CONFIG = "backups";
const PWP_CONFIG = "pwpolicy";
const LOCAL_PWP_CONFIG = "localpwpolicy";

export class Database extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            errObj: {},
            nodes: [],
            node_name: "",
            node_text: "",
            dbtype: "",
            activeItems: [
                {
                    name: "Global Database Configuration",
                    icon: <CogIcon />,
                    id: "dbconfig",
                }
            ],
            showSuffixModal: false,
            createSuffix: "",
            createBeName: "",
            createNotOK: true,
            createSuffixEntry: false,
            createSampleEntries: false,
            noSuffixInit: true,
            disableTree: false,
            createInitOption: "noInit",

            // DB config
            globalDBConfig: {
                activeTab: 0,
            },
            configUpdated: 0,
            // Chaining Config
            chainingConfig: {},
            chainingUpdated: 0,
            chainingActiveKey: 0,
            // Chaining Link
            chainingLoading: false,
            // Suffix
            suffixLoading: false,
            modalSpinning: false,
            attributes: [],
            // Loaded suffix configurations
            suffix: {},
            // Other
            vlvTableKey: 0,
            LDIFRows: [],
            BackupRows: [],
            backupRefreshing: false,
            suffixList: [],
            pwdStorageSchemes: [],
            loaded: false,
        };

        // General
        this.handleTreeClick = this.handleTreeClick.bind(this);
        this.onHandleChange = this.onHandleChange.bind(this);
        this.onHandleSelectChange = this.onHandleSelectChange.bind(this);
        this.loadGlobalConfig = this.loadGlobalConfig.bind(this);
        this.loadLDIFs = this.loadLDIFs.bind(this);
        this.loadBackups = this.loadBackups.bind(this);
        this.loadSuffixList = this.loadSuffixList.bind(this);
        this.loadPwdStorageSchemes = this.loadPwdStorageSchemes.bind(this);

        // Suffix
        this.handleShowSuffixModal = this.handleShowSuffixModal.bind(this);
        this.closeSuffixModal = this.closeSuffixModal.bind(this);
        this.createSuffix = this.createSuffix.bind(this);
        this.loadSuffix = this.loadSuffix.bind(this);
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

    componentDidUpdate(prevProps) {
        if (this.props.wasActiveList.includes(2)) {
            if (this.state.firstLoad) {
                if (!this.state.loaded) {
                    this.loadGlobalConfig();
                    this.loadChainingConfig();
                    this.loadLDIFs();
                    this.loadBackups();
                    this.loadSuffixList();
                    this.loadPwdStorageSchemes();
                }
                this.loadSuffixTree(false);
            } else {
                if (this.props.serverId !== prevProps.serverId) {
                    this.loadSuffixTree(false);
                }
            }
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
                        { suffixList: suffixList.items }
                    ));
                });
    }

    loadPwdStorageSchemes () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "pwpolicy", "list-schemes"
        ];
        log_cmd("loadPwdStorageSchemes", "Get a list of all the password storage sehemes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const schemes = JSON.parse(content);
                    this.setState(() => (
                        { pwdStorageSchemes: schemes.items }
                    ));
                });
    }

    loadNDN() {
        this.setState({
            loaded: false,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get", "nsslapd-ndn-cache-max-size"
        ];
        log_cmd("loadNDN", "Load NDN cache size", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    this.setState(prevState => ({
                        globalDBConfig: {
                            ...prevState.globalDBConfig,
                            ndncachemaxsize: attrs['nsslapd-ndn-cache-max-size'][0],
                        },
                        configUpdated: 0,
                        loaded: true,
                    }));
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading server configuration for database- ${errMsg.desc}`
                    );
                    this.setState({
                        loaded: true,
                    });
                });
    }

    loadGlobalConfig (activeTab) {
        if (this.state.firstLoad) {
            this.setState({ firstLoad: false });
        }
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
                    let dblocksMonitoring = false;
                    let dbhome = "";

                    if ('nsslapd-db-home-directory' in attrs) {
                        dbhome = attrs['nsslapd-db-home-directory'][0];
                    }
                    if (attrs['nsslapd-cache-autosize'][0] !== "0") {
                        db_cache_auto = true;
                    }
                    if (attrs['nsslapd-import-cache-autosize'][0] !== "0") {
                        import_cache_auto = true;
                    }
                    if (attrs['nsslapd-db-locks-monitoring-enabled'][0] === "on") {
                        dblocksMonitoring = true;
                    }

                    this.setState(() => (
                        {
                            globalDBConfig:
                                {
                                    loading: false,
                                    activeTab: activeTab,
                                    db_cache_auto: db_cache_auto,
                                    import_cache_auto: import_cache_auto,
                                    looklimit: attrs['nsslapd-lookthroughlimit'][0],
                                    idscanlimit: attrs['nsslapd-idlistscanlimit'][0],
                                    pagelooklimit: attrs['nsslapd-pagedlookthroughlimit'][0],
                                    pagescanlimit: attrs['nsslapd-pagedidlistscanlimit'][0],
                                    rangelooklimit: attrs['nsslapd-rangelookthroughlimit'][0],
                                    autosize: attrs['nsslapd-cache-autosize'][0],
                                    autosizesplit: attrs['nsslapd-cache-autosize-split'][0],
                                    dbcachesize: attrs['nsslapd-dbcachesize'][0],
                                    txnlogdir: attrs['nsslapd-db-logdirectory'][0],
                                    dbhomedir: dbhome,
                                    dblocks: attrs['nsslapd-db-locks'][0],
                                    dblocksMonitoring: dblocksMonitoring,
                                    dblocksMonitoringThreshold: attrs['nsslapd-db-locks-monitoring-threshold'][0],
                                    dblocksMonitoringPause: attrs['nsslapd-db-locks-monitoring-pause'][0],
                                    chxpoint: attrs['nsslapd-db-checkpoint-interval'][0],
                                    compactinterval: attrs['nsslapd-db-compactdb-interval'][0],
                                    compacttime: attrs['nsslapd-db-compactdb-time'][0],
                                    importcacheauto: attrs['nsslapd-import-cache-autosize'][0],
                                    importcachesize: attrs['nsslapd-import-cachesize'][0],
                                },
                            configUpdated: 1
                        }), () => { this.loadNDN() });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
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
        log_cmd("loadAvailableControls", "Get available controls", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const availableOids = config.items.filter((el) => !this.state.chainingConfig.oidList.includes(el));
                    this.setState((prevState) => (
                        {
                            chainingConfig: {
                                ...this.state.chainingConfig,
                                availableOids: availableOids,
                            },
                            chainingUpdated: 1
                        }
                    ), this.setState({ chainingUpdated: 0 })
                    );
                });
    }

    loadDefaultConfig() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-get-def"
        ];
        log_cmd("loadDefaultConfig", "Load chaining default configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attr = config.attrs;
                    let checkAci = false;
                    let proxy = false;
                    let refOnScope = false;
                    let useStartTLS = false;

                    if (attr.nschecklocalaci === "on") {
                        checkAci = true;
                    }
                    if (attr.nsproxiedauthorization === "on") {
                        proxy = true;
                    }
                    if (attr.nsreferralonscopedsearch === "on") {
                        refOnScope = true;
                    }
                    if (attr.nsusestarttls === "on") {
                        useStartTLS = true;
                    }
                    this.setState(() => (
                        {
                            chainingConfig: {
                                ...this.state.chainingConfig,
                                defSearchCheck: attr.nsabandonedsearchcheckinterval,
                                defBindConnLimit: attr.nsbindconnectionslimit,
                                defBindTimeout: attr.nsbindtimeout,
                                defBindRetryLimit: attr.nsbindretrylimit,
                                defConcurLimit: attr.nsconcurrentbindlimit,
                                defConcurOpLimit: attr.nsconcurrentoperationslimit,
                                defConnLife: attr.nsconnectionlife,
                                defHopLimit: attr.nshoplimit,
                                defDelay: attr.nsmaxresponsedelay,
                                defTestDelay: attr.nsmaxtestresponsedelay,
                                defOpConnLimit: attr.nsoperationconnectionslimit,
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
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading default chaining configuration - ${errMsg.desc}`
                    );
                    this.setState({
                        loading: false
                    });
                });
    }

    loadChainingConfig(tabIdx) {
        const cmd = [
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
                    let activeKey = 0;
                    if ('nsactivechainingcomponents' in config.attrs) {
                        availableComps = config.attrs.nspossiblechainingcomponents.filter((el) => !config.attrs.nsactivechainingcomponents.includes(el));
                        compList = config.attrs.nsactivechainingcomponents;
                    }
                    if ('nstransmittedcontrols' in config.attrs) {
                        oidList = config.attrs.nstransmittedcontrols;
                    }
                    if (tabIdx) {
                        activeKey = tabIdx;
                    }

                    this.setState(() => (
                        {
                            chainingConfig: {
                                ...this.state.chainingConfig,
                                oidList: oidList,
                                compList: compList,
                                availableComps: availableComps
                            },
                            chainingActiveKey: activeKey,
                        }
                    ), this.loadDefaultConfig());
                });
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

    loadSuffixTree(fullReset) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "get-tree",
        ];
        log_cmd("loadSuffixTree", "Start building the suffix tree", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let suffixData = [];
                    if (content !== "") {
                        suffixData = JSON.parse(content);
                        this.processTree(suffixData);
                    }
                    const treeData = [
                        {
                            name: "Global Database Configuration",
                            icon: <CogIcon />,
                            id: "dbconfig",
                        },
                        {
                            name: "Chaining Configuration",
                            icon: <ExternalLinkAltIcon />,
                            id: "chaining-config",
                        },
                        {
                            name: "Backups & LDIFs",
                            icon: <CopyIcon />,
                            id: "backups",
                        },
                        {
                            name: "Password Policies",
                            id: "pwp",
                            icon: <KeyIcon />,
                            children: [
                                {
                                    name: "Global Policy",
                                    icon: <HomeIcon />,
                                    id: "pwpolicy",
                                },
                                {
                                    name: "Local Policies",
                                    icon: <UsersIcon />,
                                    id: "localpwpolicy",
                                },
                            ],
                            defaultExpanded: true
                        },
                        {
                            name: "Suffixes",
                            icon: <CatalogIcon />,
                            id: "suffixes-tree",
                            children: suffixData,
                            defaultExpanded: true,
                            action: (
                                <Button
                                    onClick={this.handleShowSuffixModal}
                                    variant="plain"
                                    aria-label="Create new suffix"
                                    title="Create new suffix"
                                >
                                    <PlusIcon />
                                </Button>
                            ),
                        }
                    ];
                    let current_node = this.state.node_name;
                    if (fullReset) {
                        current_node = DB_CONFIG;
                    }

                    this.setState(() => ({
                        nodes: treeData,
                        node_name: current_node,
                    }), this.loadAttrs);
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
                    let bindmech = "SIMPLE";
                    let usestarttls = false;
                    let refOnScope = false;
                    let proxiedAuth = false;
                    let checkLocalAci = false;

                    // Handler checkboxes, need to convert "on" to true
                    if (config.attrs.nsreferralonscopedsearch[0] === "on") {
                        refOnScope = true;
                    }
                    if (config.attrs.nsproxiedauthorization[0] === "on") {
                        proxiedAuth = true;
                    }
                    if (config.attrs.nschecklocalaci[0] === "on") {
                        checkLocalAci = true;
                    }
                    if (config.attrs.nsusestarttls[0] === "on") {
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
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error getting chaining link configuration - ${errMsg.desc}`
                    );
                });
    }

    handleTreeClick(evt, treeViewItem, parentItem) {
        if (this.state.activeItems.length === 0 || treeViewItem === this.state.activeItems[0]) {
            this.setState({
                activeItems: [treeViewItem]
            });
            return;
        }

        if (treeViewItem.id === "dbconfig" ||
            treeViewItem.id === "chaining-config" ||
            treeViewItem.id === "pwpolicy" ||
            treeViewItem.id === "localpwpolicy" ||
            treeViewItem.id === "backups") {
            // Nothing special to do, these configurations have already been loaded
            this.setState(prevState => {
                return {
                    node_name: treeViewItem.id,
                    node_text: treeViewItem.name,
                    dbtype: treeViewItem.type,
                    bename: "",
                    activeItems: [treeViewItem, parentItem]
                };
            });
        } else if (treeViewItem.id !== "pwp" &&
                   treeViewItem.id !== "suffixes-tree") {
            if (treeViewItem.id in this.state) {
                // This suffix is already cached, just use what we have...
                this.setState(prevState => {
                    return {
                        node_name: treeViewItem.id,
                        node_text: treeViewItem.name,
                        dbtype: treeViewItem.type,
                        bename: treeViewItem.be,
                        activeItems: [treeViewItem, parentItem]
                    };
                });
            } else {
                this.setState({
                    disableTree: true, // Disable the tree to allow node to be fully loaded
                });
                // Load this suffix whatever it is...
                if (treeViewItem.type === "dblink") {
                    // Chained suffix
                    this.loadChainingLink(treeViewItem.id);
                } else {
                    // Suffix/subsuffix
                    this.loadSuffix(treeViewItem.id);
                }
                this.setState(prevState => {
                    return {
                        node_name: treeViewItem.id,
                        node_text: treeViewItem.name,
                        dbtype: treeViewItem.type,
                        bename: treeViewItem.be,
                        activeItems: [treeViewItem, parentItem]
                    };
                });
            }
        }
    }

    update_tree_nodes() {
        // Enable the tree, and update the titles
        this.setState({
            disableTree: false,
            loaded: true,
        }, () => {
            const className = 'pf-c-tree-view__list-item';
            const element = document.getElementById("suffixes-tree");
            if (element) {
                const elements = element.getElementsByClassName(className);
                for (const el of elements) {
                    el.setAttribute('title', el.innerText);
                }
            }
        });
    }

    handleShowSuffixModal () {
        this.setState({
            showSuffixModal: true,
            createSuffixEntry: false,
            createSampleEntries: false,
            noSuffixInit: true,
            errObj: {},
        });
    }

    onHandleSelectChange(value, event) {
        let noInit = false;
        let addSuffix = false;
        let addSample = false;

        if (value === "noInit") {
            noInit = true;
        } else if (value === "addSuffix") {
            addSuffix = true;
        } else { // addSample
            addSample = true;
        }
        this.setState({
            createInitOption: value,
            noSuffixInit: noInit,
            createSuffixEntry: addSuffix,
            createSampleEntries: addSample
        });
    }

    onHandleChange(str, e) {
        // Handle the Create Suffix modal changes
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        let createNotOK = false;

        // Check current/changed values
        if (value === "") {
            valueErr = true;
            createNotOK = true;
        } else if (e.target.id === "createSuffix" && !valid_dn(str)) {
            valueErr = true;
            createNotOK = true;
        }
        // Check existing values
        if (e.target.id !== "createSuffix") {
            if (!valid_dn(this.state.createSuffix)) {
                errObj.createSuffix = true;
                createNotOK = true;
            }
        }
        if (e.target.id !== "createBeName") {
            if (this.state.createBeName === "") {
                errObj.createBeName = true;
                createNotOK = true;
            }
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj,
            createNotOK: createNotOK
        });
    }

    closeSuffixModal() {
        this.setState({
            showSuffixModal: false,
            createNotOK: true,
        });
    }

    createSuffix() {
        // validate
        let errors = false;
        const missingArgs = {
            createSuffix: false,
            createBeName: false,
        };

        if (this.state.createSuffix === "") {
            this.props.addNotification(
                "warning",
                `Missing the suffix DN`
            );
            missingArgs.createSuffix = true;
            errors = true;
        }
        if (this.state.createBeName === "") {
            this.props.addNotification(
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

        this.setState({
            modalSpinning: true
        });

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
                    this.props.addNotification(
                        "success",
                        `Successfully create new suffix`
                    );
                    // Refresh tree
                    this.loadSuffixTree(false);
                    this.loadSuffixList();
                    this.setState({
                        modalSpinning: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error creating suffix - ${errMsg.desc}`
                    );
                    this.closeSuffixModal();
                    this.setState({
                        modalSpinning: false
                    });
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
                    const config = JSON.parse(content);
                    if ('nsslapd-cache-autosize' in config.attrs &&
                        config.attrs['nsslapd-cache-autosize'][0] !== "0") {
                        this.setState({
                            [suffix]: {
                                ...this.state[suffix],
                                autoTuning: true,
                            },
                        });
                    }
                });
    }

    loadVLV(suffix) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "vlv-index", "list", suffix
        ];
        const tableKey = this.state.vlvTableKey + 1;
        log_cmd("loadVLV", "Load VLV indexes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        [suffix]: {
                            ...this.state[suffix],
                            vlvItems: config.items,
                        },
                        vlvTableKey: tableKey,
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
                    const rows = [];
                    for (const row of config.items) {
                        rows.push(row);
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
                    const rows = [];
                    const systemRows = [];
                    for (const item of config.items) {
                        const index = item.attrs;
                        let types = [];
                        let mrs = "";
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
                        if (index.nssystemindex[0] === 'true') {
                            systemRows.push([index.cn[0], types, mrs]);
                        } else {
                            rows.push([index.cn[0], types, mrs]);
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
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading indexes for ${suffix} - ${errMsg.desc}`
                    );
                });
    }

    loadReferrals(suffix) {
        const cmd = [
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

        const cmd = [
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
                        if (config.attrs['nsslapd-readonly'][0] === "on") {
                            readonly = true;
                        }
                    }
                    if ('nsslapd-require-index' in config.attrs) {
                        if (config.attrs['nsslapd-require-index'][0] === "on") {
                            requireindex = true;
                        }
                    }
                    this.setState({
                        [suffix]: {
                            refRows: refs,
                            cachememsize: config.attrs['nsslapd-cachememsize'][0],
                            cachesize: config.attrs['nsslapd-cachesize'][0],
                            dncachememsize: config.attrs['nsslapd-dncachememsize'][0],
                            dbstate: config.attrs['nsslapd-state'][0],
                            readOnly: readonly,
                            requireIndex: requireindex,
                        }
                    }, this.getAutoTuning(suffix));

                    // Now load VLV indexes
                    const cmd = [
                        "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                        "backend", "vlv-index", "list", suffix
                    ];
                    log_cmd("loadSuffix", "Load VLV indexes", cmd);
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

                                const cmd = [
                                    "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                                    "backend", "attr-encrypt", "--list", "--just-names", suffix
                                ];
                                log_cmd("loadAttrEncrypt", "Load encrypted attrs", cmd);
                                cockpit
                                        .spawn(cmd, { superuser: true, err: "message" })
                                        .done(content => {
                                            const config = JSON.parse(content);
                                            const rows = [];
                                            for (const row of config.items) {
                                                rows.push(row);
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
                                                        const rows = [];
                                                        const systemRows = [];
                                                        for (const item of config.items) {
                                                            const index = item.attrs;
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
                                                            if (index.nssystemindex[0] === 'true') {
                                                                systemRows.push([index.cn[0], types, mrs]);
                                                            } else {
                                                                rows.push([index.cn[0], types, mrs]);
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
                                                        const errMsg = JSON.parse(err);
                                                        this.props.addNotification(
                                                            "error",
                                                            `Error loading indexes for ${suffix} - ${errMsg.desc}`
                                                        );
                                                        this.setState({
                                                            suffixLoading: false
                                                        });
                                                    });
                                        })
                                        .fail(err => {
                                            const errMsg = JSON.parse(err);
                                            this.props.addNotification(
                                                "error",
                                                `Error attribute encryption for ${suffix} - ${errMsg.desc}`
                                            );
                                            this.setState({
                                                suffixLoading: false
                                            });
                                        });
                            })
                            .fail(err => {
                                const errMsg = JSON.parse(err);
                                this.props.addNotification(
                                    "error",
                                    `Error loading VLV indexes for ${suffix} - ${errMsg.desc}`
                                );
                                this.setState({
                                    suffixLoading: false
                                });
                            });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
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
                    const rows = [];
                    for (const row of config.items) {
                        rows.push([row[0], row[1], row[2], row[3]]);
                    }
                    this.setState({
                        LDIFRows: rows,
                        backupRefreshing: false,
                    });
                });
    }

    loadBackups(refreshing) {
        if (refreshing) {
            this.setState({
                backupRefreshing: true
            });
        }
        const cmd = [
            "dsctl", "-j", this.props.serverId, "backups"
        ];
        log_cmd("loadBackupsDatabase", "Load Backups", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const rows = [];
                    for (const row of config.items) {
                        rows.push([row[0], row[1], row[2]]);
                    }
                    this.setState({
                        BackupRows: rows,
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
                    const attrContent = JSON.parse(content);
                    const attrs = [];
                    for (const content of attrContent.items) {
                        attrs.push(content.name[0]);
                    }
                    this.setState({
                        attributes: attrs,
                        loaded: true
                    }, this.update_tree_nodes);
                });
    }

    enableTree () {
        this.setState({
            disableTree: false
        });
    }

    render() {
        let db_element = "";
        let body = "";
        let disabled = "tree-view-container";
        if (this.state.disableTree) {
            disabled = "tree-view-container ds-disabled";
        }

        if (this.state.loaded) {
            if (this.state.node_name === DB_CONFIG || this.state.node_name === "") {
                db_element =
                    <GlobalDatabaseConfig
                        serverId={this.props.serverId}
                        addNotification={this.props.addNotification}
                        reload={this.loadGlobalConfig}
                        data={this.state.globalDBConfig}
                        enableTree={this.enableTree}
                        key={this.state.configUpdated}
                    />;
            } else if (this.state.node_name === CHAINING_CONFIG) {
                db_element =
                    <ChainingDatabaseConfig
                        serverId={this.props.serverId}
                        addNotification={this.props.addNotification}
                        reload={this.loadChainingConfig}
                        data={this.state.chainingConfig}
                        enableTree={this.enableTree}
                        activeKey={this.state.chainingActiveKey}
                        key={this.state.chainingUpdated}
                    />;
            } else if (this.state.node_name === PWP_CONFIG) {
                db_element =
                    <GlobalPwPolicy
                        serverId={this.props.serverId}
                        addNotification={this.props.addNotification}
                        attrs={this.state.attributes}
                        pwdStorageSchemes={this.state.pwdStorageSchemes}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name === LOCAL_PWP_CONFIG) {
                db_element =
                    <LocalPwPolicy
                        serverId={this.props.serverId}
                        addNotification={this.props.addNotification}
                        attrs={this.state.attributes}
                        pwdStorageSchemes={this.state.pwdStorageSchemes}
                        enableTree={this.enableTree}
                    />;
            } else if (this.state.node_name === BACKUP_CONFIG) {
                db_element =
                    <Backups
                        serverId={this.props.serverId}
                        addNotification={this.props.addNotification}
                        backups={this.state.BackupRows}
                        suffixes={this.state.suffixList}
                        ldifs={this.state.LDIFRows}
                        enableTree={this.enableTree}
                        reload={this.loadBackups}
                        refreshing={this.state.backupRefreshing}
                    />;
            } else if (this.state.node_name !== "") {
                // We have a suffix, or database link
                if (this.state.dbtype === "suffix" || this.state.dbtype === "subsuffix") {
                    if (this.state.suffixLoading) {
                        db_element =
                            <div className="ds-margin-top-xlg ds-loading-spinner ds-center">
                                <TextContent>
                                    <Text className="ds-margin-top-xlg" component={TextVariants.h3}>
                                        Loading suffix configuration for <b>{this.state.node_text} ...</b>
                                    </Text>
                                </TextContent>
                                <Spinner className="ds-margin-top-lg" size="xl" />
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
                                vlvTableKey={this.state.vlvTableKey}
                                reloadAttrEnc={this.loadAttrEncrypt}
                                addNotification={this.props.addNotification}
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
                                <TextContent>
                                    <Text className="ds-margin-top-xlg" component={TextVariants.h2}>
                                        Loading Chaining configuration for <b>{this.state.node_text} ...</b>
                                    </Text>
                                </TextContent>
                                <Spinner className="ds-margin-top-lg" size="xl" />
                            </div>;
                    } else {
                        db_element =
                            <ChainingConfig
                                serverId={this.props.serverId}
                                suffix={this.state.node_text}
                                bename={this.state.bename}
                                loadSuffixTree={this.loadSuffixTree}
                                addNotification={this.props.addNotification}
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
                            <div className={disabled} id="db-tree">
                                <TreeView
                                    hasSelectableNodes
                                    data={this.state.nodes}
                                    activeItems={this.state.activeItems}
                                    onSelect={this.handleTreeClick}
                                />
                            </div>
                        </div>
                    </div>
                    <div className="ds-tree-content">
                        {db_element}
                    </div>
                </div>;
        } else {
            body =
                <div className="ds-center">
                    <TextContent>
                        <Text className="ds-margin-top-xlg" component={TextVariants.h3}>
                            Loading Database Configuration ...
                        </Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="xl" />
                </div>;
        }

        return (
            <div className="container-fluid">
                {body}
                <CreateSuffixModal
                    showModal={this.state.showSuffixModal}
                    closeHandler={this.closeSuffixModal}
                    handleChange={this.onHandleChange}
                    handleSelectChange={this.onHandleSelectChange}
                    saveHandler={this.createSuffix}
                    initOption={this.state.createInitOption}
                    createNotOK={this.state.createNotOK}
                    modalSpinning={this.state.modalSpinning}
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
            handleSelectChange,
            saveHandler,
            createNotOK,
            initOption,
            modalSpinning,
            error
        } = this.props;

        let saveBtnName = "Create Suffix";
        const extraPrimaryProps = {};
        if (modalSpinning) {
            saveBtnName = "Creating ...";
            extraPrimaryProps.spinnerAriaValueText = "Creating";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Create New Suffix"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isDisabled={createNotOK || modalSpinning}
                        isLoading={modalSpinning}
                        spinnerAriaValueText={modalSpinning ? "Creating Suffix" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <FormGroup
                        label="Suffix DN"
                        fieldId="createSuffix"
                        title="Database suffix, like 'dc=example,dc=com'.  The suffix must be a valid LDAP Distiguished Name (DN)."
                        helperTextInvalid="The DN of the suffix is invalid"
                        helperTextInvalidIcon={<ExclamationCircleIcon />}
                        validated={error.createSuffix ? "error" : "noval"}
                    >
                        <TextInput
                            isRequired
                            type="text"
                            id="createSuffix"
                            aria-describedby="createSuffix"
                            name="createSuffix"
                            onChange={handleChange}
                            validated={error.createSuffix ? "error" : "noval"}
                        />
                        <FormHelperText isError isHidden={!error.createSuffix}>
                            Required field
                        </FormHelperText>
                    </FormGroup>
                    <FormGroup
                        label="Database Name"
                        fieldId="suffixName"
                        title="The name for the backend database, like 'userroot'.  The name can be a combination of alphanumeric characters, dashes (-), and underscores (_). No other characters are allowed, and the name must be unique across all backends."
                        helperTextInvalid="You must enter a name for the database"
                        helperTextInvalidIcon={<ExclamationCircleIcon />}
                        validated={error.createBeName ? "error" : "noval"}
                    >
                        <TextInput
                            isRequired
                            type="text"
                            id="createBeName"
                            aria-describedby="createSuffix"
                            name="suffixName"
                            onChange={handleChange}
                            validated={error.createBeName ? "error" : "noval"}
                        />
                        <FormHelperText isError isHidden={!error.createBeName}>
                            Required field
                        </FormHelperText>
                    </FormGroup>
                    <FormGroup
                        label="Initialization Option"
                        fieldId="initOptions"
                    >
                        <FormSelect value={initOption} onChange={handleSelectChange} aria-label="FormSelect Input">
                            <FormSelectOption key={1} value="noInit" label="Do Not Initialize Database" />
                            <FormSelectOption key={2} value="addSuffix" label="Create The Top Suffix Entry" />
                            <FormSelectOption key={3} value="addSample" label="Add Sample Entries" />
                        </FormSelect>
                    </FormGroup>
                </Form>
            </Modal>
        );
    }
}

// Property types and defaults

Database.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string
};

Database.defaultProps = {
    serverId: ""
};

CreateSuffixModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    onHandleChange: PropTypes.func,
    onHandleSelectChange: PropTypes.func,
    saveHandler: PropTypes.func,
    modalSpinning: PropTypes.bool,
    error: PropTypes.object,
};

CreateSuffixModal.defaultProps = {
    showModal: false,
    modalSpinning: false,
    error: {},
};
