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
    'nsslapd-errorlog',
    'nsslapd-errorlog-level',
    'nsslapd-errorlog-logging-enabled',
];

const rotation_attrs = [
    'nsslapd-errorlog-logrotationsync-enabled',
    'nsslapd-errorlog-logrotationsynchour',
    'nsslapd-errorlog-logrotationsyncmin',
    'nsslapd-errorlog-logrotationtime',
    'nsslapd-errorlog-logrotationtimeunit',
    'nsslapd-errorlog-maxlogsize',
    'nsslapd-errorlog-maxlogsperdir',
    'nsslapd-errorlog-compress'
];

const rotation_attrs_no_time = [
    'nsslapd-errorlog-logrotationsync-enabled',
    'nsslapd-errorlog-logrotationtime',
    'nsslapd-errorlog-logrotationtimeunit',
    'nsslapd-errorlog-maxlogsize',
    'nsslapd-errorlog-maxlogsperdir',
    'nsslapd-errorlog-compress'
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

        this.traceLevel = <>Trace Function Calls <font size="1" className="ds-info-color">(level 1)</font></>;
        this.packetLevel = <>Packet Handling <font size="1" className="ds-info-color">(level 2)</font></>;
        this.heavyLevel = <>Heavy Trace Output <font size="1" className="ds-info-color">(level 4)</font></>;
        this.connLevel = <>Connection Management <font size="1" className="ds-info-color">(level 8)</font></>;
        this.packetSentLevel = <>Packets Sent & Received <font size="1" className="ds-info-color">(level 16)</font></>;
        this.filterLevel = <>Search Filter Processing <font size="1" className="ds-info-color">(level 32)</font></>;
        this.configLevel = <>Config File Processing <font size="1" className="ds-info-color">(level 64)</font></>;
        this.aclLevel = <>Access Control List Processing <font size="1" className="ds-info-color">(level 128)</font></>;
        this.entryLevel = <>Log Entry Parsing <font size="1" className="ds-info-color">(level 2048)</font></>;
        this.houseLevel = <>Housekeeping <font size="1" className="ds-info-color">(level 4096)</font></>;
        this.replLevel = <>Replication <font size="1" className="ds-info-color">(level 8192)</font></>;
        this.cacheLevel = <>Entry Cache <font size="1" className="ds-info-color">(level 32768)</font></>;
        this.pluginLevel = <>Plugin <font size="1" className="ds-info-color">(level 65536)</font></>;
        this.aclSummaryevel = <>Access Control Summary <font size="1" className="ds-info-color">(level 262144)</font></>;
        this.dbLevel = <>Backend Database <font size="1" className="ds-info-color">(level 524288)</font></>;
        this.pwdpolicyLevel = <>Password Policy <font size="1" className="ds-info-color">(level 1048576)</font></>;

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
                { cells: [{ title: this.traceLevel }], level: 1, selected: false },
                { cells: [{ title: this.packetLevel }], level: 2, selected: false },
                { cells: [{ title: this.heavyLevel }], level: 4, selected: false },
                { cells: [{ title: this.connLevel }], level: 8, selected: false },
                { cells: [{ title: this.packetSentLevel }], level: 16, selected: false },
                { cells: [{ title: this.filterLevel }], level: 32, selected: false },
                { cells: [{ title: this.configLevel }], level: 64, selected: false },
                { cells: [{ title: this.aclLevel }], level: 128, selected: false },
                { cells: [{ title: this.entryLevel }], level: 2048, selected: false },
                { cells: [{ title: this.houseLevel }], level: 4096, selected: false },
                { cells: [{ title: this.replLevel }], level: 8192, selected: false },
                { cells: [{ title: this.cacheLevel }], level: 32768, selected: false },
                { cells: [{ title: this.pluginLevel }], level: 65536, selected: false },
                { cells: [{ title: this.aclSummaryevel }], level: 262144, selected: false },
                { cells: [{ title: this.dbLevel }], level: 524288, selected: false },
                { cells: [{ title: this.pwdpolicyLevel }], level: 1048576, selected: false },
            ],
            columns: [
                { title: 'Logging Level' },
            ],
        };

        this.onToggle = isExpanded => {
            this.setState({
                isExpanded
            });
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
        if (nav_tab === "settings") {
            config_attrs = settings_attrs;
            disableBtnName = "saveSettingsDisabled";
            // Handle the table contents check now
            for (const row of this.state.rows) {
                for (const orig_row of this.state._rows) {
                    if (orig_row.level === row.level) {
                        if (orig_row.selected !== row.selected) {
                            disableSaveBtn = false;
                            break;
                        }
                    }
                }
            }
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
            'nsslapd-errorlog-compress': value
        }, () => {
            this.validateSaveBtn('rotation', 'nsslapd-errorlog-compress', value);
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
        if (hour !== this.state['_nsslapd-errorlog-logrotationsynchour'] ||
            min !== this.state['_nsslapd-errorlog-logrotationsyncmin']) {
            disableSaveBtn = false;
        }

        this.setState({
            'nsslapd-errorlog-logrotationsynchour': hour,
            'nsslapd-errorlog-logrotationsyncmin': min,
            saveRotationDisabled: disableSaveBtn,
        });
    }

    saveConfig(nav_tab) {
        let new_level = 0;
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

        for (const row of this.state.rows) {
            if (row.selected) {
                new_level += row.level;
            }
        }
        if (new_level.toString() !== this.state['_nsslapd-errorlog-level']) {
            if (new_level === 0) {
                new_level = 16384; // default
            }
            cmd.push("nsslapd-errorlog-level" + "=" + new_level.toString());
        }

        if (cmd.length === 5) {
            // Nothing to save, just return
            return;
        }

        log_cmd("saveConfig", "Saving Error log settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.refreshConfig();
                    this.props.addNotification(
                        "success",
                        "Successfully updated Error Log settings"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.refreshConfig();
                    this.props.addNotification(
                        "error",
                        `Error saving Error Log settings - ${errMsg.desc}`
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
        log_cmd("refreshConfig", "load Error Log configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    let enabled = false;
                    let compressed = false;
                    const level_val = parseInt(attrs['nsslapd-errorlog-level'][0]);
                    const rows = [...this.state.rows];

                    if (attrs['nsslapd-errorlog-logging-enabled'][0] === "on") {
                        enabled = true;
                    }
                    if (attrs['nsslapd-errorlog-compress'][0] === "on") {
                        compressed = true;
                    }

                    for (const row in rows) {
                        if (rows[row].level & level_val) {
                            rows[row].selected = true;
                        } else {
                            rows[row].selected = false;
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
                            'nsslapd-errorlog-compress': compressed,
                            rows: rows,
                            // Record original values
                            _rows:  JSON.parse(JSON.stringify(rows)),
                            '_nsslapd-errorlog': attrs['nsslapd-errorlog'][0],
                            '_nsslapd-errorlog-level': attrs['nsslapd-errorlog-level'][0],
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
                            '_nsslapd-errorlog-compress': compressed,
                        })
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
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

    loadConfig() {
        const attrs = this.state.attrs;
        let enabled = false;
        let compressed = false;
        const level_val = parseInt(attrs['nsslapd-errorlog-level'][0]);
        const rows = [...this.state.rows];

        if (attrs['nsslapd-errorlog-logging-enabled'][0] === "on") {
            enabled = true;
        }
        if (attrs['nsslapd-errorlog-compress'][0] === "on") {
            compressed = true;
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
            'nsslapd-errorlog': attrs['nsslapd-errorlog'][0],
            'nsslapd-errorlog-level': attrs['nsslapd-errorlog-level'][0],
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
            'nsslapd-errorlog-compress': compressed,
            rows: rows,
            // Record original values
            _rows: JSON.parse(JSON.stringify(rows)),
            '_nsslapd-errorlog': attrs['nsslapd-errorlog'][0],
            '_nsslapd-errorlog-level': attrs['nsslapd-errorlog-level'][0],
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
            '_nsslapd-errorlog-compress': compressed,
        }, this.props.enableTree);
    }

    onSelect(event, isSelected, rowId) {
        let disableSaveBtn = true;
        const rows = [...this.state.rows];

        // Update the row
        rows[rowId].selected = isSelected;

        // Handle "save button" state, first check the other config settings
        for (const config_attr of settings_attrs) {
            if (this.state['_' + config_attr] !== this.state[config_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        // Handle the table contents
        for (const row of rows) {
            for (const orig_row of this.state._rows) {
                if (orig_row.level === row.level) {
                    if (orig_row.selected !== row.selected) {
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
        let hour = this.state['nsslapd-errorlog-logrotationsynchour'] ? this.state['nsslapd-errorlog-logrotationsynchour'] : "00";
        let min = this.state['nsslapd-errorlog-logrotationsyncmin'] ? this.state['nsslapd-errorlog-logrotationsyncmin'] : "00";

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
                            id="nsslapd-errorlog-logging-enabled"
                            isChecked={this.state['nsslapd-errorlog-logging-enabled']}
                            onChange={(checked, e) => {
                                this.handleChange(e, "settings");
                            }}
                            title="Enable Error logging (nsslapd-errorlog-logging-enabled)."
                            label="Enable Error Logging"
                        />
                        <Form className="ds-margin-top-lg ds-left-margin-md" isHorizontal autoComplete="off">
                            <FormGroup
                                label="Error Log Location"
                                fieldId="nsslapd-errorlog"
                                title="Enable Error logging (nsslapd-errorlog)."
                            >
                                <TextInput
                                    value={this.state['nsslapd-errorlog']}
                                    type="text"
                                    id="nsslapd-errorlog"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="nsslapd-errorlog"
                                    onChange={(str, e) => {
                                        this.handleChange(e, "settings");
                                    }}
                                />
                            </FormGroup>
                        </Form>

                        <ExpandableSection
                            className="ds-left-margin-md ds-margin-top-lg ds-font-size-md"
                            toggleText={this.state.isExpanded ? 'Hide Verbose Logging Levels' : 'Show Verbose Logging Levels'}
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
                                title="The maximum number of logs that are archived (nsslapd-errorlog-maxlogsperdir)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Maximum Number Of Logs
                                </GridItem>
                                <GridItem span={3}>
                                    <NumberInput
                                        value={this.state['nsslapd-errorlog-maxlogsperdir']}
                                        min={1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-errorlog-maxlogsperdir", "rotation") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-errorlog-maxlogsperdir", 1, 2147483647, "rotation") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-errorlog-maxlogsperdir", "rotation") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="The maximum size of each log file in megabytes (nsslapd-errorlog-maxlogsize).">
                                <GridItem className="ds-label" span={3}>
                                    Maximum Log Size (in MB)
                                </GridItem>
                                <GridItem span={3}>
                                    <NumberInput
                                        value={this.state['nsslapd-errorlog-maxlogsize']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-errorlog-maxlogsize", "rotation") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-errorlog-maxlogsize", -1, 2147483647, "rotation") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-errorlog-maxlogsize", "rotation") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <hr />
                            <Grid title="Rotate the log based this number of time units (nsslapd-errorlog-logrotationtime).">
                                <GridItem className="ds-label" span={3}>
                                    Create New Log Every ...
                                </GridItem>
                                <GridItem span={9}>
                                    <div className="ds-container">
                                        <NumberInput
                                            value={this.state['nsslapd-errorlog-logrotationtime']}
                                            min={-1}
                                            max={2147483647}
                                            onMinus={() => { this.onMinusConfig("nsslapd-errorlog-logrotationtime", "rotation") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsslapd-errorlog-logrotationtime", -1, 2147483647, "rotation") }}
                                            onPlus={() => { this.onPlusConfig("nsslapd-errorlog-logrotationtime", "rotation") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={3}
                                        />
                                        <GridItem span={2} className="ds-left-indent">
                                            <FormSelect
                                                id="nsslapd-errorlog-logrotationtimeunit"
                                                value={this.state['nsslapd-errorlog-logrotationtimeunit']}
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
                            <Grid title="The time when the log should be rotated (nsslapd-errorlog-logrotationsynchour, nsslapd-errorlog-logrotationsyncmin).">
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
                                        id="nsslapd-errorlog-compress"
                                        isChecked={this.state['nsslapd-errorlog-compress']}
                                        onChange={this.handleSwitchChange}
                                        aria-label="nsslapd-errorlog-compress"
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
                                title="The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-errorlog-logmaxdiskspace)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Log Archive Exceeds (in MB)
                                </GridItem>
                                <GridItem span={1}>
                                    <NumberInput
                                        value={this.state['nsslapd-errorlog-logmaxdiskspace']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-errorlog-logmaxdiskspace", "exp") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-errorlog-logmaxdiskspace", -1, 2147483647, "exp") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-errorlog-logmaxdiskspace", "exp") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-errorlog-logminfreediskspace)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Free Disk Space (in MB)
                                </GridItem>
                                <GridItem span={1}>
                                    <NumberInput
                                        value={this.state['nsslapd-errorlog-logminfreediskspace']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-errorlog-logminfreediskspace", "exp") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-errorlog-logminfreediskspace", -1, 2147483647, "exp") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-errorlog-logminfreediskspace", "exp") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Server deletes an old archived log file when it is older than the specified age. (nsslapd-errorlog-logexpirationtime)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Log File is Older Than ...
                                </GridItem>
                                <GridItem span={9}>
                                    <div className="ds-container">
                                        <NumberInput
                                            value={this.state['nsslapd-errorlog-logexpirationtime']}
                                            min={-1}
                                            max={2147483647}
                                            onMinus={() => { this.onMinusConfig("nsslapd-errorlog-logexpirationtime", "exp") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsslapd-errorlog-logexpirationtime", -1, 2147483647, "exp") }}
                                            onPlus={() => { this.onPlusConfig("nsslapd-errorlog-logexpirationtime", "exp") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={3}
                                        />
                                        <GridItem span={2} className="ds-left-indent">
                                            <FormSelect
                                                id="nsslapd-errorlog-logexpirationtimeunit"
                                                value={this.state['nsslapd-errorlog-logexpirationtimeunit']}
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
                        <Text component={TextVariants.h3}>Loading Error Log Settings ...</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="lg" />
                </div>;
        }

        return (
            <div id="server-errorlog-page" className={this.state.loading ? "ds-disabled ds-margin-bottom-md" : "ds-margin-bottom-md"}>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Error Log Settings <FontAwesomeIcon
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

ServerErrorLog.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerErrorLog.defaultProps = {
    serverId: "",
    attrs: {},
};
