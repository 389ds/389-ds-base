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

const accesslog_levels = [
    4,
    256,
    512
];

const settings_attrs = [
    'nsslapd-accesslog',
    'nsslapd-accesslog-level',
    'nsslapd-accesslog-logbuffering',
    'nsslapd-accesslog-logging-enabled',
    'accesslevel-4',
    'accesslevel-256',
    'accesslevel-512',
];

const rotation_attrs = [
    'nsslapd-accesslog-logrotationsync-enabled',
    'nsslapd-accesslog-logrotationsynchour',
    'nsslapd-accesslog-logrotationsyncmin',
    'nsslapd-accesslog-logrotationtime',
    'nsslapd-accesslog-logrotationtimeunit',
    'nsslapd-accesslog-maxlogsize',
    'nsslapd-accesslog-maxlogsperdir',
];

const exp_attrs = [
    'nsslapd-accesslog-logexpirationtime',
    'nsslapd-accesslog-logexpirationtimeunit',
    'nsslapd-accesslog-logmaxdiskspace',
    'nsslapd-accesslog-logminfreediskspace',
];

export class ServerAccessLog extends React.Component {
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

    componentWillMount() {
        // Loading config
        if (!this.state.loaded) {
            this.loadConfig();
        }
    }

    componentDidMount() {
        this.props.enableTree();
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
                if (attr.startsWith("accesslevel-")) {
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
            for (let level of accesslog_levels) {
                if (this.state['accesslevel-' + level.toString()]) {
                    new_level += level;
                }
            }
            cmd.push("nsslapd-accesslog-level" + "=" + new_level.toString());
        }

        log_cmd("saveConfig", "Saving access log settings", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.reloadConfig();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully updated Access Log settings"
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
                        `Error saving Access Log settings - ${errMsg.desc}`
                    );
                });
    }

    reloadConfig() {
        this.setState({
            loading: true
        });
        let cmd = [
            "dsconf", "-j", this.props.serverId, "config", "get"
        ];
        log_cmd("reloadConfig", "load Access Log configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    let enabled = false;
                    let buffering = false;
                    let level_val = parseInt(attrs['nsslapd-accesslog-level'][0]);
                    let loglevel = {};

                    if (attrs['nsslapd-accesslog-logging-enabled'][0] == "on") {
                        enabled = true;
                    }
                    if (attrs['nsslapd-accesslog-logbuffering'][0] == "on") {
                        buffering = true;
                    }
                    for (let level of accesslog_levels) {
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
                            'nsslapd-accesslog': attrs['nsslapd-accesslog'][0],
                            'nsslapd-accesslog-level': attrs['nsslapd-accesslog-level'][0],
                            'accesslevel-4': loglevel['4'],
                            'accesslevel-256': loglevel['256'],
                            'accesslevel-512': loglevel['512'],
                            'nsslapd-accesslog-logbuffering': buffering,
                            'nsslapd-accesslog-logexpirationtime': attrs['nsslapd-accesslog-logexpirationtime'][0],
                            'nsslapd-accesslog-logexpirationtimeunit': attrs['nsslapd-accesslog-logexpirationtimeunit'][0],
                            'nsslapd-accesslog-logging-enabled': enabled,
                            'nsslapd-accesslog-logmaxdiskspace': attrs['nsslapd-accesslog-logmaxdiskspace'][0],
                            'nsslapd-accesslog-logminfreediskspace': attrs['nsslapd-accesslog-logminfreediskspace'][0],
                            'nsslapd-accesslog-logrotationsync-enabled': attrs['nsslapd-accesslog-logrotationsync-enabled'][0],
                            'nsslapd-accesslog-logrotationsynchour': attrs['nsslapd-accesslog-logrotationsynchour'][0],
                            'nsslapd-accesslog-logrotationsyncmin': attrs['nsslapd-accesslog-logrotationsyncmin'][0],
                            'nsslapd-accesslog-logrotationtime': attrs['nsslapd-accesslog-logrotationtime'][0],
                            'nsslapd-accesslog-logrotationtimeunit': attrs['nsslapd-accesslog-logrotationtimeunit'][0],
                            'nsslapd-accesslog-maxlogsize': attrs['nsslapd-accesslog-maxlogsize'][0],
                            'nsslapd-accesslog-maxlogsperdir': attrs['nsslapd-accesslog-maxlogsperdir'][0],
                            // Record original values
                            '_nsslapd-accesslog': attrs['nsslapd-accesslog'][0],
                            '_nsslapd-accesslog-level': attrs['nsslapd-accesslog-level'][0],
                            '_accesslevel-4': loglevel['4'],
                            '_accesslevel-256': loglevel['256'],
                            '_accesslevel-512': loglevel['512'],
                            '_nsslapd-accesslog-logbuffering': buffering,
                            '_nsslapd-accesslog-logexpirationtime': attrs['nsslapd-accesslog-logexpirationtime'][0],
                            '_nsslapd-accesslog-logexpirationtimeunit': attrs['nsslapd-accesslog-logexpirationtimeunit'][0],
                            '_nsslapd-accesslog-logging-enabled': enabled,
                            '_nsslapd-accesslog-logmaxdiskspace': attrs['nsslapd-accesslog-logmaxdiskspace'][0],
                            '_nsslapd-accesslog-logminfreediskspace': attrs['nsslapd-accesslog-logminfreediskspace'][0],
                            '_nsslapd-accesslog-logrotationsync-enabled': attrs['nsslapd-accesslog-logrotationsync-enabled'][0],
                            '_nsslapd-accesslog-logrotationsynchour': attrs['nsslapd-accesslog-logrotationsynchour'][0],
                            '_nsslapd-accesslog-logrotationsyncmin': attrs['nsslapd-accesslog-logrotationsyncmin'][0],
                            '_nsslapd-accesslog-logrotationtime': attrs['nsslapd-accesslog-logrotationtime'][0],
                            '_nsslapd-accesslog-logrotationtimeunit': attrs['nsslapd-accesslog-logrotationtimeunit'][0],
                            '_nsslapd-accesslog-maxlogsize': attrs['nsslapd-accesslog-maxlogsize'][0],
                            '_nsslapd-accesslog-maxlogsperdir': attrs['nsslapd-accesslog-maxlogsperdir'][0],
                        })
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading Access Log configuration - ${errMsg.desc}`
                    );
                    this.setState({
                        loading: false,
                        loaded: true,
                    });
                });
    }

    loadConfig() {
        let attrs = this.state.attrs;
        let enabled = false;
        let buffering = false;
        let level_val = parseInt(attrs['nsslapd-accesslog-level'][0]);
        let loglevel = {};

        if (attrs['nsslapd-accesslog-logging-enabled'][0] == "on") {
            enabled = true;
        }
        if (attrs['nsslapd-accesslog-logbuffering'][0] == "on") {
            buffering = true;
        }
        for (let level of accesslog_levels) {
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
            'nsslapd-accesslog': attrs['nsslapd-accesslog'][0],
            'nsslapd-accesslog-level': attrs['nsslapd-accesslog-level'][0],
            'accesslevel-4': loglevel['4'],
            'accesslevel-256': loglevel['256'],
            'accesslevel-512': loglevel['512'],
            'nsslapd-accesslog-logbuffering': buffering,
            'nsslapd-accesslog-logexpirationtime': attrs['nsslapd-accesslog-logexpirationtime'][0],
            'nsslapd-accesslog-logexpirationtimeunit': attrs['nsslapd-accesslog-logexpirationtimeunit'][0],
            'nsslapd-accesslog-logging-enabled': enabled,
            'nsslapd-accesslog-logmaxdiskspace': attrs['nsslapd-accesslog-logmaxdiskspace'][0],
            'nsslapd-accesslog-logminfreediskspace': attrs['nsslapd-accesslog-logminfreediskspace'][0],
            'nsslapd-accesslog-logrotationsync-enabled': attrs['nsslapd-accesslog-logrotationsync-enabled'][0],
            'nsslapd-accesslog-logrotationsynchour': attrs['nsslapd-accesslog-logrotationsynchour'][0],
            'nsslapd-accesslog-logrotationsyncmin': attrs['nsslapd-accesslog-logrotationsyncmin'][0],
            'nsslapd-accesslog-logrotationtime': attrs['nsslapd-accesslog-logrotationtime'][0],
            'nsslapd-accesslog-logrotationtimeunit': attrs['nsslapd-accesslog-logrotationtimeunit'][0],
            'nsslapd-accesslog-maxlogsize': attrs['nsslapd-accesslog-maxlogsize'][0],
            'nsslapd-accesslog-maxlogsperdir': attrs['nsslapd-accesslog-maxlogsperdir'][0],
            // Record original values
            '_nsslapd-accesslog': attrs['nsslapd-accesslog'][0],
            '_nsslapd-accesslog-level': attrs['nsslapd-accesslog-level'][0],
            '_accesslevel-4': loglevel['4'],
            '_accesslevel-256': loglevel['256'],
            '_accesslevel-512': loglevel['512'],
            '_nsslapd-accesslog-logbuffering': buffering,
            '_nsslapd-accesslog-logexpirationtime': attrs['nsslapd-accesslog-logexpirationtime'][0],
            '_nsslapd-accesslog-logexpirationtimeunit': attrs['nsslapd-accesslog-logexpirationtimeunit'][0],
            '_nsslapd-accesslog-logging-enabled': enabled,
            '_nsslapd-accesslog-logmaxdiskspace': attrs['nsslapd-accesslog-logmaxdiskspace'][0],
            '_nsslapd-accesslog-logminfreediskspace': attrs['nsslapd-accesslog-logminfreediskspace'][0],
            '_nsslapd-accesslog-logrotationsync-enabled': attrs['nsslapd-accesslog-logrotationsync-enabled'][0],
            '_nsslapd-accesslog-logrotationsynchour': attrs['nsslapd-accesslog-logrotationsynchour'][0],
            '_nsslapd-accesslog-logrotationsyncmin': attrs['nsslapd-accesslog-logrotationsyncmin'][0],
            '_nsslapd-accesslog-logrotationtime': attrs['nsslapd-accesslog-logrotationtime'][0],
            '_nsslapd-accesslog-logrotationtimeunit': attrs['nsslapd-accesslog-logrotationtimeunit'][0],
            '_nsslapd-accesslog-maxlogsize': attrs['nsslapd-accesslog-maxlogsize'][0],
            '_nsslapd-accesslog-maxlogsperdir': attrs['nsslapd-accesslog-maxlogsperdir'][0],
        });
    }

    render() {
        let body =
            <div className="ds-margin-top-lg">
                <TabContainer id="access-log-settings" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
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
                                    <Row className="ds-margin-top" title="Enable access logging (nsslapd-accesslog-logging-enabled).">
                                        <Col sm={3}>
                                            <Checkbox
                                                id="nsslapd-accesslog-logging-enabled"
                                                defaultChecked={this.state['nsslapd-accesslog-logging-enabled']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "settings");
                                                }}
                                            >
                                                Enable Access Logging
                                            </Checkbox>
                                        </Col>
                                    </Row>
                                    <div className="ds-margin-left">
                                        <Row className="ds-margin-top" title="Enable access logging (nsslapd-accesslog).">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Access Log Location
                                            </Col>
                                            <Col sm={6}>
                                                <FormControl
                                                    id="nsslapd-accesslog"
                                                    type="text"
                                                    value={this.state['nsslapd-accesslog']}
                                                    onChange={(e) => {
                                                        this.handleChange(e, "settings");
                                                    }}
                                                />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top" title="Disable access log buffering for faster troubleshooting, but this will impact server performance (nsslapd-accesslog-logbuffering).">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                <Checkbox
                                                    id="nsslapd-accesslog-logbuffering"
                                                    defaultChecked={this.state['nsslapd-accesslog-logbuffering']}
                                                    onChange={(e) => {
                                                        this.handleChange(e, "settings");
                                                    }}
                                                >
                                                    Access Log Buffering Enabled
                                                </Checkbox>
                                            </Col>
                                        </Row>
                                        <table className="table table-striped table-bordered table-hover ds-loglevel-table ds-margin-top-lg" id="accesslog-level-table">
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
                                                            id="accesslevel-256"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['accesslevel-256']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Default Logging
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="accesslevel-4"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['accesslevel-4']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Internal Operations
                                                    </td>
                                                </tr>
                                                <tr>
                                                    <td className="ds-table-checkbox">
                                                        <input
                                                            id="accesslevel-512"
                                                            onChange={(e) => {
                                                                this.handleChange(e, "settings");
                                                            }}
                                                            checked={this.state['accesslevel-512']}
                                                            type="checkbox"
                                                        />
                                                    </td>
                                                    <td className="ds-left-align">
                                                        Entry Access and Referrals
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
                                    <Row className="ds-margin-top-xlg" title="The maximum number of logs that are archived (nsslapd-accesslog-maxlogsperdir).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Maximum Number Of Logs
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-accesslog-maxlogsperdir"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-accesslog-maxlogsperdir']}
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
                                                id="nsslapd-accesslog-maxlogsize"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-accesslog-maxlogsize']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <hr />
                                    <Row className="ds-margin-top" title="Rotate the log based this number of time units (nsslapd-accesslog-logrotationtime).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Create New Log Every...
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-accesslog-logrotationtime"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-accesslog-logrotationtime']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                        <Col sm={2}>
                                            <select
                                                className="btn btn-default dropdown"
                                                id="nsslapd-accesslog-logrotationtimeunit"
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                                value={this.state['nsslapd-accesslog-logrotationtimeunit']}
                                            >
                                                <option>minute</option>
                                                <option>hour</option>
                                                <option>day</option>
                                                <option>week</option>
                                                <option>month</option>
                                            </select>
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The hour whenthe log should be rotated (nsslapd-accesslog-logrotationsynchour).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Hour
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-accesslog-logrotationsynchour"
                                                type="number"
                                                min="0"
                                                max="23"
                                                value={this.state['nsslapd-accesslog-logrotationsynchour']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The minute within the hour to rotate the log (nsslapd-accesslog-logrotationsyncmin).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Minute
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-accesslog-logrotationsyncmin"
                                                type="number"
                                                min="0"
                                                max="59"
                                                value={this.state['nsslapd-accesslog-logrotationsyncmin']}
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
                                    <Row className="ds-margin-top-xlg" title="The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-accesslog-logmaxdiskspace).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Total Log Archive Exceeds (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-accesslog-logmaxdiskspace"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-accesslog-logmaxdiskspace']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-accesslog-logminfreediskspace).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Free Disk Space (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-accesslog-logminfreediskspace"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-accesslog-logminfreediskspace']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="Server deletes an old archived log file when it is older than the specified age. (nsslapd-accesslog-logexpirationtime).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Log File is Older Than...
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-accesslog-logexpirationtime"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-accesslog-logexpirationtime']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                        <Col sm={2}>
                                            <select
                                                className="btn btn-default dropdown"
                                                id="nsslapd-accesslog-logexpirationtimeunit"
                                                value={this.state['nsslapd-accesslog-logexpirationtimeunit']}
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
            <div id="server-accesslog-page">
                <Row>
                    <Col sm={5}>
                        <ControlLabel className="ds-suffix-header ds-margin-top-lg">
                            Access Log Settings
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

ServerAccessLog.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerAccessLog.defaultProps = {
    addNotification: noop,
    serverId: "",
    attrs: {},
};
