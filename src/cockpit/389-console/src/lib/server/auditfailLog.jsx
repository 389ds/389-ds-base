import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    NumberInput,
    Spinner,
    Switch,
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
    'nsslapd-auditfaillog',
    'nsslapd-auditfaillog-level',
    'nsslapd-auditfaillog-logbuffering',
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
    'nsslapd-auditfaillog-compress'
];

const rotation_attrs_no_time = [
    'nsslapd-auditfaillog-logrotationsync-enabled',
    'nsslapd-auditfaillog-logrotationtime',
    'nsslapd-auditfaillog-logrotationtimeunit',
    'nsslapd-auditfaillog-maxlogsize',
    'nsslapd-auditfaillog-maxlogsperdir',
    'nsslapd-auditfaillog-compress'
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
            loading: true,
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
        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.handleTimeChange = this.handleTimeChange.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.refreshConfig = this.refreshConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
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

    validateSaveBtn(nav_tab, attr, value) {
        let disableSaveBtn = true;
        let disableBtnName = "";
        let config_attrs = [];
        if (nav_tab === "settings") {
            config_attrs = settings_attrs;
            disableBtnName = "saveSettingsDisabled";
        } else if (nav_tab === "rotation") {
            disableBtnName = "saveRotationDisabled";
            config_attrs = rotation_attrs;
        } else {
            config_attrs = exp_attrs;
            disableBtnName = "saveExpDisabled";
        }

        // Check if a setting was changed, if so enable the save button
        for (const config_attr of config_attrs) {
            if (attr === config_attr && this.state['_' + config_attr].toString() !== value.toString()) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const config_attr of config_attrs) {
            if (attr !== config_attr && this.state['_' + config_attr].toString() !== this.state[config_attr].toString()) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [disableBtnName]: disableSaveBtn
        });
    }

    handleChange(e, nav_tab) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;

        this.setState({
            [attr]: value,
        }, () => { this.validateSaveBtn(nav_tab, attr, value) } );
    }

    handleSwitchChange(value) {
        // log compression
        this.setState({
            'nsslapd-auditfaillog-compress': value
        }, () => {
            this.validateSaveBtn('rotation', 'nsslapd-auditfaillog-compress', value);
        });
    }

    handleTimeChange(time_str) {
        let disableSaveBtn = true;
        const time_parts = time_str.split(":");
        let hour = time_parts[0];
        let min = time_parts[1];
        if (hour.length === 2 && hour[0] === "0") {
            hour = hour[1];
        }
        if (min.length === 2 && min[0] === "0") {
            min = min[1];
        }

        // Start doing the Save button checking
        for (const config_attr of rotation_attrs_no_time) {
            if (this.state[config_attr] !== this.state['_' + config_attr]) {
                disableSaveBtn = false;
                break;
            }
        }
        if (hour !== this.state['_nsslapd-auditfaillog-logrotationsynchour'] ||
            min !== this.state['_nsslapd-auditfaillog-logrotationsyncmin']) {
            disableSaveBtn = false;
        }

        this.setState({
            'nsslapd-auditfaillog-logrotationsynchour': hour,
            'nsslapd-auditfaillog-logrotationsyncmin': min,
            saveRotationDisabled: disableSaveBtn,
        });
    }

    saveConfig(nav_tab) {
        this.setState({
            loading: true
        });

        let config_attrs = [];
        if (nav_tab === "settings") {
            config_attrs = settings_attrs;
        } else if (nav_tab === "rotation") {
            config_attrs = rotation_attrs;
        } else {
            config_attrs = exp_attrs;
        }

        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'config', 'replace'
        ];

        for (const attr of config_attrs) {
            if (this.state['_' + attr] !== this.state[attr]) {
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

        if (cmd.length === 5) {
            // Nothing to save, just return
            return;
        }

        log_cmd("saveConfig", "Saving audit fail log settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.refreshConfig();
                    this.props.addNotification(
                        "success",
                        "Successfully updated Audit Fail Log settings"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.refreshConfig();
                    this.props.addNotification(
                        "error",
                        `Error saving Audit Fail Log settings - ${errMsg.desc}`
                    );
                });
    }

    refreshConfig() {
        this.setState({
            loading: true,
            loaded: false,
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("refreshConfig", "load Audit Fail Log configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    let enabled = false;
                    let compressed = false;

                    if (attrs['nsslapd-auditfaillog-logging-enabled'][0] === "on") {
                        enabled = true;
                    }
                    if (attrs['nsslapd-auditfaillog-compress'][0] === "on") {
                        compressed = true;
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
                            'nsslapd-auditfaillog-compress': compressed,
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
                            '_nsslapd-auditfaillog-compress': compressed,
                        })
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading Audit Fail Log configuration - ${errMsg.desc}`
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
        let compressed = false;

        if (attrs['nsslapd-auditfaillog-logging-enabled'][0] === "on") {
            enabled = true;
        }
        if (attrs['nsslapd-auditfaillog-compress'][0] === "on") {
            compressed = true;
        }

        this.setState({
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
            'nsslapd-auditfaillog-compress': compressed,
            // Record original values,
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
            '_nsslapd-auditfaillog-compress': compressed,
        }, this.props.enableTree);
    }

    render() {
        let saveSettingsName = "Save Log Settings";
        let saveRotationName = "Save Rotation Settings";
        let saveDeletionName = "Save Deletion Settings";
        const extraPrimaryProps = {};
        let rotationTime = "";
        let hour = this.state['nsslapd-auditfaillog-logrotationsynchour'] ? this.state['nsslapd-auditfaillog-logrotationsynchour'] : "00";
        let min = this.state['nsslapd-auditfaillog-logrotationsyncmin'] ? this.state['nsslapd-auditfaillog-logrotationsyncmin'] : "00";

        if (this.state.loading) {
            saveSettingsName = "Saving settings ...";
            saveRotationName = "Saving settings ...";
            saveDeletionName = "Saving settings ...";
            extraPrimaryProps.spinnerAriaValueText = "Loading";
        }

        // Adjust time string for TimePicket
        if (hour.length === 1) {
            hour = "0" + hour;
        }
        if (min.length === 1) {
            min = "0" + min;
        }
        rotationTime = hour + ":" + min;

        let body =
            <div className="ds-margin-top-lg ds-left-margin">
                <Tabs className="ds-margin-top-xlg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Settings</TabTitleText>}>
                        <Checkbox
                            className="ds-margin-top-xlg"
                            id="nsslapd-auditfaillog-logging-enabled"
                            isChecked={this.state['nsslapd-auditfaillog-logging-enabled']}
                            onChange={(checked, e) => {
                                this.handleChange(e, "settings");
                            }}
                            title="Enable audit fail logging (nsslapd-auditfaillog-logging-enabled)."
                            label="Enable Audit Fail Logging"
                        />
                        <Grid className="ds-margin-top-xlg ds-margin-left" title="Enable audit fail logging (nsslapd-auditfaillog).">
                            <GridItem className="ds-label" span={2}>
                                Audit Fail Log Location
                            </GridItem>
                            <GridItem span={10}>
                                <TextInput
                                    value={this.state['nsslapd-auditfaillog']}
                                    type="text"
                                    id="nsslapd-auditfaillog"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="nsslapd-auditfaillog"
                                    onChange={(str, e) => {
                                        this.handleChange(e, "settings");
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Button
                            key="save settings"
                            isDisabled={this.state.saveSettingsDisabled || this.state.loading}
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
                    <Tab eventKey={1} title={<TabTitleText>Rotation Policy</TabTitleText>}>
                        <Form className="ds-margin-top-lg" isHorizontal autoComplete="off">
                            <Grid
                                className="ds-margin-top"
                                title="The maximum number of logs that are archived (nsslapd-auditfaillog-maxlogsperdir)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Maximum Number Of Logs
                                </GridItem>
                                <GridItem span={3}>
                                    <NumberInput
                                        value={this.state['nsslapd-auditfaillog-maxlogsperdir']}
                                        min={1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-auditfaillog-maxlogsperdir", "rotation") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-auditfaillog-maxlogsperdir", 1, 2147483647, "rotation") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-auditfaillog-maxlogsperdir", "rotation") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="The maximum size of each log file in megabytes (nsslapd-auditfaillog-maxlogsize).">
                                <GridItem className="ds-label" span={3}>
                                    Maximum Log Size (in MB)
                                </GridItem>
                                <GridItem span={3}>
                                    <NumberInput
                                        value={this.state['nsslapd-auditfaillog-maxlogsize']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-auditfaillog-maxlogsize", "rotation") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-auditfaillog-maxlogsize", -1, 2147483647, "rotation") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-auditfaillog-maxlogsize", "rotation") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <hr />
                            <Grid title="Rotate the log based this number of time units (nsslapd-auditfaillog-logrotationtime).">
                                <GridItem className="ds-label" span={3}>
                                    Create New Log Every ...
                                </GridItem>
                                <GridItem span={9}>
                                    <div className="ds-container">
                                        <NumberInput
                                            value={this.state['nsslapd-auditfaillog-logrotationtime']}
                                            min={-1}
                                            max={2147483647}
                                            onMinus={() => { this.onMinusConfig("nsslapd-auditfaillog-logrotationtime", "rotation") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsslapd-auditfaillog-logrotationtime", -1, 2147483647, "rotation") }}
                                            onPlus={() => { this.onPlusConfig("nsslapd-auditfaillog-logrotationtime", "rotation") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={3}
                                        />
                                        <GridItem span={2} className="ds-left-indent">
                                            <FormSelect
                                                id="nsslapd-auditfaillog-logrotationtimeunit"
                                                value={this.state['nsslapd-auditfaillog-logrotationtimeunit']}
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
                                    </div>
                                </GridItem>
                            </Grid>
                            <Grid title="The time when the log should be rotated (nsslapd-auditfaillog-logrotationsynchour, nsslapd-auditfaillog-logrotationsyncmin).">
                                <GridItem className="ds-label" span={3}>
                                    Time Of Day
                                </GridItem>
                                <GridItem span={1}>
                                    <TimePicker
                                        time={rotationTime}
                                        onChange={this.handleTimeChange}
                                        is24Hour
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="Compress (gzip) the log after it's rotated.">
                                <GridItem className="ds-label" span={3}>
                                    Compress Rotated Logs
                                </GridItem>
                                <GridItem span={8}>
                                    <Switch
                                        id="nsslapd-auditfaillog-compress"
                                        isChecked={this.state['nsslapd-auditfaillog-compress']}
                                        onChange={this.handleSwitchChange}
                                        aria-label="nsslapd-auditfaillog-compress"
                                    />
                                </GridItem>
                            </Grid>
                        </Form>
                        <Button
                            key="save rot settings"
                            isDisabled={this.state.saveRotationDisabled || this.state.loading}
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

                    <Tab eventKey={2} title={<TabTitleText>Deletion Policy</TabTitleText>}>
                        <Form className="ds-margin-top-lg" isHorizontal autoComplete="off">
                            <Grid
                                className="ds-margin-top"
                                title="The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-auditfaillog-logmaxdiskspace)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Log Archive Exceeds (in MB)
                                </GridItem>
                                <GridItem span={1}>
                                    <NumberInput
                                        value={this.state['nsslapd-auditfaillog-logmaxdiskspace']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-auditfaillog-logmaxdiskspace", "exp") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-auditfaillog-logmaxdiskspace", -1, 2147483647, "exp") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-auditfaillog-logmaxdiskspace", "exp") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-auditfaillog-logminfreediskspace)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Free Disk Space (in MB)
                                </GridItem>
                                <GridItem span={1}>
                                    <NumberInput
                                        value={this.state['nsslapd-auditfaillog-logminfreediskspace']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-auditfaillog-logminfreediskspace", "exp") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-auditfaillog-logminfreediskspace", -1, 2147483647, "exp") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-auditfaillog-logminfreediskspace", "exp") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Server deletes an old archived log file when it is older than the specified age. (nsslapd-auditfaillog-logexpirationtime)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Log File is Older Than ...
                                </GridItem>
                                <GridItem span={9}>
                                    <div className="ds-container">
                                        <NumberInput
                                            value={this.state['nsslapd-auditfaillog-logexpirationtime']}
                                            min={-1}
                                            max={2147483647}
                                            onMinus={() => { this.onMinusConfig("nsslapd-auditfaillog-logexpirationtime", "exp") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsslapd-auditfaillog-logexpirationtime", -1, 2147483647, "exp") }}
                                            onPlus={() => { this.onPlusConfig("nsslapd-auditfaillog-logexpirationtime", "exp") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={3}
                                        />
                                        <GridItem span={2} className="ds-left-indent">
                                            <FormSelect
                                                id="nsslapd-auditfaillog-logexpirationtimeunit"
                                                value={this.state['nsslapd-auditfaillog-logexpirationtimeunit']}
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
                                    </div>
                                </GridItem>
                            </Grid>
                        </Form>
                        <Button
                            key="save del settings"
                            isDisabled={this.state.saveExpDisabled || this.state.loading}
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
                        <Text component={TextVariants.h3}>Loading Audit Fail Log Settings ...</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="lg" />
                </div>;
        }

        return (
            <div id="server-auditfaillog-page" className={this.state.loading ? "ds-disabled" : ""}>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Audit Fail Log Settings <FontAwesomeIcon
                                    size="lg"
                                    className="ds-left-margin ds-refresh"
                                    icon={faSyncAlt}
                                    title="Refresh log settings"
                                    onClick={() => {
                                        this.refreshConfig();
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

ServerAuditFailLog.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerAuditFailLog.defaultProps = {
    serverId: "",
    attrs: {},
};
