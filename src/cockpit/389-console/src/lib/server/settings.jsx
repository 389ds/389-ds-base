import cockpit from "cockpit";
import React from "react";
import { log_cmd, valid_dn } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Form,
    FormHelperText,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    NumberInput,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';
import PropTypes from "prop-types";

const general_attrs = [
    'nsslapd-port',
    'nsslapd-secureport',
    'nsslapd-localhost',
    'nsslapd-listenhost',
    'nsslapd-bakdir',
    'nsslapd-ldifdir',
    'nsslapd-schemadir',
    'nsslapd-certdir'
];

const path_attrs = [
    'nsslapd-bakdir',
    'nsslapd-ldifdir',
    'nsslapd-schemadir',
    'nsslapd-certdir'
];

const rootdn_attrs = [
    'nsslapd-rootpwstoragescheme',
    'nsslapd-rootpw',
    'confirmRootpw',
];

const disk_attrs = [
    'nsslapd-disk-monitoring',
    'nsslapd-disk-monitoring-logging-critical',
    'nsslapd-disk-monitoring-threshold',
    'nsslapd-disk-monitoring-grace-period',
];

const adv_attrs = [
    'nsslapd-allow-anonymous-access',
    'nsslapd-entryusn-global',
    'nsslapd-ignore-time-skew',
    'nsslapd-readonly',
    'nsslapd-anonlimitsdn',
    'nsslapd-schemacheck',
    'nsslapd-syntaxcheck',
    'nsslapd-plugin-logging',
    'nsslapd-syntaxlogging',
    'nsslapd-plugin-binddn-tracking',
    'nsslapd-attribute-name-exceptions',
    'nsslapd-dn-validate-strict',
];

export class ServerSettings extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            loading: true,
            activeTabKey: 0,
            attrs: this.props.attrs,
            // Setting lists
            configSaveDisabled: true,
            configReloading: false,
            errObjConfig: {},
            rootDNReloading: false,
            rootDNSaveDisabled: true,
            errObjRootDN: {},
            diskMonReloading: false,
            diskMonSaveDisabled: true,
            errObjDiskMon: {},
            advSaveDisabled: true,
            advReloading: false,
            errObjAdv: {},
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.options = [
            { value: 'PBKDF2_SHA256', label: 'PBKDF2_SHA256', disabled: false },
            { value: 'SSHA512', label: 'SSHA512', disabled: false },
            { value: 'SSHA384', label: 'SSHA384', disabled: false },
            { value: 'SSHA256', label: 'SSHA256', disabled: false },
            { value: 'SSHA', label: 'SSHA', disabled: false },
            { value: 'MD5', label: 'MD5', disabled: false },
            { value: 'SMD5', label: 'SMD5', disabled: false },
            { value: 'CRYPT-MD5', label: 'CRYPT-MD5', disabled: false },
            { value: 'CRYPT-SHA512', label: 'CRYPT-SHA512', disabled: false },
            { value: 'CRYPT-SHA256', label: 'CRYPT-SHA256', disabled: false },
            { value: 'CRYPT', label: 'CRYPT', disabled: false },
            { value: 'GOST_YESCRYPT', label: 'GOST_YESCRYPT', disabled: false },
            { value: 'CLEAR', label: 'CLEAR', disabled: false },
        ];

        this.validatePaths = this.validatePaths.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
        this.reloadConfig = this.reloadConfig.bind(this);
        this.saveRootDN = this.saveRootDN.bind(this);
        this.reloadRootDN = this.reloadRootDN.bind(this);
        this.saveDiskMonitoring = this.saveDiskMonitoring.bind(this);
        this.reloadDiskMonitoring = this.reloadDiskMonitoring.bind(this);
        this.saveAdvanced = this.saveAdvanced.bind(this);
        this.reloadAdvanced = this.reloadAdvanced.bind(this);
        this.onMinusConfig = (id, nav_tab) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validateSaveBtn(nav_tab, id, Number(this.state[id])) });
        };
        this.onConfigChange = (event, id, min, max, nav_tab) => {
            let maxValue = this.maxValue;
            if (max !== 0) {
                maxValue = max;
            }
            let newValue = isNaN(event.target.value) ? min : Number(event.target.value);
            newValue = newValue > maxValue ? maxValue : newValue < min ? min : newValue
            this.setState({
                [id]: newValue
            }, () => { this.validateSaveBtn(nav_tab, id, newValue) });
        };
        this.onPlusConfig = (id, nav_tab) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validateSaveBtn(nav_tab, id, Number(this.state[id])) });
        }
        this.validateSaveBtn = this.validateSaveBtn.bind(this);
    }

    componentDidMount() {
        // Loading config
        if (!this.state.loaded) {
            this.loadConfig();
        } else {
            this.props.enableTree();
        }
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    validatePaths(disableSaveBtn) {
        let disableBtn = disableSaveBtn;
        const errObj = this.state.errObjConfig;

        for (const attr of path_attrs) {
            const cmd = `[ -d "${this.state[attr]}" ]`
            cockpit
                    .script(cmd, [], { superuser: true, err: "message" })
                    .done(output => {
                        errObj[attr] = false;
                        this.setState({
                            errObjConfig: errObj,
                            configSaveDisabled: disableBtn
                        });
                    })
                    .fail(err => {
                        errObj[attr] = true;
                        disableBtn = true;
                        this.setState({
                            configSaveDisabled: disableBtn,
                            errObjConfig: errObj
                        });
                    });
        }
    }

    validateSaveBtn(nav_tab, attr, value) {
        let disableSaveBtn = true;
        let disableBtnName = "";
        let config_attrs = [];
        let valueErr = false;
        let errObj;
        if (nav_tab == "config") {
            config_attrs = general_attrs;
            disableBtnName = "configSaveDisabled";
            errObj = this.state.errObjConfig;
        } else if (nav_tab == "rootdn") {
            disableBtnName = "rootDNSaveDisabled";
            config_attrs = rootdn_attrs;
            errObj = this.state.errObjRootDN;
        } else if (nav_tab == "diskmon") {
            disableBtnName = "diskMonSaveDisabled";
            config_attrs = disk_attrs;
            errObj = this.state.errObjDiskMon;
        } else if (nav_tab == "adv") {
            disableBtnName = "advSaveDisabled";
            config_attrs = adv_attrs;
            errObj = this.state.errObjAdv;
        }

        // Check if a setting was changed, if so enable the save button
        for (const config_attr of config_attrs) {
            if (attr == config_attr && this.state['_' + config_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const config_attr of config_attrs) {
            if (attr != config_attr && this.state['_' + config_attr] != this.state[config_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        if (nav_tab == "config") {
            if (attr != 'nsslapd-listenhost' && value == "") {
                // Only listenhost is allowed to be blank
                valueErr = true;
                disableSaveBtn = true;
            }
        } else if (nav_tab == "rootdn") {
            // Handle validating passwords are in sync
            if (attr == 'nsslapd-rootpw') {
                if (value != this.state.confirmRootpw) {
                    disableSaveBtn = true;
                    valueErr = true;
                    errObj['nsslapd-rootpw'] = true;
                } else {
                    errObj.confirmRootpw = false;
                    errObj['nsslapd-rootpw'] = false;
                }
            }
            if (attr == 'confirmRootpw') {
                if (value != this.state['nsslapd-rootpw']) {
                    disableSaveBtn = true;
                    valueErr = true;
                    errObj.confirmRootpw = true;
                } else {
                    errObj.confirmRootpw = false;
                    errObj['nsslapd-rootpw'] = false;
                }
            }

            if (value == "") {
                disableSaveBtn = true;
                valueErr = true;
            }
        } else if (nav_tab == "diskmon") {
            if (value == "" && (typeof value !== "boolean")) {
                valueErr = true;
                disableSaveBtn = true;
            }
            if (attr === 'nsslapd-disk-monitoring-threshold') {
                const numVal = Number(value);
                if (numVal < 4096) {
                    valueErr = true;
                    disableSaveBtn = true;
                }
            }
        } else if (nav_tab == "adv") {
            // Handle special cases for anon limit dn
            if (attr == 'nsslapd-anonlimitsdn' && !valid_dn(value)) {
                valueErr = true;
                errObj[attr] = true;
            }
            if (value == "" && attr != 'nsslapd-anonlimitsdn' && (typeof value !== "boolean")) {
                valueErr = true;
                disableSaveBtn = true;
            }
        }

        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            errObjConfig: errObj,
            [disableBtnName]: disableSaveBtn
        }, () => { this.validatePaths(disableSaveBtn) });
    }

    handleChange(e, nav_tab) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;

        this.setState({
            [attr]: value,
        }, () => { this.validateSaveBtn(nav_tab, attr, value) } );
    }

    loadConfig() {
        const attrs = this.state.attrs;
        // Handle the checkbox values
        let diskMonitoring = false;
        let diskLogCritical = false;
        let schemaCheck = false;
        let syntaxCheck = false;
        let pluginLogging = false;
        let syntaxLogging = false;
        let bindDNTracking = false;
        let nameExceptions = false;
        let dnValidate = false;
        let usnGlobal = false;
        let ignoreSkew = false;
        let readOnly = false;
        let listenhost = "";

        if (attrs['nsslapd-entryusn-global'][0] == "on") {
            usnGlobal = true;
        }
        if (attrs['nsslapd-ignore-time-skew'][0] == "on") {
            ignoreSkew = true;
        }
        if (attrs['nsslapd-readonly'][0] == "on") {
            readOnly = true;
        }
        if (attrs['nsslapd-disk-monitoring'][0] == "on") {
            diskMonitoring = true;
        }
        if (attrs['nsslapd-disk-monitoring-logging-critical'][0] == "on") {
            diskLogCritical = true;
        }
        if (attrs['nsslapd-schemacheck'][0] == "on") {
            schemaCheck = true;
        }
        if (attrs['nsslapd-syntaxcheck'][0] == "on") {
            syntaxCheck = true;
        }
        if (attrs['nsslapd-plugin-logging'][0] == "on") {
            pluginLogging = true;
        }
        if (attrs['nsslapd-syntaxlogging'][0] == "on") {
            syntaxLogging = true;
        }
        if (attrs['nsslapd-plugin-binddn-tracking'][0] == "on") {
            bindDNTracking = true;
        }
        if (attrs['nsslapd-attribute-name-exceptions'][0] == "on") {
            nameExceptions = true;
        }
        if (attrs['nsslapd-dn-validate-strict'][0] == "on") {
            dnValidate = true;
        }
        if ('nsslapd-listenhost' in attrs) {
            listenhost = attrs['nsslapd-listenhost'][0];
        }

        this.setState({
            loaded: true,
            loading: false,
            errObjConfig: {},
            errObjRootDN: {},
            errObjDiskMon: {},
            errObjAdv: {},
            // Settings
            'nsslapd-port': attrs['nsslapd-port'][0],
            'nsslapd-secureport': attrs['nsslapd-secureport'][0],
            'nsslapd-localhost': attrs['nsslapd-localhost'][0],
            'nsslapd-listenhost': listenhost,
            'nsslapd-bakdir': attrs['nsslapd-bakdir'][0],
            'nsslapd-ldifdir': attrs['nsslapd-ldifdir'][0],
            'nsslapd-schemadir': attrs['nsslapd-schemadir'][0],
            'nsslapd-certdir': attrs['nsslapd-certdir'][0],
            'nsslapd-rootdn': attrs['nsslapd-rootdn'][0],
            'nsslapd-rootpw': attrs['nsslapd-rootpw'][0],
            confirmRootpw: attrs['nsslapd-rootpw'][0],
            'nsslapd-rootpwstoragescheme': attrs['nsslapd-rootpwstoragescheme'][0],
            'nsslapd-anonlimitsdn': attrs['nsslapd-anonlimitsdn'][0],
            'nsslapd-disk-monitoring-threshold': attrs['nsslapd-disk-monitoring-threshold'][0],
            'nsslapd-disk-monitoring-grace-period': attrs['nsslapd-disk-monitoring-grace-period'][0],
            'nsslapd-allow-anonymous-access': attrs['nsslapd-allow-anonymous-access'][0],
            'nsslapd-disk-monitoring': diskMonitoring,
            'nsslapd-disk-monitoring-logging-critical': diskLogCritical,
            'nsslapd-schemacheck': schemaCheck,
            'nsslapd-syntaxcheck': syntaxCheck,
            'nsslapd-plugin-logging': pluginLogging,
            'nsslapd-syntaxlogging': syntaxLogging,
            'nsslapd-plugin-binddn-tracking': bindDNTracking,
            'nsslapd-attribute-name-exceptions': nameExceptions,
            'nsslapd-dn-validate-strict': dnValidate,
            'nsslapd-entryusn-global': usnGlobal,
            'nsslapd-ignore-time-skew': ignoreSkew,
            'nsslapd-readonly': readOnly,
            // Record original values
            '_nsslapd-port': attrs['nsslapd-port'][0],
            '_nsslapd-secureport': attrs['nsslapd-secureport'][0],
            '_nsslapd-localhost': attrs['nsslapd-localhost'][0],
            '_nsslapd-listenhost': listenhost,
            '_nsslapd-bakdir': attrs['nsslapd-bakdir'][0],
            '_nsslapd-ldifdir': attrs['nsslapd-ldifdir'][0],
            '_nsslapd-schemadir': attrs['nsslapd-schemadir'][0],
            '_nsslapd-certdir': attrs['nsslapd-certdir'][0],
            '_nsslapd-rootdn': attrs['nsslapd-rootdn'][0],
            '_nsslapd-rootpw': attrs['nsslapd-rootpw'][0],
            _confirmRootpw: attrs['nsslapd-rootpw'][0],
            '_nsslapd-rootpwstoragescheme': attrs['nsslapd-rootpwstoragescheme'][0],
            '_nsslapd-anonlimitsdn': attrs['nsslapd-anonlimitsdn'][0],
            '_nsslapd-disk-monitoring-threshold': attrs['nsslapd-disk-monitoring-threshold'][0],
            '_nsslapd-disk-monitoring-grace-period': attrs['nsslapd-disk-monitoring-grace-period'][0],
            '_nsslapd-allow-anonymous-access': attrs['nsslapd-allow-anonymous-access'][0],
            '_nsslapd-disk-monitoring': diskMonitoring,
            '_nsslapd-disk-monitoring-logging-critical': diskLogCritical,
            '_nsslapd-schemacheck': schemaCheck,
            '_nsslapd-syntaxcheck': syntaxCheck,
            '_nsslapd-plugin-logging': pluginLogging,
            '_nsslapd-syntaxlogging': syntaxLogging,
            '_nsslapd-plugin-binddn-tracking': bindDNTracking,
            '_nsslapd-attribute-name-exceptions': nameExceptions,
            '_nsslapd-dn-validate-strict': dnValidate,
            '_nsslapd-entryusn-global': usnGlobal,
            '_nsslapd-ignore-time-skew': ignoreSkew,
            '_nsslapd-readonly': readOnly,
        }, this.props.enableTree);
    }

    saveRootDN() {
        this.setState({
            rootDNReloading: true,
        });
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];

        for (const attr of rootdn_attrs) {
            if (attr != 'confirmRootpw' && this.state['_' + attr] != this.state[attr]) {
                cmd.push(attr + "=" + this.state[attr]);
            }
        }

        log_cmd("saveRootDN", "Saving changes to root DN", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.reloadRootDN();
                    this.props.addNotification(
                        "success",
                        "Successfully updated Directory Manager configuration"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.reloadRootDN();
                    this.props.addNotification(
                        "error",
                        `Error updating Directory Manager configuration - ${errMsg.desc}`
                    );
                });
    }

    reloadRootDN() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadConfig", "Reload Directory Manager configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    this.setState(() => (
                        {
                            rootDNReloading: false,
                            'nsslapd-rootdn': attrs['nsslapd-rootdn'][0],
                            'nsslapd-rootpw': attrs['nsslapd-rootpw'][0],
                            confirmRootpw: attrs['nsslapd-rootpw'][0],
                            'nsslapd-rootpwstoragescheme': attrs['nsslapd-rootpwstoragescheme'][0],
                            // Record original values
                            '_nsslapd-rootdn': attrs['nsslapd-rootdn'][0],
                            '_nsslapd-rootpw': attrs['nsslapd-rootpw'][0],
                            _confirmRootpw: attrs['nsslapd-rootpw'][0],
                            '_nsslapd-rootpwstoragescheme': attrs['nsslapd-rootpwstoragescheme'][0],
                            rootDNSaveDisabled: true
                        })
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.setState({
                        rootDNReloading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error reloading Directory Manager configuration - ${errMsg.desc}`
                    );
                });
    }

    saveDiskMonitoring() {
        this.setState({
            diskMonReloading: true,
        });
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];
        for (const attr of disk_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                let val = this.state[attr];
                if (typeof val === "boolean") {
                    if (val) {
                        val = "on";
                    } else {
                        val = "off";
                    }
                }
                cmd.push(attr + "=" + val);
            }
        }

        log_cmd("saveRootDN", "Saving changes to Disk Monitoring", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.reloadDiskMonitoring();
                    this.props.addNotification(
                        "success",
                        "Successfully updated Disk Monitoring configuration"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.reloadDiskMonitoring();
                    this.props.addNotification(
                        "error",
                        `Error updating Disk Monitoring configuration - ${errMsg.desc}`
                    );
                });
    }

    reloadDiskMonitoring() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadDiskMonitoring", "Reload Disk Monitoring configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    // Handle the checkbox values
                    let diskMonitoring = false;
                    let diskLogCritical = false;

                    if (attrs['nsslapd-disk-monitoring'][0] == "on") {
                        diskMonitoring = true;
                    }
                    if (attrs['nsslapd-disk-monitoring-logging-critical'][0] == "on") {
                        diskLogCritical = true;
                    }
                    this.setState(() => (
                        {
                            diskMonReloading: false,
                            'nsslapd-disk-monitoring-threshold': attrs['nsslapd-disk-monitoring-threshold'][0],
                            'nsslapd-disk-monitoring-grace-period': attrs['nsslapd-disk-monitoring-grace-period'][0],
                            'nsslapd-disk-monitoring': diskMonitoring,
                            'nsslapd-disk-monitoring-logging-critical': diskLogCritical,
                            // Record original values
                            '_nsslapd-disk-monitoring-threshold': attrs['nsslapd-disk-monitoring-threshold'][0],
                            '_nsslapd-disk-monitoring-grace-period': attrs['nsslapd-disk-monitoring-grace-period'][0],
                            '_nsslapd-disk-monitoring': diskMonitoring,
                            '_nsslapd-disk-monitoring-logging-critical': diskLogCritical,
                            diskMonSaveDisabled: true
                        })
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.setState({
                        diskMonReloading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error reloading Disk Monitoring configuration - ${errMsg.desc}`
                    );
                });
    }

    saveAdvanced() {
        this.setState({
            advReloading: true,
        });
        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];
        for (const attr of adv_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                let val = this.state[attr];
                if (typeof val === "boolean") {
                    if (val) {
                        val = "on";
                    } else {
                        val = "off";
                    }
                }
                cmd.push(attr + "=" + val);
            }
        }

        log_cmd("saveAdvanced", "Saving Advanced configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.reloadAdvanced();
                    this.props.addNotification(
                        "success",
                        "Successfully updated Advanced configuration"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.reloadAdvanced();
                    this.props.addNotification(
                        "error",
                        `Error updating Advanced configuration - ${errMsg.desc}`
                    );
                });
    }

    reloadAdvanced() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadAdvanced", "Reload Advanced configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    // Handle the checkbox values
                    let schemaCheck = false;
                    let syntaxCheck = false;
                    let pluginLogging = false;
                    let syntaxLogging = false;
                    let bindDNTracking = false;
                    let nameExceptions = false;
                    let dnValidate = false;
                    let usnGlobal = false;
                    let ignoreSkew = false;
                    let readOnly = false;

                    if (attrs['nsslapd-entryusn-global'][0] == "on") {
                        usnGlobal = true;
                    }
                    if (attrs['nsslapd-ignore-time-skew'][0] == "on") {
                        ignoreSkew = true;
                    }
                    if (attrs['nsslapd-readonly'][0] == "on") {
                        readOnly = true;
                    }
                    if (attrs['nsslapd-schemacheck'][0] == "on") {
                        schemaCheck = true;
                    }
                    if (attrs['nsslapd-syntaxcheck'][0] == "on") {
                        syntaxCheck = true;
                    }
                    if (attrs['nsslapd-plugin-logging'][0] == "on") {
                        pluginLogging = true;
                    }
                    if (attrs['nsslapd-syntaxlogging'][0] == "on") {
                        syntaxLogging = true;
                    }
                    if (attrs['nsslapd-plugin-binddn-tracking'][0] == "on") {
                        bindDNTracking = true;
                    }
                    if (attrs['nsslapd-attribute-name-exceptions'][0] == "on") {
                        nameExceptions = true;
                    }
                    if (attrs['nsslapd-dn-validate-strict'][0] == "on") {
                        dnValidate = true;
                    }

                    this.setState(() => (
                        {
                            'nsslapd-anonlimitsdn': attrs['nsslapd-anonlimitsdn'][0],
                            'nsslapd-allow-anonymous-access': attrs['nsslapd-allow-anonymous-access'][0],
                            'nsslapd-schemacheck': schemaCheck,
                            'nsslapd-syntaxcheck': syntaxCheck,
                            'nsslapd-plugin-logging': pluginLogging,
                            'nsslapd-syntaxLogging': syntaxLogging,
                            'nsslapd-plugin-binddn-tracking': bindDNTracking,
                            'nsslapd-attribute-name-exceptions': nameExceptions,
                            'nsslapd-dn-validate-strict': dnValidate,
                            'nsslapd-entryusn-global': usnGlobal,
                            'nsslapd-ignore-time-skew': ignoreSkew,
                            'nsslapd-readonly': readOnly,
                            // Record original values
                            '_nsslapd-anonlimitsdn': attrs['nsslapd-anonlimitsdn'][0],
                            '_nsslapd-allow-anonymous-access': attrs['nsslapd-allow-anonymous-access'][0],
                            '_nsslapd-schemacheck': schemaCheck,
                            '_nsslapd-syntaxcheck': syntaxCheck,
                            '_nsslapd-plugin-logging': pluginLogging,
                            '_nsslapd-syntaxLogging': syntaxLogging,
                            '_nsslapd-plugin-binddn-tracking': bindDNTracking,
                            '_nsslapd-attribute-name-exceptions': nameExceptions,
                            '_nsslapd-dn-validate-strict': dnValidate,
                            '_nsslapd-entryusn-global': usnGlobal,
                            '_nsslapd-ignore-time-skew': ignoreSkew,
                            '_nsslapd-readonly': readOnly,
                            advReloading: false,
                            advSaveDisabled: true,
                        })
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading Advanced configuration - ${errMsg.desc}`
                    );
                    this.setState({
                        advReloading: false,
                    });
                });
    }

    saveConfig() {
        // Build up the command list
        this.setState({
            configReloading: true,
        });
        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];

        for (const attr of general_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                cmd.push(attr + "=" + this.state[attr]);
            }
        }

        log_cmd("saveConfig", "Applying server config change", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    // Continue with the next mod
                    this.reloadConfig();
                    this.props.addNotification(
                        "warning",
                        "Successfully updated server configuration.  These " +
                            "changes require the server to be restarted to take effect."
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.reloadConfig();
                    this.props.addNotification(
                        "error",
                        `Error updating server configuration - ${errMsg.desc}`
                    );
                });
    }

    reloadConfig() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadConfig", "Reload server configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    let listenhost = "";

                    if ('nsslapd-listenhost' in attrs) {
                        listenhost = attrs['nsslapd-listenhost'][0];
                    }
                    this.setState(() => (
                        {
                            configReloading: false,
                            configSaveDisabled: true,
                            errObjConfig: {},
                            errObjRootDN: {},
                            errObjDiskMon: {},
                            errObjAdv: {},
                            'nsslapd-port': attrs['nsslapd-port'][0],
                            'nsslapd-secureport': attrs['nsslapd-secureport'][0],
                            'nsslapd-localhost': attrs['nsslapd-localhost'][0],
                            'nsslapd-listenhost': listenhost,
                            'nsslapd-bakdir': attrs['nsslapd-bakdir'][0],
                            'nsslapd-ldifdir': attrs['nsslapd-ldifdir'][0],
                            'nsslapd-schemadir': attrs['nsslapd-schemadir'][0],
                            'nsslapd-certdir': attrs['nsslapd-certdir'][0],
                            // Record original values
                            '_nsslapd-port': attrs['nsslapd-port'][0],
                            '_nsslapd-secureport': attrs['nsslapd-secureport'][0],
                            '_nsslapd-localhost': attrs['nsslapd-localhost'][0],
                            '_nsslapd-listenhost': listenhost,
                            '_nsslapd-bakdir': attrs['nsslapd-bakdir'][0],
                            '_nsslapd-ldifdir': attrs['nsslapd-ldifdir'][0],
                            '_nsslapd-schemadir': attrs['nsslapd-schemadir'][0],
                            '_nsslapd-certdir': attrs['nsslapd-certdir'][0],
                        })
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error reloading server configuration - ${errMsg.desc}`
                    );
                    this.setState({
                        configReloading: false,
                    });
                });
    }

    render() {
        let body = "";
        let diskMonitor = "";

        let saveBtnName = "Save Settings";
        const extraPrimaryProps = {};
        if (this.state.configReloading || this.state.rootDNReloading ||
            this.state.diskMonReloading || this.state.advReloading) {
            saveBtnName = "Saving settings ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        if (this.state['nsslapd-disk-monitoring']) {
            diskMonitor =
                <Form isHorizontal autoComplete="off" className="ds-margin-top-lg ds-left-indent-lg ds-margin-bottom">
                    <Grid
                        title="The available disk space, in bytes, that will trigger the shutdown process. Default is 2mb. Once below half of the threshold then we enter the shutdown mode. Value range: 4096 - 9223372036854775807. (nsslapd-disk-monitoring-threshold)"
                    >
                        <GridItem className="ds-label" span={3}>
                            Disk Monitoring Threshold
                        </GridItem>
                        <GridItem span={9}>
                            <NumberInput
                                value={this.state['nsslapd-disk-monitoring-threshold']}
                                min={4096}
                                max={9223372036854775807}
                                onMinus={() => { this.onMinusConfig("nsslapd-disk-monitoring-threshold", "diskmon") }}
                                onChange={(e) => { this.onConfigChange(e, "nsslapd-disk-monitoring-threshold", 1, 9223372036854775807, "diskmon") }}
                                onPlus={() => { this.onPlusConfig("nsslapd-disk-monitoring-threshold", "diskmon") }}
                                inputName="input"
                                inputAriaLabel="number input"
                                minusBtnAriaLabel="minus"
                                plusBtnAriaLabel="plus"
                                widthChars={8}
                            />
                            <FormHelperText isError isHidden={!this.state.errObjDiskMon['nsslapd-disk-monitoring-threshold']}>
                                Value must be greater than or equal to 4096
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid
                        title="How many minutes to wait to allow an admin to clean up disk space before shutting slapd down. The default is 60 minutes. (nsslapd-disk-monitoring-grace-period)"
                    >
                        <GridItem className="ds-label" span={3}>
                            Disk Monitoring Grace Period
                        </GridItem>
                        <GridItem span={9}>
                            <NumberInput
                                value={this.state['nsslapd-disk-monitoring-grace-period']}
                                min={1}
                                max={2147483647}
                                onMinus={() => { this.onMinusConfig("nsslapd-disk-monitoring-grace-period", "diskmon") }}
                                onChange={(e) => { this.onConfigChange(e, "nsslapd-disk-monitoring-grace-period", 1, 2147483647, "diskmon") }}
                                onPlus={() => { this.onPlusConfig("nsslapd-disk-monitoring-grace-period", "diskmon") }}
                                inputName="input"
                                inputAriaLabel="number input"
                                minusBtnAriaLabel="minus"
                                plusBtnAriaLabel="plus"
                                widthChars={8}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        className="ds-margin-top"
                        title="When disk space gets critically low do not remove logs to free up disk space. (nsslapd-disk-monitoring-logging-critical)"
                    >
                        <GridItem span={9}>
                            <Checkbox
                                id="nsslapd-disk-monitoring-logging-critical"
                                isChecked={this.state['nsslapd-disk-monitoring-logging-critical']}
                                onChange={(str, e) => {
                                    this.handleChange(e, "diskmon");
                                }}
                                label="Preserve Logs Even If Disk Space Gets Low"
                            />
                        </GridItem>
                    </Grid>
                </Form>;
        }

        if (this.state.loading) {
            body =
                <div className="ds-loading-spinner ds-margin-top ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>Loading Server Settings ...</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="md" />
                </div>;
        } else {
            body =
                <div className="ds-margin-bottom-md">
                    <Grid>
                        <GridItem span={12}>
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Server Settings <FontAwesomeIcon
                                        size="lg"
                                        className="ds-left-margin ds-refresh"
                                        icon={faSyncAlt}
                                        title="Refresh configuration settings"
                                        onClick={this.reloadConfig}
                                    />
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>

                    <div className={this.state.loading ? 'ds-fadeout' : 'ds-fadein ds-left-margin'}>
                        <Tabs isFilled className="ds-margin-top-lg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                            <Tab eventKey={0} title={<TabTitleText>General Settings</TabTitleText>}>
                                <Form autoComplete="off" className="ds-margin-top-xlg">
                                    <Grid
                                        title="The version of the Directory Server package"
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            Server Version
                                        </GridItem>
                                        <GridItem span={10}>
                                            <TextInput
                                                value={this.props.version}
                                                type="text"
                                                id="server-version"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="server-version"
                                                isDisabled
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The server's local hostname (nsslapd-localhost)."
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            Server Hostname
                                        </GridItem>
                                        <GridItem span={10}>
                                            <TextInput
                                                value={this.state['nsslapd-localhost']}
                                                type="text"
                                                id="nsslapd-localhost"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="server-hostname"
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "config");
                                                }}
                                                validated={this.state.errObjConfig['nsslapd-localhost'] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The server's port number (nsslapd-port)."
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            LDAP Port
                                        </GridItem>
                                        <GridItem span={10}>
                                            <NumberInput
                                                value={this.state['nsslapd-port']}
                                                min={1}
                                                max={65534}
                                                onMinus={() => { this.onMinusConfig("nsslapd-port", "config") }}
                                                onChange={(e) => { this.onConfigChange(e, "nsslapd-port", 1, 65534, "config") }}
                                                onPlus={() => { this.onPlusConfig("nsslapd-port", "config") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={8}
                                                validated={this.state.errObjConfig['nsslapd-port'] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The server's secure port number (nsslapd-secureport)."
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            LDAPS Port
                                        </GridItem>
                                        <GridItem span={10}>
                                            <NumberInput
                                                value={this.state['nsslapd-secureport']}
                                                min={1}
                                                max={65534}
                                                onMinus={() => { this.onMinusConfig("nsslapd-secureport", "config") }}
                                                onChange={(e) => { this.onConfigChange(e, "nsslapd-secureport", 1, 65534, "config") }}
                                                onPlus={() => { this.onPlusConfig("nsslapd-secureport", "config") }}
                                                inputName="input"
                                                inputAriaLabel="number input"
                                                minusBtnAriaLabel="minus"
                                                plusBtnAriaLabel="plus"
                                                widthChars={8}
                                                validated={this.state.errObjConfig['nsslapd-secureport'] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="This parameter can be used to restrict the Directory Server instance to a single IP interface (hostname, or IP address).  Requires restart. (nsslapd-listenhost)."
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            Listen Host Address
                                        </GridItem>
                                        <GridItem span={10}>
                                            <TextInput
                                                value={this.state['nsslapd-listenhost']}
                                                type="text"
                                                id="nsslapd-listenhost"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="server-listenhost"
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "config");
                                                }}
                                                validated={this.state.errObjConfig['nsslapd-listenhost'] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title={this.state.errObjConfig['nsslapd-bakdir'] ? "Invalid backup directory path!" : "The location where database backups are stored (nsslapd-bakdir)."}
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            Backup Directory
                                        </GridItem>
                                        <GridItem span={10}>
                                            <TextInput
                                                value={this.state['nsslapd-bakdir']}
                                                type="text"
                                                id="nsslapd-bakdir"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="server-bakdir"
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "config");
                                                }}
                                                validated={this.state.errObjConfig['nsslapd-bakdir'] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title={this.state.errObjConfig['nsslapd-ldifdir'] ? "Invalid LDIF directory path!" : "The location where the server's LDIF files are located (nsslapd-ldifdir)."}
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            LDIF File Directory
                                        </GridItem>
                                        <GridItem span={10}>
                                            <TextInput
                                                value={this.state['nsslapd-ldifdir']}
                                                type="text"
                                                id="nsslapd-ldifdir"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="server-ldifdir"
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "config");
                                                }}
                                                validated={this.state.errObjConfig['nsslapd-ldifdir'] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title={this.state.errObjConfig['nsslapd-schemadir'] ? "Invalid schema directory path!" : "The location for the servers custom schema files. (nsslapd-schemadir)."}
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            Schema Directory
                                        </GridItem>
                                        <GridItem span={10}>
                                            <TextInput
                                                value={this.state['nsslapd-schemadir']}
                                                type="text"
                                                id="nsslapd-schemadir"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="server-schemadir"
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "config");
                                                }}
                                                validated={this.state.errObjConfig['nsslapd-schemadir'] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title={this.state.errObjConfig['nsslapd-certdir'] ? "Invalid certificate directory path!" : "The location of the server's certificates (nsslapd-certdir)."}
                                    >
                                        <GridItem className="ds-label" span={2}>
                                            Certificate Directory
                                        </GridItem>
                                        <GridItem span={10}>
                                            <TextInput
                                                value={this.state['nsslapd-certdir']}
                                                type="text"
                                                id="nsslapd-certdir"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="server-certdir"
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "config");
                                                }}
                                                validated={this.state.errObjConfig['nsslapd-certdir'] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                </Form>
                                <Button
                                    isDisabled={this.state.configSaveDisabled || this.state.configReloading}
                                    variant="primary"
                                    className="ds-margin-top-xlg"
                                    onClick={this.saveConfig}
                                    isLoading={this.state.configReloading}
                                    spinnerAriaValueText={this.state.configReloading ? "Saving" : undefined}
                                    {...extraPrimaryProps}
                                >
                                    {saveBtnName}
                                </Button>
                            </Tab>

                            <Tab eventKey={1} title={<TabTitleText>Directory Manager</TabTitleText>}>
                                <Form className="ds-margin-top-xlg" isHorizontal autoComplete="off">
                                    <Grid
                                        title="The DN of the unrestricted directory manager (nsslapd-rootdn)."
                                    >
                                        <GridItem className="ds-label" span={3}>
                                            Directory Manager DN
                                        </GridItem>
                                        <GridItem span={9}>
                                            <TextInput
                                                value={this.state['nsslapd-rootdn']}
                                                type="text"
                                                id="nsslapd-rootdn"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="nsslapd-rootdn"
                                                isDisabled
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The password for the Root DN/Directory Manager (nsslapd-rootpw)."
                                    >
                                        <GridItem className="ds-label" span={3}>
                                            Directory Manager Password
                                        </GridItem>
                                        <GridItem span={9}>
                                            <TextInput
                                                value={this.state['nsslapd-rootpw']}
                                                type="password"
                                                id="nsslapd-rootpw"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="nsslapd-rootpw"
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "rootdn");
                                                }}
                                                validated={this.state.errObjRootDN['nsslapd-rootpw'] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="Confirm the Directory Manager password"
                                    >
                                        <GridItem className="ds-label" span={3}>
                                            Confirm Password
                                        </GridItem>
                                        <GridItem span={9}>
                                            <TextInput
                                                value={this.state.confirmRootpw}
                                                type="password"
                                                id="confirmRootpw"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="confirmRootpw"
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "rootdn");
                                                }}
                                                validated={this.state.errObjRootDN.confirmRootpw ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="Set the Directory Manager password storage scheme (nsslapd-rootpwstoragescheme)."
                                    >
                                        <GridItem className="ds-label" span={3}>
                                            Password Storage Scheme
                                        </GridItem>
                                        <GridItem span={9}>
                                            <FormSelect
                                                id="nsslapd-rootpwstoragescheme"
                                                value={this.state['nsslapd-rootpwstoragescheme']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "rootdn");
                                                }}
                                                aria-label="FormSelect Input"
                                            >
                                                {this.options.map((option, index) => (
                                                    <FormSelectOption key={index} value={option.value} label={option.label} />
                                                ))}
                                            </FormSelect>
                                        </GridItem>
                                    </Grid>
                                </Form>
                                <Button
                                    variant="primary"
                                    className="ds-margin-top-xlg"
                                    isDisabled={this.state.rootDNSaveDisabled || this.state.rootDNReloading}
                                    onClick={this.saveRootDN}
                                    isLoading={this.state.rootDNReloading}
                                    spinnerAriaValueText={this.state.rootDNReloading ? "Saving" : undefined}
                                    {...extraPrimaryProps}
                                >
                                    {saveBtnName}
                                </Button>
                            </Tab>
                            <Tab eventKey={2} title={<TabTitleText>Disk Monitoring</TabTitleText>}>
                                <Form className="ds-margin-left ds-margin-top-xlg" autoComplete="off">
                                    <Checkbox
                                        id="nsslapd-disk-monitoring"
                                        isChecked={this.state['nsslapd-disk-monitoring']}
                                        onChange={(str, e) => {
                                            this.handleChange(e, "diskmon");
                                        }}
                                        label="Enable Disk Space Monitoring"
                                    />
                                </Form>
                                {diskMonitor}
                                <Button
                                    isDisabled={this.state.diskMonSaveDisabled || this.state.diskMonReloading}
                                    variant="primary"
                                    className="ds-margin-top-xlg"
                                    onClick={this.saveDiskMonitoring}
                                    isLoading={this.state.diskMonReloading}
                                    spinnerAriaValueText={this.state.diskMonReloading ? "Saving" : undefined}
                                    {...extraPrimaryProps}
                                >
                                    {saveBtnName}
                                </Button>
                            </Tab>
                            <Tab eventKey={3} title={<TabTitleText>Advanced Settings</TabTitleText>}>
                                <Form className="ds-margin-top-xlg ds-margin-left" isHorizontal autoComplete="off">
                                    <Grid>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-schemacheck"
                                                isChecked={this.state['nsslapd-schemacheck']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                title="Enable schema checking (nsslapd-schemacheck)."
                                                aria-label="uncontrolled checkbox example"
                                                label="Enable Schema Checking"
                                            />
                                        </GridItem>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-syntaxcheck"
                                                isChecked={this.state['nsslapd-syntaxcheck']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                title="Enable attribute syntax checking (nsslapd-syntaxcheck)."
                                                label="Enable Attribute Syntax Checking"
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-plugin-logging"
                                                isChecked={this.state['nsslapd-plugin-logging']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                title="Enable plugins to log access and audit events.  (nsslapd-plugin-logging)."
                                                label="Enable Plugin Logging"
                                            />
                                        </GridItem>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-syntaxlogging"
                                                isChecked={this.state['nsslapd-syntaxlogging']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                title="Enable syntax logging (nsslapd-syntaxlogging)."
                                                label="Enable Attribute Syntax Logging"
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-plugin-binddn-tracking"
                                                isChecked={this.state['nsslapd-plugin-binddn-tracking']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                label="Enable Plugin Bind DN Tracking"
                                                title="Enabling this feature will write new operational attributes to the modified entry: internalModifiersname & internalCreatorsname. These new attributes contain the plugin DN, while modifiersname will be the original binding entry that triggered the update. (nsslapd-plugin-binddn-tracking)."
                                            />
                                        </GridItem>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-attribute-name-exceptions"
                                                isChecked={this.state['nsslapd-attribute-name-exceptions']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                title="Allows non-standard characters in attribute names to be used for backwards compatibility with older servers (nsslapd-attribute-name-exceptions)."
                                                label="Allow Attribute Naming Exceptions"
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-dn-validate-strict"
                                                isChecked={this.state['nsslapd-dn-validate-strict']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                label="Strict DN Syntax Validation"
                                                title="Enables strict syntax validation for DNs, according to section 3 in RFC 4514 (nsslapd-dn-validate-strict)."
                                            />
                                        </GridItem>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-entryusn-global"
                                                isChecked={this.state['nsslapd-entryusn-global']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                title="For USN plugin - maintain unique USNs across all back end databases (nsslapd-entryusn-global)."
                                                label="Maintain Unique USNs Across All Backends"
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-ignore-time-skew"
                                                isChecked={this.state['nsslapd-ignore-time-skew']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                title="Ignore replication time skew when acquiring a replica to start a replciation session (nsslapd-ignore-time-skew)."
                                                label="Ignore CSN Time Skew"
                                            />
                                        </GridItem>
                                        <GridItem span={5}>
                                            <Checkbox
                                                id="nsslapd-readonly"
                                                isChecked={this.state['nsslapd-readonly']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                title="Make entire server read-only (nsslapd-readonly)"
                                                label="Server Read-Only"
                                            />
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        className="ds-margin-top"
                                        title="Allow anonymous binds to the server (nsslapd-allow-anonymous-access)."
                                    >
                                        <GridItem className="ds-label" span={3}>
                                            Allow Anonymous Access
                                        </GridItem>
                                        <GridItem span={9}>
                                            <FormSelect
                                                id="nsslapd-allow-anonymous-access"
                                                value={this.state['nsslapd-allow-anonymous-access']}
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                aria-label="FormSelect Input"
                                            >
                                                <FormSelectOption key="0" value="on" label="on" />
                                                <FormSelectOption key="1" value="off" label="off" />
                                                <FormSelectOption
                                                    key="2"
                                                    value="rootdse"
                                                    label="rootdse"
                                                    title="Allows anonymous search and read access to search the root DSE itself, but restricts access to all other directory entries. "
                                                />
                                            </FormSelect>
                                        </GridItem>
                                    </Grid>
                                    <Grid
                                        title="The DN of a template entry containing the resource limits to apply to anonymous connections (nsslapd-anonlimitsdn)."
                                    >
                                        <GridItem className="ds-label" span={3}>
                                            Anonymous Resource Limits DN
                                        </GridItem>
                                        <GridItem span={9}>
                                            <TextInput
                                                value={this.state['nsslapd-anonlimitsdn']}
                                                type="text"
                                                id="nsslapd-anonlimitsdn"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="nsslapd-anonlimitsdn"
                                                onChange={(str, e) => {
                                                    this.handleChange(e, "adv");
                                                }}
                                                validated={this.state.errObjAdv.anonLimitsDN ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                </Form>
                                <Button
                                    isDisabled={this.state.advSaveDisabled || this.state.advReloading}
                                    variant="primary"
                                    className="ds-margin-top-xlg"
                                    onClick={this.saveAdvanced}
                                    isLoading={this.state.advReloading}
                                    spinnerAriaValueText={this.state.advReloading ? "Saving" : undefined}
                                    {...extraPrimaryProps}
                                >
                                    {saveBtnName}
                                </Button>
                            </Tab>
                        </Tabs>
                    </div>
                </div>;
        }

        return (
            <div
                id="server-settings-page" className={this.state.configReloading || this.state.rootDNReloading ||
                this.state.diskMonReloading || this.state.advReloading ? "ds-disabled" : ""}
            >
                {body}
            </div>
        );
    }
}

// Property types and defaults

ServerSettings.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    version: PropTypes.string,
    attrs: PropTypes.object,
};

ServerSettings.defaultProps = {
    serverId: "",
    version: "",
    attrs: {},
};
