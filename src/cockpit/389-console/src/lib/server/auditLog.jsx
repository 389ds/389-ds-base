import cockpit from "cockpit";
import React from "react";
import { listsEqual, log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Form,
    FormGroup,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    NumberInput,
    Select,
    SelectVariant,
    SelectOption,
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
    'nsslapd-auditlog',
    'nsslapd-auditlog-logging-enabled',
    'nsslapd-auditlog-logbuffering',
];

const rotation_attrs = [
    'nsslapd-auditlog-logrotationsync-enabled',
    'nsslapd-auditlog-logrotationsynchour',
    'nsslapd-auditlog-logrotationsyncmin',
    'nsslapd-auditlog-logrotationtime',
    'nsslapd-auditlog-logrotationtimeunit',
    'nsslapd-auditlog-maxlogsize',
    'nsslapd-auditlog-maxlogsperdir',
    'nsslapd-auditlog-compress'
];

const rotation_attrs_no_time = [
    'nsslapd-auditlog-logrotationsync-enabled',
    'nsslapd-auditlog-logrotationtime',
    'nsslapd-auditlog-logrotationtimeunit',
    'nsslapd-auditlog-maxlogsize',
    'nsslapd-auditlog-maxlogsperdir',
    'nsslapd-auditlog-compress'
];

const exp_attrs = [
    'nsslapd-auditlog-logexpirationtime',
    'nsslapd-auditlog-logexpirationtimeunit',
    'nsslapd-auditlog-logmaxdiskspace',
    'nsslapd-auditlog-logminfreediskspace',
];

const _ = cockpit.gettext;

export class ServerAuditLog extends React.Component {
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
            displayAttrs: [],
            isDisplayAttrOpen: false,
            attributes: this.props.displayAttrs,
            displayAllAttrs: false,
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.handleOnDisplayAttrSelect = (event, selection) => {
            if (this.state.displayAttrs.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        displayAttrs: prevState.displayAttrs.filter((item) => item !== selection),
                        isDisplayAttrOpen: false
                    }), () => { this.validateSaveBtn("settings", "none", "none") }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        displayAttrs: [...prevState.displayAttrs, selection],
                        isDisplayAttrOpen: false
                    }), () => { this.validateSaveBtn("settings", "none", "none") }
                );
            }
        };
        this.handleOnDisplayAttrToggle = isDisplayAttrOpen => {
            this.setState({
                isDisplayAttrOpen
            });
        };
        this.handleOnDisplayAttrClear = () => {
            this.setState({
                displayAttrs: [],
                isDisplayAttrOpen: false
            }, () => { this.validateSaveBtn("settings", "none", "none") });
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

        if (this.state.displayAllAttrs !== this.state._displayAllAttrs) {
            disableSaveBtn = false;
        }
        if (!this.state.displayAllAttrs && !listsEqual(this.state.displayAttrs, this.state._displayAttrs)) {
            disableSaveBtn = false;
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
            'nsslapd-auditlog-compress': value
        }, () => {
            this.validateSaveBtn('rotation', 'nsslapd-auditlog-compress', value);
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
        if (hour !== this.state['_nsslapd-auditlog-logrotationsynchour'] ||
            min !== this.state['_nsslapd-auditlog-logrotationsyncmin']) {
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

        if (!this.state.displayAllAttrs && !listsEqual(this.state.displayAttrs, this.state._displayAttrs)) {
            if (this.state.displayAttrs.length > 0) {
                const val = this.state.displayAttrs.join(' ');
                cmd.push('nsslapd-auditlog-display-attrs=' + val);
            } else {
                cmd.push('nsslapd-auditlog-display-attrs=');
            }
        }
        if (this.state.displayAllAttrs !== this.state._displayAllAttrs) {
            if (this.state.displayAllAttrs) {
                cmd.push('nsslapd-auditlog-display-attrs=*');
            } else if (this.state.displayAttrs.length === 0) {
                cmd.push('nsslapd-auditlog-display-attrs=');
            }
        }

        if (cmd.length === 5) {
            // Nothing to save, just return
            return;
        }

        log_cmd("saveConfig", "Saving audit log settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.refreshConfig();
                    this.props.addNotification(
                        "success",
                        _("Successfully updated Audit Log settings")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.refreshConfig();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error saving Audit Log settings - $0"), errMsg.desc)
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
        log_cmd("refreshConfig", "load Audit Log configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    let enabled = false;
                    let compressed = false;
                    let buffering = false;
                    let display_attrs = [];
                    let displayAllAttrs = this.state.displayAllAttrs;

                    if (attrs['nsslapd-auditlog-logging-enabled'][0] === "on") {
                        enabled = true;
                    }
                    if (attrs['nsslapd-auditlog-compress'][0] === "on") {
                        compressed = true;
                    }
                    if (attrs['nsslapd-auditlog-logbuffering'][0] === "on") {
                        buffering = true;
                    }
                    if ('nsslapd-auditlog-display-attrs' in attrs) {
                        if (attrs['nsslapd-auditlog-display-attrs'][0] === "*") {
                            displayAllAttrs = true;
                        } else if (attrs['nsslapd-auditlog-display-attrs'][0] !== "") {
                            displayAllAttrs = false;
                            display_attrs = attrs['nsslapd-auditlog-display-attrs'][0].split(/[, ]+/);
                        }
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
                        'nsslapd-auditlog-compress': compressed,
                        'nsslapd-auditlog-logbuffering': buffering,
                        displayAttrs: display_attrs,
                        displayAllAttrs,
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
                        '_nsslapd-auditlog-compress': compressed,
                        '_nsslapd-auditlog-logbuffering': buffering,
                        _displayAttrs: display_attrs,
                        _displayAllAttrs: displayAllAttrs,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading Audit Log configuration - $0"), errMsg.desc)
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
        let buffering = false;
        let display_attrs = [];
        let displayAllAttrs = this.state.displayAllAttrs;

        if (attrs['nsslapd-auditlog-logging-enabled'][0] === "on") {
            enabled = true;
        }
        if (attrs['nsslapd-auditlog-compress'][0] === "on") {
            compressed = true;
        }
        if (attrs['nsslapd-auditlog-logbuffering'][0] === "on") {
            buffering = true;
        }

        if ('nsslapd-auditlog-display-attrs' in attrs) {
            if (attrs['nsslapd-auditlog-display-attrs'][0] === "*") {
                displayAllAttrs = true;
            } else if (attrs['nsslapd-auditlog-display-attrs'][0] !== "") {
                display_attrs = attrs['nsslapd-auditlog-display-attrs'][0].split(/[, ]+/);
            }
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
            'nsslapd-auditlog-compress': compressed,
            'nsslapd-auditlog-logbuffering': buffering,
            displayAttrs: display_attrs,
            displayAllAttrs,
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
            '_nsslapd-auditlog-compress': compressed,
            '_nsslapd-auditlog-logbuffering': buffering,
            _displayAttrs: display_attrs,
            _displayAllAttrs: displayAllAttrs,
        }, this.props.enableTree);
    }

    render() {
        let saveSettingsName = _("Save Log Settings");
        let saveRotationName = _("Save Rotation Settings");
        let saveDeletionName = _("Save Deletion Settings");
        const extraPrimaryProps = {};
        let rotationTime = "";
        let hour = this.state['nsslapd-auditlog-logrotationsynchour'] ? this.state['nsslapd-auditlog-logrotationsynchour'] : "00";
        let min = this.state['nsslapd-auditlog-logrotationsyncmin'] ? this.state['nsslapd-auditlog-logrotationsyncmin'] : "00";

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
                            id="nsslapd-auditlog-logging-enabled"
                            isChecked={this.state['nsslapd-auditlog-logging-enabled']}
                            onChange={(checked, e) => {
                                this.handleChange(e, "settings");
                            }}
                            title={_("Enable audit logging (nsslapd-auditlog-logging-enabled).")}
                            label={_("Enable Audit Logging")}
                        />
                        <Form className="ds-margin-top-lg ds-left-margin-md" isHorizontal autoComplete="off">
                            <FormGroup
                                label={_("Audit Log Location")}
                                fieldId="nsslapd-auditlog"
                                title={_("Enable audit logging (nsslapd-auditlog).")}
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
                        <Checkbox
                            className="ds-left-margin-md ds-margin-top-lg"
                            id="nsslapd-auditlog-logbuffering"
                            isChecked={this.state['nsslapd-auditlog-logbuffering']}
                            onChange={(checked, e) => {
                                this.handleChange(e, "settings");
                            }}
                            title={_("This applies to both the audit & auditfail logs.  Disable audit log buffering for faster troubleshooting, but this will impact server performance (nsslapd-auditlog-logbuffering).")}
                            label={_("Audit Log Buffering Enabled")}
                        />
                        <Form className="ds-margin-top-lg ds-left-margin-md" isHorizontal autoComplete="off">
                            <FormGroup
                                label={_("Display Attributes")}
                                fieldId="nsslapd-auditlog-display-attrs"
                                title={_("Display attributes from the entry in the audit log (nsslapd-auditlog-display-attrs).")}
                            >
                                <div className={this.state.displayAllAttrs ? "ds-hidden" : "ds-margin-bottom"}>
                                    <Select
                                        variant={SelectVariant.typeaheadMulti}
                                        typeAheadAriaLabel="Type an attribute"
                                        onToggle={this.handleOnDisplayAttrToggle}
                                        onSelect={this.handleOnDisplayAttrSelect}
                                        onClear={this.handleOnDisplayAttrClear}
                                        selections={this.state.displayAttrs}
                                        isOpen={this.state.isDisplayAttrOpen}
                                        aria-labelledby="typeAhead-audit-display-attr"
                                        placeholderText={_("Type an attribute...")}
                                        noResultsFoundText={_("There are no matching attributes")}
                                    >
                                        {this.state.attributes.map((attr, index) => (
                                            <SelectOption
                                                key={index}
                                                value={attr}
                                            />
                                        ))}
                                    </Select>
                                </div>
                                <Checkbox
                                    className="ds-lower-field-md"
                                    id="displayAllAttrs"
                                    isChecked={this.state.displayAllAttrs}
                                    onChange={(checked, e) => {
                                        this.handleChange(e, "settings");
                                    }}
                                    title={_("Display all attributes from the entry in the audit log (nsslapd-auditlog-display-attrs).")}
                                    label={_("All Attributes")}
                                />
                            </FormGroup>
                        </Form>
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
                                title={_("The maximum number of logs that are archived (nsslapd-auditlog-maxlogsperdir).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Maximum Number Of Logs")}
                                </GridItem>
                                <GridItem span={3}>
                                    <NumberInput
                                        value={this.state['nsslapd-auditlog-maxlogsperdir']}
                                        min={1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-auditlog-maxlogsperdir", "rotation") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-auditlog-maxlogsperdir", 1, 2147483647, "rotation") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-auditlog-maxlogsperdir", "rotation") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("The maximum size of each log file in megabytes (nsslapd-auditlog-maxlogsize).")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Maximum Log Size (in MB)")}
                                </GridItem>
                                <GridItem span={3}>
                                    <NumberInput
                                        value={this.state['nsslapd-auditlog-maxlogsize']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-auditlog-maxlogsize", "rotation") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-auditlog-maxlogsize", -1, 2147483647, "rotation") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-auditlog-maxlogsize", "rotation") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <hr />
                            <Grid title={_("Rotate the log based this number of time units (nsslapd-auditlog-logrotationtime).")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Create New Log Every ...")}
                                </GridItem>
                                <GridItem span={9}>
                                    <div className="ds-container">
                                        <NumberInput
                                            value={this.state['nsslapd-auditlog-logrotationtime']}
                                            min={-1}
                                            max={2147483647}
                                            onMinus={() => { this.onMinusConfig("nsslapd-auditlog-logrotationtime", "rotation") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsslapd-auditlog-logrotationtime", -1, 2147483647, "rotation") }}
                                            onPlus={() => { this.onPlusConfig("nsslapd-auditlog-logrotationtime", "rotation") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={3}
                                        />
                                        <GridItem span={2} className="ds-left-indent">
                                            <FormSelect
                                                id="nsslapd-auditlog-logrotationtimeunit"
                                                value={this.state['nsslapd-auditlog-logrotationtimeunit']}
                                                onChange={(str, e) => {
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
                            <Grid title={_("The time when the log should be rotated (nsslapd-auditlog-logrotationsynchour, nsslapd-auditlog-logrotationsyncmin).")}>
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
                                        id="nsslapd-auditlog-compress"
                                        isChecked={this.state['nsslapd-auditlog-compress']}
                                        onChange={this.handleSwitchChange}
                                        aria-label="nsslapd-auditlog-compress"
                                    />`
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
                                title={_("The server deletes the oldest archived log when the total of all the logs reaches this amount (nsslapd-auditlog-logmaxdiskspace).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Log Archive Exceeds (in MB)")}
                                </GridItem>
                                <GridItem span={1}>
                                    <NumberInput
                                        value={this.state['nsslapd-auditlog-logmaxdiskspace']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-auditlog-logmaxdiskspace", "exp") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-auditlog-logmaxdiskspace", -1, 2147483647, "exp") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-auditlog-logmaxdiskspace", "exp") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The server deletes the oldest archived log file when available disk space is less than this amount. (nsslapd-auditlog-logminfreediskspace).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Free Disk Space (in MB)")}
                                </GridItem>
                                <GridItem span={1}>
                                    <NumberInput
                                        value={this.state['nsslapd-auditlog-logminfreediskspace']}
                                        min={-1}
                                        max={2147483647}
                                        onMinus={() => { this.onMinusConfig("nsslapd-auditlog-logminfreediskspace", "exp") }}
                                        onChange={(e) => { this.onConfigChange(e, "nsslapd-auditlog-logminfreediskspace", -1, 2147483647, "exp") }}
                                        onPlus={() => { this.onPlusConfig("nsslapd-auditlog-logminfreediskspace", "exp") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={6}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Server deletes an old archived log file when it is older than the specified age. (nsslapd-auditlog-logexpirationtime).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Log File is Older Than ...")}
                                </GridItem>
                                <GridItem span={9}>
                                    <div className="ds-container">
                                        <NumberInput
                                            value={this.state['nsslapd-auditlog-logexpirationtime']}
                                            min={-1}
                                            max={2147483647}
                                            onMinus={() => { this.onMinusConfig("nsslapd-auditlog-logexpirationtime", "exp") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsslapd-auditlog-logexpirationtime", -1, 2147483647, "exp") }}
                                            onPlus={() => { this.onPlusConfig("nsslapd-auditlog-logexpirationtime", "exp") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={3}
                                        />
                                        <GridItem span={2} className="ds-left-indent">
                                            <FormSelect
                                                id="nsslapd-auditlog-logexpirationtimeunit"
                                                value={this.state['nsslapd-auditlog-logexpirationtimeunit']}
                                                onChange={(str, e) => {
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
                        <Text component={TextVariants.h3}>{_("Loading Audit Log settings ...")}</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="lg" />
                </div>
            );
        }

        return (
            <div id="server-auditlog-page" className={this.state.loading ? "ds-disabled" : ""}>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                {_("Audit Log Settings")}
                                <FontAwesomeIcon
                                    size="lg"
                                    className="ds-left-margin ds-refresh"
                                    icon={faSyncAlt}
                                    title={_("Refresh log settings")}
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

ServerAuditLog.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
    displayAttrs: PropTypes.array,
};

ServerAuditLog.defaultProps = {
    serverId: "",
    attrs: {},
    displayAttrs: [],
};
