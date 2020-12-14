import cockpit from "cockpit";
import React from "react";
import { log_cmd, valid_dn } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Col,
    ControlLabel,
    Form,
    FormControl,
    Icon,
    Nav,
    NavItem,
    Row,
    Spinner,
    TabContainer,
    TabContent,
    noop,
    TabPane,
} from "patternfly-react";
import PropTypes from "prop-types";
import "../../css/ds.css";

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
            activeKey: 1,
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

        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleConfigChange = this.handleConfigChange.bind(this);
        this.handleRootDNChange = this.handleRootDNChange.bind(this);
        this.handleDiskMonChange = this.handleDiskMonChange.bind(this);
        this.handleAdvChange = this.handleAdvChange.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
        this.reloadConfig = this.reloadConfig.bind(this);
        this.saveRootDN = this.saveRootDN.bind(this);
        this.reloadRootDN = this.reloadRootDN.bind(this);
        this.saveDiskMonitoring = this.saveDiskMonitoring.bind(this);
        this.reloadDiskMonitoring = this.reloadDiskMonitoring.bind(this);
        this.saveAdvanced = this.saveAdvanced.bind(this);
        this.reloadAdvanced = this.reloadAdvanced.bind(this);
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

    handleConfigChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;
        let valueErr = false;
        let errObj = this.state.errObjConfig;

        // Check if a setting was changed, if so enable the save button
        for (let general_attr of general_attrs) {
            if (attr == general_attr && this.state['_' + general_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let general_attr of general_attrs) {
            if (attr != general_attr && this.state['_' + general_attr] != this.state[general_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        if (attr != 'nsslapd-listenhost' && value == "") {
            // Only listenhost is allowed to be blank
            valueErr = true;
            disableSaveBtn = true;
        }
        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            configSaveDisabled: disableSaveBtn,
            errObjConfig: errObj,
        });
    }

    handleRootDNChange(e) {
        let value = e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;
        let valueErr = false;
        let errObj = this.state.errObjRootDN;

        // Check if a setting was changed, if so enable the save button
        for (let rootdn_attr of rootdn_attrs) {
            if (attr == rootdn_attr && this.state['_' + rootdn_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let rootdn_attr of rootdn_attrs) {
            if (attr != rootdn_attr && this.state['_' + rootdn_attr] != this.state[rootdn_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        // Handle validating passwords are in sync
        if (attr == 'nsslapd-rootpw') {
            if (value != this.state.confirmRootpw) {
                disableSaveBtn = true;
                errObj['nsslapd-rootpw'] = true;
            } else {
                errObj['nsslapd-rootpw'] = false;
            }
        }
        if (attr == 'confirmRootpw') {
            if (value != this.state['nsslapd-rootpw']) {
                disableSaveBtn = true;
                errObj['confirmRootpw'] = true;
            } else {
                errObj['confirmRootpw'] = false;
            }
        }

        if (value == "") {
            disableSaveBtn = true;
            valueErr = true;
        }
        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            rootDNSaveDisabled: disableSaveBtn,
            errObjRootDN: errObj
        });
    }

    handleDiskMonChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;
        let valueErr = false;
        let errObj = this.state.errObjDiskMon;

        // Check if a setting was changed, if so enable the save button
        for (let disk_attr of disk_attrs) {
            if (attr == disk_attr && this.state['_' + disk_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let disk_attr of disk_attrs) {
            if (attr != disk_attr && this.state['_' + disk_attr] != this.state[disk_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        if (value == "" && e.target.type !== 'checkbox') {
            valueErr = true;
            disableSaveBtn = true;
        }
        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            diskMonSaveDisabled: disableSaveBtn,
            errObjDiskMon: errObj
        });
    }

    handleAdvChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;
        let valueErr = false;
        let errObj = this.state.errObjAdv;

        // Check if a setting was changed, if so enable the save button
        for (let adv_attr of adv_attrs) {
            if (attr == adv_attr && this.state['_' + adv_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let adv_attr of adv_attrs) {
            if (attr != adv_attr && this.state['_' + adv_attr] != this.state[adv_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        // Handle special cases for anon limit dn
        if (attr == 'nsslapd-anonlimitsdn' && !valid_dn(value)) {
            errObj[attr] = true;
        }
        if (value == "" && attr != 'nsslapd-anonlimitsdn' && e.target.type !== 'checkbox') {
            valueErr = true;
            disableSaveBtn = true;
        }

        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            advSaveDisabled: disableSaveBtn,
            errObjAdv: errObj,
        });
    }

    loadConfig() {
        let attrs = this.state.attrs;
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
            'confirmRootpw': attrs['nsslapd-rootpw'][0],
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
            '_confirmRootpw': attrs['nsslapd-rootpw'][0],
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
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];

        for (let attr of rootdn_attrs) {
            if (attr != 'confirmRootpw' && this.state['_' + attr] != this.state[attr]) {
                cmd.push(attr + "=" + this.state[attr]);
            }
        }

        log_cmd("saveRootDN", "Saving changes to root DN", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.reloadRootDN();
                    this.props.addNotification(
                        "success",
                        "Successfully updated Directory Manager configuration"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.reloadRootDN();
                    this.props.addNotification(
                        "error",
                        `Error updating Directory Manager configuration - ${errMsg.desc}`
                    );
                });
    }

    reloadRootDN() {
        this.setState({
            rootDNReloading: true,
        });
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadConfig", "Reload Directory Manager configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    this.setState(() => (
                        {
                            rootDNReloading: false,
                            'nsslapd-rootdn': attrs['nsslapd-rootdn'][0],
                            'nsslapd-rootpw': attrs['nsslapd-rootpw'][0],
                            'confirmRootpw': attrs['nsslapd-rootpw'][0],
                            'nsslapd-rootpwstoragescheme': attrs['nsslapd-rootpwstoragescheme'][0],
                            // Record original values
                            '_nsslapd-rootdn': attrs['nsslapd-rootdn'][0],
                            '_nsslapd-rootpw': attrs['nsslapd-rootpw'][0],
                            '_confirmRootpw': attrs['nsslapd-rootpw'][0],
                            '_nsslapd-rootpwstoragescheme': attrs['nsslapd-rootpwstoragescheme'][0],
                            rootDNSaveDisabled: true
                        })
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
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
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];

        for (let attr of disk_attrs) {
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
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.reloadDiskMonitoring();
                    this.props.addNotification(
                        "success",
                        "Successfully updated Disk Monitoring configuration"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.reloadDiskMonitoring();
                    this.props.addNotification(
                        "error",
                        `Error updating Disk Monitoring configuration - ${errMsg.desc}`
                    );
                });
    }

    reloadDiskMonitoring() {
        this.setState({
            diskMonReloading: true,
        });
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadDiskMonitoring", "Reload Disk Monitoring configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
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
                    let errMsg = JSON.parse(err);
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
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];
        for (let attr of adv_attrs) {
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
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.reloadAdvanced();
                    this.props.addNotification(
                        "success",
                        "Successfully updated Advanced configuration"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.reloadAdvanced();
                    this.props.addNotification(
                        "error",
                        `Error updating Advanced configuration - ${errMsg.desc}`
                    );
                });
    }

    reloadAdvanced() {
        this.setState({
            advReloading: true,
        });
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadAdvanced", "Reload Advanced configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
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
                    let errMsg = JSON.parse(err);
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
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];

        for (let attr of general_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                cmd.push(attr + "=" + this.state[attr]);
            }
        }

        log_cmd("saveConfig", "Applying server config change", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    // Continue with the next mod
                    this.reloadConfig();
                    this.props.addNotification(
                        "success",
                        "Successfully updated server configuration.  These " +
                            "changes require the server to be restarted to take effect."
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.reloadConfig();
                    this.props.addNotification(
                        "error",
                        `Error updating server configuration - ${errMsg.desc}`
                    );
                });
    }

    reloadConfig() {
        this.setState({
            configReloading: true,
        });
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadConfig", "Reload server configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    let listenhost = "";

                    if ('nsslapd-listenhost' in attrs) {
                        listenhost = attrs['nsslapd-listenhost'][0];
                    }
                    this.setState(() => (
                        {
                            configReloading: false,
                            configSaveDisabled: true,
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
                    let errMsg = JSON.parse(err);
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
        let reloadSpinner = "";
        let diskMonitor = "";

        if (this.state['nsslapd-disk-monitoring']) {
            diskMonitor =
                <Form horizontal className="ds-margin-top">
                    <Row
                        className="ds-margin-top"
                        title="The available disk space, in bytes, that will trigger the shutdown process. Default is 2mb. Once below half of the threshold then we enter the shutdown mode. (nsslapd-disk-monitoring-threshold)"
                    >
                        <Col componentClass={ControlLabel} sm={4}>
                            Disk Monitoring Threshold
                        </Col>
                        <Col sm={4}>
                            <FormControl
                                id="nsslapd-disk-monitoring-threshold"
                                type="text"
                                value={this.state['nsslapd-disk-monitoring-threshold']}
                                onChange={this.handleDiskMonChange}
                                className={this.state.errObjDiskMon.diskThreshold ? "ds-input-bad" : ""}
                            />
                        </Col>
                    </Row>
                    <Row
                        className="ds-margin-top"
                        title="How many minutes to wait to allow an admin to clean up disk space before shutting slapd down. The default is 60 minutes. (nsslapd-disk-monitoring-grace-period)."
                    >
                        <Col componentClass={ControlLabel} sm={4}>
                            Disk Monitoring Grace Period
                        </Col>
                        <Col sm={4}>
                            <FormControl
                                id="nsslapd-disk-monitoring-grace-period"
                                type="text"
                                value={this.state['nsslapd-disk-monitoring-grace-period']}
                                onChange={this.handleDiskMonChange}
                                className={this.state.errObjDiskMon.diskGracePeriod ? "ds-input-bad" : ""}
                            />
                        </Col>
                    </Row>
                    <Row
                        className="ds-margin-top"
                        title="When disk space gets critically low do not remove logs to free up disk space (nsslapd-disk-monitoring-logging-critical)."
                    >
                        <Col componentClass={ControlLabel} sm={4}>
                            Server Logs
                        </Col>
                        <Col sm={4}>
                            <Checkbox
                                id="nsslapd-disk-monitoring-logging-critical"
                                defaultChecked={this.state['nsslapd-disk-monitoring-logging-critical']}
                                onChange={this.handleDiskMonChange}
                            >
                                Preserve Logs Even If Disk Space Gets Low
                            </Checkbox>
                        </Col>
                    </Row>
                </Form>;
        }

        if (this.state.configReloading || this.state.rootDNReloading ||
            this.state.diskMonReloading || this.state.advReloading) {
            reloadSpinner = <Spinner loading size="md" />;
        }

        if (this.state.loading) {
            body =
                <div className="ds-loading-spinner ds-margin-top ds-center">
                    <h4>Loading Server Settings ...</h4>
                    <Spinner className="ds-margin-top" loading size="md" />
                </div>;
        } else {
            body =
                <div>
                    <Row>
                        <Col sm={4}>
                            <ControlLabel className="ds-suffix-header ds-margin-top-lg ds-margin-left-sm">
                                Server Settings
                                <Icon className="ds-left-margin ds-refresh"
                                    type="fa" name="refresh" title="Refresh configuration settings"
                                    onClick={this.reloadConfig}
                                />
                            </ControlLabel>
                        </Col>
                        <Col sm={8} className="ds-margin-top-lg">
                            {reloadSpinner}
                        </Col>
                    </Row>
                    <div className={this.state.loading ? 'ds-fadeout' : 'ds-fadein ds-margin-left'}>
                        <TabContainer id="server-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                            <div className="ds-margin-top">
                                <Nav bsClass="nav nav-tabs nav-tabs-pf">
                                    <NavItem eventKey={1}>
                                        <div dangerouslySetInnerHTML={{__html: 'General Settings'}} />
                                    </NavItem>
                                    <NavItem eventKey={2}>
                                        <div dangerouslySetInnerHTML={{__html: 'Directory Manager'}} />
                                    </NavItem>
                                    <NavItem eventKey={3}>
                                        <div dangerouslySetInnerHTML={{__html: 'Disk Monitoring'}} />
                                    </NavItem>
                                    <NavItem eventKey={4}>
                                        <div dangerouslySetInnerHTML={{__html: 'Advanced Settings'}} />
                                    </NavItem>
                                </Nav>
                                <TabContent className="ds-margin-top-lg">
                                    <TabPane eventKey={1}>
                                        <Form className="ds-margin-top-lg" horizontal>
                                            <Row title="The version of the Directory Server rpm package" className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={3}>
                                                    Server Version
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="server-version"
                                                        type="text"
                                                        value={this.props.version}
                                                        disabled
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="The server's local hostname (nsslapd-localhost)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={3}>
                                                    Server Hostname
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="nsslapd-localhost"
                                                        type="text"
                                                        value={this.state['nsslapd-localhost']}
                                                        onChange={this.handleConfigChange}
                                                        className={this.state.errObjConfig['nsslapd-localhost'] ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="The server's port number (nsslapd-port)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={3}>
                                                    LDAP Port
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="nsslapd-port"
                                                        type="number"
                                                        min="0"
                                                        max="65535"
                                                        value={this.state['nsslapd-port']}
                                                        onChange={this.handleConfigChange}
                                                        className={this.state.errObjConfig['nsslapd-port'] ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="The server's secure port number (nsslapd-port)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={3}>
                                                    LDAPS Port
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="nsslapd-secureport"
                                                        type="number"
                                                        min="1"
                                                        max="65535"
                                                        value={this.state['nsslapd-secureport']}
                                                        onChange={this.handleConfigChange}
                                                        className={this.state.errObjConfig['nsslapd-secureport'] ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row
                                                title="This parameter can be used to restrict the Directory Server instance to a single IP interface (hostname, or IP address).  Requires restart. (nsslapd-listenhost)."
                                                className="ds-margin-top"
                                            >
                                                <Col componentClass={ControlLabel} sm={3}>
                                                    Listen Host Address
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="nsslapd-listenhost"
                                                        type="text"
                                                        value={this.state['nsslapd-listenhost']}
                                                        onChange={this.handleConfigChange}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="The location where database backups are stored (nsslapd-bakdir)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={3}>
                                                    Backup Directory
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="nsslapd-bakdir"
                                                        type="text"
                                                        value={this.state['nsslapd-bakdir']}
                                                        onChange={this.handleConfigChange}
                                                        className={this.state.errObjConfig['nsslapd-bakdir'] ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="The location where the server's LDIF files are located (nsslapd-ldifdir)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={3}>
                                                    LDIF File Directory
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="nsslapd-ldifdir"
                                                        type="text"
                                                        value={this.state['nsslapd-ldifdir']}
                                                        onChange={this.handleConfigChange}
                                                        className={this.state.errObjConfig['nsslapd-ldifdir'] ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="The location for the servers custom schema files. (nsslapd-schemadir)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={3}>
                                                    Schema Directory
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="nsslapd-schemadir"
                                                        type="text"
                                                        value={this.state['nsslapd-schemadir']}
                                                        onChange={this.handleConfigChange}
                                                        className={this.state.errObjConfig['nsslapd-schemadir'] ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="The location of the server's certificates (nsslapd-certdir)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={3}>
                                                    Certificate Directory
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="nsslapd-certdir"
                                                        type="text"
                                                        value={this.state['nsslapd-certdir']}
                                                        onChange={this.handleConfigChange}
                                                        className={this.state.errObjConfig['nsslapd-certdir'] ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Button
                                                disabled={this.state.configSaveDisabled}
                                                bsStyle="primary"
                                                className="ds-margin-top-med"
                                                onClick={this.saveConfig}
                                            >
                                                Save
                                            </Button>
                                        </Form>
                                    </TabPane>
                                    <TabPane eventKey={2}>
                                        <Form className="ds-margin-top-lg" horizontal>
                                            <Row title="The DN of the unrestricted directory manager (nsslapd-rootdn)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={4}>
                                                    Directory Manager DN
                                                </Col>
                                                <Col sm={4}>
                                                    <FormControl
                                                        disabled
                                                        type="text"
                                                        value={this.state['nsslapd-rootdn']}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="The Directory Manager password (nsslapd-rootpw)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={4}>
                                                    Directory Manager Password
                                                </Col>
                                                <Col sm={4}>
                                                    <FormControl
                                                        id="nsslapd-rootpw"
                                                        type="password"
                                                        value={this.state['nsslapd-rootpw']}
                                                        onChange={this.handleRootDNChange}
                                                        className={this.state.errObjRootDN['nsslapd-rootpw'] ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="The Directory Manager password (nsslapd-rootpw)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={4}>
                                                    Confirm Password
                                                </Col>
                                                <Col sm={4}>
                                                    <FormControl
                                                        id="confirmRootpw"
                                                        type="password"
                                                        value={this.state.confirmRootpw}
                                                        onChange={this.handleRootDNChange}
                                                        className={this.state.errObjRootDN.confirmRootpw ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row title="Set the Directory Manager password storage scheme (nsslapd-rootpwstoragescheme)." className="ds-margin-top">
                                                <Col componentClass={ControlLabel} sm={4}>
                                                    Password Storage Scheme
                                                </Col>
                                                <Col sm={4}>
                                                    <select
                                                      className="btn btn-default dropdown" id="nsslapd-rootpwstoragescheme"
                                                      onChange={this.handleRootDNChange} value={this.state['nsslapd-rootpwstoragescheme']}>
                                                        <option>PBKDF2_SHA256</option>
                                                        <option>SSHA512</option>
                                                        <option>SSHA384</option>
                                                        <option>SSHA256</option>
                                                        <option>SSHA</option>
                                                        <option>MD5</option>
                                                        <option>SMD5</option>
                                                        <option>CRYPT-MD5</option>
                                                        <option>CRYPT-SHA512</option>
                                                        <option>CRYPT-SHA256</option>
                                                        <option>CRYPT</option>
                                                        <option>GOST_YESCRYPT</option>
                                                        <option>CLEAR</option>
                                                    </select>
                                                </Col>
                                            </Row>
                                            <Button
                                                bsStyle="primary"
                                                className="ds-margin-top-med"
                                                disabled={this.state.rootDNSaveDisabled}
                                                onClick={this.saveRootDN}
                                            >
                                                Save
                                            </Button>
                                        </Form>
                                    </TabPane>

                                    <TabPane eventKey={3}>
                                        <Form className="ds-margin-left ds-margin-top-lg">
                                            <Row title="Enable disk space monitoring (nsslapd-disk-monitoring)." className="ds-margin-top">
                                                <Checkbox
                                                    id="nsslapd-disk-monitoring"
                                                    checked={this.state['nsslapd-disk-monitoring']}
                                                    onChange={this.handleDiskMonChange}
                                                >
                                                    Enable Disk Space Monitoring
                                                </Checkbox>
                                            </Row>
                                        </Form>
                                        {diskMonitor}
                                        <Button
                                            disabled={this.state.diskMonSaveDisabled}
                                            bsStyle="primary"
                                            className="ds-margin-top-med"
                                            onClick={this.saveDiskMonitoring}
                                        >
                                            Save
                                        </Button>
                                    </TabPane>

                                    <TabPane eventKey={4}>
                                        <Form className="ds-margin-top ds-margin-left" horizontal>
                                            <Row className="ds-margin-top-lg">
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-schemacheck"
                                                        defaultChecked={this.state['nsslapd-schemacheck']}
                                                        onChange={this.handleAdvChange}
                                                        title="Enable schema checking (nsslapd-schemacheck)."
                                                    >
                                                        Enable Schema Checking
                                                    </Checkbox>
                                                </Col>
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-syntaxcheck"
                                                        defaultChecked={this.state['nsslapd-syntaxcheck']}
                                                        onChange={this.handleAdvChange}
                                                        title="Enable attribute syntax checking (nsslapd-syntaxcheck)."
                                                    >
                                                        Enable Attribute Syntax Checking
                                                    </Checkbox>
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top">
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-plugin-logging"
                                                        defaultChecked={this.state['nsslapd-plugin-logging']}
                                                        onChange={this.handleAdvChange}
                                                        title="Enable plugins to log access and audit events.  (nsslapd-plugin-logging)."
                                                    >
                                                        Enable Plugin Logging
                                                    </Checkbox>
                                                </Col>
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-syntaxlogging"
                                                        defaultChecked={this.state['nsslapd-syntaxlogging']}
                                                        onChange={this.handleAdvChange}
                                                        title="Enable syntax logging (nsslapd-syntaxlogging)."
                                                    >
                                                        Enable Attribute Syntax Logging
                                                    </Checkbox>
                                                </Col>

                                            </Row>
                                            <Row className="ds-margin-top">
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-plugin-binddn-tracking"
                                                        defaultChecked={this.state['nsslapd-plugin-binddn-tracking']}
                                                        onChange={this.handleAdvChange}
                                                        title="Enabling this feature will write new operational attributes to the modified entry: internalModifiersname & internalCreatorsname. These new attributes contain the plugin DN, while modifiersname will be the original binding entry that triggered the update. (nsslapd-plugin-binddn-tracking)."
                                                    >
                                                        Enable Plugin Bind DN Tracking
                                                    </Checkbox>
                                                </Col>
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-attribute-name-exceptions"
                                                        defaultChecked={this.state['nsslapd-attribute-name-exceptions']}
                                                        onChange={this.handleAdvChange}
                                                        title="Allows non-standard characters in attribute names to be used for backwards compatibility with older servers (nsslapd-attribute-name-exceptions)."
                                                    >
                                                        Allow Attribute Naming Exceptions
                                                    </Checkbox>
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top">
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-dn-validate-strict"
                                                        defaultChecked={this.state['nsslapd-dn-validate-strict']}
                                                        onChange={this.handleAdvChange}
                                                        title="Enables strict syntax validation for DNs, according to section 3 in RFC 4514 (nsslapd-dn-validate-strict)."
                                                    >
                                                        Strict DN Syntax Validation
                                                    </Checkbox>
                                                </Col>
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-entryusn-global"
                                                        defaultChecked={this.state['nsslapd-entryusn-global']}
                                                        onChange={this.handleAdvChange}
                                                        title="For USN plugin - maintain unique USNs across all back end databases (nsslapd-entryusn-global)."
                                                    >
                                                        Maintain Unique USNs Across All Backends
                                                    </Checkbox>
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top">
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-ignore-time-skew"
                                                        defaultChecked={this.state['nsslapd-ignore-time-skew']}
                                                        onChange={this.handleAdvChange}
                                                        title="Ignore replication time skew when acquiring a replica to start a replciation session (nsslapd-ignore-time-skew)."
                                                    >
                                                        Ignore CSN Time Skew
                                                    </Checkbox>
                                                </Col>
                                                <Col sm={5}>
                                                    <Checkbox
                                                        id="nsslapd-readonly"
                                                        defaultChecked={this.state['nsslapd-readonly']}
                                                        onChange={this.handleAdvChange}
                                                        title="Make entire server read-only (nsslapd-readonly)"
                                                    >
                                                        Server Read-Only
                                                    </Checkbox>
                                                </Col>
                                            </Row>
                                        </Form>
                                        <Form className="ds-margin-left">
                                            <Row
                                                className="ds-margin-top-xlg"
                                                title="Allow anonymous binds to the server (nsslapd-allow-anonymous-access)."
                                            >
                                                <Col componentClass={ControlLabel} sm={5}>
                                                    Allow Anonymous Access
                                                </Col>
                                                <Col sm={4}>
                                                    <select
                                                        className="btn btn-default dropdown" id="nsslapd-allow-anonymous-access"
                                                        onChange={this.handleAdvChange} value={this.state['nsslapd-allow-anonymous-access']}
                                                    >
                                                        <option>on</option>
                                                        <option>off</option>
                                                        <option title="Allows anonymous search and read access to search the root DSE itself, but restricts access to all other directory entries. ">rootdse</option>
                                                    </select>
                                                </Col>
                                            </Row>
                                            <Row
                                                className="ds-margin-top"
                                                title="The DN of a template entry containing the resource limits to apply to anonymous connections (nsslapd-anonlimitsdn)."
                                            >
                                                <Col componentClass={ControlLabel} sm={5}>
                                                    Anonymous Resource Limits DN
                                                </Col>
                                                <Col sm={4}>
                                                    <FormControl
                                                        id="nsslapd-anonlimitsdn"
                                                        type="text"
                                                        value={this.state['nsslapd-anonlimitsdn']}
                                                        onChange={this.handleAdvChange}
                                                        className={this.state.errObjAdv.anonLimitsDN ? "ds-input-bad" : ""}
                                                    />
                                                </Col>
                                            </Row>
                                            <Button
                                                disabled={this.state.advSaveDisabled}
                                                bsStyle="primary"
                                                className="ds-margin-top-lg"
                                                onClick={this.saveAdvanced}
                                            >
                                                Save
                                            </Button>
                                        </Form>
                                    </TabPane>
                                </TabContent>
                            </div>
                        </TabContainer>
                    </div>
                </div>;
        }

        return (
            <div id="server-settings-page">
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
    addNotification: noop,
    serverId: "",
    version: "",
    attrs: {},
};
