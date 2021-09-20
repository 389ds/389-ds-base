import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Form,
    FormGroup,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    TimePicker,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';
import PropTypes from "prop-types";

const settings_attrs = [
    'nsslapd-auditlog',
    'nsslapd-auditlog-level',
    'nsslapd-auditlog-logbuffering',
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

const rotation_attrs_no_time = [
    'nsslapd-auditlog-logrotationsync-enabled',
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
            activeTabKey: 0,
            saveSettingsDisabled: true,
            saveRotationDisabled: true,
            saveExpDisabled: true,
            attrs: this.props.attrs,
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.handleChange = this.handleChange.bind(this);
        this.handleTimeChange = this.handleTimeChange.bind(this);
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

    handleChange(e, nav_tab) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let disableSaveBtn = true;
        let disableBtnName = "";
        let config_attrs = [];
        if (nav_tab == "settings") {
            config_attrs = settings_attrs;
            disableBtnName = "saveSettingsDisabled";
        } else if (nav_tab == "rotation") {
            disableBtnName = "saveRotationDisabled";
            config_attrs = rotation_attrs;
        } else {
            config_attrs = exp_attrs;
            disableBtnName = "saveExpDisabled";
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

        this.setState({
            [attr]: value,
            [disableBtnName]: disableSaveBtn,
        });
    }

    handleTimeChange(time_str) {
        let disableSaveBtn = true;
        const time_parts = time_str.split(":");
        let hour = time_parts[0];
        let min = time_parts[1];
        if (hour.length == 2 && hour[0] == "0") {
            hour = hour[1];
        }
        if (min.length == 2 && min[0] == "0") {
            min = min[1];
        }

        // Start doing the Save button checking
        for (const config_attr of rotation_attrs_no_time) {
            if (this.state[config_attr] != this.state['_' + config_attr]) {
                disableSaveBtn = false;
                break;
            }
        }
        if (hour != this.state['_nsslapd-auditlog-logrotationsynchour'] ||
            min != this.state['_nsslapd-auditlog-logrotationsyncmin']) {
            disableSaveBtn = false;
        }

        this.setState({
            'nsslapd-auditlog-logrotationsynchour': hour,
            'nsslapd-auditlog-logrotationsyncmin': min,
            saveRotationDisabled: disableSaveBtn,
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

        const cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'config', 'replace'
        ];

        for (const attr of config_attrs) {
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

        if (cmd.length == 5) {
            // Nothing to save, just return
            return;
        }

        log_cmd("saveConfig", "Saving audit log settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
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
                    const errMsg = JSON.parse(err);
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

    reloadConfig(refresh) {
        this.setState({
            loading: refresh,
            loaded: !refresh,
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadConfig", "load Audit Log configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
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
                    const errMsg = JSON.parse(err);
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

    loadConfig() {
        const attrs = this.state.attrs;
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
            // Record original values,
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

    render() {
        let saveSettingsName = "Save Log Settings";
        let saveRotationName = "Save Rotation Settings";
        let saveDeletionName = "Save Deletion Settings";
        const extraPrimaryProps = {};
        let rotationTime = "";
        let hour = this.state['nsslapd-auditlog-logrotationsynchour'] ? this.state['nsslapd-auditlog-logrotationsynchour'] : "00";
        let min = this.state['nsslapd-auditlog-logrotationsyncmin'] ? this.state['nsslapd-auditlog-logrotationsyncmin'] : "00";

        if (this.state.loading) {
            saveSettingsName = "Saving settings ...";
            saveRotationName = "Saving settings ...";
            saveDeletionName = "Saving settings ...";
            extraPrimaryProps.spinnerAriaValueText = "Loading";
        }

        // Adjust time string for TimePicket
        if (hour.length == 1) {
            hour = "0" + hour;
        }
        if (min.length == 1) {
            min = "0" + min;
        }
        rotationTime = hour + ":" + min;

        let body =
            <div className="ds-margin-top-lg ds-left-margin">
                <Tabs className="ds-margin-top-xlg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText><b>Settings</b></TabTitleText>}>
                        <Checkbox
                            className="ds-margin-top-xlg"
                            id="nsslapd-auditlog-logging-enabled"
                            isChecked={this.state['nsslapd-auditlog-logging-enabled']}
                            onChange={(checked, e) => {
                                this.handleChange(e, "settings");
                            }}
                            title="Enable audit logging (nsslapd-auditlog-logging-enabled)."
                            label="Enable Audit Logging"
                        />
                        <Form className="ds-margin-top-xlg ds-margin-left" isHorizontal>
                            <FormGroup
                                label="Audit Log Location"
                                fieldId="nsslapd-auditlog"
                                title="Enable audit logging (nsslapd-auditlog)."
                            >
                                <TextInput
                                    value={this.state['nsslapd-auditlog']}
                                    type="text"
                                    id="nsslapd-auditlog"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="nsslapd-auditlog"
                                    onChange={(str, e) => {
                                        this.handleChange(e, "settings");
                                    }}
                                />
                            </FormGroup>
                        </Form>
                        <Button
                            key="save settings"
                            isDisabled={this.state.saveSettingsDisabled}
                            variant="primary"
                            className="ds-margin-top-xlg"
                            onClick={() => {
                                this.saveConfig("settings");
                            }}
                            isLoading={this.state.loading}
                            spinnerAriaValueText={this.state.loading ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveSettingsName}
                        </Button>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText><b>Rotation Policy</b></TabTitleText>}>
                        <Form className="ds-margin-top-lg" isHorizontal>
                            <Grid
                                className="ds-margin-top"
                                title="The maximum number of logs that are archived (nsslapd-auditlog-maxlogsperdir)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Maximum Number Of Logs
                                </GridItem>
                                <GridItem span={3}>
                                    <TextInput
                                        value={this.state['nsslapd-auditlog-maxlogsperdir']}
                                        type="number"
                                        id="nsslapd-auditlog-maxlogsperdir"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="server-auditlog-maxlogsperdir"
                                        onChange={(str, e) => {
                                            this.handleChange(e, "rotation");
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="The maximum size of each log file in megabytes (nsslapd-auditlog-maxlogsize).">
                                <GridItem className="ds-label" span={3}>
                                    Maximum Log Size (in MB)
                                </GridItem>
                                <GridItem span={3}>
                                    <TextInput
                                        value={this.state['nsslapd-auditlog-maxlogsize']}
                                        type="number"
                                        id="nsslapd-auditlog-maxlogsize"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="server-auditlog-maxlogsize"
                                        onChange={(str, e) => {
                                            this.handleChange(e, "rotation");
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <hr />
                            <Grid title="Rotate the log based this number of time units (nsslapd-auditlog-logrotationtime).">
                                <GridItem className="ds-label" span={3}>
                                    Create New Log Every ...
                                </GridItem>
                                <GridItem span={1}>
                                    <TextInput
                                        value={this.state['nsslapd-auditlog-logrotationtime']}
                                        type="number"
                                        id="nsslapd-auditlog-logrotationtime"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="server-auditlog-logrotationtime"
                                        onChange={(str, e) => {
                                            this.handleChange(e, "rotation");
                                        }}
                                    />
                                </GridItem>
                                <GridItem span={2} className="ds-left-margin">
                                    <FormSelect
                                        id="nsslapd-auditlog-logrotationtimeunit"
                                        value={this.state['nsslapd-auditlog-logrotationtimeunit']}
                                        onChange={(str, e) => {
                                            this.handleChange(e, "rotation");
                                        }}
                                        aria-label="FormSelect Input"
                                    >
                                        <FormSelectOption key="0" value="minute" label="minute" />
                                        <FormSelectOption key="1" value="hour" label="hour" />
                                        <FormSelectOption key="2" value="day" label="day" />
                                        <FormSelectOption key="3" value="week" label="week" />
                                        <FormSelectOption key="4" value="month" label="month" />
                                    </FormSelect>
                                </GridItem>
                            </Grid>
                            <Grid title="The time when the log should be rotated (nsslapd-auditlog-logrotationsynchour, nsslapd-auditlog-logrotationsyncmin).">
                                <GridItem className="ds-label" span={3}>
                                    Time Of Day
                                </GridItem>
                                <GridItem span={3}>
                                    <TimePicker
                                        time={rotationTime}
                                        onChange={this.handleTimeChange}
                                        is24Hour
                                    />
                                </GridItem>
                            </Grid>
                        </Form>
                        <Button
                            key="save rot settings"
                            isDisabled={this.state.saveRotationDisabled}
                            variant="primary"
                            className="ds-margin-top-xlg"
                            onClick={() => {
                                this.saveConfig("rotation");
                            }}
                            isLoading={this.state.loading}
                            spinnerAriaValueText={this.state.loading ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveRotationName}
                        </Button>
                    </Tab>

                    <Tab eventKey={2} title={<TabTitleText><b>Deletion Policy</b></TabTitleText>}>
                        <Form className="ds-margin-top-lg" isHorizontal>
                            <Grid
                                className="ds-margin-top"
                                title="The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-auditlog-logmaxdiskspace)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Log Archive Exceeds (in MB)
                                </GridItem>
                                <GridItem span={1}>
                                    <TextInput
                                        value={this.state['nsslapd-auditlog-logmaxdiskspace']}
                                        type="number"
                                        id="nsslapd-auditlog-logmaxdiskspace"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="server-auditlog-logmaxdiskspace"
                                        onChange={(str, e) => {
                                            this.handleChange(e, "exp");
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-auditlog-logminfreediskspace)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Free Disk Space (in MB)
                                </GridItem>
                                <GridItem span={1}>
                                    <TextInput
                                        value={this.state['nsslapd-auditlog-logminfreediskspace']}
                                        type="number"
                                        id="nsslapd-auditlog-logminfreediskspace"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="server-auditlog-logminfreediskspace"
                                        onChange={(str, e) => {
                                            this.handleChange(e, "exp");
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Server deletes an old archived log file when it is older than the specified age. (nsslapd-auditlog-logexpirationtime)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Log File is Older Than ...
                                </GridItem>
                                <GridItem span={1}>
                                    <TextInput
                                        value={this.state['nsslapd-auditlog-logexpirationtime']}
                                        type="number"
                                        id="nsslapd-auditlog-logexpirationtime"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="server-auditlog-logexpirationtime"
                                        onChange={(str, e) => {
                                            this.handleChange(e, "exp");
                                        }}
                                    />
                                </GridItem>
                                <GridItem span={2} className="ds-left-margin">
                                    <FormSelect
                                        id="nsslapd-auditlog-logexpirationtimeunit"
                                        value={this.state['nsslapd-auditlog-logexpirationtimeunit']}
                                        onChange={(str, e) => {
                                            this.handleChange(e, "exp");
                                        }}
                                        aria-label="FormSelect Input"
                                    >
                                        <FormSelectOption key="2" value="day" label="day" />
                                        <FormSelectOption key="3" value="week" label="week" />
                                        <FormSelectOption key="4" value="month" label="month" />
                                    </FormSelect>
                                </GridItem>
                            </Grid>
                        </Form>
                        <Button
                            key="save del settings"
                            isDisabled={this.state.saveExpDisabled}
                            variant="primary"
                            className="ds-margin-top-xlg"
                            onClick={() => {
                                this.saveConfig("exp");
                            }}
                            isLoading={this.state.loading}
                            spinnerAriaValueText={this.state.loading ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveDeletionName}
                        </Button>
                    </Tab>
                </Tabs>
            </div>;

        if (!this.state.loaded) {
            body =
                <div className="ds-loading-spinner ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>Loading Audit Log settings ...</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="lg" />
                </div>;
        }

        return (
            <div id="server-auditlog-page" className={this.state.loading ? "ds-disabled" : ""}>
                <Grid>
                    <GridItem span={3}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Audit Log Settings <FontAwesomeIcon
                                    size="lg"
                                    className="ds-left-margin ds-refresh"
                                    icon={faSyncAlt}
                                    title="Refresh log settings"
                                    onClick={() => {
                                        this.reloadConfig(true);
                                    }}
                                />
                            </Text>
                        </TextContent>
                    </GridItem>
                </Grid>
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
    serverId: "",
    attrs: {},
};
