import cockpit from "cockpit";
import React from "react";
import Switch from "react-switch";
import { NotificationController, ConfirmPopup } from "./lib/notifications.jsx";
import { log_cmd } from "./lib/tools.jsx";
import { Typeahead } from "react-bootstrap-typeahead";
import { CertificateManagement } from "./lib/security/certificateManagement.jsx";
import { SecurityEnableModal } from "./lib/security/securityModals.jsx";
import { Ciphers } from "./lib/security/ciphers.jsx";
import {
    Nav,
    NavItem,
    TabContainer,
    TabContent,
    TabPane,
    Col,
    Row,
    ControlLabel,
    Button,
    Checkbox,
    Spinner
} from "patternfly-react";
import PropTypes from "prop-types";
import "./css/ds.css";

export class Security extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            loaded: false,
            saving: false,
            notifications: [],
            activeKey: 1,

            errObj: {},
            showConfirmDisable: false,
            showSecurityEnableModal: false,
            primaryCertName: '',
            serverCertNames: [],
            serverCerts: [],
            // Ciphers
            supportedCiphers: [],
            enabledCiphers: [],
            // Config settings
            securityEnabled: false,
            requireSecureBinds: false,
            secureListenhost: false,
            securePort: '636',
            clientAuth: false,
            checkHostname: false,
            validateCert: '',
            sslVersionMin: '',
            sslVersionMax: '',
            allowWeakCipher: false,
            nssslpersonalityssl: '',
            // Original config Settings
            _securityEnabled: false,
            _requireSecureBinds: false,
            _secureListenhost: false,
            _securePort: '636',
            _clientAuth: false,
            _checkHostname: false,
            _validateCert: '',
            _sslVersionMin: '',
            _sslVersionMax: '',
            _allowWeakCipher: false,
            _nssslpersonalityssl: '',
        };

        this.handleChange = this.handleChange.bind(this);
        this.addNotification = this.addNotification.bind(this);
        this.removeNotification = this.removeNotification.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.handleTypeaheadChange = this.handleTypeaheadChange.bind(this);
        this.loadSecurityConfig = this.loadSecurityConfig.bind(this);
        this.loadEnabledCiphers = this.loadEnabledCiphers.bind(this);
        this.loadSupportedCiphers = this.loadSupportedCiphers.bind(this);
        this.loadCerts = this.loadCerts.bind(this);
        this.loadCACerts = this.loadCACerts.bind(this);
        this.closeConfirmDisable = this.closeConfirmDisable.bind(this);
        this.enableSecurity = this.enableSecurity.bind(this);
        this.disableSecurity = this.disableSecurity.bind(this);
        this.saveSecurityConfig = this.saveSecurityConfig.bind(this);
        this.closeSecurityEnableModal = this.closeSecurityEnableModal.bind(this);
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

    componentWillMount () {
        if (!this.state.loaded) {
            this.setState({securityEnabled: true}, this.setState({securityEnabled: false}));
            this.loadSecurityConfig();
        }
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
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.addNotification(
                        "error",
                        `Error loading security configuration - ${msg}`
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
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.addNotification(
                        "error",
                        `Error loading security configuration - ${msg}`
                    );
                });
    }

    loadCACerts () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "ca-certificate", "list",
        ];
        log_cmd("loadCACerts", "Load certificates", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let certs = JSON.parse(content);
                    this.setState(() => (
                        {
                            CACerts: certs,
                            loaded: true
                        })
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.addNotification(
                        "error",
                        `Error loading CA certificates - ${msg}`
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
                    let certNames = [];
                    for (let cert of certs) {
                        certNames.push(cert.attrs['nickname']);
                    }
                    this.setState(() => (
                        {
                            serverCerts: certs,
                            serverCertNames: certNames,
                        }), this.loadCACerts
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.addNotification(
                        "error",
                        `Error loading server certificates - ${msg}`
                    );
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
                    const nickname = config.items['nssslpersonalityssl'];
                    this.setState(() => (
                        {
                            nssslpersonalityssl: nickname,
                            _nssslpersonalityssl: nickname,
                        }
                    ), this.loadSupportedCiphers);
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.addNotification(
                        "error",
                        `Error loading security RSA configuration - ${msg}`
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

                    if ('nsslapd-security' in attrs) {
                        if (attrs['nsslapd-security'].toLowerCase() == "on") {
                            secEnabled = true;
                        }
                    }
                    if ('nsslapd-require-secure-binds' in attrs) {
                        if (attrs['nsslapd-require-secure-binds'].toLowerCase() == "on") {
                            secReqSecBinds = true;
                        }
                    }
                    if ('nssslclientauth' in attrs) {
                        if (attrs['nssslclientauth'] != "") {
                            clientAuth = attrs['nssslclientauth'];
                        }
                    }
                    if ('nsslapd-validate-cert' in attrs) {
                        if (attrs['nsslapd-validate-cert'] != "") {
                            validateCert = attrs['nsslapd-validate-cert'].toLowerCase();
                        }
                    }
                    if ('allowweakcipher' in attrs) {
                        if (attrs['allowweakcipher'].toLowerCase() == "on") {
                            allowWeak = true;
                        }
                    }
                    if ('nsssl3ciphers' in attrs) {
                        if (attrs['nsssl3ciphers'] != "") {
                            cipherPref = attrs['nsssl3ciphers'];
                        }
                    }

                    this.setState(() => (
                        {
                            securityEnabled: secEnabled,
                            requireSecureBinds: secReqSecBinds,
                            secureListenhost: attrs['nsslapd-securelistenhost'],
                            securePort: attrs['nsslapd-secureport'],
                            clientAuth: clientAuth,
                            checkHostname: attrs['nsslapd-ssl-check-hostname'],
                            validateCert: validateCert,
                            sslVersionMin: attrs['sslversionmin'],
                            sslVersionMax: attrs['sslversionmax'],
                            allowWeakCipher: allowWeak,
                            cipherPref: cipherPref,
                            _securityEnabled: secEnabled,
                            _requireSecureBinds: secReqSecBinds,
                            _secureListenhost: attrs['nsslapd-securelistenhost'],
                            _securePort: attrs['nsslapd-secureport'],
                            _clientAuth: clientAuth,
                            _checkHostname: attrs['nsslapd-ssl-check-hostname'],
                            _validateCert: validateCert,
                            _sslVersionMin: attrs['sslversionmin'],
                            _sslVersionMax: attrs['sslversionmax'],
                            _allowWeakCipher: allowWeak,
                        }
                    ), function() {
                        if (!saving) {
                            this.loadRSAConfig();
                        }
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.addNotification(
                        "error",
                        `Error loading security configuration - ${msg}`
                    );
                });
    }

    handleNavSelect(key) {
        this.setState({
            activeKey: key
        });
    }

    handleSwitchChange(value) {
        if (!value) {
            // We are disabling security, ask for confirmation
            this.setState({showConfirmDisable: true});
        } else {
            // Check if we have certs, if we do make the user choose one from dropdown list, otherwise reject the
            // enablement
            if (this.state.serverCerts.length > 0) {
                this.setState({
                    primaryCertName: this.state.nssslpersonalityssl,
                    showSecurityEnableModal: true,
                });
            } else {
                this.addNotification(
                    "error",
                    `There must be at least one server certificate present in the security database to enable security`
                );
            }
        }
    }

    closeSecurityEnableModal () {
        this.setState({
            showSecurityEnableModal: false,
        });
    }

    handleSecEnableChange (e) {
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
        log_cmd("enableSecurity", "Enable security", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.addNotification(
                        "success",
                        `Successfully enabled security.  You must restart the server for this to take effect.`
                    );
                    this.setState({
                        securityEnabled: true,
                        secEnableSpinner: false,
                        showSecurityEnableModal: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.addNotification(
                        "error",
                        `Error enabling security - ${msg}`
                    );
                    this.setState({
                        secEnableSpinner: false,
                        showSecurityEnableModal: false,
                    });
                });
    }

    disableSecurity () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "disable",
        ];
        log_cmd("disableSecurity", "Disable security", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.addNotification(
                        "success",
                        `Successfully disabled security.  You must restart the server for this to take effect.`
                    );
                    this.setState({
                        securityEnabled: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.addNotification(
                        "error",
                        `Error disabling security - ${msg}`
                    );
                });
    }

    saveSecurityConfig () {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'security', 'set'
        ];

        if (this.state._validateCert != this.state.validateCert) {
            cmd.push("--verify-cert-chain-on-startup=" + this.state.validateCert);
        }
        if (this.state._sslVersionMin != this.state.sslVersionMin) {
            cmd.push("--tls-protocol-min=" + this.state.sslVersionMin);
        }
        if (this.state._sslVersionMax != this.state.sslVersionMax) {
            cmd.push("--tls-protocol-max=" + this.state.sslVersionMax);
        }
        if (this.state._clientAuth != this.state.clientAuth) {
            cmd.push("--tls-client-auth=" + this.state.clientAuth);
        }
        if (this.state._securePort != this.state.securePort) {
            cmd.push("--secure-port=" + this.state.securePort);
        }
        if (this.state._secureListenhost != this.state.secureListenhost) {
            cmd.push("--listen-host=" + this.state.secureListenhost);
        }
        if (this.state._allowWeakCipher != this.state.allowWeakCipher) {
            let val = "off";
            if (this.state.allowWeakCipher) {
                val = "on";
            }
            cmd.push("--allow-insecure-ciphers=" + val);
        }
        if (this.state._checkHostname != this.state.checkHostname) {
            let val = "off";
            if (this.state.checkHostname) {
                val = "on";
            }
            cmd.push("--check-hostname=" + val);
        }
        if (this.state._requireSecureBinds != this.state.requireSecureBinds) {
            let val = "off";
            if (this.state.requireSecureBinds) {
                val = "on";
            }
            cmd.push("--require-secure-authentication=" + val);
        }

        if (cmd.length > 5) {
            log_cmd("saveSecurityConfig", "Applying security config change", cmd);
            let msg = "Successfully updated security configuration.  You must restart the server for these changes to take effect.";

            this.setState({
                // Start the spinner
                saving: true
            });

            cockpit
                    .spawn(cmd, {superuser: true, "err": "message"})
                    .done(content => {
                        this.loadSecurityConfig(1);
                        this.addNotification(
                            "success",
                            msg
                        );
                        this.setState({
                            saving: false
                        });
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.loadSecurityConfig();
                        this.setState({
                            saving: false
                        });
                        let msg = errMsg.desc;
                        if ('info' in errMsg) {
                            msg = errMsg.desc + " - " + errMsg.info;
                        }
                        this.addNotification(
                            "error",
                            `Error updating security configuration - ${msg}`
                        );
                    });
        }
    }

    handleTypeaheadChange(value) {
        if (value.length == 0) {
            return;
        }
        this.setState({
            nssslpersonalityssl: value[0],
        });
    }

    handleChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value,
        });
    }

    handleLoginModal(e) {
        const value = e.target.value.trim();
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

    render() {
        let securityPage = "";
        let serverCert = [this.state.nssslpersonalityssl];

        if (this.state.loaded && !this.state.saving) {
            let configPage = "";
            if (this.state.securityEnabled) {
                configPage =
                    <div>
                        <Row className="ds-margin-top" title="The server's secure port number (nsslapd-secureport).">
                            <Col componentClass={ControlLabel} sm={3}>
                                Server Secure Port
                            </Col>
                            <Col sm={4}>
                                <input id="securePort" className="ds-input-auto" onChange={this.handleChange} type="text" defaultValue={this.state.securePort} />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="This parameter can be used to restrict the Directory Server instance to a single IP interface (hostname, or IP address).  This parameter specifically sets what interface to use for TLS traffic.  Requires restart. (nsslapd-securelistenhost).">
                            <Col componentClass={ControlLabel} sm={3}>
                                Secure Listen Host
                            </Col>
                            <Col sm={4}>
                                <input id="secureListenhost" className="ds-input-auto" type="text" onChange={this.handleChange} defaultValue={this.state.secureListenhost} />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The name, or nickname, of the server certificate inthe NSS datgabase the server should use (nsSSLPersonalitySSL).">
                            <Col className="ds-no-padding" sm={3}>
                                <ControlLabel>Server Certificate Name</ControlLabel>
                            </Col>
                            <Col sm={4}>
                                <Typeahead
                                    id="serverCertNameTypeahead"
                                    onChange={this.handleTypeaheadChange}
                                    selected={serverCert}
                                    emptyLabel="No matching certificates found"
                                    options={this.state.serverCertNames}
                                    newSelectionPrefix="Select a server certificate"
                                    placeholder="Type a sever certificate nickname..."
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The minimum SSL/TLS version the server will accept (sslversionmin).">
                            <Col componentClass={ControlLabel} sm={3}>
                                Minimum TLS Version
                            </Col>
                            <Col sm={4}>
                                <select id="sslVersionMin" className="btn btn-default dropdown ds-select" onChange={this.handleChange} defaultValue={this.state.sslVersionMin}>
                                    <option />
                                    <option>TLS1.3</option>
                                    <option>TLS1.2</option>
                                    <option>TLS1.1</option>
                                    <option>TLS1.0</option>
                                    <option>SSL3</option>
                                </select>
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The maximum SSL/TLS version the server will accept (sslversionmax).">
                            <Col componentClass={ControlLabel} sm={3}>
                                Maximum TLS Version
                            </Col>
                            <Col sm={4}>
                                <select id="sslVersionMax" className="btn btn-default dropdown ds-select" onChange={this.handleChange} defaultValue={this.state.sslVersionMax}>
                                    <option />
                                    <option>TLS1.3</option>
                                    <option>TLS1.2</option>
                                    <option>TLS1.1</option>
                                    <option>TLS1.0</option>
                                    <option>SSL3</option>
                                </select>
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="Sets how the Directory Server enforces TLS client authentication (nsSSLClientAuth).">
                            <Col componentClass={ControlLabel} sm={3}>
                                Client Authentication
                            </Col>
                            <Col sm={4}>
                                <select id="clientAuth" className="btn btn-default dropdown ds-select" onChange={this.handleChange} defaultValue={this.state.clientAuth}>
                                    <option>off</option>
                                    <option>allowed</option>
                                    <option>required</option>
                                </select>
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="Validate server's certificate expiration date (nsslapd-validate-cert).">
                            <Col componentClass={ControlLabel} sm={3}>
                                Validate Certificate
                            </Col>
                            <Col sm={4}>
                                <select id="validateCert" className="btn btn-default dropdown ds-select" onChange={this.handleChange} defaultValue={this.state.validateCert}>
                                    <option>warn</option>
                                    <option>on</option>
                                    <option>off</option>
                                </select>
                            </Col>
                        </Row>
                        <p />
                        <Row>
                            <Col sm={5}>
                                <Checkbox
                                    id="requireSecureBinds"
                                    defaultChecked={this.state.requireSecureBinds}
                                    onChange={this.handleChange}
                                    title="Require all connections use TLS (nsslapd-require-secure-binds)."
                                >
                                    Require Secure Connections
                                </Checkbox>
                            </Col>
                        </Row>
                        <Row>
                            <Col sm={5}>
                                <Checkbox
                                    id="checkHostname"
                                    defaultChecked={this.state.checkHostname}
                                    onChange={this.handleChange}
                                    title="Verify authenticity of a request by matching the host name against the value assigned to the common name (cn) attribute of the subject name (subjectDN field) in the certificate being presented. (nsslapd-ssl-check-hostname)."
                                >
                                    Verify Certificate Subject Hostname
                                </Checkbox>
                            </Col>
                        </Row>
                        <Row>
                            <Col sm={5}>
                                <Checkbox
                                    id="allowWeakCipher"
                                    defaultChecked={this.state.allowWeakCipher}
                                    onChange={this.handleChange}
                                    title="Allow weak ciphers (allowWeakCipher)."
                                >
                                    Allow Weak Ciphers
                                </Checkbox>
                            </Col>
                        </Row>
                        <p />
                        <Button
                            bsStyle="primary"
                            className="ds-margin-top-med"
                            onClick={() => {
                                this.saveSecurityConfig();
                            }}
                        >
                            Save Configuration
                        </Button>
                    </div>;
            }

            securityPage =
                <div className="container-fluid">
                    <NotificationController
                        notifications={this.state.notifications}
                        removeNotificationAction={this.removeNotification}
                    />
                    <div className="ds-tab-table">
                        <TabContainer id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                            <div>
                                <Nav bsClass="nav nav-tabs nav-tabs-pf">
                                    <NavItem eventKey={1}>
                                        <div dangerouslySetInnerHTML={{__html: 'Security Configuration'}} />
                                    </NavItem>
                                    <NavItem eventKey={2}>
                                        <div dangerouslySetInnerHTML={{__html: 'Certificate Management'}} />
                                    </NavItem>
                                    <NavItem eventKey={3}>
                                        <div dangerouslySetInnerHTML={{__html: 'Cipher Preferences'}} />
                                    </NavItem>
                                </Nav>
                                <TabContent>
                                    <TabPane eventKey={1}>
                                        <div className="ds-margin-top-xlg ds-indent">
                                            <Row>
                                                <Col componentClass={ControlLabel} sm={2}>
                                                    Security Enabled
                                                </Col>
                                                <Col sm={2}>
                                                    <Switch
                                                        onChange={this.handleSwitchChange}
                                                        checked={this.state.securityEnabled}
                                                        height={20}
                                                    />
                                                </Col>
                                            </Row>
                                            <hr />
                                            {configPage}
                                        </div>
                                    </TabPane>

                                    <TabPane eventKey={2}>
                                        <div className="ds-margin-top-lg">
                                            <CertificateManagement
                                                serverId={this.props.serverId}
                                                CACerts={this.state.CACerts}
                                                ServerCerts={this.state.serverCerts}
                                                addNotification={this.addNotification}
                                            />
                                        </div>
                                    </TabPane>

                                    <TabPane eventKey={3}>
                                        <div className="ds-indent ds-tab-table">
                                            <Ciphers
                                                serverId={this.props.serverId}
                                                supportedCiphers={this.state.supportedCiphers}
                                                cipherPref={this.state.cipherPref}
                                                enabledCiphers={this.state.enabledCiphers}
                                                addNotification={this.addNotification}
                                            />
                                        </div>
                                    </TabPane>
                                </TabContent>
                            </div>
                        </TabContainer>
                    </div>
                </div>;
        } else if (this.state.saving) {
            securityPage =
                <div className="ds-loading-spinner ds-center">
                    <p />
                    <h4>Saving security information ...</h4>
                    <Spinner loading size="md" />
                </div>;
        } else {
            securityPage =
                <div className="ds-loading-spinner ds-center">
                    <p />
                    <h4>Loading security information ...</h4>
                    <Spinner loading size="md" />
                </div>;
        }
        return (
            <div>
                {securityPage}
                <ConfirmPopup
                    showModal={this.state.showConfirmDisable}
                    closeHandler={this.closeConfirmDisable}
                    actionFunc={this.disableSecurity}
                    msg="Are you sure you want to disable security?"
                    msgContent="Attention: this requires the server to be restarted to take effect."
                />
                <SecurityEnableModal
                    showModal={this.state.showSecurityEnableModal}
                    closeHandler={this.closeSecurityEnableModal}
                    handleChange={this.handleSecEnableChange}
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
    serverId: PropTypes.string,
};

Security.defaultProps = {
    serverId: "",
};

export default Security;
