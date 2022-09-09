import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    ExpandableSection,
    Form,
    FormGroup,
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
import {
    Table,
    TableHeader,
    TableBody,
    TableVariant
} from '@patternfly/react-table';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';
import PropTypes from "prop-types";

const settings_attrs = [
    'nsslapd-securitylog',
    'nsslapd-securitylog-level',
    'nsslapd-securitylog-logbuffering',
    'nsslapd-securitylog-logging-enabled',
];

const rotation_attrs = [
    'nsslapd-securitylog-logrotationsync-enabled',
    'nsslapd-securitylog-logrotationsynchour',
    'nsslapd-securitylog-logrotationsyncmin',
    'nsslapd-securitylog-logrotationtime',
    'nsslapd-securitylog-logrotationtimeunit',
    'nsslapd-securitylog-maxlogsize',
    'nsslapd-securitylog-maxlogsperdir',
    'nsslapd-securitylog-compress'
];

const rotation_attrs_no_time = [
    'nsslapd-securitylog-logrotationsync-enabled',
    'nsslapd-securitylog-logrotationtime',
    'nsslapd-securitylog-logrotationtimeunit',
    'nsslapd-securitylog-maxlogsize',
    'nsslapd-securitylog-maxlogsperdir',
    'nsslapd-securitylog-compress'
];

const exp_attrs = [
    'nsslapd-securitylog-logexpirationtime',
    'nsslapd-securitylog-logexpirationtimeunit',
    'nsslapd-securitylog-logmaxdiskspace',
    'nsslapd-securitylog-logminfreediskspace',
];

export class ServerSecurityLog extends React.Component {
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
            canSelectAll: false,
            isExpanded: false,
            rows: [
                { cells: ['Default Logging'], level: 256, selected: true },
                { cells: ['Internal Operations'], level: 4, selected: false },
                { cells: ['Entry Security and Referrals'], level: 512, selected: false }
            ],
            columns: [
                { title: 'Logging Level' },
            ],
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.onToggle = isExpanded => {
            this.setState({
                isExpanded
            });
        };

        this.handleChange = this.handleChange.bind(this);
        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.handleTimeChange = this.handleTimeChange.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.refreshConfig = this.refreshConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
        this.onSelect = this.onSelect.bind(this);
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
        if (nav_tab == "settings") {
            config_attrs = settings_attrs;
            disableBtnName = "saveSettingsDisabled";
            // Handle the table contents check now
            for (const row of this.state.rows) {
                for (const orig_row of this.state._rows) {
                    if (orig_row.cells[0] == row.cells[0]) {
                        if (orig_row.selected != row.selected) {
                            disableSaveBtn = false;
                            break;
                        }
                    }
                }
            }
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
            'nsslapd-securitylog-compress': value
        }, () => {
            this.validateSaveBtn('rotation', 'nsslapd-securitylog-compress', value);
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
        if (hour != this.state['_nsslapd-securitylog-logrotationsynchour'] ||
            min != this.state['_nsslapd-securitylog-logrotationsyncmin']) {
            disableSaveBtn = false;
        }

        this.setState({
            'nsslapd-securitylog-logrotationsynchour': hour,
            'nsslapd-securitylog-logrotationsyncmin': min,
            saveRotationDisabled: disableSaveBtn,
        });
    }

    saveConfig(nav_tab) {
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

        for (const row of this.state.rows) {
            if (row.selected) {
                new_level += row.level;
            }
        }
        if (new_level.toString() != this.state['_nsslapd-securitylog-level']) {
            if (new_level == 0) {
                new_level = 256; // default
            }
            cmd.push("nsslapd-securitylog-level" + "=" + new_level.toString());
        }

        if (cmd.length == 5) {
            // Nothing to save, just return
            return;
        }

        log_cmd("saveConfig", "Saving security log settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.refreshConfig();
                    this.props.addNotification(
                        "success",
                        "Successfully updated Security Log settings"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.refreshConfig();
                    this.props.addNotification(
                        "error",
                        `Error saving Security Log settings - ${errMsg.desc}`
                    );
                });
    }

    refreshConfig(refesh) {
        this.setState({
            loading: true,
            loaded: false,
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("refreshConfig", "load Security Log configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    let enabled = false;
                    let buffering = false;
                    let compress = false;
                    const level_val = parseInt(attrs['nsslapd-securitylog-level'][0]);
                    const rows = [...this.state.rows];

                    if (attrs['nsslapd-securitylog-logging-enabled'][0] == "on") {
                        enabled = true;
                    }
                    if (attrs['nsslapd-securitylog-logbuffering'][0] == "on") {
                        buffering = true;
                    }
                    if (attrs['nsslapd-securitylog-compress'][0] == "on") {
                        compress = true;
                    }
                    for (const row in rows) {
                        if (rows[row].level & level_val) {
                            rows[row].selected = true;
                        } else {
                            rows[row].selected = false;
                        }
                    }

                    this.setState({
                        loading: false,
                        loaded: true,
                        saveSettingsDisabled: true,
                        saveRotationDisabled: true,
                        saveExpDisabled: true,
                        'nsslapd-securitylog': attrs['nsslapd-securitylog'][0],
                        'nsslapd-securitylog-level': attrs['nsslapd-securitylog-level'][0],
                        'nsslapd-securitylog-logbuffering': buffering,
                        'nsslapd-securitylog-logexpirationtime': attrs['nsslapd-securitylog-logexpirationtime'][0],
                        'nsslapd-securitylog-logexpirationtimeunit': attrs['nsslapd-securitylog-logexpirationtimeunit'][0],
                        'nsslapd-securitylog-logging-enabled': enabled,
                        'nsslapd-securitylog-logmaxdiskspace': attrs['nsslapd-securitylog-logmaxdiskspace'][0],
                        'nsslapd-securitylog-logminfreediskspace': attrs['nsslapd-securitylog-logminfreediskspace'][0],
                        'nsslapd-securitylog-logrotationsync-enabled': attrs['nsslapd-securitylog-logrotationsync-enabled'][0],
                        'nsslapd-securitylog-logrotationsynchour': attrs['nsslapd-securitylog-logrotationsynchour'][0],
                        'nsslapd-securitylog-logrotationsyncmin': attrs['nsslapd-securitylog-logrotationsyncmin'][0],
                        'nsslapd-securitylog-logrotationtime': attrs['nsslapd-securitylog-logrotationtime'][0],
                        'nsslapd-securitylog-logrotationtimeunit': attrs['nsslapd-securitylog-logrotationtimeunit'][0],
                        'nsslapd-securitylog-maxlogsize': attrs['nsslapd-securitylog-maxlogsize'][0],
                        'nsslapd-securitylog-maxlogsperdir': attrs['nsslapd-securitylog-maxlogsperdir'][0],
                        'nsslapd-securitylog-compress': compress,
                        rows: rows,
                        // Record original values
                        _rows:  JSON.parse(JSON.stringify(rows)),
                        '_nsslapd-securitylog': attrs['nsslapd-securitylog'][0],
                        '_nsslapd-securitylog-level': attrs['nsslapd-securitylog-level'][0],
                        '_nsslapd-securitylog-logbuffering': buffering,
                        '_nsslapd-securitylog-logexpirationtime': attrs['nsslapd-securitylog-logexpirationtime'][0],
                        '_nsslapd-securitylog-logexpirationtimeunit': attrs['nsslapd-securitylog-logexpirationtimeunit'][0],
                        '_nsslapd-securitylog-logging-enabled': enabled,
                        '_nsslapd-securitylog-logmaxdiskspace': attrs['nsslapd-securitylog-logmaxdiskspace'][0],
                        '_nsslapd-securitylog-logminfreediskspace': attrs['nsslapd-securitylog-logminfreediskspace'][0],
                        '_nsslapd-securitylog-logrotationsync-enabled': attrs['nsslapd-securitylog-logrotationsync-enabled'][0],
                        '_nsslapd-securitylog-logrotationsynchour': attrs['nsslapd-securitylog-logrotationsynchour'][0],
                        '_nsslapd-securitylog-logrotationsyncmin': attrs['nsslapd-securitylog-logrotationsyncmin'][0],
                        '_nsslapd-securitylog-logrotationtime': attrs['nsslapd-securitylog-logrotationtime'][0],
                        '_nsslapd-securitylog-logrotationtimeunit': attrs['nsslapd-securitylog-logrotationtimeunit'][0],
                        '_nsslapd-securitylog-maxlogsize': attrs['nsslapd-securitylog-maxlogsize'][0],
                        '_nsslapd-securitylog-maxlogsperdir': attrs['nsslapd-securitylog-maxlogsperdir'][0],
                        '_nsslapd-securitylog-compress': compress,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error loading Security Log configuration - ${errMsg.desc}`
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
        let buffering = false;
        let compress = false;
        const level_val = parseInt(attrs['nsslapd-securitylog-level'][0]);
        const rows = [...this.state.rows];

        if (attrs['nsslapd-securitylog-logging-enabled'][0] == "on") {
            enabled = true;
        }
        if (attrs['nsslapd-securitylog-logbuffering'][0] == "on") {
            buffering = true;
        }
        if (attrs['nsslapd-securitylog-compress'][0] == "on") {
            compress = true;
        }
        for (const row in rows) {
            if (rows[row].level & level_val) {
                rows[row].selected = true;
            } else {
                rows[row].selected = false;
            }
        }

        this.setState({
            loading: false,
            loaded: true,
            saveSettingsDisabled: true,
            saveRotationDisabled: true,
            saveExpDisabled: true,
            'nsslapd-securitylog': attrs['nsslapd-securitylog'][0],
            'nsslapd-securitylog-level': attrs['nsslapd-securitylog-level'][0],
            'nsslapd-securitylog-logbuffering': buffering,
            'nsslapd-securitylog-logexpirationtime': attrs['nsslapd-securitylog-logexpirationtime'][0],
            'nsslapd-securitylog-logexpirationtimeunit': attrs['nsslapd-securitylog-logexpirationtimeunit'][0],
            'nsslapd-securitylog-logging-enabled': enabled,
            'nsslapd-securitylog-logmaxdiskspace': attrs['nsslapd-securitylog-logmaxdiskspace'][0],
            'nsslapd-securitylog-logminfreediskspace': attrs['nsslapd-securitylog-logminfreediskspace'][0],
            'nsslapd-securitylog-logrotationsync-enabled': attrs['nsslapd-securitylog-logrotationsync-enabled'][0],
            'nsslapd-securitylog-logrotationsynchour': attrs['nsslapd-securitylog-logrotationsynchour'][0],
            'nsslapd-securitylog-logrotationsyncmin': attrs['nsslapd-securitylog-logrotationsyncmin'][0],
            'nsslapd-securitylog-logrotationtime': attrs['nsslapd-securitylog-logrotationtime'][0],
            'nsslapd-securitylog-logrotationtimeunit': attrs['nsslapd-securitylog-logrotationtimeunit'][0],
            'nsslapd-securitylog-maxlogsize': attrs['nsslapd-securitylog-maxlogsize'][0],
            'nsslapd-securitylog-maxlogsperdir': attrs['nsslapd-securitylog-maxlogsperdir'][0],
            'nsslapd-securitylog-compress': compress,
            rows: rows,
            // Record original values
            _rows: JSON.parse(JSON.stringify(rows)),
            '_nsslapd-securitylog': attrs['nsslapd-securitylog'][0],
            '_nsslapd-securitylog-level': attrs['nsslapd-securitylog-level'][0],
            '_nsslapd-securitylog-logbuffering': buffering,
            '_nsslapd-securitylog-logexpirationtime': attrs['nsslapd-securitylog-logexpirationtime'][0],
            '_nsslapd-securitylog-logexpirationtimeunit': attrs['nsslapd-securitylog-logexpirationtimeunit'][0],
            '_nsslapd-securitylog-logging-enabled': enabled,
            '_nsslapd-securitylog-logmaxdiskspace': attrs['nsslapd-securitylog-logmaxdiskspace'][0],
            '_nsslapd-securitylog-logminfreediskspace': attrs['nsslapd-securitylog-logminfreediskspace'][0],
            '_nsslapd-securitylog-logrotationsync-enabled': attrs['nsslapd-securitylog-logrotationsync-enabled'][0],
            '_nsslapd-securitylog-logrotationsynchour': attrs['nsslapd-securitylog-logrotationsynchour'][0],
            '_nsslapd-securitylog-logrotationsyncmin': attrs['nsslapd-securitylog-logrotationsyncmin'][0],
            '_nsslapd-securitylog-logrotationtime': attrs['nsslapd-securitylog-logrotationtime'][0],
            '_nsslapd-securitylog-logrotationtimeunit': attrs['nsslapd-securitylog-logrotationtimeunit'][0],
            '_nsslapd-securitylog-maxlogsize': attrs['nsslapd-securitylog-maxlogsize'][0],
            '_nsslapd-securitylog-maxlogsperdir': attrs['nsslapd-securitylog-maxlogsperdir'][0],
            '_nsslapd-securitylog-compress': compress,
        }, this.props.enableTree);
    }

    onSelect(event, isSelected, rowId) {
        let disableSaveBtn = true;
        const rows = JSON.parse(JSON.stringify(this.state.rows));

        // Update the row
        rows[rowId].selected = isSelected;

        // Handle "save button" state, first check the other config settings
        for (const config_attr of settings_attrs) {
            if (this.state['_' + config_attr] != this.state[config_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        // Handle the table contents
        for (const row of rows) {
            for (const orig_row of this.state._rows) {
                if (orig_row.cells[0] == row.cells[0]) {
                    if (orig_row.selected != row.selected) {
                        disableSaveBtn = false;
                        break;
                    }
                }
            }
        }

        this.setState({
            rows,
            saveSettingsDisabled: disableSaveBtn,
        });
    }

    render() {
        let saveSettingsName = "Save Log Settings";
        let saveRotationName = "Save Rotation Settings";
        let saveDeletionName = "Save Deletion Settings";
        const extraPrimaryProps = {};
        let rotationTime = "";
        let hour = this.state['nsslapd-securitylog-logrotationsynchour'] ? this.state['nsslapd-securitylog-logrotationsynchour'] : "00";
        let min = this.state['nsslapd-securitylog-logrotationsyncmin'] ? this.state['nsslapd-securitylog-logrotationsyncmin'] : "00";

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
                    <Tab eventKey={0} title={<TabTitleText>Settings</TabTitleText>}>
                        <Checkbox
                            className="ds-margin-top-xlg"
                            id="nsslapd-securitylog-logging-enabled"
                            isChecked={this.state['nsslapd-securitylog-logging-enabled']}
                            onChange={(checked, e) => {
                                this.handleChange(e, "settings");
                            }}
                            title="Enable security logging (nsslapd-securitylog-logging-enabled)."
                            label="Enable Security Logging"
                        />
                        <Form className="ds-margin-top-lg ds-left-margin-md" isHorizontal autoComplete="off">
                            <FormGroup
                                label="Security Log Location"
                                fieldId="nsslapd-securitylog"
                                title="Enable security logging (nsslapd-securitylog)."
                            >
                                <TextInput
                                    value={this.state['nsslapd-securitylog']}
                                    type="text"
                                    id="nsslapd-securitylog"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="nsslapd-securitylog"
                                    onChange={(str, e) => {
                                        this.handleChange(e, "settings");
                                    }}
                                />
                            </FormGroup>
                        </Form>
                        <Checkbox
                            className="ds-left-margin-md ds-margin-top-lg"
                            id="nsslapd-securitylog-logbuffering"
                            isChecked={this.state['nsslapd-securitylog-logbuffering']}
                            onChange={(checked, e) => {
                                this.handleChange(e, "settings");
                            }}
                            title="Disable security log buffering for faster troubleshooting, but this will impact server performance (nsslapd-securitylog-logbuffering)."
                            label="Security Log Buffering Enabled"
                        />


                        <ExpandableSection
                            className="ds-hidden ds-left-margin-md ds-margin-top-lg ds-font-size-md"
                            toggleText={this.state.isExpanded ? 'Hide Logging Levels' : 'Show Logging Levels'}
                            onToggle={this.onToggle}
                            isExpanded={this.state.isExpanded}
                        >
                            <Table
                                className="ds-left-margin"
                                onSelect={this.onSelect}
                                canSelectAll={this.state.canSelectAll}
                                variant={TableVariant.compact}
                                aria-label="Selectable Table"
                                cells={this.state.columns}
                                rows={this.state.rows}
                            >
                                <TableHeader />
                                <TableBody />
                            </Table>
                        </ExpandableSection>

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
                                title="The maximum number of logs that are archived (nsslapd-securitylog-maxlogsperdir)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Maximum Number Of Logs
                                </GridItem>
                                <GridItem span={3}>
                                    <NumberInput
                                        value={this.state['nsslapd-securitylog-maxlogsperdir']}
                                        min={1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-securitylog-maxlogsperdir", "rotation") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-securitylog-maxlogsperdir", 1, 2147483647, "rotation") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-securitylog-maxlogsperdir", "rotation") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="The maximum size of each log file in megabytes (nsslapd-securitylog-maxlogsize).">
                                <GridItem className="ds-label" span={3}>
                                    Maximum Log Size (in MB)
                                </GridItem>
                                <GridItem span={3}>
                                    <NumberInput
                                        value={this.state['nsslapd-securitylog-maxlogsize']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-securitylog-maxlogsize", "rotation") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-securitylog-maxlogsize", -1, 2147483647, "rotation") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-securitylog-maxlogsize", "rotation") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <hr />
                            <Grid title="Rotate the log based this number of time units (nsslapd-securitylog-logrotationtime).">
                                <GridItem className="ds-label" span={3}>
                                    Create New Log Every ...
                                </GridItem>
                                <GridItem span={9}>
                                    <div className="ds-container">
                                        <NumberInput
                                            value={this.state['nsslapd-securitylog-logrotationtime']}
                                            min={-1}
                                            max={2147483647}
                                            onMinus={() => { this.onMinusConfig("nsslapd-securitylog-logrotationtime", "rotation") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsslapd-securitylog-logrotationtime", -1, 2147483647, "rotation") }}
                                            onPlus={() => { this.onPlusConfig("nsslapd-securitylog-logrotationtime", "rotation") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={3}
                                        />
                                        <GridItem span={2} className="ds-left-indent">
                                            <FormSelect
                                                id="nsslapd-securitylog-logrotationtimeunit"
                                                value={this.state['nsslapd-securitylog-logrotationtimeunit']}
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
                            <Grid title="The time when the log should be rotated (nsslapd-securitylog-logrotationsynchour, nsslapd-securitylog-logrotationsyncmin).">
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
                                <GridItem className="ds-label" span={8}>
                                    <Switch
                                        id="nsslapd-securitylog-compress"
                                        isChecked={this.state['nsslapd-securitylog-compress']}
                                        onChange={this.handleSwitchChange}
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
                                title="The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-securitylog-logmaxdiskspace)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Log Archive Exceeds (in MB)
                                </GridItem>
                                <GridItem span={1}>
                                    <NumberInput
                                        value={this.state['nsslapd-securitylog-logmaxdiskspace']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-securitylog-logmaxdiskspace", "exp") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-securitylog-logmaxdiskspace", -1, 2147483647, "exp") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-securitylog-logmaxdiskspace", "exp") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-securitylog-logminfreediskspace)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Free Disk Space (in MB)
                                </GridItem>
                                <GridItem span={1}>
                                    <NumberInput
                                        value={this.state['nsslapd-securitylog-logminfreediskspace']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-securitylog-logminfreediskspace", "exp") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-securitylog-logminfreediskspace", -1, 2147483647, "exp") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-securitylog-logminfreediskspace", "exp") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Server deletes an old archived log file when it is older than the specified age. (nsslapd-securitylog-logexpirationtime)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Log File is Older Than ...
                                </GridItem>
                                <GridItem span={9}>
                                    <div className="ds-container">
                                        <NumberInput
                                            value={this.state['nsslapd-securitylog-logexpirationtime']}
                                            min={-1}
                                            max={2147483647}
                                            onMinus={() => { this.onMinusConfig("nsslapd-securitylog-logexpirationtime", "exp") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsslapd-securitylog-logexpirationtime", -1, 2147483647, "exp") }}
                                            onPlus={() => { this.onPlusConfig("nsslapd-securitylog-logexpirationtime", "exp") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={3}
                                        />
                                        <GridItem span={2} className="ds-left-indent">
                                            <FormSelect
                                                id="nsslapd-securitylog-logexpirationtimeunit"
                                                value={this.state['nsslapd-securitylog-logexpirationtimeunit']}
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
                <div className="ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>Loading Security Log Settings ...</Text>
                    </TextContent>
                    <Spinner size="xl" />
                </div>;
        }

        return (
            <div id="server-securitylog-page" className={this.state.loading ? "ds-disabled" : ""}>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Security Log Settings <FontAwesomeIcon
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

ServerSecurityLog.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerSecurityLog.defaultProps = {
    serverId: "",
    attrs: {},
};
