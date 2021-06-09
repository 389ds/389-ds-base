import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
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
    Button,
    Checkbox
} from "@patternfly/react-core";
import PropTypes from "prop-types";

const settings_attrs = [
    'nsslapd-auditfaillog',
    'nsslapd-auditfaillog-logging-enabled',
];

const rotation_attrs = [
    'nsslapd-auditfaillog-logrotationsync-enabled',
    'nsslapd-auditfaillog-logrotationsynchour',
    'nsslapd-auditfaillog-logrotationsyncmin',
    'nsslapd-auditfaillog-logrotationtime',
    'nsslapd-auditfaillog-logrotationtimeunit',
    'nsslapd-auditfaillog-maxlogsize',
    'nsslapd-auditfaillog-maxlogsperdir',
];

const exp_attrs = [
    'nsslapd-auditfaillog-logexpirationtime',
    'nsslapd-auditfaillog-logexpirationtimeunit',
    'nsslapd-auditfaillog-logmaxdiskspace',
    'nsslapd-auditfaillog-logminfreediskspace',
];

export class ServerAuditFailLog extends React.Component {
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
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'config', 'replace'
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
                        "Successfully updated Audit Failure Log settings"
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
                        `Error saving Audit Failure Log settings - ${errMsg.desc}`
                    );
                });
    }

    reloadConfig() {
        this.setState({
            loading: true,
        });
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("loadConfig", "load Audit Failure Log configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    let enabled = false;

                    if (attrs['nsslapd-auditfaillog-logging-enabled'][0] == "on") {
                        enabled = true;
                    }

                    this.setState(() => (
                        {
                            loading: false,
                            loaded: true,
                            saveSettingsDisabled: true,
                            saveRotationDisabled: true,
                            saveExpDisabled: true,
                            'nsslapd-auditfaillog': attrs['nsslapd-auditfaillog'][0],
                            'nsslapd-auditfaillog-logexpirationtime': attrs['nsslapd-auditfaillog-logexpirationtime'][0],
                            'nsslapd-auditfaillog-logexpirationtimeunit': attrs['nsslapd-auditfaillog-logexpirationtimeunit'][0],
                            'nsslapd-auditfaillog-logging-enabled': enabled,
                            'nsslapd-auditfaillog-logmaxdiskspace': attrs['nsslapd-auditfaillog-logmaxdiskspace'][0],
                            'nsslapd-auditfaillog-logminfreediskspace': attrs['nsslapd-auditfaillog-logminfreediskspace'][0],
                            'nsslapd-auditfaillog-logrotationsync-enabled': attrs['nsslapd-auditfaillog-logrotationsync-enabled'][0],
                            'nsslapd-auditfaillog-logrotationsynchour': attrs['nsslapd-auditfaillog-logrotationsynchour'][0],
                            'nsslapd-auditfaillog-logrotationsyncmin': attrs['nsslapd-auditfaillog-logrotationsyncmin'][0],
                            'nsslapd-auditfaillog-logrotationtime': attrs['nsslapd-auditfaillog-logrotationtime'][0],
                            'nsslapd-auditfaillog-logrotationtimeunit': attrs['nsslapd-auditfaillog-logrotationtimeunit'][0],
                            'nsslapd-auditfaillog-maxlogsize': attrs['nsslapd-auditfaillog-maxlogsize'][0],
                            'nsslapd-auditfaillog-maxlogsperdir': attrs['nsslapd-auditfaillog-maxlogsperdir'][0],
                            // Record original values
                            '_nsslapd-auditfaillog': attrs['nsslapd-auditfaillog'][0],
                            '_nsslapd-auditfaillog-logexpirationtime': attrs['nsslapd-auditfaillog-logexpirationtime'][0],
                            '_nsslapd-auditfaillog-logexpirationtimeunit': attrs['nsslapd-auditfaillog-logexpirationtimeunit'][0],
                            '_nsslapd-auditfaillog-logging-enabled': enabled,
                            '_nsslapd-auditfaillog-logmaxdiskspace': attrs['nsslapd-auditfaillog-logmaxdiskspace'][0],
                            '_nsslapd-auditfaillog-logminfreediskspace': attrs['nsslapd-auditfaillog-logminfreediskspace'][0],
                            '_nsslapd-auditfaillog-logrotationsync-enabled': attrs['nsslapd-auditfaillog-logrotationsync-enabled'][0],
                            '_nsslapd-auditfaillog-logrotationsynchour': attrs['nsslapd-auditfaillog-logrotationsynchour'][0],
                            '_nsslapd-auditfaillog-logrotationsyncmin': attrs['nsslapd-auditfaillog-logrotationsyncmin'][0],
                            '_nsslapd-auditfaillog-logrotationtime': attrs['nsslapd-auditfaillog-logrotationtime'][0],
                            '_nsslapd-auditfaillog-logrotationtimeunit': attrs['nsslapd-auditfaillog-logrotationtimeunit'][0],
                            '_nsslapd-auditfaillog-maxlogsize': attrs['nsslapd-auditfaillog-maxlogsize'][0],
                            '_nsslapd-auditfaillog-maxlogsperdir': attrs['nsslapd-auditfaillog-maxlogsperdir'][0],
                        })
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading Audit Failure Log configuration - ${errMsg.desc}`
                    );
                    this.setState({
                        loading: false,
                    });
                });
    }

    loadConfig() {
        let attrs = this.state.attrs;
        let enabled = false;

        if (attrs['nsslapd-auditfaillog-logging-enabled'][0] == "on") {
            enabled = true;
        }

        this.setState({
            loaded: true,
            saveSettingsDisabled: true,
            saveRotationDisabled: true,
            saveExpDisabled: true,
            'nsslapd-auditfaillog': attrs['nsslapd-auditfaillog'][0],
            'nsslapd-auditfaillog-logexpirationtime': attrs['nsslapd-auditfaillog-logexpirationtime'][0],
            'nsslapd-auditfaillog-logexpirationtimeunit': attrs['nsslapd-auditfaillog-logexpirationtimeunit'][0],
            'nsslapd-auditfaillog-logging-enabled': enabled,
            'nsslapd-auditfaillog-logmaxdiskspace': attrs['nsslapd-auditfaillog-logmaxdiskspace'][0],
            'nsslapd-auditfaillog-logminfreediskspace': attrs['nsslapd-auditfaillog-logminfreediskspace'][0],
            'nsslapd-auditfaillog-logrotationsync-enabled': attrs['nsslapd-auditfaillog-logrotationsync-enabled'][0],
            'nsslapd-auditfaillog-logrotationsynchour': attrs['nsslapd-auditfaillog-logrotationsynchour'][0],
            'nsslapd-auditfaillog-logrotationsyncmin': attrs['nsslapd-auditfaillog-logrotationsyncmin'][0],
            'nsslapd-auditfaillog-logrotationtime': attrs['nsslapd-auditfaillog-logrotationtime'][0],
            'nsslapd-auditfaillog-logrotationtimeunit': attrs['nsslapd-auditfaillog-logrotationtimeunit'][0],
            'nsslapd-auditfaillog-maxlogsize': attrs['nsslapd-auditfaillog-maxlogsize'][0],
            'nsslapd-auditfaillog-maxlogsperdir': attrs['nsslapd-auditfaillog-maxlogsperdir'][0],
            // Record original values
            '_nsslapd-auditfaillog': attrs['nsslapd-auditfaillog'][0],
            '_nsslapd-auditfaillog-logexpirationtime': attrs['nsslapd-auditfaillog-logexpirationtime'][0],
            '_nsslapd-auditfaillog-logexpirationtimeunit': attrs['nsslapd-auditfaillog-logexpirationtimeunit'][0],
            '_nsslapd-auditfaillog-logging-enabled': enabled,
            '_nsslapd-auditfaillog-logmaxdiskspace': attrs['nsslapd-auditfaillog-logmaxdiskspace'][0],
            '_nsslapd-auditfaillog-logminfreediskspace': attrs['nsslapd-auditfaillog-logminfreediskspace'][0],
            '_nsslapd-auditfaillog-logrotationsync-enabled': attrs['nsslapd-auditfaillog-logrotationsync-enabled'][0],
            '_nsslapd-auditfaillog-logrotationsynchour': attrs['nsslapd-auditfaillog-logrotationsynchour'][0],
            '_nsslapd-auditfaillog-logrotationsyncmin': attrs['nsslapd-auditfaillog-logrotationsyncmin'][0],
            '_nsslapd-auditfaillog-logrotationtime': attrs['nsslapd-auditfaillog-logrotationtime'][0],
            '_nsslapd-auditfaillog-logrotationtimeunit': attrs['nsslapd-auditfaillog-logrotationtimeunit'][0],
            '_nsslapd-auditfaillog-maxlogsize': attrs['nsslapd-auditfaillog-maxlogsize'][0],
            '_nsslapd-auditfaillog-maxlogsperdir': attrs['nsslapd-auditfaillog-maxlogsperdir'][0],
        }, this.props.enableTree);
    }

    render() {
        let body =
            <div className="ds-margin-top-lg">
                <TabContainer id="auditfail-log-settings" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
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
                                    <Row className="ds-margin-top" title="Enable access logging (nsslapd-auditfaillog-logging-enabled).">
                                        <Col sm={3}>
                                            <Checkbox
                                                id="nsslapd-auditfaillog-logging-enabled"
                                                isChecked={this.state['nsslapd-auditfaillog-logging-enabled']}
                                                onChange={(checked, e) => {
                                                    this.handleChange(e, "settings");
                                                }}
                                                label="Enable Audit Failure Logging"
                                            />
                                        </Col>
                                    </Row>
                                    <div className="ds-margin-left">
                                        <Row className="ds-margin-top" title="Enable access logging (nsslapd-auditfaillog).">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Audit Failure Log Location
                                            </Col>
                                            <Col sm={6}>
                                                <FormControl
                                                    id="nsslapd-auditfaillog"
                                                    type="text"
                                                    value={this.state['nsslapd-auditfaillog']}
                                                    onChange={(e) => {
                                                        this.handleChange(e, "settings");
                                                    }}
                                                />
                                            </Col>
                                        </Row>
                                    </div>
                                    <Button
                                        isDisabled={this.state.saveSettingsDisabled}
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
                                    <Row className="ds-margin-top-xlg" title="The maximum number of logs that are archived (nsslapd-auditfaillog-maxlogsperdir).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Maximum Number Of Logs
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditfaillog-maxlogsperdir"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditfaillog-maxlogsperdir']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top-lg" title="The maximum size of each log file in megabytes (nsslapd-auditfaillog-maxlogsize).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Maximum Log Size (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditfaillog-maxlogsize"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditfaillog-maxlogsize']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <hr />
                                    <Row className="ds-margin-top" title="Rotate the log based this number of time units (nsslapd-auditfaillog-logrotationtime).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Create New Log Every...
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditfaillog-logrotationtime"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditfaillog-logrotationtime']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                        <Col sm={2}>
                                            <select
                                                className="btn btn-default dropdown"
                                                id="nsslapd-auditfaillog-logrotationtimeunit"
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                                value={this.state['nsslapd-auditfaillog-logrotationtimeunit']}
                                            >
                                                <option>minute</option>
                                                <option>hour</option>
                                                <option>day</option>
                                                <option>week</option>
                                                <option>month</option>
                                            </select>
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The hour whenthe log should be rotated (nsslapd-auditfaillog-logrotationsynchour).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Hour
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditfaillog-logrotationsynchour"
                                                type="number"
                                                min="0"
                                                max="23"
                                                value={this.state['nsslapd-auditfaillog-logrotationsynchour']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The minute within the hour to rotate the log (nsslapd-auditfaillog-logrotationsyncmin).">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Minute
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditfaillog-logrotationsyncmin"
                                                type="number"
                                                min="0"
                                                max="59"
                                                value={this.state['nsslapd-auditfaillog-logrotationsyncmin']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Button
                                        isDisabled={this.state.saveRotationDisabled}
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
                                    <Row className="ds-margin-top-xlg" title="The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-auditfaillog-logmaxdiskspace).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Total Log Archive Exceeds (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditfaillog-logmaxdiskspace"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditfaillog-logmaxdiskspace']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-auditfaillog-logminfreediskspace).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Free Disk Space (in MB)
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditfaillog-logminfreediskspace"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditfaillog-logminfreediskspace']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="Server deletes an old archived log file when it is older than the specified age. (nsslapd-auditfaillog-logexpirationtime).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Log File is Older Than...
                                        </Col>
                                        <Col sm={2}>
                                            <FormControl
                                                id="nsslapd-auditfaillog-logexpirationtime"
                                                type="number"
                                                min="1"
                                                max="2147483647"
                                                value={this.state['nsslapd-auditfaillog-logexpirationtime']}
                                                onChange={(e) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                            />
                                        </Col>
                                        <Col sm={2}>
                                            <select
                                                className="btn btn-default dropdown"
                                                id="nsslapd-auditfaillog-logexpirationtimeunit"
                                                value={this.state['nsslapd-auditfaillog-logexpirationtimeunit']}
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
                                        isDisabled={this.state.saveExpDisabled}
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
            <div id="server-auditfaillog-page">
                <Row>
                    <Col sm={5}>
                        <ControlLabel className="ds-suffix-header ds-margin-top-lg">
                            Audit Failure Log Settings
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

ServerAuditFailLog.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerAuditFailLog.defaultProps = {
    addNotification: noop,
    serverId: "",
    attrs: {},
};
