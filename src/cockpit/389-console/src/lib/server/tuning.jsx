import cockpit from "cockpit";
import React from "react";
import CustomCollapse from "../customCollapse.jsx";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Col,
    ControlLabel,
    Form,
    FormControl,
    Icon,
    Checkbox,
    Row,
    noop,
    Spinner,
} from "patternfly-react";
import PropTypes from "prop-types";

const tuning_attrs = [
    'nsslapd-ndn-cache-enabled',
    'nsslapd-ignore-virtual-attrs',
    'nsslapd-connection-nocanon',
    'nsslapd-enable-turbo-mode',
    'nsslapd-threadnumber',
    'nsslapd-maxdescriptors',
    'nsslapd-timelimit',
    'nsslapd-sizelimit',
    'nsslapd-pagedsizelimit',
    'nsslapd-idletimeout',
    'nsslapd-ioblocktimeout',
    'nsslapd-outbound-ldap-io-timeout',
    'nsslapd-maxbersize',
    'nsslapd-maxsasliosize',
    'nsslapd-listen-backlog-size',
    'nsslapd-max-filter-nest-level',
    'nsslapd-ndn-cache-max-size',
];

export class ServerTuning extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            loading: false,
            loaded: false,
            activeKey: 1,
            saveDisabled: true,
            errObj: {},
            attrs: this.props.attrs,
        };

        this.handleChange = this.handleChange.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
    }

    componentDidMount() {
        if (!this.state.loaded) {
            this.loadConfig();
        } else {
            this.props.enableTree();
        }
    }

    handleChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;
        let valueErr = false;
        let errObj = this.state.errObj;

        // Check if a setting was changed, if so enable the save button
        for (let tuning_attr of tuning_attrs) {
            if (attr == tuning_attr && this.state['_' + tuning_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let tuning_attr of tuning_attrs) {
            if (attr != tuning_attr && this.state['_' + tuning_attr] != this.state[tuning_attr]) {
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
            saveDisabled: disableSaveBtn,
            errObj: errObj,
        });
    }

    loadConfig(reloading) {
        if (reloading) {
            this.setState({
                loading: true
            });
        }

        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("loadConfig", "Load server configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    let ndnEnabled = false;
                    let ignoreVirtAttrs = false;
                    let connNoCannon = false;
                    let turboMode = false;

                    if (attrs['nsslapd-ndn-cache-enabled'][0] == "on") {
                        ndnEnabled = true;
                    }
                    if (attrs['nsslapd-ignore-virtual-attrs'][0] == "on") {
                        ignoreVirtAttrs = true;
                    }
                    if (attrs['nsslapd-connection-nocanon'][0] == "on") {
                        connNoCannon = true;
                    }
                    if (attrs['nsslapd-enable-turbo-mode'][0] == "on") {
                        turboMode = true;
                    }
                    this.setState({
                        loaded: true,
                        loading: false,
                        // Settings
                        'nsslapd-ndn-cache-enabled': ndnEnabled,
                        'nsslapd-ignore-virtual-attrs': ignoreVirtAttrs,
                        'nsslapd-connection-nocanon': connNoCannon,
                        'nsslapd-enable-turbo-mode': turboMode,
                        'nsslapd-threadnumber': attrs['nsslapd-threadnumber'][0],
                        'nsslapd-maxdescriptors': attrs['nsslapd-maxdescriptors'][0],
                        'nsslapd-timelimit': attrs['nsslapd-timelimit'][0],
                        'nsslapd-sizelimit': attrs['nsslapd-sizelimit'][0],
                        'nsslapd-pagedsizelimit': attrs['nsslapd-pagedsizelimit'][0],
                        'nsslapd-idletimeout': attrs['nsslapd-idletimeout'][0],
                        'nsslapd-ioblocktimeout': attrs['nsslapd-ioblocktimeout'][0],
                        'nsslapd-outbound-ldap-io-timeout': attrs['nsslapd-outbound-ldap-io-timeout'][0],
                        'nsslapd-maxbersize': attrs['nsslapd-maxbersize'][0],
                        'nsslapd-maxsasliosize': attrs['nsslapd-maxsasliosize'][0],
                        'nsslapd-listen-backlog-size': attrs['nsslapd-listen-backlog-size'][0],
                        'nsslapd-max-filter-nest-level': attrs['nsslapd-max-filter-nest-level'][0],
                        'nsslapd-ndn-cache-max-size': attrs['nsslapd-ndn-cache-max-size'][0],
                        // Record original values
                        '_nsslapd-ndn-cache-enabled': ndnEnabled,
                        '_nsslapd-ignore-virtual-attrs': ignoreVirtAttrs,
                        '_nsslapd-connection-nocanon': connNoCannon,
                        '_nsslapd-enable-turbo-mode': turboMode,
                        '_nsslapd-threadnumber': attrs['nsslapd-threadnumber'][0],
                        '_nsslapd-maxdescriptors': attrs['nsslapd-maxdescriptors'][0],
                        '_nsslapd-timelimit': attrs['nsslapd-timelimit'][0],
                        '_nsslapd-sizelimit': attrs['nsslapd-sizelimit'][0],
                        '_nsslapd-pagedsizelimit': attrs['nsslapd-pagedsizelimit'][0],
                        '_nsslapd-idletimeout': attrs['nsslapd-idletimeout'][0],
                        '_nsslapd-ioblocktimeout': attrs['nsslapd-ioblocktimeout'][0],
                        '_nsslapd-outbound-ldap-io-timeout': attrs['nsslapd-outbound-ldap-io-timeout'][0],
                        '_nsslapd-maxbersize': attrs['nsslapd-maxbersize'][0],
                        '_nsslapd-maxsasliosize': attrs['nsslapd-maxsasliosize'][0],
                        '_nsslapd-listen-backlog-size': attrs['nsslapd-listen-backlog-size'][0],
                        '_nsslapd-max-filter-nest-level': attrs['nsslapd-max-filter-nest-level'][0],
                        '_nsslapd-ndn-cache-max-size': attrs['nsslapd-ndn-cache-max-size'][0],
                    }, this.props.enableTree());
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.setState({
                        loaded: true
                    });
                    this.props.addNotification(
                        "error",
                        `Error loading server configuration - ${errMsg.desc}`
                    );
                });
    }

    saveConfig() {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];

        for (let attr of tuning_attrs) {
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

        log_cmd("saveConfig", "Saving Tuning configuration", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadConfig(1);
                    this.props.addNotification(
                        "success",
                        "Successfully updated Advanced configuration"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.loadConfig(1);
                    this.props.addNotification(
                        "error",
                        `Error updating Advanced configuration - ${errMsg.desc}`
                    );
                });
    }

    render () {
        let reloadSpinner = "";
        let body = "";

        if (this.state.loading) {
            reloadSpinner = <Spinner loading size="md" />;
        }
        if (!this.state.loaded) {
            body =
                <div className="ds-loading-spinner ds-margin-top ds-center">
                    <h4>Loading tuning configuration ...</h4>
                    <Spinner className="ds-margin-top" loading size="md" />
                </div>;
        } else {
            body =
                <div>
                    <Row>
                        <Col sm={4}>
                            <ControlLabel className="ds-suffix-header ds-margin-top-lg ds-margin-left-sm">
                                Tuning & Limits
                                <Icon className="ds-left-margin ds-refresh"
                                    type="fa" name="refresh" title="Refresh tuning settings"
                                    onClick={() => {
                                        this.loadConfig(1);
                                    }}
                                />
                            </ControlLabel>
                        </Col>
                        <Col sm={8} className="ds-margin-top-lg">
                            {reloadSpinner}
                        </Col>
                    </Row>
                    <hr />
                    <Form horizontal>
                        <Row title="The number of worker threads that handle database operations (nsslapd-threadnumber)." className="ds-margin-top">
                            <Col componentClass={ControlLabel} sm={4}>
                                Number Of Worker Threads
                            </Col>
                            <Col sm={3}>
                                <FormControl
                                    id="nsslapd-threadnumber"
                                    type="number"
                                    min="-1"
                                    max="1048576"
                                    value={this.state['nsslapd-threadnumber']}
                                    onChange={this.handleChange}
                                    className={this.state.errObj['nsslapd-threadnumber'] ? "ds-input-bad" : ""}
                                />
                            </Col>
                        </Row>
                        <Row title="The maximum number of file descriptors the server will use (nsslapd-maxdescriptors)." className="ds-margin-top">
                            <Col componentClass={ControlLabel} sm={4}>
                                Maximum File Descriptors
                            </Col>
                            <Col sm={3}>
                                <FormControl
                                    id="nsslapd-maxdescriptors"
                                    type="number"
                                    min="1024"
                                    max="1048576"
                                    value={this.state['nsslapd-maxdescriptors']}
                                    onChange={this.handleChange}
                                    className={this.state.errObj['nsslapd-maxdescriptors'] ? "ds-input-bad" : ""}
                                />
                            </Col>
                        </Row>
                        <Row title="The maximum number of seconds allocated for a search request (nsslapd-timelimit)." className="ds-margin-top">
                            <Col componentClass={ControlLabel} sm={4}>
                                Search Time Limit
                            </Col>
                            <Col sm={3}>
                                <FormControl
                                    id="nsslapd-timelimit"
                                    type="number"
                                    min="-1"
                                    max="2147483647"
                                    value={this.state['nsslapd-timelimit']}
                                    onChange={this.handleChange}
                                    className={this.state.errObj['nsslapd-timelimit'] ? "ds-input-bad" : ""}
                                />
                            </Col>
                        </Row>
                        <Row title="The maximum number of entries to return from a search operation (nsslapd-sizelimit)." className="ds-margin-top">
                            <Col componentClass={ControlLabel} sm={4}>
                                Search Size Limit
                            </Col>
                            <Col sm={3}>
                                <FormControl
                                    id="nsslapd-sizelimit"
                                    type="number"
                                    min="-1"
                                    max="2147483647"
                                    value={this.state['nsslapd-sizelimit']}
                                    onChange={this.handleChange}
                                    className={this.state.errObj['nsslapd-sizelimit'] ? "ds-input-bad" : ""}
                                />
                            </Col>
                        </Row>
                        <Row title="The maximum number of entries to return from a paged search operation (nsslapd-pagedsizelimit)." className="ds-margin-top">
                            <Col componentClass={ControlLabel} sm={4}>
                                Paged Search Size Limit
                            </Col>
                            <Col sm={3}>
                                <FormControl
                                    id="nsslapd-pagedsizelimit"
                                    type="number"
                                    min="-1"
                                    max="2147483647"
                                    value={this.state['nsslapd-pagedsizelimit']}
                                    onChange={this.handleChange}
                                    className={this.state.errObj['nsslapd-pagedsizelimit'] ? "ds-input-bad" : ""}
                                />
                            </Col>
                        </Row>
                        <Row title="Sets the amount of time in seconds after which an idle LDAP client connection is closed by the server (nsslapd-idletimeout)." className="ds-margin-top">
                            <Col componentClass={ControlLabel} sm={4}>
                                Idle Connection Timeout
                            </Col>
                            <Col sm={3}>
                                <FormControl
                                    id="nsslapd-idletimeout"
                                    type="number"
                                    min="0"
                                    max="2147483647"
                                    value={this.state['nsslapd-idletimeout']}
                                    onChange={this.handleChange}
                                    className={this.state.errObj['nsslapd-idletimeout'] ? "ds-input-bad" : ""}
                                />
                            </Col>
                        </Row>
                        <Row title="Sets the amount of time in milliseconds after which the connection to a stalled LDAP client is closed (nsslapd-ioblocktimeout)." className="ds-margin-top">
                            <Col componentClass={ControlLabel} sm={4}>
                                I/O Block Timeout
                            </Col>
                            <Col sm={3}>
                                <FormControl
                                    id="nsslapd-ioblocktimeout"
                                    type="number"
                                    min="0"
                                    max="2147483647"
                                    value={this.state['nsslapd-ioblocktimeout']}
                                    onChange={this.handleChange}
                                    className={this.state.errObj['nsslapd-ioblocktimeout'] ? "ds-input-bad" : ""}
                                />
                            </Col>
                        </Row>
                    </Form>
                    <CustomCollapse className="ds-margin-left-sm ds-margin-right">
                        <div className="ds-margin-top">
                            <Form horizontal>
                                <Row className="ds-margin-top" title="Sets the I/O wait time for all outbound LDAP connections (nsslapd-outbound-ldap-io-timeout).">
                                    <Col componentClass={ControlLabel} sm={4}>
                                        Outbound IO Timeout
                                    </Col>
                                    <Col sm={3}>
                                        <FormControl
                                            type="number"
                                            min="0"
                                            max="2147483647"
                                            id="nsslapd-outbound-ldap-io-timeout"
                                            value={this.state['nsslapd-outbound-ldap-io-timeout']}
                                            onChange={this.handleChange}
                                        />
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top" title="The maximum size in bytes allowed for an incoming message (nsslapd-maxbersize).">
                                    <Col componentClass={ControlLabel} sm={4}>
                                        Maximum BER Size
                                    </Col>
                                    <Col sm={3}>
                                        <FormControl
                                            type="number"
                                            min="1"
                                            max="2147483647"
                                            id="nsslapd-maxbersize"
                                            value={this.state['nsslapd-maxbersize']}
                                            onChange={this.handleChange}
                                        />
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top" title="The maximum allowed SASL IO packet size that the server will accept (nsslapd-maxsasliosize).">
                                    <Col componentClass={ControlLabel} sm={4}>
                                        Maximum SASL IO Size
                                    </Col>
                                    <Col sm={3}>
                                        <FormControl
                                            type="number"
                                            min="-1"
                                            max="2147483647"
                                            id="nsslapd-maxsasliosize"
                                            value={this.state['nsslapd-maxsasliosize']}
                                            onChange={this.handleChange}
                                        />
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top" title="The maximum length for how long the connection queue for the socket can grow before refusing connections (nsslapd-listen-backlog-size).">
                                    <Col componentClass={ControlLabel} sm={4}>
                                        Listen Backlog Size
                                    </Col>
                                    <Col sm={3}>
                                        <FormControl
                                            type="number"
                                            min="64"
                                            id="nsslapd-listen-backlog-size"
                                            value={this.state['nsslapd-listen-backlog-size']}
                                            onChange={this.handleChange}
                                        />
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top" title="Sets how deep a nested search filter is analysed (nsslapd-max-filter-nest-level).">
                                    <Col componentClass={ControlLabel} sm={4}>
                                        Maximum Nested Filter Level
                                    </Col>
                                    <Col sm={3}>
                                        <FormControl
                                            type="number"
                                            min="0"
                                            id="nsslapd-max-filter-nest-level"
                                            value={this.state['nsslapd-max-filter-nest-level']}
                                            onChange={this.handleChange}
                                        />
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top-xlg">
                                    <Col componentClass={ControlLabel} sm={4} title="Enable the normalized DN cache.  Each thread has its own cache (nsslapd-ndn-cache-enabled).">
                                        <Checkbox
                                            checked={this.state['nsslapd-ndn-cache-enabled']}
                                            id="nsslapd-ndn-cache-enabled"
                                            onChange={this.handleChange}
                                        >
                                            Enable Normalized DN Cache
                                        </Checkbox>
                                    </Col>
                                    <Col sm={4}>
                                        <div className="ds-inline">
                                            <FormControl
                                                id="nsslapd-ndn-cache-max-size"
                                                type="number"
                                                min="1048576"
                                                max="2147483647"
                                                className="ds-input-right"
                                                value={this.state['nsslapd-ndn-cache-max-size']}
                                                onChange={this.handleChange}
                                                title="Per thread NDN cache size in bytes (nsslapd-ndn-cache-max-size)."
                                            />
                                        </div>
                                        <div className="ds-inline ds-left-margin ds-lower-field">
                                            <font size="2">bytes</font>
                                        </div>
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top">
                                    <Col sm={4} componentClass={ControlLabel} title="Disable DNS reverse entries for outgoing connections (nsslapd-connection-nocanon).">
                                        <Checkbox
                                            id="nsslapd-connection-nocanon"
                                            defaultChecked={this.state['nsslapd-connection-nocanon']}
                                            onChange={this.handleChange}
                                        >
                                            Disable Reverse DNS Lookups
                                        </Checkbox>
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top">
                                    <Col sm={4} componentClass={ControlLabel} title="Sets the worker threads to continuously read a connection without passing it back to the polling mechanism. (nsslapd-enable-turbo-mode).">
                                        <Checkbox
                                            id="nsslapd-enable-turbo-mode"
                                            defaultChecked={this.state['nsslapd-enable-turbo-mode']}
                                            onChange={this.handleChange}
                                        >
                                            Enable Connection Turbo Mode
                                        </Checkbox>
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top">
                                    <Col sm={4} componentClass={ControlLabel} title="Disable the virtual attribute lookup in a search entry (nsslapd-ignore-virtual-attrs).">
                                        <Checkbox
                                            id="nsslapd-ignore-virtual-attrs"
                                            defaultChecked={this.state['nsslapd-ignore-virtual-attrs']}
                                            onChange={this.handleChange}
                                        >
                                            Disable Virtual Attribute Lookups
                                        </Checkbox>
                                    </Col>
                                </Row>
                            </Form>
                        </div>
                    </CustomCollapse>
                    <Button
                        disabled={this.state.saveDisabled}
                        bsStyle="primary"
                        className="ds-margin-top-lg ds-margin-left"
                        onClick={this.saveConfig}
                    >
                        Save
                    </Button>
                </div>;
        }

        return (
            <div id="tuning-content">
                {body}
            </div>
        );
    }
}

// Property types and defaults

ServerTuning.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerTuning.defaultProps = {
    addNotification: noop,
    serverId: "",
    attrs: {},
};
