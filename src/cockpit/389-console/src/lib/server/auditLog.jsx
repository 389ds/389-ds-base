import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
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
import {
    Button
} from "@patternfly/react-core";
import PropTypes from "prop-types";

const settings_attrs = [
    'nsslapd-auditlog',
    'nsslapd-auditlog-logging-enabled',
];

const rotation_attrs = [
    'nsslapd-auditlog-logrotationsync-enabled',
    'nsslapd-auditlog-logrotationsynchour',
    'nsslapd-auditlog-logrotationsyncmin',
    'nsslapd-auditlog-logrotationtime',
    'nsslapd-auditlog-logrotationtimeunit',
    'nsslapd-auditlog-maxlogsize',
    'nsslapd-auditlog-maxlogsperdir',
];

const exp_attrs = [
    'nsslapd-auditlog-logexpirationtime',
    'nsslapd-auditlog-logexpirationtimeunit',
    'nsslapd-auditlog-logmaxdiskspace',
    'nsslapd-auditlog-logminfreediskspace',
];

export class ServerAuditLog extends React.Component {
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

        log_cmd("saveConfig", "Saving audit log settings", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.reloadConfig();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully updated Audit Log settings"
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
                        `Error saving Audit Log settings - ${errMsg.desc}`
                    );
                });
    }

    loadConfig() {
        let attrs = this.state.attrs;
        let enabled = false;

        if (attrs['nsslapd-auditlog-logging-enabled'][0] == "on") {
            enabled = true;
        }

        this.setState({
            loading: false,
            loaded: true,
            saveSettingsDisabled: true,
            saveRotationDisabled: true,
            saveExpDisabled: true,
            'nsslapd-auditlog': attrs['nsslapd-auditlog'][0],
            'nsslapd-auditlog-logexpirationtime': attrs['nsslapd-auditlog-logexpirationtime'][0],
            'nsslapd-auditlog-logexpirationtimeunit': attrs['nsslapd-auditlog-logexpirationtimeunit'][0],
            'nsslapd-auditlog-logging-enabled': enabled,
            'nsslapd-auditlog-logmaxdiskspace': attrs['nsslapd-auditlog-logmaxdiskspace'][0],
            'nsslapd-auditlog-logminfreediskspace': attrs['nsslapd-auditlog-logminfreediskspace'][0],
            'nsslapd-auditlog-logrotationsync-enabled': attrs['nsslapd-auditlog-logrotationsync-enabled'][0],
            'nsslapd-auditlog-logrotationsynchour': attrs['nsslapd-auditlog-logrotationsynchour'][0],
            'nsslapd-auditlog-logrotationsyncmin': attrs['nsslapd-auditlog-logrotationsyncmin'][0],
            'nsslapd-auditlog-logrotationtime': attrs['nsslapd-auditlog-logrotationtime'][0],
            'nsslapd-auditlog-logrotationtimeunit': attrs['nsslapd-auditlog-logrotationtimeunit'][0],
            'nsslapd-auditlog-maxlogsize': attrs['nsslapd-auditlog-maxlogsize'][0],
            'nsslapd-auditlog-maxlogsperdir': attrs['nsslapd-auditlog-maxlogsperdir'][0],
            // Record original values
            '_nsslapd-auditlog': attrs['nsslapd-auditlog'][0],
            '_nsslapd-auditlog-logexpirationtime': attrs['nsslapd-auditlog-logexpirationtime'][0],
            '_nsslapd-auditlog-logexpirationtimeunit': attrs['nsslapd-auditlog-logexpirationtimeunit'][0],
            '_nsslapd-auditlog-logging-enabled': enabled,
            '_nsslapd-auditlog-logmaxdiskspace': attrs['nsslapd-auditlog-logmaxdiskspace'][0],
            '_nsslapd-auditlog-logminfreediskspace': attrs['nsslapd-auditlog-logminfreediskspace'][0],
            '_nsslapd-auditlog-logrotationsync-enabled': attrs['nsslapd-auditlog-logrotationsync-enabled'][0],
            '_nsslapd-auditlog-logrotationsynchour': attrs['nsslapd-auditlog-logrotationsynchour'][0],
            '_nsslapd-auditlog-logrotationsyncmin': attrs['nsslapd-auditlog-logrotationsyncmin'][0],
            '_nsslapd-auditlog-logrotationtime': attrs['nsslapd-auditlog-logrotationtime'][0],
            '_nsslapd-auditlog-logrotationtimeunit': attrs['nsslapd-auditlog-logrotationtimeunit'][0],
            '_nsslapd-auditlog-maxlogsize': attrs['nsslapd-auditlog-maxlogsize'][0],
            '_nsslapd-auditlog-maxlogsperdir': attrs['nsslapd-auditlog-maxlogsperdir'][0],
        }, this.props.enableTree);
    }

    reloadConfig() {
        this.setState({
            loading: true,
        });
        let cmd = [
            "dsconf", "-j", this.props.serverId, "config", "get"
        ];
        log_cmd("reloadConfig", "load Audit Log configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    let enabled = false;

                    if (attrs['nsslapd-auditlog-logging-enabled'][0] == "on") {
                        enabled = true;
                    }

                    this.setState(() => (
                        {
                            loading: false,
                            loaded: true,
                            saveSettingsDisabled: true,
                            saveRotationDisabled: true,
                            saveExpDisabled: true,
                            'nsslapd-auditlog': attrs['nsslapd-auditlog'][0],
                            'nsslapd-auditlog-logexpirationtime': attrs['nsslapd-auditlog-logexpirationtime'][0],
                            'nsslapd-auditlog-logexpirationtimeunit': attrs['nsslapd-auditlog-logexpirationtimeunit'][0],
                            'nsslapd-auditlog-logging-enabled': enabled,
                            'nsslapd-auditlog-logmaxdiskspace': attrs['nsslapd-auditlog-logmaxdiskspace'][0],
                            'nsslapd-auditlog-logminfreediskspace': attrs['nsslapd-auditlog-logminfreediskspace'][0],
                            'nsslapd-auditlog-logrotationsync-enabled': attrs['nsslapd-auditlog-logrotationsync-enabled'][0],
                            'nsslapd-auditlog-logrotationsynchour': attrs['nsslapd-auditlog-logrotationsynchour'][0],
                            'nsslapd-auditlog-logrotationsyncmin': attrs['nsslapd-auditlog-logrotationsyncmin'][0],
                            'nsslapd-auditlog-logrotationtime': attrs['nsslapd-auditlog-logrotationtime'][0],
                            'nsslapd-auditlog-logrotationtimeunit': attrs['nsslapd-auditlog-logrotationtimeunit'][0],
                            'nsslapd-auditlog-maxlogsize': attrs['nsslapd-auditlog-maxlogsize'][0],
                            'nsslapd-auditlog-maxlogsperdir': attrs['nsslapd-auditlog-maxlogsperdir'][0],
                            // Record original values
                            '_nsslapd-auditlog': attrs['nsslapd-auditlog'][0],
                            '_nsslapd-auditlog-logexpirationtime': attrs['nsslapd-auditlog-logexpirationtime'][0],
                            '_nsslapd-auditlog-logexpirationtimeunit': attrs['nsslapd-auditlog-logexpirationtimeunit'][0],
                            '_nsslapd-auditlog-logging-enabled': enabled,
                            '_nsslapd-auditlog-logmaxdiskspace': attrs['nsslapd-auditlog-logmaxdiskspace'][0],
                            '_nsslapd-auditlog-logminfreediskspace': attrs['nsslapd-auditlog-logminfreediskspace'][0],
                            '_nsslapd-auditlog-logrotationsync-enabled': attrs['nsslapd-auditlog-logrotationsync-enabled'][0],
                            '_nsslapd-auditlog-logrotationsynchour': attrs['nsslapd-auditlog-logrotationsynchour'][0],
                            '_nsslapd-auditlog-logrotationsyncmin': attrs['nsslapd-auditlog-logrotationsyncmin'][0],
                            '_nsslapd-auditlog-logrotationtime': attrs['nsslapd-auditlog-logrotationtime'][0],
                            '_nsslapd-auditlog-logrotationtimeunit': attrs['nsslapd-auditlog-logrotationtimeunit'][0],
                            '_nsslapd-auditlog-maxlogsize': attrs['nsslapd-auditlog-maxlogsize'][0],
                            '_nsslapd-auditlog-maxlogsperdir': attrs['nsslapd-auditlog-maxlogsperdir'][0],
                        })
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading Audit Log configuration - ${errMsg.desc}`
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
                <TabContainer id="audit-log-settings" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
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
                                    <Row className="ds-margin-top" title="Enable access logging (nsslapd-auditlog-logging-enabled).">
                                        <Col sm={3}>
                                            <Checkbox
                                                id="nsslapd-auditlog-logging-enabled"
                                                defaultChecked={this.state['nsslapd-auditlog-logging-enabled']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "settings");
                                                }}
                                            >
                                                Enable Audit Logging
                                            </Checkbox>
                                        </Col>
                                    </Row>
                                    <div className="ds-margin-left">
                                        <Row className="ds-margin-top" title="Enable access logging (nsslapd-auditlog).">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Audit Log Location
                                            </Col>
                                            <Col sm={6}>
                                                <FormControl
                                                    id="nsslapd-auditlog"
                                                    type="text"
                                                    value={this.state['nsslapd-auditlog']}
                                                    onChange={(e) => {
                                                        this.handleChange(e, "settings");
                                                    }}
                                                />
                                            </Col>
                                        </Row>
                                    </div>
                                    <Button
                                        disabled={this.state.saveSettingsDisabled}
                                        variant="primary"
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
                                    <Row className="ds-margin-top-xlg" title="The maximum number of logs that are archived (nsslapd-auditlog-maxlogsperdir).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Maximum Number Of Logs
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditlog-maxlogsperdir"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditlog-maxlogsperdir']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top-lg" title="The maximum size of each log file in megabytes (nsslapd-auditlog-maxlogsize).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Maximum Log Size (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditlog-maxlogsize"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditlog-maxlogsize']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <hr />
                                    <Row className="ds-margin-top" title="Rotate the log based this number of time units (nsslapd-auditlog-logrotationtime).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Create New Log Every...
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditlog-logrotationtime"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditlog-logrotationtime']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                        <Col sm={2}>
                                            <select
                                                className="btn btn-default dropdown"
                                                id="nsslapd-auditlog-logrotationtimeunit"
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                                value={this.state['nsslapd-auditlog-logrotationtimeunit']}
                                            >
                                                <option>minute</option>
                                                <option>hour</option>
                                                <option>day</option>
                                                <option>week</option>
                                                <option>month</option>
                                            </select>
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The hour whenthe log should be rotated (nsslapd-auditlog-logrotationsynchour).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Hour
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditlog-logrotationsynchour"
                                                type="number"
                                                min="0"
                                                max="23"
                                                value={this.state['nsslapd-auditlog-logrotationsynchour']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The minute within the hour to rotate the log (nsslapd-auditlog-logrotationsyncmin).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Minute
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditlog-logrotationsyncmin"
                                                type="number"
                                                min="0"
                                                max="59"
                                                value={this.state['nsslapd-auditlog-logrotationsyncmin']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Button
                                        disabled={this.state.saveRotationDisabled}
                                        variant="primary"
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
                                    <Row className="ds-margin-top-xlg" title="The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-auditlog-logmaxdiskspace).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Total Log Archive Exceeds (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditlog-logmaxdiskspace"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditlog-logmaxdiskspace']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-auditlog-logminfreediskspace).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Free Disk Space (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditlog-logminfreediskspace"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditlog-logminfreediskspace']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="Server deletes an old archived log file when it is older than the specified age. (nsslapd-auditlog-logexpirationtime).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Log File is Older Than...
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditlog-logexpirationtime"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditlog-logexpirationtime']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                        <Col sm={2}>
                                            <select
                                                className="btn btn-default dropdown"
                                                id="nsslapd-auditlog-logexpirationtimeunit"
                                                value={this.state['nsslapd-auditlog-logexpirationtimeunit']}
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
                                        variant="primary"
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
            <div id="server-auditlog-page">
                <Row>
                    <Col sm={5}>
                        <ControlLabel className="ds-suffix-header ds-margin-top-lg">
                            Audit Log Settings
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

ServerAuditLog.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerAuditLog.defaultProps = {
    addNotification: noop,
    serverId: "",
    attrs: {},
};
