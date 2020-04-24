import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
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
import "../../css/ds.css";
import PropTypes from "prop-types";

const errorlog_levels = [
    1, 2, 4, 8, 16, 32, 64, 128, 2048,
    4096, 8192, 32768, 65536, 262144,
];

const settings_attrs = [
    'nsslapd-errorlog',
    'nsslapd-errorlog-level',
    'nsslapd-errorlog-logging-enabled',
    'errorlevel-1',
    'errorlevel-2',
    'errorlevel-4',
    'errorlevel-8',
    'errorlevel-16',
    'errorlevel-32',
    'errorlevel-64',
    'errorlevel-128',
    'errorlevel-2048',
    'errorlevel-4096',
    'errorlevel-8192',
    'errorlevel-32768',
    'errorlevel-65536',
    'errorlevel-262144',
];

const rotation_attrs = [
    'nsslapd-errorlog-logrotationsync-enabled',
    'nsslapd-errorlog-logrotationsynchour',
    'nsslapd-errorlog-logrotationsyncmin',
    'nsslapd-errorlog-logrotationtime',
    'nsslapd-errorlog-logrotationtimeunit',
    'nsslapd-errorlog-maxlogsize',
    'nsslapd-errorlog-maxlogsperdir',
];

const exp_attrs = [
    'nsslapd-errorlog-logexpirationtime',
    'nsslapd-errorlog-logexpirationtimeunit',
    'nsslapd-errorlog-logmaxdiskspace',
    'nsslapd-errorlog-logminfreediskspace',
];

export class ServerErrorLog extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            loading: false,
            loaded: false,
            activeKey: 1,
            saveSettingsDisabled: true,
            saveRotationDisabled: true,
            saveExpDisabled: true,
            attrs: this.props.attrs,
        };

        this.handleChange = this.handleChange.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.reloadConfig = this.reloadConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
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

    handleChange(e, nav_tab) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;
        let disableBtnName = "";
        let config_attrs = [];
        if (nav_tab == "settings") {
            config_attrs = settings_attrs;
            disableBtnName = "saveSettingsDisabled";
        } else if (nav_tab == "rotation") {
            config_attrs = rotation_attrs;
            disableBtnName = "saveRotationDisabled";
        } else {
            config_attrs = exp_attrs;
            disableBtnName = "saveExpDisabled";
        }

        // Check if a setting was changed, if so enable the save button
        for (let config_attr of config_attrs) {
            if (attr == config_attr && this.state['_' + config_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let config_attr of config_attrs) {
            if (attr != config_attr && this.state['_' + config_attr] != this.state[config_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [attr]: value,
            [disableBtnName]: disableSaveBtn,
        });
    }

    saveConfig(nav_tab) {
        let level_change = false;
        let new_level = 0;
        this.setState({
            loading: true
        });

        let config_attrs = [];
        if (nav_tab == "settings") {
            config_attrs = settings_attrs;
        } else if (nav_tab == "rotation") {
            config_attrs = rotation_attrs;
        } else {
            config_attrs = exp_attrs;
        }

        let cmd = [
            'dsconf', '-j', this.props.serverId, 'config', 'replace'
        ];

        for (let attr of config_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                if (attr.startsWith("errorlevel-")) {
                    level_change = true;
                    continue;
                }
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

        if (level_change) {
            for (let level of errorlog_levels) {
                if (this.state['errorlevel-' + level.toString()]) {
                    new_level += level;
                }
            }
            cmd.push("nsslapd-errorlog-level" + "=" + new_level.toString());
        }

        log_cmd("saveConfig", "Saving error log settings", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.reloadConfig();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully updated Error Log settings"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.reloadConfig();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error saving Error Log settings - ${errMsg.desc}`
                    );
                });
    }

    loadConfig() {
        let attrs = this.state.attrs;
        let enabled = false;
        let level_val = parseInt(attrs['nsslapd-errorlog-level'][0]);
        let loglevel = {};

        if (attrs['nsslapd-errorlog-logging-enabled'][0] == "on") {
            enabled = true;
        }
        for (let level of errorlog_levels) {
            if (level & level_val) {
                loglevel[level.toString()] = true;
            } else {
                loglevel[level.toString()] = false;
            }
        }

        this.setState({
            loading: false,
            loaded: true,
            saveSettingsDisabled: true,
            saveRotationDisabled: true,
            saveExpDisabled: true,
            'nsslapd-errorlog': attrs['nsslapd-errorlog'][0],
            'nsslapd-errorlog-level': attrs['nsslapd-errorlog-level'][0],
            'errorlevel-1': loglevel['1'],
            'errorlevel-2': loglevel['2'],
            'errorlevel-4': loglevel['4'],
            'errorlevel-8': loglevel['8'],
            'errorlevel-16': loglevel['16'],
            'errorlevel-32': loglevel['32'],
            'errorlevel-64': loglevel['64'],
            'errorlevel-128': loglevel['128'],
            'errorlevel-2048': loglevel['2048'],
            'errorlevel-4096': loglevel['4096'],
            'errorlevel-8192': loglevel['8192'],
            'errorlevel-32768': loglevel['32768'],
            'errorlevel-65536': loglevel['65536'],
            'errorlevel-262144': loglevel['262144'],
            'nsslapd-errorlog-logexpirationtime': attrs['nsslapd-errorlog-logexpirationtime'][0],
            'nsslapd-errorlog-logexpirationtimeunit': attrs['nsslapd-errorlog-logexpirationtimeunit'][0],
            'nsslapd-errorlog-logging-enabled': enabled,
            'nsslapd-errorlog-logmaxdiskspace': attrs['nsslapd-errorlog-logmaxdiskspace'][0],
            'nsslapd-errorlog-logminfreediskspace': attrs['nsslapd-errorlog-logminfreediskspace'][0],
            'nsslapd-errorlog-logrotationsync-enabled': attrs['nsslapd-errorlog-logrotationsync-enabled'][0],
            'nsslapd-errorlog-logrotationsynchour': attrs['nsslapd-errorlog-logrotationsynchour'][0],
            'nsslapd-errorlog-logrotationsyncmin': attrs['nsslapd-errorlog-logrotationsyncmin'][0],
            'nsslapd-errorlog-logrotationtime': attrs['nsslapd-errorlog-logrotationtime'][0],
            'nsslapd-errorlog-logrotationtimeunit': attrs['nsslapd-errorlog-logrotationtimeunit'][0],
            'nsslapd-errorlog-maxlogsize': attrs['nsslapd-errorlog-maxlogsize'][0],
            'nsslapd-errorlog-maxlogsperdir': attrs['nsslapd-errorlog-maxlogsperdir'][0],
            // Record original values
            '_nsslapd-errorlog': attrs['nsslapd-errorlog'][0],
            '_nsslapd-errorlog-level': attrs['nsslapd-errorlog-level'][0],
            '_errorlevel-1': loglevel['1'],
            '_errorlevel-2': loglevel['2'],
            '_errorlevel-4': loglevel['4'],
            '_errorlevel-8': loglevel['8'],
            '_errorlevel-16': loglevel['16'],
            '_errorlevel-32': loglevel['32'],
            '_errorlevel-64': loglevel['64'],
            '_errorlevel-128': loglevel['128'],
            '_errorlevel-2048': loglevel['2048'],
            '_errorlevel-4096': loglevel['4096'],
            '_errorlevel-8192': loglevel['8192'],
            '_errorlevel-32768': loglevel['32768'],
            '_errorlevel-65536': loglevel['65536'],
            '_errorlevel-262144': loglevel['262144'],
            '_nsslapd-errorlog-logexpirationtime': attrs['nsslapd-errorlog-logexpirationtime'][0],
            '_nsslapd-errorlog-logexpirationtimeunit': attrs['nsslapd-errorlog-logexpirationtimeunit'][0],
            '_nsslapd-errorlog-logging-enabled': enabled,
            '_nsslapd-errorlog-logmaxdiskspace': attrs['nsslapd-errorlog-logmaxdiskspace'][0],
            '_nsslapd-errorlog-logminfreediskspace': attrs['nsslapd-errorlog-logminfreediskspace'][0],
            '_nsslapd-errorlog-logrotationsync-enabled': attrs['nsslapd-errorlog-logrotationsync-enabled'][0],
            '_nsslapd-errorlog-logrotationsynchour': attrs['nsslapd-errorlog-logrotationsynchour'][0],
            '_nsslapd-errorlog-logrotationsyncmin': attrs['nsslapd-errorlog-logrotationsyncmin'][0],
            '_nsslapd-errorlog-logrotationtime': attrs['nsslapd-errorlog-logrotationtime'][0],
            '_nsslapd-errorlog-logrotationtimeunit': attrs['nsslapd-errorlog-logrotationtimeunit'][0],
            '_nsslapd-errorlog-maxlogsize': attrs['nsslapd-errorlog-maxlogsize'][0],
            '_nsslapd-errorlog-maxlogsperdir': attrs['nsslapd-errorlog-maxlogsperdir'][0],
        }, this.props.enableTree);
    }

    reloadConfig() {
        this.setState({
            loading: true,
        });
        let cmd = [
            "dsconf", "-j", this.props.serverId, "config", "get"
        ];
        log_cmd("reloadConfig", "load Error Log configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    let enabled = false;
                    let level_val = parseInt(attrs['nsslapd-errorlog-level'][0]);
                    let loglevel = {};

                    if (attrs['nsslapd-errorlog-logging-enabled'][0] == "on") {
                        enabled = true;
                    }
                    for (let level of errorlog_levels) {
                        if (level & level_val) {
                            loglevel[level.toString()] = true;
                        } else {
                            loglevel[level.toString()] = false;
                        }
                    }

                    this.setState(() => (
                        {
                            loading: false,
                            loaded: true,
                            saveSettingsDisabled: true,
                            saveRotationDisabled: true,
                            saveExpDisabled: true,
                            'nsslapd-errorlog': attrs['nsslapd-errorlog'][0],
                            'nsslapd-errorlog-level': attrs['nsslapd-errorlog-level'][0],
                            'errorlevel-1': loglevel['1'],
                            'errorlevel-2': loglevel['2'],
                            'errorlevel-4': loglevel['4'],
                            'errorlevel-8': loglevel['8'],
                            'errorlevel-16': loglevel['16'],
                            'errorlevel-32': loglevel['32'],
                            'errorlevel-64': loglevel['64'],
                            'errorlevel-128': loglevel['128'],
                            'errorlevel-2048': loglevel['2048'],
                            'errorlevel-4096': loglevel['4096'],
                            'errorlevel-8192': loglevel['8192'],
                            'errorlevel-32768': loglevel['32768'],
                            'errorlevel-65536': loglevel['65536'],
                            'errorlevel-262144': loglevel['262144'],
                            'nsslapd-errorlog-logexpirationtime': attrs['nsslapd-errorlog-logexpirationtime'][0],
                            'nsslapd-errorlog-logexpirationtimeunit': attrs['nsslapd-errorlog-logexpirationtimeunit'][0],
                            'nsslapd-errorlog-logging-enabled': enabled,
                            'nsslapd-errorlog-logmaxdiskspace': attrs['nsslapd-errorlog-logmaxdiskspace'][0],
                            'nsslapd-errorlog-logminfreediskspace': attrs['nsslapd-errorlog-logminfreediskspace'][0],
                            'nsslapd-errorlog-logrotationsync-enabled': attrs['nsslapd-errorlog-logrotationsync-enabled'][0],
                            'nsslapd-errorlog-logrotationsynchour': attrs['nsslapd-errorlog-logrotationsynchour'][0],
                            'nsslapd-errorlog-logrotationsyncmin': attrs['nsslapd-errorlog-logrotationsyncmin'][0],
                            'nsslapd-errorlog-logrotationtime': attrs['nsslapd-errorlog-logrotationtime'][0],
                            'nsslapd-errorlog-logrotationtimeunit': attrs['nsslapd-errorlog-logrotationtimeunit'][0],
                            'nsslapd-errorlog-maxlogsize': attrs['nsslapd-errorlog-maxlogsize'][0],
                            'nsslapd-errorlog-maxlogsperdir': attrs['nsslapd-errorlog-maxlogsperdir'][0],
                            // Record original values
                            '_nsslapd-errorlog': attrs['nsslapd-errorlog'][0],
                            '_nsslapd-errorlog-level': attrs['nsslapd-errorlog-level'][0],
                            '_errorlevel-1': loglevel['1'],
                            '_errorlevel-2': loglevel['2'],
                            '_errorlevel-4': loglevel['4'],
                            '_errorlevel-8': loglevel['8'],
                            '_errorlevel-16': loglevel['16'],
                            '_errorlevel-32': loglevel['32'],
                            '_errorlevel-64': loglevel['64'],
                            '_errorlevel-128': loglevel['128'],
                            '_errorlevel-2048': loglevel['2048'],
                            '_errorlevel-4096': loglevel['4096'],
                            '_errorlevel-8192': loglevel['8192'],
                            '_errorlevel-32768': loglevel['32768'],
                            '_errorlevel-65536': loglevel['65536'],
                            '_errorlevel-262144': loglevel['262144'],
                            '_nsslapd-errorlog-logexpirationtime': attrs['nsslapd-errorlog-logexpirationtime'][0],
                            '_nsslapd-errorlog-logexpirationtimeunit': attrs['nsslapd-errorlog-logexpirationtimeunit'][0],
                            '_nsslapd-errorlog-logging-enabled': enabled,
                            '_nsslapd-errorlog-logmaxdiskspace': attrs['nsslapd-errorlog-logmaxdiskspace'][0],
                            '_nsslapd-errorlog-logminfreediskspace': attrs['nsslapd-errorlog-logminfreediskspace'][0],
                            '_nsslapd-errorlog-logrotationsync-enabled': attrs['nsslapd-errorlog-logrotationsync-enabled'][0],
                            '_nsslapd-errorlog-logrotationsynchour': attrs['nsslapd-errorlog-logrotationsynchour'][0],
                            '_nsslapd-errorlog-logrotationsyncmin': attrs['nsslapd-errorlog-logrotationsyncmin'][0],
                            '_nsslapd-errorlog-logrotationtime': attrs['nsslapd-errorlog-logrotationtime'][0],
                            '_nsslapd-errorlog-logrotationtimeunit': attrs['nsslapd-errorlog-logrotationtimeunit'][0],
                            '_nsslapd-errorlog-maxlogsize': attrs['nsslapd-errorlog-maxlogsize'][0],
                            '_nsslapd-errorlog-maxlogsperdir': attrs['nsslapd-errorlog-maxlogsperdir'][0],
                        })
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading Error Log configuration - ${errMsg.desc}`
                    );
                    this.setState({
                        loading: false,
                        loaded: true,
                    });
                });
    }

    render() {
        let body =
            <div className="ds-margin-top-lg">
                <TabContainer id="error-log-settings" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div className="ds-margin-top">
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: 'Settings'}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: 'Rotation Policy'}} />
                            </NavItem>
                            <NavItem eventKey={3}>
                                <div dangerouslySetInnerHTML={{__html: 'Deletion Policy'}} />
                            </NavItem>
                        </Nav>

                        <TabContent className="ds-margin-top-lg">
                            <TabPane eventKey={1}>
                                <Form>
                                    <Row className="ds-margin-top" title="Enable access logging (nsslapd-errorlog-logging-enabled).">
                                        <Col sm={3}>
                                            <Checkbox
                                                id="nsslapd-errorlog-logging-enabled"
                                                defaultChecked={this.state['nsslapd-errorlog-logging-enabled']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "settings");
                                                }}
                                            >
                                                Enable Error Logging
                                            </Checkbox>
                                        </Col>
                                    </Row>
                                    <div className="ds-margin-left">
                                        <Row className="ds-margin-top" title="Enable access logging (nsslapd-errorlog).">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Error Log Location
                                            </Col>
                                            <Col sm={6}>
                                                <FormControl
                                                    id="nsslapd-errorlog"
                                                    type="text"
                                                    value={this.state['nsslapd-errorlog']}
                                                    onChange={(e) => {
                                                        this.handleChange(e, "settings");
                                                    }}
                                                />
                                            </Col>
                                        </Row>
                                        <table className="table table-striped table-bordered table-hover ds-loglevel-table ds-margin-top-lg" id="errorlog-level-table">
                                            <thead>
                                                <tr>
                                                    <th className="ds-table-checkbox" />
                                                    <th>Logging Level</th>
                                                </tr>
                                            </thead>
                                            <tbody>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-1"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-1']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Trace Function Calls
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-2"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-2']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Packet Handling
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-4"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-4']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Heavy Trace Output
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-8"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-8']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Connection Management
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-16"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-16']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Packets Sent & Received
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-32"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-32']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Search Filter Processing
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-64"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-64']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Config File Processing
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-128"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-128']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Access Control List Processing
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-256"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-2048']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Log Entry Parsing
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-4096"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-4096']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Housekeeping
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-8192"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-8192']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Replication
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-32768"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-32768']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Entry Cache
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-65536"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-65536']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Plugin
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="errorlevel-262144"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['errorlevel-262144']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Access Control Summary
                                                    </td>
                                                </tr>
                                            </tbody>
                                        </table>
                                    </div>
                                    <Button
                                        disabled={this.state.saveSettingsDisabled}
                                        bsStyle="primary"
                                        className="ds-margin-top-med"
                                        onClick={() => {
                                            this.saveConfig("settings");
                                        }}
                                    >
                                        Save Settings
                                    </Button>
                                </Form>
                            </TabPane>
                        </TabContent>

                        <TabContent className="ds-margin-top-lg">
                            <TabPane eventKey={2}>
                                <Form horizontal>
                                    <Row className="ds-margin-top-xlg" title="The maximum number of logs that are archived (nsslapd-errorlog-maxlogsperdir).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Maximum Number Of Logs
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-errorlog-maxlogsperdir"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-errorlog-maxlogsperdir']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top-lg" title="The maximum size of each log file in megabytes (nsslapd-errorlog-maxlogsize).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Maximum Log Size (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-errorlog-maxlogsize"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-errorlog-maxlogsize']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <hr />
                                    <Row className="ds-margin-top" title="Rotate the log based this number of time units (nsslapd-errorlog-logrotationtime).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Create New Log Every...
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-errorlog-logrotationtime"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-errorlog-logrotationtime']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                        <Col sm={2}>
                                            <select
                                                className="btn btn-default dropdown"
                                                id="nsslapd-errorlog-logrotationtimeunit"
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                                value={this.state['nsslapd-errorlog-logrotationtimeunit']}
                                            >
                                                <option>minute</option>
                                                <option>hour</option>
                                                <option>day</option>
                                                <option>week</option>
                                                <option>month</option>
                                            </select>
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The hour whenthe log should be rotated (nsslapd-errorlog-logrotationsynchour).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Hour
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-errorlog-logrotationsynchour"
                                                type="number"
                                                min="0"
                                                max="23"
                                                value={this.state['nsslapd-errorlog-logrotationsynchour']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The minute within the hour to rotate the log (nsslapd-errorlog-logrotationsyncmin).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Minute
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-errorlog-logrotationsyncmin"
                                                type="number"
                                                min="0"
                                                max="59"
                                                value={this.state['nsslapd-errorlog-logrotationsyncmin']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Button
                                        disabled={this.state.saveRotationDisabled}
                                        bsStyle="primary"
                                        className="ds-margin-top-med"
                                        onClick={() => {
                                            this.saveConfig("rotation");
                                        }}
                                    >
                                        Save Rotation Settings
                                    </Button>
                                </Form>
                            </TabPane>
                        </TabContent>

                        <TabContent className="ds-margin-top-lg">
                            <TabPane eventKey={3}>
                                <Form horizontal>
                                    <Row className="ds-margin-top-xlg" title="The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-errorlog-logmaxdiskspace).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Total Log Archive Exceeds (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-errorlog-logmaxdiskspace"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-errorlog-logmaxdiskspace']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-errorlog-logminfreediskspace).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Free Disk Space (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-errorlog-logminfreediskspace"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-errorlog-logminfreediskspace']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="Server deletes an old archived log file when it is older than the specified age. (nsslapd-errorlog-logexpirationtime).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Log File is Older Than...
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-errorlog-logexpirationtime"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-errorlog-logexpirationtime']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                        <Col sm={2}>
                                            <select
                                                className="btn btn-default dropdown"
                                                id="nsslapd-errorlog-logexpirationtimeunit"
                                                value={this.state['nsslapd-errorlog-logexpirationtimeunit']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            >
                                                <option>day</option>
                                                <option>week</option>
                                                <option>month</option>
                                            </select>
                                        </Col>
                                    </Row>
                                    <Button
                                        disabled={this.state.saveExpDisabled}
                                        bsStyle="primary"
                                        className="ds-margin-top-med"
                                        onClick={() => {
                                            this.saveConfig("exp");
                                        }}
                                    >
                                        Save Deletion Settings
                                    </Button>
                                </Form>
                            </TabPane>
                        </TabContent>
                    </div>
                </TabContainer>
            </div>;

        if (this.state.loading || !this.state.loaded) {
            body = <Spinner loading size="md" />;
        }

        return (
            <div id="server-errorlog-page">
                <Row>
                    <Col sm={5}>
                        <ControlLabel className="ds-suffix-header ds-margin-top-lg">
                            Error Log Settings
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh the Access Log settings"
                                onClick={this.reloadConfig}
                                disabled={this.state.loading}
                            />
                        </ControlLabel>
                    </Col>
                </Row>
                {body}
            </div>
        );
    }
}

// Property types and defaults

ServerErrorLog.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerErrorLog.defaultProps = {
    addNotification: noop,
    serverId: "",
    attrs: {},
};
