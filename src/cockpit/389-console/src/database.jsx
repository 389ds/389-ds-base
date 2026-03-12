import cockpit from "cockpit";
import React from "react";
import { log_cmd, valid_dn, valid_db_name } from "./lib/tools.jsx";
import {
    ChainingConfig,
    ChainingDatabaseConfig
} from "./lib/database/chaining.jsx";
import { GlobalDatabaseConfig, GlobalDatabaseConfigMDB } from "./lib/database/databaseConfig.jsx";
import { Suffix } from "./lib/database/suffix.jsx";
import { Backups } from "./lib/database/backups.jsx";
import { GlobalPwPolicy } from "./lib/database/globalPwp.jsx";
import { LocalPwPolicy } from "./lib/database/localPwp.jsx";
import {
    Button,
    Card,
    ProgressStepper,
    ProgressStep,
    Form,
    FormGroup,
    FormSelect,
    FormSelectOption,
    FormHelperText,
    HelperText,
    HelperTextItem,
    Modal,
    ModalVariant,
    Spinner,
    TextInput,
    TreeView,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { TreeIcon, LeafIcon, LinkIcon } from '@patternfly/react-icons';
import {
    CatalogIcon,
    CogIcon,
    CopyIcon,
    HomeIcon,
    ExternalLinkAltIcon,
    KeyIcon,
    UsersIcon,
} from '@patternfly/react-icons';
import { PropTypes } from "prop-types";
import { ExclamationCircleIcon } from '@patternfly/react-icons/dist/js/icons/exclamation-circle-icon';
import { PlusIcon } from '@patternfly/react-icons/dist/js/icons/plus-icon';
import InProgressIcon from '@patternfly/react-icons/dist/esm/icons/in-progress-icon';
import PendingIcon from '@patternfly/react-icons/dist/esm/icons/pending-icon';
import CheckCircleIcon from '@patternfly/react-icons/dist/esm/icons/check-circle-icon';
const DB_CONFIG = "dbconfig";
const CHAINING_CONFIG = "chaining-config";
const BACKUP_CONFIG = "backups";
const PWP_CONFIG = "pwpolicy";
const LOCAL_PWP_CONFIG = "localpwpolicy";
const BE_IMPL_BDB = "bdb";
const BE_IMPL_MDB = "mdb";

const _ = cockpit.gettext;

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
                    name: _("Global Database Configuration"),
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
            backendImplement: BE_IMPL_MDB,
            configUpdated: 0,
            // Chaining Config
            chainingConfig: {},
            chainingUpdated: 0,
            chainingActiveKey: 0,
            // Chaining Link
            chainingLoading: false,
            // Suffix
            suffixLoading: false,
            suffixConfigLoaded: false,
            suffixConfigLoading: false,
            suffixVlvLoaded: false,
            suffixVlvLoading: false,
            suffixAttrEncLoaded: false,
            suffixAttrEncLoading: false,
            suffixIndexesLoaded: false,
            suffixIndexesLoading: false,
            modalSpinning: false,
            attributes: [],
            objectClasses: [],
            attributesFullData: [],
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
            globalConfigLoaded: false,
            globalConfigLoading: false,
            chainingConfigLoaded: false,
            chainingConfigLoading: false,
            ldifsLoaded: false,
            ldifsLoading: false,
            backupsLoaded: false,
            backupsLoading: false,
            pwdSchemesLoaded: false,
            pwdSchemesLoading: false,
            suffixListLoaded: false,
            suffixListLoading: false,
            suffixTreeLoaded: false,
            suffixTreeLoading: false,
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
        this.loadSchema = this.loadSchema.bind(this);

        // ChainingConfig
        this.loadChainingLink = this.loadChainingLink.bind(this);
        this.loadChainingConfig = this.loadChainingConfig.bind(this);
        this.loadAvailableControls = this.loadAvailableControls.bind(this);
        this.loadDefaultConfig = this.loadDefaultConfig.bind(this);

        // Other
        this.loadSuffixTree = this.loadSuffixTree.bind(this);
        this.enableTree = this.enableTree.bind(this);
        this.resetLoadProgress = this.resetLoadProgress.bind(this);
    }

    resetLoadProgress() {
        this.setState({
            loaded: false,
            globalConfigLoaded: false,
            globalConfigLoading: false,
            chainingConfigLoaded: false,
            chainingConfigLoading: false,
            ldifsLoaded: false,
            ldifsLoading: false,
            backupsLoaded: false,
            backupsLoading: false,
            pwdSchemesLoaded: false,
            pwdSchemesLoading: false,
            suffixListLoaded: false,
            suffixListLoading: false,
            suffixTreeLoaded: false,
            suffixTreeLoading: false,
        });
    }

    componentDidUpdate(prevProps) {
        if (this.props.wasActiveList.includes(2)) {
            if (this.state.firstLoad) {
                if (!this.state.loaded) {
                    this.resetLoadProgress();
                    this.loadGlobalConfig();
                } else {
                    this.loadSuffixTree(false);
                }
            } else {
                if (this.props.serverId !== prevProps.serverId) {
                    this.resetLoadProgress();
                    this.loadGlobalConfig();
                }
            }
        }
    }

    loadSuffixList (reloading = false) {
        this.setState({ suffixListLoading: true });
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
                        {
                            suffixList: suffixList.items,
                            suffixListLoaded: true,
                            suffixListLoading: false,
                        }
                    ), () => {
                        if (!reloading) {
                            this.loadSuffixTree(false);
                        }
                    });
                }).fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading suffix list - $0"), errMsg.desc)
                    );
                    this.setState({
                        suffixListLoaded: true,
                        suffixListLoading: false,
                    }, () => {
                        if (!reloading) {
                            this.loadSuffixTree(false);
                        }
                    });
                });
    }

    loadPwdStorageSchemes (reloading = false) {
        this.setState({ pwdSchemesLoading: true });
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
                        {
                            pwdStorageSchemes: schemes.items,
                            pwdSchemesLoaded: true,
                            pwdSchemesLoading: false,
                        }
                    ), () => {
                        if (!reloading) {
                            this.loadSuffixList();
                        }
                    });
                }).fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading password storage schemes - $0"), errMsg.desc)
                    );
                    this.setState({
                        pwdSchemesLoaded: true,
                        pwdSchemesLoading: false,
                    }, () => {
                        if (!reloading) {
                            this.loadSuffixList();
                        }
                    });
                });
    }

    loadNDN(reloading = false) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get", "nsslapd-ndn-cache-max-size", "nsslapd-ndn-cache-enabled"
        ];
        log_cmd("loadNDN", "Load NDN cache size", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    const ndn_cache_enabled = attrs['nsslapd-ndn-cache-enabled'][0] === "on";
                    this.setState(prevState => ({
                        globalDBConfig: {
                            ...prevState.globalDBConfig,
                            ndncachemaxsize: attrs['nsslapd-ndn-cache-max-size'][0],
                            ndn_cache_enabled: ndn_cache_enabled,
                        },
                        globalConfigLoaded: true,
                        globalConfigLoading: false,
                        configUpdated: 0,
                    }), () => {
                        if (!reloading) {
                            this.loadChainingConfig();
                        }
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading server configuration for database- $0"), errMsg.desc)
                    );
                    this.setState({
                        globalConfigLoaded: true,
                        globalConfigLoading: false,
                    }, () => {
                        if (!reloading) {
                            this.loadChainingConfig();
                        }
                    });
                });
    }

    loadGlobalConfig (activeTab, reloading = false) {
        if (this.state.firstLoad) {
            this.setState({ firstLoad: false });
        }
        this.setState({ globalConfigLoading: true });
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
                    let dbhome = "";
                    let dbimplement = "";
                    let db_cache_auto = false;

                    if ('nsslapd-backend-implement' in attrs) {
                        dbimplement = attrs['nsslapd-backend-implement'][0];
                        this.setState({ backendImplement: dbimplement });
                    }
                    if (dbimplement === BE_IMPL_BDB) {
                        let import_cache_auto = false;
                        let dblocksMonitoring = false;

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
                                        activeTab,
                                        db_cache_auto,
                                        import_cache_auto,
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
                                        dblocksMonitoring,
                                        dblocksMonitoringThreshold: attrs['nsslapd-db-locks-monitoring-threshold'][0],
                                        dblocksMonitoringPause: attrs['nsslapd-db-locks-monitoring-pause'][0],
                                        chxpoint: attrs['nsslapd-db-checkpoint-interval'][0],
                                        compactinterval: attrs['nsslapd-db-compactdb-interval'][0],
                                        compacttime: attrs['nsslapd-db-compactdb-time'][0],
                                        importcacheauto: attrs['nsslapd-import-cache-autosize'][0],
                                        importcachesize: attrs['nsslapd-import-cachesize'][0],
                                        dynamiclistsenabled: attrs['nsslapd-dynamic-lists-enabled'][0] == "on" ? true : false,
                                        dynamiclistattr: attrs['nsslapd-dynamic-lists-attr'][0],
                                        dynamicoc: attrs['nsslapd-dynamic-lists-oc'][0],
                                        dynamicurlattr: attrs['nsslapd-dynamic-lists-url-attr'][0],
                                    },
                                configUpdated: 1
                            }), () => {
                                this.loadNDN(reloading);
                            });
                    } else if (dbimplement === BE_IMPL_MDB) {
                        let db_cache_auto = false;
                        if ('nsslapd-directory' in attrs) {
                            dbhome = attrs['nsslapd-directory'][0];
                        }

                        if (attrs['nsslapd-cache-autosize'][0] !== "-1") {
                            db_cache_auto = true;
                        }

                        this.setState(() => (
                            {
                                globalDBConfig:
                                    {
                                        loading: false,
                                        activeTab,
                                        looklimit: attrs['nsslapd-lookthroughlimit'][0],
                                        idscanlimit: attrs['nsslapd-idlistscanlimit'][0],
                                        pagelooklimit: attrs['nsslapd-pagedlookthroughlimit'][0],
                                        pagescanlimit: attrs['nsslapd-pagedidlistscanlimit'][0],
                                        rangelooklimit: attrs['nsslapd-rangelookthroughlimit'][0],
                                        mdbmaxsize: attrs['nsslapd-mdb-max-size'][0],
                                        mdbmaxreaders: attrs['nsslapd-mdb-max-readers'][0],
                                        mdbmaxdbs: attrs['nsslapd-mdb-max-dbs'][0],
                                        dbhomedir: dbhome,
                                        autosize: attrs['nsslapd-cache-autosize'][0],
                                        dynamiclistsenabled: attrs['nsslapd-dynamic-lists-enabled'][0] == "on" ? true : false,
                                        dynamiclistattr: attrs['nsslapd-dynamic-lists-attr'][0],
                                        dynamicoc: attrs['nsslapd-dynamic-lists-oc'][0],
                                        dynamicurlattr: attrs['nsslapd-dynamic-lists-url-attr'][0],
                                    },
                                configUpdated: 1
                            }), () => {
                                this.loadNDN(reloading);
                            });
                    }
                }
                )
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading database configuration - $0"), errMsg.desc)
                    );
                    this.loadNDN(reloading); // updates the loading state
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
                                availableOids,
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
                        cockpit.format(_("Error loading default chaining configuration - $0"), errMsg.desc)
                    );
                    this.setState({
                        loading: false
                    });
                });
    }

    loadChainingConfig(tabIdx, reloading = false) {
        this.setState({ chainingConfigLoading: true });
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
                                oidList,
                                compList,
                                availableComps
                            },
                            chainingActiveKey: activeKey,
                            chainingConfigLoaded: true,
                            chainingConfigLoading: false,
                        }
                    ), () => {
                        this.loadDefaultConfig();
                        if (!reloading) {
                            this.loadLDIFs();
                        }
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading chaining configuration - $0"), errMsg.desc)
                    );
                    this.setState({
                        chainingConfigLoading: false,
                        chainingConfigLoaded: true,
                    });
                    if (!reloading) {
                        this.loadLDIFs();
                    }
                });
    }

    processTree(suffixData) {
        for (const suffix of suffixData) {
            if (suffix.type === "suffix") {
                suffix.icon = <TreeIcon size="sm" />;
            } else if (suffix.type === "subsuffix") {
                suffix.icon = <LeafIcon size="sm" />;
            } else {
                suffix.icon = <LinkIcon size="sm" />;
            }
            if (suffix.children.length === 0) {
                delete suffix.children;
            } else {
                this.processTree(suffix.children);
            }
        }
    }

    loadSuffixTree(fullReset, reloading = false) {
        this.setState({ suffixTreeLoading: true });
        const treeData = [
            {
                name: _("Global Database Configuration"),
                icon: <CogIcon />,
                id: "dbconfig",
            },
            {
                name: _("Chaining Configuration"),
                icon: <ExternalLinkAltIcon />,
                id: "chaining-config",
            },
            {
                name: _("Backups & LDIFs"),
                icon: <CopyIcon />,
                id: "backups",
            },
            {
                name: _("Password Policies"),
                id: "pwp",
                icon: <KeyIcon />,
                children: [
                    {
                        name: _("Global Policy"),
                        icon: <HomeIcon />,
                        id: "pwpolicy",
                    },
                    {
                        name: _("Local Policies"),
                        icon: <UsersIcon />,
                        id: "localpwpolicy",
                    },
                ],
                defaultExpanded: true
            },
            {
                name: _("Suffixes"),
                icon: <CatalogIcon />,
                id: "suffixes-tree",
                children: [],
                defaultExpanded: true,
                action: (
                    <Button
                        onClick={this.handleShowSuffixModal}
                        variant="plain"
                        aria-label="Create new suffix"
                        title={_("Create new suffix")}
                    >
                        <PlusIcon />
                    </Button>
                ),
            }
        ];

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

                    let current_node = this.state.node_name;
                    if (fullReset) {
                        current_node = DB_CONFIG;
                    }

                    treeData[4].children = suffixData; // suffixes node
                    this.setState(() => ({
                        nodes: treeData,
                        node_name: current_node,
                        suffixTreeLoaded: true,
                        suffixTreeLoading: false,
                    }), this.loadSchema);
                })
                .fail(err => {
                    // Handle backend get-tree failure gracefully
                    let current_node = this.state.node_name;
                    if (fullReset) {
                        current_node = DB_CONFIG;
                    }

                    this.setState(() => ({
                        nodes: treeData,
                        node_name: current_node,
                        suffixTreeLoaded: true,
                        suffixTreeLoading: false,
                    }), this.loadSchema);
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
                        cockpit.format(_("Error getting chaining link configuration - $0"), errMsg.desc)
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
            createInitOption: "noInit",
            errObj: {},
        });
    }

    onHandleSelectChange(_event, value) {
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

    onHandleChange(e, str) {
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
        } else if (e.target.id === "createBeName" && !valid_db_name(str)) {
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
            if (this.state.createBeName === "" || !valid_db_name(this.state.createBeName)) {
                errObj.createBeName = true;
                createNotOK = true;
            }
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj,
            createNotOK
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
                _("Missing the suffix DN")
            );
            missingArgs.createSuffix = true;
            errors = true;
        }
        if (this.state.createBeName === "") {
            this.props.addNotification(
                "warning",
                _("Missing the suffix backend name")
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
                        _("Successfully create new suffix")
                    );
                    // Refresh tree
                    this.loadSuffixTree(false);
                    this.loadSuffixList(true);
                    this.setState({
                        modalSpinning: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error creating suffix - $0"), errMsg.desc)
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
                        cockpit.format(_("Error loading indexes for $0 - $1"), suffix, errMsg.desc)
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
            suffixLoading: true,
            suffixConfigLoaded: false,
            suffixConfigLoading: true,
            suffixVlvLoaded: false,
            suffixVlvLoading: false,
            suffixAttrEncLoaded: false,
            suffixAttrEncLoading: false,
            suffixIndexesLoaded: false,
            suffixIndexesLoading: false,
        }, this.loadSchema());

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
                        },
                        suffixConfigLoaded: true,
                        suffixConfigLoading: false,
                        suffixVlvLoading: true,
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
                                }, () => {
                                    this.setState({
                                        suffixVlvLoaded: true,
                                        suffixVlvLoading: false,
                                        suffixAttrEncLoading: true,
                                    });
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
                                            }, () => {
                                                this.setState({
                                                    suffixAttrEncLoaded: true,
                                                    suffixAttrEncLoading: false,
                                                    suffixIndexesLoading: true,
                                                });
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
                                                            suffixIndexesLoaded: true,
                                                            suffixIndexesLoading: false,
                                                            suffixLoading: false
                                                        });
                                                    })
                                                    .fail(err => {
                                                        const errMsg = JSON.parse(err);
                                                        this.props.addNotification(
                                                            "error",
                                                            cockpit.format(_("Error loading indexes for $0 - $1"), suffix, errMsg.desc)
                                                        );
                                                        this.setState({
                                                            suffixIndexesLoading: false,
                                                            suffixLoading: false
                                                        });
                                                    });
                                        })
                                        .fail(err => {
                                            const errMsg = JSON.parse(err);
                                            this.props.addNotification(
                                                "error",
                                                cockpit.format(_("Error attribute encryption for $0 - $1"), suffix, errMsg.desc)
                                            );
                                            this.setState({
                                                suffixAttrEncLoading: false,
                                                suffixLoading: false
                                            });
                                        });
                            })
                            .fail(err => {
                                const errMsg = JSON.parse(err);
                                this.props.addNotification(
                                    "error",
                                    cockpit.format(_("Error loading VLV indexes for $0 - $1"), suffix, errMsg.desc)
                                );
                                this.setState({
                                    suffixVlvLoading: false,
                                    suffixLoading: false
                                });
                            });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading config for $0 - $1"), suffix, errMsg.desc)
                    );
                    this.setState({
                        suffixConfigLoading: false,
                        suffixLoading: false
                    });
                });
    }

    loadLDIFs(reloading = false) {
        this.setState({ ldifsLoading: true });
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
                        ldifsLoaded: true,
                        ldifsLoading: false,
                    }, () => {
                        if (!reloading) {
                            this.loadBackups();
                        }
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading LDIF files - $0"), errMsg.desc)
                    );
                    this.setState({
                        ldifsLoading: false,
                        ldifsLoaded: true,
                    });
                    if (!reloading) {
                        this.loadBackups();
                    }
                });
    }

    loadBackups(refreshing, reloading = false) {
        if (refreshing) {
            this.setState({
                backupRefreshing: true
            });
        }
        this.setState({ backupsLoading: true });
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
                        backupsLoaded: true,
                        backupsLoading: false,
                        backupRefreshing: false
                    }, () => {
                        if (!reloading && !refreshing) {
                            this.loadPwdStorageSchemes();
                        }
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading backups - $0"), errMsg.desc)
                    );
                    this.setState({
                        backupsLoading: false,
                        backupRefreshing: false,
                        backupsLoaded: true,
                    });
                    if (!reloading && !refreshing) {
                        this.loadPwdStorageSchemes();
                    }
                });
    }

    loadSchema() {
        const attr_cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "attributetypes",
            "list"
        ];
        log_cmd("loadSchema", "Plugins Get attrs", attr_cmd);
        cockpit
                .spawn(attr_cmd, { superuser: true, err: "message" })
                .done(content => {
                    const attrContent = JSON.parse(content);
                    const attrs = [];
                    for (const content of attrContent.items) {
                        attrs.push(content.name[0]);
                    }

                    const oc_cmd = [
                        "dsconf",
                        "-j",
                        "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                        "schema",
                        "objectclasses",
                        "list"
                    ];
                    log_cmd("loadSchema", "Get objectClasses", oc_cmd);
                    cockpit
                            .spawn(oc_cmd, { superuser: true, err: "message" })
                            .done(content => {
                                const ocContent = JSON.parse(content);
                                const ocs = [];
                                for (const content of ocContent.items) {
                                    ocs.push(content.name[0]);
                                }
                                this.setState({
                                    attributes: attrs,
                                    objectClasses: ocs,
                                    attributesFullData: attrContent.items
                                }, this.update_tree_nodes);
                            })
                }).fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading schema - $0"), errMsg.desc)
                    );
                    this.update_tree_nodes();
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
                if (this.state.backendImplement === BE_IMPL_BDB) {
                    db_element = (
                        <GlobalDatabaseConfig
                            serverId={this.props.serverId}
                            addNotification={this.props.addNotification}
                            reload={(activeTab) => this.loadGlobalConfig(activeTab, true)}
                            data={this.state.globalDBConfig}
                            enableTree={this.enableTree}
                            key={this.state.configUpdated}
                            objectClasses={this.state.objectClasses}
                            attributes={this.state.attributesFullData}
                            loading={this.state.globalConfigLoading}
                        />
                    );
                } else if (this.state.backendImplement === BE_IMPL_MDB) {
                    db_element = (
                        <GlobalDatabaseConfigMDB
                            serverId={this.props.serverId}
                            addNotification={this.props.addNotification}
                            reload={(activeTab) => this.loadGlobalConfig(activeTab, true)}
                            data={this.state.globalDBConfig}
                            enableTree={this.enableTree}
                            key={this.state.configUpdated}
                            attributes={this.state.attributesFullData}
                            objectClasses={this.state.objectClasses}
                            loading={this.state.globalConfigLoading}
                        />
                    );
                }
            } else if (this.state.node_name === CHAINING_CONFIG) {
                db_element = (
                    <ChainingDatabaseConfig
                        serverId={this.props.serverId}
                        addNotification={this.props.addNotification}
                        reload={(tabIdx) => this.loadChainingConfig(tabIdx, true)}
                        data={this.state.chainingConfig}
                        enableTree={this.enableTree}
                        activeKey={this.state.chainingActiveKey}
                        key={this.state.chainingUpdated}
                    />
                );
            } else if (this.state.node_name === PWP_CONFIG) {
                db_element = (
                    <GlobalPwPolicy
                        serverId={this.props.serverId}
                        addNotification={this.props.addNotification}
                        attrs={this.state.attributes}
                        pwdStorageSchemes={this.state.pwdStorageSchemes}
                        enableTree={this.enableTree}
                    />
                );
            } else if (this.state.node_name === LOCAL_PWP_CONFIG) {
                db_element = (
                    <LocalPwPolicy
                        serverId={this.props.serverId}
                        addNotification={this.props.addNotification}
                        attrs={this.state.attributes}
                        pwdStorageSchemes={this.state.pwdStorageSchemes}
                        enableTree={this.enableTree}
                    />
                );
            } else if (this.state.node_name === BACKUP_CONFIG) {
                db_element = (
                    <Backups
                        serverId={this.props.serverId}
                        addNotification={this.props.addNotification}
                        backups={this.state.BackupRows}
                        suffixes={this.state.suffixList}
                        ldifs={this.state.LDIFRows}
                        enableTree={this.enableTree}
                        handleReload={(refreshing) => this.loadBackups(refreshing, true)}
                        refreshing={this.state.backupRefreshing}
                    />
                );
            } else if (this.state.node_name !== "") {
                // We have a suffix, or database link
                if (this.state.dbtype === "suffix" || this.state.dbtype === "subsuffix") {
                    if (this.state.suffixLoading) {
                        db_element = (
                            <div className="ds-margin-top-xlg ds-loading-spinner ds-center">
                                <TextContent>
                                    <Text className="ds-margin-top-xlg" component={TextVariants.h3}>
                                        {_("Loading suffix configuration for ")}<b>{this.state.node_text} ...</b>
                                        <Spinner isInline className="ds-left-margin" size="lg" />
                                    </Text>
                                </TextContent>
                                <ProgressStepper
                                    className="ds-margin-top-xlg"
                                    aria-label="Progress stepper for suffix loading stages"
                                    isCenterAligned
                                >
                                    <ProgressStep
                                        isCurrent={this.state.suffixConfigLoading}
                                        variant={this.state.suffixConfigLoaded ? "success" : "pending"}
                                        icon={!this.state.suffixConfigLoaded ? this.state.suffixConfigLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                                        id="suffixConfigLoading"
                                        titleId="load suffix configuration"
                                        aria-label="Loading suffix configuration step"
                                    >
                                        {!this.state.suffixConfigLoaded ? _("Loading Suffix Config") : _("Suffix Config Loaded")}
                                    </ProgressStep>
                                    <ProgressStep
                                        isCurrent={this.state.suffixVlvLoading}
                                        variant={this.state.suffixVlvLoaded ? "success" : "pending"}
                                        icon={!this.state.suffixVlvLoaded ? this.state.suffixVlvLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                                        id="suffixVlvLoading"
                                        titleId="load suffix vlv indexes"
                                        aria-label="Loading suffix vlv indexes step"
                                    >
                                        {!this.state.suffixVlvLoaded ? _("Loading VLV Indexes") : _("VLV Indexes Loaded")}
                                    </ProgressStep>
                                    <ProgressStep
                                        isCurrent={this.state.suffixAttrEncLoading}
                                        variant={this.state.suffixAttrEncLoaded ? "success" : "pending"}
                                        icon={!this.state.suffixAttrEncLoaded ? this.state.suffixAttrEncLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                                        id="suffixAttrEncLoading"
                                        titleId="load suffix attribute encryption"
                                        aria-label="Loading suffix attribute encryption step"
                                    >
                                        {!this.state.suffixAttrEncLoaded ? _("Loading Attribute Encryption") : _("Attribute Encryption Loaded")}
                                    </ProgressStep>
                                    <ProgressStep
                                        isCurrent={this.state.suffixIndexesLoading}
                                        variant={this.state.suffixIndexesLoaded ? "success" : "pending"}
                                        icon={!this.state.suffixIndexesLoaded ? this.state.suffixIndexesLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                                        id="suffixIndexesLoading"
                                        titleId="load suffix indexes"
                                        aria-label="Loading suffix indexes step"
                                    >
                                        {!this.state.suffixIndexesLoaded ? _("Loading Indexes") : _("Indexes Loaded")}
                                    </ProgressStep>
                                </ProgressStepper>
                            </div>
                        );
                    } else {
                        db_element = (
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
                                reloadLDIFs={() => this.loadLDIFs(true)}
                                LDIFRows={this.state.LDIFRows}
                                dbtype={this.state.dbtype}
                                data={this.state[this.state.node_text]}
                                attrs={this.state.attributes}
                                enableTree={this.enableTree}
                                key={this.state.node_text}
                            />
                        );
                    }
                } else {
                    // Chaining
                    if (this.state.chainingLoading) {
                        db_element = (
                            <div className="ds-margin-top ds-loading-spinner ds-center">
                                <TextContent>
                                    <Text className="ds-margin-top-xlg" component={TextVariants.h2}>
                                        {_("Loading Chaining configuration for ")}<b>{this.state.node_text} ...</b>
                                    </Text>
                                </TextContent>
                                <Spinner className="ds-margin-top-lg" size="xl" />
                            </div>
                        );
                    } else {
                        db_element = (
                            <ChainingConfig
                                serverId={this.props.serverId}
                                suffix={this.state.node_text}
                                bename={this.state.bename}
                                loadSuffixTree={this.loadSuffixTree}
                                addNotification={this.props.addNotification}
                                data={this.state[this.state.node_text]}
                                enableTree={this.enableTree}
                                reload={this.loadChainingLink}
                            />
                        );
                    }
                }
            }
            body = (
                <div className="ds-container">
                    <Card className="ds-tree">
                        <div className={disabled} id="db-tree">
                            <TreeView
                                hasSelectableNodes
                                data={this.state.nodes}
                                activeItems={this.state.activeItems}
                                onSelect={this.handleTreeClick}
                            />
                        </div>
                    </Card>
                    <div className="ds-tree-content">
                        {db_element}
                    </div>
                </div>
            );
        } else {
            body = (
                <div className="ds-center">
                    <TextContent>
                        <Text className="ds-margin-top-xlg" component={TextVariants.h3}>
                            {_("Loading Database Configuration ...")}
                            <Spinner isInline className="ds-left-margin" size="lg" />
                        </Text>
                    </TextContent>
                    <ProgressStepper
                        className="ds-margin-top-xlg"
                        aria-label="Progress stepper for database loading stages"
                        isCenterAligned
                    >
                        <ProgressStep
                            isCurrent={this.state.globalConfigLoading}
                            variant={this.state.globalConfigLoaded ? "success" : "pending"}
                            icon={!this.state.globalConfigLoaded ? this.state.globalConfigLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                            id="globalConfigLoading"
                            titleId="load database global configuration"
                            aria-label="Loading database global configuration step"
                        >
                            {!this.state.globalConfigLoaded ? _("Loading Global Config") : _("Global Config Loaded")}
                        </ProgressStep>
                        <ProgressStep
                            isCurrent={this.state.chainingConfigLoading}
                            variant={this.state.chainingConfigLoaded ? "success" : "pending"}
                            icon={!this.state.chainingConfigLoaded ? this.state.chainingConfigLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                            id="chainingConfigLoading"
                            titleId="load database chaining configuration"
                            aria-label="Loading database chaining configuration step"
                        >
                            {!this.state.chainingConfigLoaded ? _("Loading Chaining Config") : _("Chaining Config Loaded")}
                        </ProgressStep>
                        <ProgressStep
                            isCurrent={this.state.ldifsLoading}
                            variant={this.state.ldifsLoaded ? "success" : "pending"}
                            icon={!this.state.ldifsLoaded ? this.state.ldifsLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                            id="ldifLoading"
                            titleId="load ldif files"
                            aria-label="Loading ldif files step"
                        >
                            {!this.state.ldifsLoaded ? _("Loading LDIFs") : _("LDIFs Loaded")}
                        </ProgressStep>
                        <ProgressStep
                            isCurrent={this.state.backupsLoading}
                            variant={this.state.backupsLoaded ? "success" : "pending"}
                            icon={!this.state.backupsLoaded ? this.state.backupsLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                            id="backupLoading"
                            titleId="load backup files"
                            aria-label="Loading backup files step"
                        >
                            {!this.state.backupsLoaded ? _("Loading Backups") : _("Backups Loaded")}
                        </ProgressStep>
                        <ProgressStep
                            isCurrent={this.state.pwdSchemesLoading}
                            variant={this.state.pwdSchemesLoaded ? "success" : "pending"}
                            icon={!this.state.pwdSchemesLoaded ? this.state.pwdSchemesLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                            id="pwdSchemesLoading"
                            titleId="load password storage schemes"
                            aria-label="Loading password storage schemes step"
                        >
                            {!this.state.pwdSchemesLoaded ? _("Loading Password Schemes") : _("Password Schemes Loaded")}
                        </ProgressStep>
                        <ProgressStep
                            isCurrent={this.state.suffixListLoading}
                            variant={this.state.suffixListLoaded ? "success" : "pending"}
                            icon={!this.state.suffixListLoaded ? this.state.suffixListLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                            id="suffixListLoading"
                            titleId="load suffix list"
                            aria-label="Loading suffix list step"
                        >
                            {!this.state.suffixListLoaded ? _("Loading Suffix List") : _("Suffix List Loaded")}
                        </ProgressStep>
                        <ProgressStep
                            isCurrent={this.state.suffixTreeLoading}
                            variant={this.state.suffixTreeLoaded ? "success" : "pending"}
                            icon={!this.state.suffixTreeLoaded ? this.state.suffixTreeLoading ? <InProgressIcon /> : <PendingIcon /> : <CheckCircleIcon />}
                            id="suffixTreeLoading"
                            titleId="load suffix tree"
                            aria-label="Loading suffix tree step"
                        >
                            {!this.state.suffixTreeLoaded ? _("Loading Suffix Tree") : _("Suffix Tree Loaded")}
                        </ProgressStep>
                    </ProgressStepper>
                </div>
            );
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

        let saveBtnName = _("Create Suffix");
        const extraPrimaryProps = {};
        if (modalSpinning) {
            saveBtnName = _("Creating ...");
            extraPrimaryProps.spinnerAriaValueText = _("Creating");
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Create New Suffix")}
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
                        spinnerAriaValueText={modalSpinning ? _("Creating Suffix") : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <FormGroup
                        label={_("Suffix DN")}
                        fieldId="createSuffix"
                        title={_("Database suffix, like 'dc=example,dc=com'.  The suffix must be a valid LDAP Distinguished Name (DN).")}
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
                        <FormHelperText>
                                <HelperText>
                                <HelperTextItem icon={<ExclamationCircleIcon />} variant={error.createSuffix ? "error" : "default"}>
                                    {error.createBeName ? 'The DN of the suffix is invalid' : 'Required field'}
                                </HelperTextItem>
                                </HelperText>
                        </FormHelperText>
                    </FormGroup>
                    <FormGroup
                        label={_("Database Name")}
                        fieldId="suffixName"
                        title={_("The name for the backend database, like 'userroot'.  The name can be a combination of alphanumeric characters, dashes (-), and underscores (_). No other characters are allowed, and the name must be unique across all backends.")}
                    >
                        <TextInput
                            isRequired
                            type="text"
                            id="createBeName"
                            aria-describedby="createBeName"
                            name="suffixName"
                            onChange={handleChange}
                            validated={error.createBeName ? "error" : "noval"}
                        />
                        <FormHelperText>
                                <HelperText>
                                <HelperTextItem icon={<ExclamationCircleIcon />} variant={error.createBeName ? "error" : "default"}>
                                    {error.createBeName ? 'You must enter a name for the database' : 'Required field'}
                                </HelperTextItem>
                                </HelperText>
                        </FormHelperText>
                    </FormGroup>
                    <FormGroup
                        label={_("Initialization Option")}
                        fieldId="initOptions"
                    >
                        <FormSelect value={initOption} onChange={handleSelectChange} aria-label="FormSelect Input">
                            <FormSelectOption key={1} value="noInit" label={_("Do Not Initialize Database")} />
                            <FormSelectOption key={2} value="addSuffix" label={_("Create The Top Suffix Entry")} />
                            <FormSelectOption key={3} value="addSample" label={_("Add Sample Entries")} />
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
    handleChange: PropTypes.func,
    handleSelectChange: PropTypes.func,
    saveHandler: PropTypes.func,
    modalSpinning: PropTypes.bool,
    error: PropTypes.object,
};

CreateSuffixModal.defaultProps = {
    showModal: false,
    modalSpinning: false,
    error: {},
};
