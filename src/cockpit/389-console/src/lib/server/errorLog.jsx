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
    Thead,
    Tr,
    Th,
    Tbody,
    Td
} from '@patternfly/react-table';
import { SyncAltIcon } from '@patternfly/react-icons';
import PropTypes from "prop-types";

const settings_attrs = [
    'nsslapd-errorlog',
    'nsslapd-errorlog-level',
    'nsslapd-errorlog-logging-enabled',
];

const _ = cockpit.gettext;

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

        this.traceLevel = <>{_("Trace Function Calls")} <font size="1" className="ds-info-color">{_("(level 1)")}</font></>;
        this.packetLevel = <>{_("Packet Handling")} <font size="1" className="ds-info-color">{_("(level 2)")}</font></>;
        this.heavyLevel = <>{_("Heavy Trace Output")} <font size="1" className="ds-info-color">{_("(level 4)")}</font></>;
        this.connLevel = <>{_("Connection Management")} <font size="1" className="ds-info-color">{_("(level 8)")}</font></>;
        this.packetSentLevel = <>{_("Packets Sent & Received")} <font size="1" className="ds-info-color">{_("(level 16)")}</font></>;
        this.filterLevel = <>{_("Search Filter Processing")} <font size="1" className="ds-info-color">{_("(level 32)")}</font></>;
        this.configLevel = <>{_("Config File Processing")} <font size="1" className="ds-info-color">{_("(level 64)")}</font></>;
        this.aclLevel = <>{_("Access Control List Processing")} <font size="1" className="ds-info-color">{_("(level 128)")}</font></>;
        this.entryLevel = <>{_("Log Entry Parsing")} <font size="1" className="ds-info-color">{_("(level 2048)")}</font></>;
        this.houseLevel = <>{_("Housekeeping")} <font size="1" className="ds-info-color">{_("(level 4096)")}</font></>;
        this.replLevel = <>{_("Replication")} <font size="1" className="ds-info-color">{_("(level 8192)")}</font></>;
        this.cacheLevel = <>{_("Entry Cache")} <font size="1" className="ds-info-color">{_("(level 32768)")}</font></>;
        this.pluginLevel = <>{_("Plugin")} <font size="1" className="ds-info-color">{_("(level 65536)")}</font></>;
        this.aclSummaryevel = <>{_("Access Control Summary")} <font size="1" className="ds-info-color">{_("(level 262144)")}</font></>;
        this.dbLevel = <>{_("Backend Database")} <font size="1" className="ds-info-color">{_("(level 524288)")}</font></>;
        this.pwdpolicyLevel = <>{_("Password Policy")} <font size="1" className="ds-info-color">{_("(level 1048576)")}</font></>;

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
                { title: _("Logging Level") },
            ],
        };

        this.handleOnToggle = (_event, isExpanded) => {
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
        this.handleRefreshConfig = this.handleRefreshConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
        this.handleOnSelect = this.handleOnSelect.bind(this);
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
            newValue = newValue > maxValue ? maxValue : newValue < min ? min : newValue;
            this.setState({
                [id]: newValue
            }, () => { this.validateSaveBtn(nav_tab, id, newValue) });
        };
        this.onPlusConfig = (id, nav_tab) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validateSaveBtn(nav_tab, id, Number(this.state[id])) });
        };
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
        }, () => { this.validateSaveBtn(nav_tab, attr, value) });
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

        const cmd = [
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
                    this.handleRefreshConfig();
                    this.props.addNotification(
                        "success",
                        _("Successfully updated Error Log settings")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.handleRefreshConfig();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error saving Error Log settings - $0"), errMsg.desc)
                    );
                });
    }

    handleRefreshConfig() {
        this.setState({
            loading: true,
            loaded: false,
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("handleRefreshConfig", "load Error Log configuration", cmd);
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
                            rows,
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
                        cockpit.format(_("Error loading Error Log configuration - $0"), errMsg.desc)
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
            rows,
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

    handleOnSelect = (_event, isSelected, rowIndex) => {
        let disableSaveBtn = true;
        const rows = [...this.state.rows];
        
        // Update the selected row
        rows[rowIndex].selected = isSelected;

        // Check other config settings
        for (const config_attr of settings_attrs) {
            if (this.state['_' + config_attr] !== this.state[config_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        // Check table contents
        for (const row of rows) {
            for (const orig_row of this.state._rows) {
                if (orig_row.level === row.level && orig_row.selected !== row.selected) {
                    disableSaveBtn = false;
                    break;
                }
            }
        }

        this.setState({
            rows,
            saveSettingsDisabled: disableSaveBtn,
        });
    }

    render() {
        let saveSettingsName = _("Save Log Settings");
        let saveRotationName = _("Save Rotation Settings");
        let saveDeletionName = _("Save Deletion Settings");
        const extraPrimaryProps = {};
        let rotationTime = "";
        let hour = this.state['nsslapd-errorlog-logrotationsynchour'] ? this.state['nsslapd-errorlog-logrotationsynchour'] : "00";
        let min = this.state['nsslapd-errorlog-logrotationsyncmin'] ? this.state['nsslapd-errorlog-logrotationsyncmin'] : "00";

        if (this.state.loading) {
            saveSettingsName = _("Saving settings ...");
            saveRotationName = _("Saving settings ...");
            saveDeletionName = _("Saving settings ...");
            extraPrimaryProps.spinnerAriaValueText = _("Loading");
        }

        // Adjust time string for TimePicket
        if (hour.length === 1) {
            hour = "0" + hour;
        }
        if (min.length === 1) {
            min = "0" + min;
        }
        rotationTime = hour + ":" + min;

        let body = (
            <div className="ds-margin-top-lg ds-left-margin">
                <Tabs className="ds-margin-top-xlg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>{_("Settings")}</TabTitleText>}>
                        <Checkbox
                            className="ds-margin-top-xlg"
                            id="nsslapd-errorlog-logging-enabled"
                            isChecked={this.state['nsslapd-errorlog-logging-enabled']}
                            onChange={(e, checked) => {
                                this.handleChange(e, "settings");
                            }}
                            title={_("Enable Error logging (nsslapd-errorlog-logging-enabled).")}
                            label={_("Enable Error Logging")}
                        />
                        <Form className="ds-margin-top-lg ds-left-margin-md" isHorizontal autoComplete="off">
                            <FormGroup
                                label={_("Error Log Location")}
                                fieldId="nsslapd-errorlog"
                                title={_("Enable Error logging (nsslapd-errorlog).")}
                            >
                                <TextInput
                                    value={this.state['nsslapd-errorlog']}
                                    type="text"
                                    id="nsslapd-errorlog"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="nsslapd-errorlog"
                                    onChange={(e, str) => {
                                        this.handleChange(e, "settings");
                                    }}
                                />
                            </FormGroup>
                        </Form>

                        <ExpandableSection
                            className="ds-left-margin-md ds-margin-top-lg ds-font-size-md"
                            toggleText={this.state.isExpanded ? _("Hide Verbose Logging Levels") : _("Show Verbose Logging Levels")}
                            onToggle={(event, isExpanded) => this.handleOnToggle(event, isExpanded)}
                            isExpanded={this.state.isExpanded}
                        >
                            <Table
                                aria-label="Selectable Error Log Levels"
                                className="ds-left-margin"
                                variant="compact"
                            >
                                <Thead>
                                    <Tr>
                                        <Th screenReaderText="Checkboxes" />
                                        <Th>{_("Logging Level")}</Th>
                                    </Tr>
                                </Thead>
                                <Tbody>
                                    {this.state.rows.map((row, rowIndex) => (
                                        <Tr key={rowIndex}>
                                            <Td
                                                select={{
                                                    rowIndex,
                                                    onSelect: (_event, isSelecting) => 
                                                        this.handleOnSelect(_event, isSelecting, rowIndex),
                                                    isSelected: row.selected
                                                }}
                                            />
                                            <Td>
                                                {row.cells[0].title}
                                            </Td>
                                        </Tr>
                                    ))}
                                </Tbody>
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
                            spinnerAriaValueText={this.state.loading ? _("Saving") : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveSettingsName}
                        </Button>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>{_("Rotation Policy")}</TabTitleText>}>
                        <Form className="ds-margin-top-lg" isHorizontal autoComplete="off">
                            <Grid
                                className="ds-margin-top"
                                title={_("The maximum number of logs that are archived (nsslapd-errorlog-maxlogsperdir).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Maximum Number Of Logs")}
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
                            <Grid title={_("The maximum size of each log file in megabytes (nsslapd-errorlog-maxlogsize).")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Maximum Log Size (in MB)")}
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
                            <Grid title={_("Rotate the log based this number of time units (nsslapd-errorlog-logrotationtime).")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Create New Log Every ...")}
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
                                                onChange={(e, str) => {
                                                    this.handleChange(e, "rotation");
                                                }}
                                                aria-label="FormSelect Input"
                                            >
                                                <FormSelectOption key="0" value="minute" label={_("minute")} />
                                                <FormSelectOption key="1" value="hour" label={_("hour")} />
                                                <FormSelectOption key="2" value="day" label={_("day")} />
                                                <FormSelectOption key="3" value="week" label={_("week")} />
                                                <FormSelectOption key="4" value="month" label={_("month")} />
                                            </FormSelect>
                                        </GridItem>
                                    </div>
                                </GridItem>
                            </Grid>
                            <Grid title={_("The time when the log should be rotated (nsslapd-errorlog-logrotationsynchour, nsslapd-errorlog-logrotationsyncmin).")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Time Of Day")}
                                </GridItem>
                                <GridItem span={1}>
                                    <TimePicker
                                        time={rotationTime}
                                        onChange={this.handleTimeChange}
                                        is24Hour
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("Compress (gzip) the log after it's rotated.")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Compress Rotated Logs")}
                                </GridItem>
                                <GridItem span={8}>
                                    <Switch
                                        id="nsslapd-errorlog-compress"
                                        isChecked={this.state['nsslapd-errorlog-compress']}
                                        onChange={(_event, value) => this.handleSwitchChange(value)}
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
                            spinnerAriaValueText={this.state.loading ? _("Saving") : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveRotationName}
                        </Button>
                    </Tab>

                    <Tab eventKey={2} title={<TabTitleText>{_("Deletion Policy")}</TabTitleText>}>
                        <Form className="ds-margin-top-lg" isHorizontal autoComplete="off">
                            <Grid
                                className="ds-margin-top"
                                title={_("The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-errorlog-logmaxdiskspace).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Log Archive Exceeds (in MB)")}
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
                                title={_("The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-errorlog-logminfreediskspace).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Free Disk Space (in MB)")}
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
                                title={_("Server deletes an old archived log file when it is older than the specified age. (nsslapd-errorlog-logexpirationtime).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Log File is Older Than ...")}
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
                                                onChange={(e, str) => {
                                                    this.handleChange(e, "exp");
                                                }}
                                                aria-label="FormSelect Input"
                                            >
                                                <FormSelectOption key="2" value="day" label={_("day")} />
                                                <FormSelectOption key="3" value="week" label={_("week")} />
                                                <FormSelectOption key="4" value="month" label={_("month")} />
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
                            spinnerAriaValueText={this.state.loading ? _("Saving") : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveDeletionName}
                        </Button>
                    </Tab>
                </Tabs>
            </div>
        );

        if (!this.state.loaded) {
            body = (
                <div className="ds-loading-spinner ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>{_("Loading Error Log Settings ...")}</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="lg" />
                </div>
            );
        }

        return (
            <div id="server-errorlog-page" className={this.state.loading ? "ds-disabled ds-margin-bottom-md" : "ds-margin-bottom-md"}>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                {_("Error Log Settings")}
                                <Button 
                                    variant="plain"
                                    aria-label={_("Refresh log settings")}
                                    onClick={() => {
                                        this.handleRefreshConfig();
                                    }}
                                >
                                    <SyncAltIcon />
                                </Button>
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
