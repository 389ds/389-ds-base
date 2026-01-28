import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "./lib/notifications.jsx";
import { log_cmd } from "./lib/tools.jsx";
import { CertificateManagement } from "./lib/security/certificateManagement.jsx";
import { SecurityEnableModal } from "./lib/security/securityModals.jsx";
import { Ciphers } from "./lib/security/ciphers.jsx";
import {
	Button,
	Checkbox,
	Form,
	Grid,
	GridItem,
	Spinner,
	Switch,
	Tab,
	Tabs,
	TabTitleText,
	TextInput,
	Text,
	TextContent,
	TextVariants
} from '@patternfly/react-core';
import TypeaheadSelect from "./dsBasicComponents.jsx";
import PropTypes from "prop-types";
import { SyncAltIcon } from '@patternfly/react-icons';

const _ = cockpit.gettext;

const configAttrs = [
    'sslVersionMin',
    'sslVersionMax',
    'secureListenhost',
    'clientAuth',
    'validateCert',
    'requireSecureBinds',
    'checkHostname',
    'allowWeakCipher',
    'nstlsallowclientrenegotiation',
];

const configCoreAttrs = [
    'secureListenhost',
    'requireSecureBinds',
    'checkHostname',
    'allowWeakCipher',
    'nstlsallowclientrenegotiation',
];

export class Security extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            loaded: false,
            saving: false,
            activeTabKey: 0,
            errObj: {},
            showConfirmDisable: false,
            showSecurityEnableModal: false,
            primaryCertName: '',
            serverCertNames: [],
            serverCerts: [],
            CACerts: [],
            CACertNames: [],
            isMinSSLOpen: false,
            isMaxSSLOpen: false,
            isClientAuthOpen: false,
            isValidateCertOpen: false,
            disableSaveBtn: true,
            modalChecked: false,
            modalSpinning: false,
            // Ciphers
            supportedCiphers: [],
            enabledCiphers: [],
            // Certificate Signing Requests
            serverCSRs: [],
            // Orphan keys
            serverOrphanKeys: [],
            // Config settings
            securityEnabled: false,
            requireSecureBinds: false,
            secureListenhost: false,
            clientAuth: false,
            checkHostname: false,
            validateCert: '',
            sslVersionMin: '',
            sslVersionMax: '',
            allowWeakCipher: false,
            nssslpersonalityssl: '',
            nstlsallowclientrenegotiation: true,
            // Original config Settings
            _securityEnabled: false,
            _requireSecureBinds: false,
            _secureListenhost: false,
            _clientAuth: false,
            _checkHostname: false,
            _validateCert: '',
            _sslVersionMin: '',
            _sslVersionMax: '',
            _allowWeakCipher: false,
            _nssslpersonalityssl: '',
            _nssslpersonalityssllist: "",
            _nstlsallowclientrenegotiation: true,

            isServerCertOpen: false,
        };

        // Server Cert
        this.handleServerCertSelect = (event, selection) => {
            let disableSaveBtn = !this.configChanged();
            if (this.state._nssslpersonalityssl !== selection) {
                disableSaveBtn = false;
            }
            this.setState({
                nssslpersonalityssl: selection,
                isServerCertOpen: false,
                disableSaveBtn,
            });
        };
        this.handleServerCertToggle = (_event, isServerCertOpen) => {
        this.setState({
            isServerCertOpen
        });
        };
        this.handleServerCertClear = () => {
            this.setState({
                nssslpersonalityssl: '',
                isServerCertOpen: false
            });
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        // Select handlers
        this.configChanged = () => {
            // Check if a core/non-select setting was changed
            for (const config_attr of configCoreAttrs) {
                if (this.state[config_attr] !== this.state['_' + config_attr]) {
                    return true;
                }
            }
            return false;
        };

        this.handleMinSSLToggle = (_event, isMinSSLOpen) => {
            this.setState({
                isMinSSLOpen
            });
        };
        this.handleMinSSLSelect = (event, selection, isPlaceholder) => {
            let disableSaveBtn = !this.configChanged();

            // Check if a setting was changed, if so enable the save button
            if (this.state._sslVersionMin !== selection) {
                disableSaveBtn = false;
            }
            this.setState({
                sslVersionMin: selection,
                isMinSSLOpen: false,
                disableSaveBtn
            });
        };

        this.handleMaxSSLToggle = (_event, isMaxSSLOpen) => {
            this.setState({
                isMaxSSLOpen
            });
        };
        this.handleMaxSSLSelect = (event, selection, isPlaceholder) => {
            let disableSaveBtn = !this.configChanged();

            // Check if a setting was changed, if so enable the save button
            if (this.state._sslVersionMax !== selection) {
                disableSaveBtn = false;
            }
            this.setState({
                sslVersionMax: selection,
                isMaxSSLOpen: false,
                disableSaveBtn
            });
        };

        this.handleClientAuthToggle = (_event, isClientAuthOpen) => {
            this.setState({
                isClientAuthOpen
            });
        };
        this.handleClientAuthSelect = (event, selection, isPlaceholder) => {
            let disableSaveBtn = !this.configChanged();

            // Check if a setting was changed, if so enable the save button
            if (this.state._clientAuth !== selection) {
                disableSaveBtn = false;
            }

            this.setState({
                clientAuth: selection,
                isClientAuthOpen: false,
                disableSaveBtn
            });
        };

        this.handleValidateCertToggle = (_event, isValidateCertOpen) => {
            this.setState({
                isValidateCertOpen
            });
        };
        this.handleValidateCertSelect = (event, selection, isPlaceholder) => {
            let disableSaveBtn = !this.configChanged();

            // Check if a setting was changed, if so enable the save button
            if (this.state._validateCert !== selection) {
                disableSaveBtn = false;
            }
            this.setState({
                validateCert: selection,
                isValidateCertOpen: false,
                disableSaveBtn
            });
        };

        this.handleChange = this.handleChange.bind(this);
        this.onModalChange = this.onModalChange.bind(this);
        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.handleTypeaheadChange = this.handleTypeaheadChange.bind(this);
        this.loadSecurityConfig = this.loadSecurityConfig.bind(this);
        this.loadEnabledCiphers = this.loadEnabledCiphers.bind(this);
        this.loadSupportedCiphers = this.loadSupportedCiphers.bind(this);
        this.loadCerts = this.loadCerts.bind(this);
        this.loadCACerts = this.loadCACerts.bind(this);
        this.loadCSRs = this.loadCSRs.bind(this);
        this.closeConfirmDisable = this.closeConfirmDisable.bind(this);
        this.enableSecurity = this.enableSecurity.bind(this);
        this.disableSecurity = this.disableSecurity.bind(this);
        this.saveSecurityConfig = this.saveSecurityConfig.bind(this);
        this.closeSecurityEnableModal = this.closeSecurityEnableModal.bind(this);
        this.handleReloadConfig = this.handleReloadConfig.bind(this);
        this.onSelectToggle = this.onSelectToggle.bind(this);
        this.onSelectClear = this.onSelectClear.bind(this);
        this.handleTypeaheadChange = this.handleTypeaheadChange.bind(this);
        this.onSecEnableChange = this.onSecEnableChange.bind(this);
    }

    componentDidMount () {
        if (!this.state.loaded) {
            this.setState({ securityEnabled: true }, this.setState({ securityEnabled: false }));
            this.loadSecurityConfig();
        } else {
            this.props.enableTree();
        }
    }

    onModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    handleReloadConfig () {
        this.setState({
            loaded: false
        }, this.loadSecurityConfig);
    }

    loadSupportedCiphers () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "ciphers", "list", "--supported"
        ];
        log_cmd("loadSupportedCiphers", "Load the security configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        supportedCiphers: config.items
                    }, this.loadEnabledCiphers);
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading security configuration - $0"), msg)
                    );
                });
    }

    loadEnabledCiphers () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "ciphers", "list", "--enabled"
        ];
        log_cmd("loadEnabledCiphers", "Load the security configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    this.setState({
                        enabledCiphers: config.items,
                    }, this.loadCerts);
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading security configuration - $0"), msg)
                    );
                });
    }

    loadCACerts () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "ca-certificate", "list",
        ];
        log_cmd("loadCACerts", "Load CA certificates", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const certs = JSON.parse(content);
                    const certNames = [];
                    for (const cert of certs) {
                        certNames.push(cert.attrs.nickname);
                    }
                    this.setState(() => (
                        {
                            CACerts: certs,
                            CACertNames: certNames,
                        }), this.loadCSRs
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading CA certificates - $0"), msg)
                    );
                });
    }

    loadCerts () {
        // Set loaded: true
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "certificate", "list",
        ];
        log_cmd("loadCerts", "Load certificates", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const certs = JSON.parse(content);
                    const certNames = [];
                    for (const cert of certs) {
                        certNames.push(cert.attrs.nickname);
                    }
                    this.setState(() => (
                        {
                            serverCerts: certs,
                            serverCertNames: certNames,
                        }), this.loadCACerts
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading server certificates - $0"), msg)
                    );
                });
    }

    loadCSRs () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "csr", "list",
        ];
        log_cmd("loadCSRs", "Load CSRs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const csrs = JSON.parse(content);
                    this.setState(() => (
                        {
                            serverCSRs: csrs,
                        }), this.loadOrphanKeys
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading CSRs - $0"), msg)
                    );
                });
    }

    loadOrphanKeys () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "key", "list", "--orphan"
        ];
        log_cmd("loadOrphanKeys", "Load Orphan Keys", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const keys = JSON.parse(content);
                    this.setState(() => (
                        {
                            serverOrphanKeys: keys,
                            loaded: true
                        }), this.props.enableTree()
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (!errMsg.desc.includes('certutil: no keys found')) {
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error loading Orphan Keys - $0"), errMsg.desc)
                        );
                    }
                    this.setState({
                        loaded: true,
                        serverOrphanKeys: []
                    }, this.props.enableTree());
                });
    }

    loadRSAConfig() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "rsa", "get"
        ];
        log_cmd("loadRSAConfig", "Load the RSA configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const nickname = config.items.nssslpersonalityssl;
                    this.setState(() => (
                        {
                            nssslpersonalityssl: nickname,
                            _nssslpersonalityssl: nickname,
                        }
                    ), this.loadSupportedCiphers);
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading security RSA configuration - $0"), msg)
                    );
                });
    }

    loadSecurityConfig(saving) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "get"
        ];
        log_cmd("loadSecurityConfig", "Load the security configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.items;
                    let secEnabled = false;
                    let secReqSecBinds = false;
                    let clientAuth = "allowed";
                    let validateCert = "warn";
                    let cipherPref = "default";
                    let allowWeak = false;
                    let renegot = true;
                    let checkHostname = false;

                    if ('nstlsallowclientrenegotiation' in config.items) {
                        if (config.items.nstlsallowclientrenegotiation === "off") {
                            renegot = false;
                        }
                    }
                    if ('nsslapd-security' in attrs) {
                        if (attrs['nsslapd-security'].toLowerCase() === "on") {
                            secEnabled = true;
                        }
                    }
                    if ('nsslapd-require-secure-binds' in attrs) {
                        if (attrs['nsslapd-require-secure-binds'].toLowerCase() === "on") {
                            secReqSecBinds = true;
                        }
                    }
                    if ('nssslclientauth' in attrs) {
                        if (attrs.nssslclientauth !== "") {
                            clientAuth = attrs.nssslclientauth;
                        }
                    }
                    if ('nsslapd-validate-cert' in attrs) {
                        if (attrs['nsslapd-validate-cert'] !== "") {
                            validateCert = attrs['nsslapd-validate-cert'].toLowerCase();
                        }
                    }
                    if ('allowweakcipher' in attrs) {
                        if (attrs.allowweakcipher.toLowerCase() === "on") {
                            allowWeak = true;
                        }
                    }
                    if ('nsslapd-ssl-check-hostname' in attrs) {
                        if (attrs['nsslapd-ssl-check-hostname'].toLowerCase() === "on") {
                            checkHostname = true;
                        }
                    }
                    if ('nsssl3ciphers' in attrs) {
                        if (attrs.nsssl3ciphers !== "") {
                            cipherPref = attrs.nsssl3ciphers;
                        }
                    }

                    this.setState(() => (
                        {
                            securityEnabled: secEnabled,
                            requireSecureBinds: secReqSecBinds,
                            secureListenhost: attrs['nsslapd-securelistenhost'],
                            clientAuth,
                            checkHostname,
                            validateCert,
                            sslVersionMin: attrs.sslversionmin,
                            sslVersionMax: attrs.sslversionmax,
                            allowWeakCipher: allowWeak,
                            cipherPref,
                            nstlsallowclientrenegotiation: renegot,
                            _nstlsallowclientrenegotiation: renegot,
                            _securityEnabled: secEnabled,
                            _requireSecureBinds: secReqSecBinds,
                            _secureListenhost: attrs['nsslapd-securelistenhost'],
                            _clientAuth: clientAuth,
                            _checkHostname: checkHostname,
                            _validateCert: validateCert,
                            _sslVersionMin: attrs.sslversionmin,
                            _sslVersionMax: attrs.sslversionmax,
                            _allowWeakCipher: allowWeak,
                            disableSaveBtn: true,
                        }
                    ), function() {
                        if (!saving) {
                            this.loadRSAConfig();
                        }
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading security configuration - $0"), msg)
                    );
                });
    }

    handleSwitchChange(value) {
        if (!value) {
            // We are disabling security, ask for confirmation
            this.setState({ showConfirmDisable: true });
        } else {
            // Check if we have certs, if we do make the user choose one from dropdown list, otherwise reject the
            // enablement
            if (this.state.serverCerts.length > 0) {
                this.setState({
                    primaryCertName: this.state.nssslpersonalityssl,
                    showSecurityEnableModal: true,
                });
            } else {
                this.props.addNotification(
                    "error",
                    _("There must be at least one server certificate present in the security database to enable security")
                );
            }
        }
    }

    closeSecurityEnableModal () {
        this.setState({
            showSecurityEnableModal: false,
        });
    }

    onSecEnableChange (e) {
        const value = e.target.value.trim();
        this.setState({
            primaryCertName: value,
        });
    }

    closeConfirmDisable () {
        this.setState({
            showConfirmDisable: false
        });
    }

    enableSecurity () {
        /* start the spinner */
        this.setState({
            secEnableSpinner: true
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "enable",
        ];

        if (this.state._nssslpersonalityssl !== this.state.primaryCertName) {
            const rsa_cmd = [
                'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
                'security', 'rsa', 'set', '--nss-cert-name=' + this.state.primaryCertName
            ];
            log_cmd("enableSecurity", "Update RSA", rsa_cmd);
            cockpit
                    .spawn(rsa_cmd, { superuser: true, err: "message" })
                    .done(() => {
                        this.loadSecurityConfig();
                        log_cmd("enableSecurity", "Enable security", cmd);
                        cockpit
                                .spawn(cmd, { superuser: true, err: "message" })
                                .done(() => {
                                    this.loadSecurityConfig();
                                    this.props.addNotification(
                                        "success",
                                        _("Successfully enabled security.")
                                    );
                                    this.props.addNotification(
                                        "warning",
                                        _("You must restart the Directory Server for these changes to take effect.")
                                    );
                                    this.setState({
                                        securityEnabled: true,
                                        secEnableSpinner: false,
                                        showSecurityEnableModal: false,
                                    });
                                })
                                .fail(err => {
                                    const errMsg = JSON.parse(err);
                                    let msg = errMsg.desc;
                                    if ('info' in errMsg) {
                                        msg = errMsg.desc + " - " + errMsg.info;
                                    }
                                    this.props.addNotification(
                                        "error",
                                        cockpit.format(_("Error enabling security - $0"), msg)
                                    );
                                    this.setState({
                                        secEnableSpinner: false,
                                        showSecurityEnableModal: false,
                                    });
                                });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        let msg = errMsg.desc;
                        if ('info' in errMsg) {
                            msg = errMsg.desc + " - " + errMsg.info;
                        }
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error enabling security (RSA cert name)- $0"), msg)
                        );
                        this.setState({
                            secEnableSpinner: false,
                            showSecurityEnableModal: false,
                        });
                    });
        } else {
            log_cmd("enableSecurity", "Enable security", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(() => {
                        this.props.addNotification(
                            "success",
                            _("Successfully enabled security.")
                        );
                        this.props.addNotification(
                            "warning",
                            _("You must restart the Directory Server for these changes to take effect.")
                        );
                        this.setState({
                            securityEnabled: true,
                            secEnableSpinner: false,
                            showSecurityEnableModal: false,
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        let msg = errMsg.desc;
                        if ('info' in errMsg) {
                            msg = errMsg.desc + " - " + errMsg.info;
                        }
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error enabling security - $0"), msg)
                        );
                        this.setState({
                            secEnableSpinner: false,
                            showSecurityEnableModal: false,
                        });
                    });
        }
    }

    disableSecurity () {
        this.setState({
            modalSpinning: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "disable",
        ];
        log_cmd("disableSecurity", "Disable security", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.props.addNotification(
                        "success",
                        _("Successfully disabled security.")
                    );
                    this.props.addNotification(
                        "warning",
                        _("You must restart the Directory Server for these changes to take effect.")
                    );
                    this.setState({
                        securityEnabled: false,
                        modalSpinning: false,
                        showConfirmDisable: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error disabling security - $0"), msg)
                    );
                    this.setState({
                        modalSpinning: false,
                        showConfirmDisable: false,
                    });
                });
    }

    saveSecurityConfig () {
        // Validate some setting first
        let sslMin = this.state._sslVersionMin;
        let sslMax = this.state._sslVersionMax;
        if (this.state._sslVersionMin !== this.state.sslVersionMin) {
            sslMin = this.state.sslVersionMin;
        }
        if (this.state._sslVersionMax !== this.state.sslVersionMax) {
            sslMax = this.state.sslVersionMax;
        }

        if (sslMin > sslMax) {
            this.props.addNotification(
                "error",
                _("The TLS minimum version must be less than or equal to the TLS maximum version")
            );
            // Reset page
            this.loadSecurityConfig();
            return;
        }

        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'security', 'set'
        ];
        const rsa_cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'security', 'rsa', 'set'
        ];

        if (this.state._validateCert !== this.state.validateCert) {
            cmd.push("--verify-cert-chain-on-startup=" + this.state.validateCert);
        }
        if (this.state._sslVersionMin !== this.state.sslVersionMin) {
            cmd.push("--tls-protocol-min=" + this.state.sslVersionMin);
        }
        if (this.state._sslVersionMax !== this.state.sslVersionMax) {
            cmd.push("--tls-protocol-max=" + this.state.sslVersionMax);
        }
        if (this.state._clientAuth !== this.state.clientAuth) {
            cmd.push("--tls-client-auth=" + this.state.clientAuth);
        }
        if (this.state._secureListenhost !== this.state.secureListenhost) {
            cmd.push("--listen-host=" + this.state.secureListenhost);
        }
        if (this.state._allowWeakCipher !== this.state.allowWeakCipher) {
            let val = "off";
            if (this.state.allowWeakCipher) {
                val = "on";
            }
            cmd.push("--allow-insecure-ciphers=" + val);
        }
        if (this.state._checkHostname !== this.state.checkHostname) {
            let val = "off";
            if (this.state.checkHostname) {
                val = "on";
            }
            cmd.push("--check-hostname=" + val);
        }
        if (this.state._requireSecureBinds !== this.state.requireSecureBinds) {
            let val = "off";
            if (this.state.requireSecureBinds) {
                val = "on";
            }
            cmd.push("--require-secure-authentication=" + val);
        }

        if (this.state._nstlsallowclientrenegotiation !== this.state.nstlsallowclientrenegotiation) {
            let val = "off";
            if (this.state.nstlsallowclientrenegotiation) {
                val = "on";
            }
            cmd.push("--tls-client-renegotiation=" + val);
        }

        if (this.state._nssslpersonalityssl !== this.state.nssslpersonalityssl) {
            rsa_cmd.push("--nss-cert-name=" + this.state.nssslpersonalityssl);
        }
        if (rsa_cmd.length > 6) {
            log_cmd("saveSecurityConfig", "Applying security RSA config change", rsa_cmd);
            const msg = _("Successfully updated security RSA configuration.");

            this.setState({
                // Start the spinner
                saving: true
            });

            cockpit
                    .spawn(rsa_cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.loadSecurityConfig();
                        if (cmd.length < 6) {
                            this.props.addNotification(
                                "success",
                                msg
                            );
                            this.props.addNotification(
                                "warning",
                                _("You must restart the Directory Server for these changes to take effect.")
                            );
                            this.setState({
                                saving: false
                            });
                        }
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.loadSecurityConfig();
                        this.setState({
                            saving: false
                        });
                        let msg = errMsg.desc;
                        if ('info' in errMsg) {
                            msg = errMsg.desc + " - " + errMsg.info;
                        }
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error updating security RSA configuration - $0"), msg)
                        );
                    });
        }

        if (cmd.length > 5) {
            log_cmd("saveSecurityConfig", "Applying security config change", cmd);
            const msg = _("Successfully updated security configuration.");

            this.setState({
                // Start the spinner
                saving: true
            });

            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.loadSecurityConfig(1);
                        this.props.addNotification(
                            "success",
                            msg
                        );
                        this.props.addNotification(
                            "warning",
                            _("You must restart the Directory Server for these changes to take effect.")
                        );
                        this.setState({
                            saving: false
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.loadSecurityConfig();
                        this.setState({
                            saving: false
                        });
                        let msg = errMsg.desc;
                        if ('info' in errMsg) {
                            msg = errMsg.desc + " - " + errMsg.info;
                        }
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error updating security configuration - $0"), msg)
                        );
                    });
        }
    }

    handleTypeaheadChange(value, collection) {
        this.setState({
            [collection]: [...this.state[collection], value],
        });
    }

    handleChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (const config_attr of configAttrs) {
            if (attr === config_attr && this.state['_' + config_attr] !== value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const config_attr of configAttrs) {
            if (attr !== config_attr && this.state['_' + config_attr] !== this.state[config_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [attr]: value,
            disableSaveBtn
        });
    }

    onSelectToggle = (_event, isExpanded, toggleId) => {
        this.setState({
            [toggleId]: isExpanded
        });
    };

    onSelectClear = (toggleId, collection) => {
        this.setState({
            [toggleId]: false,
            [collection]: []
        });
    };

    render() {
        let securityPage = "";
        const serverCert = [this.state.nssslpersonalityssl];
        let saveBtnName = _("Save Settings");
        const extraPrimaryProps = {};
        if (this.state.saving) {
            saveBtnName = _("Saving settings ...");
            extraPrimaryProps.spinnerAriaValueText = _("Loading");
        }

        if (this.state.loaded && !this.state.saving) {
            let configPage = "";
            if (this.state.securityEnabled) {
                configPage = (
                    <div className="ds-margin-bottom-md">
                        <Form isHorizontal autoComplete="off">
                            <Grid
                                title={_("The name, or nickname, of the server certificate inthe NSS database the server should use (nsSSLPersonalitySSL).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Server Certificate Name")}
                                </GridItem>
                                <GridItem span={8}>
                                    <TypeaheadSelect
                                        selected={serverCert}
                                        onSelect={this.handleServerCertSelect}
                                        onClear={this.handleServerCertClear}
                                        options={this.state.serverCertNames}
                                        isOpen={this.state.isServerCertOpen}
                                        onToggle={this.handleServerCertToggle}
                                        placeholder={_("Type a sever certificate nickname...")}
                                        noResultsText={_("There are no matching entries")}
                                        ariaLabel="Type a server certificate nickname"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The minimum SSL/TLS version the server will accept (sslversionmin).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Minimum TLS Version")}
                                </GridItem>
                                <GridItem span={8}>
                                    <TypeaheadSelect
                                        selected={this.state.sslVersionMin}
                                        onSelect={this.handleMinSSLSelect}
                                        options={["TLS1.3", "TLS1.2", "TLS1.1", "TLS1.0", "SSL3"]}
                                        isOpen={this.state.isMinSSLOpen}
                                        onToggle={this.handleMinSSLToggle}
                                        ariaLabel="Select Input"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The maximum SSL/TLS version the server will accept (sslversionmax).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Maximum TLS Version")}
                                </GridItem>
                                <GridItem span={8}>
                                    <TypeaheadSelect
                                        selected={this.state.sslVersionMax}
                                        onSelect={this.handleMaxSSLSelect}
                                        options={["TLS1.3", "TLS1.2", "TLS1.1", "TLS1.0", "SSL3"]}
                                        isOpen={this.state.isMaxSSLOpen}
                                        onToggle={this.handleMaxSSLToggle}
                                        ariaLabel="Select Input"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Sets how the Directory Server enforces TLS client authentication (nsSSLClientAuth).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Client Authentication")}
                                </GridItem>
                                <GridItem span={8}>
                                    <TypeaheadSelect
                                        selected={this.state.clientAuth}
                                        onSelect={this.handleClientAuthSelect}
                                        options={["off", "allowed", "required"]}
                                        isOpen={this.state.isClientAuthOpen}
                                        onToggle={this.handleClientAuthToggle}
                                        ariaLabel="Select Input"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Validate server's certificate expiration date (nsslapd-validate-cert).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Validate Certificate")}
                                </GridItem>
                                <GridItem span={8}>
                                    <TypeaheadSelect
                                        selected={this.state.validateCert}
                                        onSelect={this.handleValidateCertSelect}
                                        options={["warn", "on", "off"]}
                                        isOpen={this.state.isValidateCertOpen}
                                        onToggle={this.handleValidateCertToggle}
                                        ariaLabel="Select Input"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("This parameter can be used to restrict the Directory Server instance to a single IP interface (hostname, or IP address).  This parameter specifically sets what interface to use for TLS traffic.  Requires restart. (nsslapd-securelistenhost).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Secure Listen Host")}
                                </GridItem>
                                <GridItem span={8}>
                                    <TextInput
                                        value={this.state.secureListenhost}
                                        type="text"
                                        id="secureListenhost"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="server-hostname"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Require all connections use TLS (nsslapd-require-secure-binds).")}
                            >
                                <GridItem className="ds-label" span={4}>
                                    <Checkbox
                                        id="requireSecureBinds"
                                        isChecked={this.state.requireSecureBinds}
                                        onChange={(e, checked) => {
                                            this.handleChange(e);
                                        }}
                                        label={_("Require Secure Connections")}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Verify authenticity of a request by matching the host name against the value assigned to the common name (cn) attribute of the subject name (subjectDN field) in the certificate being presented. (nsslapd-ssl-check-hostname).")}
                            >
                                <GridItem className="ds-label" span={4}>
                                    <Checkbox
                                        id="checkHostname"
                                        isChecked={this.state.checkHostname}
                                        onChange={(e, checked) => {
                                            this.handleChange(e);
                                        }}
                                        label={_("Verify Certificate Subject Hostname")}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Allow weak ciphers (allowWeakCipher).")}
                            >
                                <GridItem className="ds-label" span={4}>
                                    <Checkbox
                                        id="allowWeakCipher"
                                        isChecked={this.state.allowWeakCipher}
                                        onChange={(e, checked) => {
                                            this.handleChange(e);
                                        }}
                                        title={_("Allow weak ciphers (allowWeakCipher).")}
                                        label={_("Allow Weak Ciphers")}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Allow client-initiated renegotiation (nsTLSAllowClientRenegotiation).")}
                            >
                                <GridItem className="ds-label" span={4}>
                                    <Checkbox
                                        id="nstlsallowclientrenegotiation"
                                        isChecked={this.state.nstlsallowclientrenegotiation}
                                        onChange={(e, checked) => {
                                            this.handleChange(e);
                                        }}
                                        title={_("Allow client-initiated renegotiation (nsTLSAllowClientRenegotiation).")}
                                        label={_("Allow Client Renegotiation")}
                                    />
                                </GridItem>
                            </Grid>
                        </Form>
                        <Button
                            variant="primary"
                            className="ds-margin-top-xlg"
                            onClick={() => {
                                this.saveSecurityConfig();
                            }}
                            isDisabled={this.state.disableSaveBtn || this.state.saving}
                            isLoading={this.state.saving}
                            spinnerAriaValueText={this.state.saving ? _("Saving") : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnName}
                        </Button>
                    </div>
                );
            }

            securityPage = (
                <div className="ds-margin-bottom-md">
                    <Grid>
                        <GridItem span={12}>
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    {_("Security Settings")}
                                    <Button
                                        variant="plain"
                                        aria-label={_("Refresh settings")}
                                        onClick={this.handleReloadConfig}
                                    >
                                        <SyncAltIcon size="lg" />
                                    </Button>
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>
                    <div className="ds-tab-table">
                        <Tabs className="ds-margin-top-xlg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                            <Tab eventKey={0} title={<TabTitleText>{_("Security Configuration")}</TabTitleText>}>
                                <Grid className="ds-margin-top-xlg ds-left-indent-md">
                                    <GridItem className="ds-label" span={4}>
                                        <Switch
                                            id="simple-switch"
                                            label={_("Security Enabled")}
                                            labelOff="Security Disabled"
                                            isChecked={this.state.securityEnabled}
                                            onChange={(_event, value) => this.handleSwitchChange(value)}
                                        />
                                    </GridItem>
                                    <hr />
                                    {configPage}
                                </Grid>
                            </Tab>
                            <Tab eventKey={1} title={<TabTitleText>{_("Certificate Management")}</TabTitleText>}>
                                <CertificateManagement
                                    serverId={this.props.serverId}
                                    CACerts={this.state.CACerts}
                                    ServerCerts={this.state.serverCerts}
                                    ServerCSRs={this.state.serverCSRs}
                                    ServerKeys={this.state.serverOrphanKeys}
                                    addNotification={this.props.addNotification}
                                    certDir={this.props.certDir}
                                    certNicknames={this.state.serverCertNames}
                                    CACertNicknames={this.state.CACertNames}
                                    reloadCerts={this.loadCerts}
                                />
                            </Tab>
                            <Tab eventKey={2} title={<TabTitleText>{_("Cipher Preferences")}</TabTitleText>}>
                                <div className="ds-indent ds-tab-table">
                                    <Ciphers
                                        key={this.state.cipherPref}
                                        serverId={this.props.serverId}
                                        supportedCiphers={this.state.supportedCiphers}
                                        cipherPref={this.state.cipherPref}
                                        enabledCiphers={this.state.enabledCiphers}
                                        addNotification={this.props.addNotification}
                                        reload={this.loadSecurityConfig}
                                    />
                                </div>
                            </Tab>
                        </Tabs>
                    </div>
                </div>
            );
        } else {
            securityPage = (
                <div className="ds-margin-top-xlg ds-loading-spinner ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Loading Security Information ...")}
                        </Text>
                    </TextContent>
                    <Spinner className="ds-margin-top-lg" size="lg" />
                </div>
            );
        }
        return (
            <div className={this.state.saving ? "ds-disabled" : ""}>
                {securityPage}
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDisable}
                    closeHandler={this.closeConfirmDisable}
                    handleChange={this.onModalChange}
                    actionHandler={this.disableSecurity}
                    spinning={this.state.modalSpinning}
                    item="Requires server restart to take effect."
                    checked={this.state.modalChecked}
                    mTitle={_("Disable Security")}
                    mMsg={_("Are you sure you want to disable security?")}
                    mSpinningMsg={_("Disabling ...")}
                    mBtnName={_("Disable")}
                />

                <SecurityEnableModal
                    showModal={this.state.showSecurityEnableModal}
                    closeHandler={this.closeSecurityEnableModal}
                    handleChange={this.onSecEnableChange}
                    saveHandler={this.enableSecurity}
                    primaryName={this.state.primaryCertName}
                    certs={this.state.serverCerts}
                    spinning={this.state.secEnableSpinner}
                />
            </div>
        );
    }
}

// Props and defaultProps

Security.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
};

Security.defaultProps = {
    serverId: "",
};

export default Security;
