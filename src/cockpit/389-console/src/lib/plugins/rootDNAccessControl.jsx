import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Checkbox,
    Form,
    FormHelperText,
    Grid,
    GridItem,
    Select,
    SelectVariant,
    SelectOption,
    TimePicker,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd, listsEqual } from "../tools.jsx";

class RootDNAccessControl extends React.Component {
    componentDidMount(prevProps) {
        this.updateFields();
    }

    componentDidUpdate(prevProps) {
        if (this.props.rows !== prevProps.rows) {
            this.updateFields();
        }
    }

    constructor(props) {
        super(props);

        this.state = {
            saving: false,
            saveBtnDisabled: true,
            error: {},

            // Settings
            allowHost: [],
            allowHostOptions: [],
            denyHost: [],
            denyHostOptions: [],
            allowIP: [],
            allowIPOptions: [],
            denyIP: [],
            denyIPOptions: [],
            openTime: "0000",
            closeTime: "1159",
            daysAllowed: "",
            allowMon: false,
            allowTue: false,
            allowWed: false,
            allowThu: false,
            allowFri: false,
            allowSat: false,
            allowSun: false,
            // original values
            _allowHost: [],
            _denyHost: [],
            _allowIP: [],
            _denyIP: [],
            _openTime: "00:00",
            _closeTime: "23:59",
            _daysAllowed: "",
            _allowMon: false,
            _allowTue: false,
            _allowWed: false,
            _allowThu: false,
            _allowFri: false,
            _allowSat: false,
            _allowSun: false,
            // Typeahead state
            isAllowHostOpen: false,
            isDenyHostOpen: false,
            isAllowIPOpen: false,
            isDenyIPOpen: false
        };

        // Allow Host
        this.onAllowHostSelect = (event, selection) => {
            if (this.state.allowHost.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        allowHost: prevState.allowHost.filter((item) => item !== selection),
                        isAllowHostOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        allowHost: [...prevState.allowHost, selection],
                        isAllowHostOpen: false
                    }),
                );
            }
        };
        this.onAllowHostToggle = isAllowHostOpen => {
            this.setState({
                isAllowHostOpen
            });
        };
        this.onAllowHostClear = () => {
            this.setState({
                allowHost: [],
                isAllowHostOpen: false
            });
        };
        this.onAllowHostCreateOption = newValue => {
            if (!this.state.allowHostOptions.includes(newValue)) {
                this.setState({
                    allowHostOptions: [...this.state.allowHostOptions, newValue],
                    isAllowHostOpen: false
                });
            }
        };

        // Deny Host
        this.onDenyHostSelect = (event, selection) => {
            if (this.state.denyHost.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        denyHost: prevState.denyHost.filter((item) => item !== selection),
                        isDenyHostOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        denyHost: [...prevState.denyHost, selection],
                        isDenyHostOpen: false
                    }),
                );
            }
        };
        this.onDenyHostToggle = isDenyHostOpen => {
            this.setState({
                isDenyHostOpen
            });
        };
        this.onDenyHostClear = () => {
            this.setState({
                denyHost: [],
                isDenyHostOpen: false
            });
        };
        this.onDenyHostCreateOption = newValue => {
            if (!this.state.denyHostOptions.includes(newValue)) {
                this.setState({
                    denyHostOptions: [...this.state.denyHostOptions, newValue],
                    isDenyHostOpen: false
                });
            }
        };

        // Allow IP Adddress
        this.onAllowIPSelect = (event, selection) => {
            if (this.state.allowIP.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        allowIP: prevState.allowIP.filter((item) => item !== selection),
                        isAllowIPOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        allowIP: [...prevState.allowIP, selection],
                        isAllowIPOpen: false
                    }),
                );
            }
        };
        this.onAllowIPToggle = isAllowIPOpen => {
            this.setState({
                isAllowIPOpen
            });
        };
        this.onAllowIPClear = () => {
            this.setState({
                allowIP: [],
                isAllowIPOpen: false
            });
        };
        this.onAllowIPCreateOption = newValue => {
            if (!this.state.allowIPOptions.includes(newValue)) {
                this.setState({
                    allowIPOptions: [...this.state.allowIPOptions, newValue],
                    isAllowIPOpen: false
                });
            }
        };

        // Deny IP Adddress
        this.onDenyIPSelect = (event, selection) => {
            if (this.state.denyIP.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        denyIP: prevState.denyIP.filter((item) => item !== selection),
                        isDenyIPOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        denyIP: [...prevState.denyIP, selection],
                        isDenyIPOpen: false
                    }),
                );
            }
        };
        this.onDenyIPToggle = isDenyIPOpen => {
            this.setState({
                isDenyIPOpen
            });
        };
        this.onDenyIPClear = () => {
            this.setState({
                denyIP: [],
                isDenyIPOpen: false
            });
        };
        this.onDenyIPCreateOption = newValue => {
            if (!this.state.denyIPOptions.includes(newValue)) {
                this.setState({
                    denyIPOptions: [...this.state.denyIPOptions, newValue],
                    isDenyIPOpen: false
                });
            }
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleTimeChange = this.handleTimeChange.bind(this);
        this.savePlugin = this.savePlugin.bind(this);
        this.updateFields = this.updateFields.bind(this);
        this.validate = this.validate.bind(this);
    }

    validate() {
        const errObj = {};
        let all_good = false;

        const dayAttrs = [
            'allowMon', 'allowTue', 'allowWed', 'allowThu', 'allowFri',
            'allowSat', 'allowSun'
        ];
        for (const check_attr of dayAttrs) {
            if (this.state[check_attr]) {
                // At least one day must be set
                all_good = true;
                break;
            }
        }
        if (!all_good) {
            // No days were set
            errObj.daysAllowed = true;
        } else {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;

            const attrLists = [
                'allowHost', 'denyHost', 'allowIP', 'denyIP'
            ];
            for (const check_attr of attrLists) {
                if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                    all_good = true;
                    break;
                }
            }

            const attrs = [
                'openTime', 'closeTime', 'allowMon', 'allowTue', 'allowWed',
                'allowThu', 'allowFri', 'allowSat', 'allowSun'
            ];
            for (const check_attr of attrs) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabled: !all_good,
            error: errObj
        });
    }

    handleFieldChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validate() });
    }

    handleTimeChange(id, time_str) {
        const time_val = time_str.replace(":", "");
        this.setState({
            [id]: time_val
        }, () => { this.validate() });
    }

    updateFields() {
        let allowHostList = [];
        let denyHostList = [];
        let allowIPList = [];
        let denyIPList = [];
        const daysAllowed = {};

        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "RootDN Access Control");

            if (pluginRow["rootdn-allow-host"] !== undefined) {
                for (const value of pluginRow["rootdn-allow-host"]) {
                    allowHostList = [...allowHostList, value];
                }
            }
            if (pluginRow["rootdn-deny-host"] !== undefined) {
                for (const value of pluginRow["rootdn-deny-host"]) {
                    denyHostList = [...denyHostList, value];
                }
            }
            if (pluginRow["rootdn-allow-ip"] !== undefined) {
                for (const value of pluginRow["rootdn-allow-ip"]) {
                    allowIPList = [...allowIPList, value];
                }
            }
            if (pluginRow["rootdn-deny-ip"] !== undefined) {
                for (const value of pluginRow["rootdn-deny-ip"]) {
                    denyIPList = [...denyIPList, value];
                }
            }

            if (pluginRow["rootdn-days-allowed"] !== undefined) {
                const daysStr = pluginRow["rootdn-days-allowed"][0].toLowerCase();
                for (const day of ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']) {
                    if (daysStr.includes(day.toLowerCase())) {
                        daysAllowed['allow' + day] = true;
                        daysAllowed['_allow' + day] = true;
                    }
                }
            }

            let openTime = "00:00";
            if (pluginRow["rootdn-open-time"] !== undefined) {
                const openHour = pluginRow["rootdn-open-time"][0].substring(0, 2);
                const openMin = pluginRow["rootdn-open-time"][0].substring(2, 4);
                openTime = openHour + ":" + openMin;
            }

            let closeTime = "11:59";
            if (pluginRow["rootdn-close-time"] !== undefined) {
                const closeHour = pluginRow["rootdn-close-time"][0].substring(0, 2);
                const closeMin = pluginRow["rootdn-close-time"][0].substring(2, 4);
                closeTime = closeHour + ":" + closeMin;
            }

            this.setState({
                openTime: openTime,
                closeTime: closeTime,
                daysAllowed: daysAllowed,
                denyIP: denyIPList,
                allowIP: allowIPList,
                denyHost: denyHostList,
                allowHost: allowHostList,
                ...daysAllowed,
                _openTime:
                    pluginRow["rootdn-open-time"] === undefined
                        ? ""
                        : pluginRow["rootdn-open-time"][0],
                _closeTime:
                    pluginRow["rootdn-close-time"] === undefined
                        ? ""
                        : pluginRow["rootdn-close-time"][0],
                _daysAllowed: daysAllowed,
                _denyIP: denyIPList,
                _allowIP: allowIPList,
                _denyHost: denyHostList,
                _allowHost: allowHostList,
            });
        }
    }

    savePlugin() {
        // First builds the days allowed
        let daysAllowed = "";
        for (const day of ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun']) {
            if (this.state['allow' + day]) {
                daysAllowed += day + ",";
            }
        }
        // Strip trailing comma
        daysAllowed = daysAllowed.substring(0, daysAllowed.length - 1);

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "root-dn",
            "set",
            "--open-time",
            this.state.openTime.replace(":", "") || "delete",
            "--close-time",
            this.state.closeTime.replace(":", "") || "delete",
            "--days-allowed",
            daysAllowed || "delete"
        ];

        // Delete attributes if the user set an empty value to the field
        cmd = [...cmd, "--allow-host"];
        if (this.state.allowHost.length != 0) {
            for (const value of this.state.allowHost) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }
        cmd = [...cmd, "--deny-host"];
        if (this.state.denyHost.length != 0) {
            for (const value of this.state.denyHost) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }
        cmd = [...cmd, "--allow-ip"];
        if (this.state.allowIP.length != 0) {
            for (const value of this.state.allowIP) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }
        cmd = [...cmd, "--allow-host"];
        if (this.state.allowHost.length != 0) {
            for (const value of this.state.allowHost) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        this.setState({
            saving: true
        });

        log_cmd('savePlugin', 'Update Root DN access control', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Successfully updated the RootDN Access Control`
                    );
                    this.props.pluginListHandler();
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to update RootDN Access Control Plugin - ${errMsg.desc}`
                    );
                    this.props.pluginListHandler();
                    this.setState({
                        saving: false
                    });
                });
    }

    render() {
        const {
            allowHost,
            denyHost,
            allowIP,
            denyIP,
            openTime,
            closeTime,
            allowMon,
            allowTue,
            allowWed,
            allowThu,
            allowFri,
            allowSat,
            allowSun,
            error,
            saveBtnDisabled,
            saving,
        } = this.state;

        let saveBtnName = "Save";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Saving ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <div className={saving ? "ds-disabled" : ""}>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="RootDN Access Control"
                    pluginName="RootDN Access Control"
                    cmdName="root-dn"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title="Sets what hosts, by fully-qualified domain name, the root user is allowed to use to access the Directory Server. Wildcards are accepted. Any hosts not listed are implicitly denied (rootdn-allow-host)">
                            <GridItem className="ds-label" span={2}>
                                Allow Host
                            </GridItem>
                            <GridItem span={10}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a hostname "
                                    onToggle={this.onAllowHostToggle}
                                    onSelect={this.onAllowHostSelect}
                                    onClear={this.onAllowHostClear}
                                    selections={allowHost}
                                    isOpen={this.state.isAllowHostOpen}
                                    aria-labelledby="typeAhead-allow-host"
                                    placeholderText="Type a hostname ..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={this.onAllowHostCreateOption}
                                >
                                    {this.state.allowHostOptions.map((host, index) => (
                                        <SelectOption
                                            key={index}
                                            value={host}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title="Sets what hosts, by fully-qualified domain name, the root user is not allowed to use to access the Directory Server.  Wildcards are accepted.  Any hosts not listed are implicitly allowed (rootdn-deny-host). If a host address is listed in both the rootdn-allow-host and rootdn-deny-host attributes, it is denied access.">
                            <GridItem className="ds-label" span={2}>
                                Deny Host
                            </GridItem>
                            <GridItem span={10}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a hostname "
                                    onToggle={this.onDenyHostToggle}
                                    onSelect={this.onDenyHostSelect}
                                    onClear={this.onDenyHostClear}
                                    selections={denyHost}
                                    isOpen={this.state.isDenyHostOpen}
                                    aria-labelledby="typeAhead-deny-host"
                                    placeholderText="Type a hostname ..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={this.onDenyHostCreateOption}
                                >
                                    {this.state.denyHostOptions.map((host, index) => (
                                        <SelectOption
                                            key={index}
                                            value={host}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title="Sets what IP addresses, either IPv4 or IPv6, for machines the root user is allowed to use to access the Directory Server. Wildcards are accepted.  Any IP addresses not listed are implicitly denied (rootdn-allow-ip)">
                            <GridItem className="ds-label" span={2}>
                                Allow IP address
                            </GridItem>
                            <GridItem span={10}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an IP address"
                                    onToggle={this.onAllowIPToggle}
                                    onSelect={this.onAllowIPSelect}
                                    onClear={this.onAllowIPClear}
                                    selections={allowIP}
                                    isOpen={this.state.isAllowIPOpen}
                                    aria-labelledby="typeAhead-allow-ip"
                                    placeholderText="Type an IP address ..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={this.onAllowIPCreateOption}
                                >
                                    {this.state.allowIPOptions.map((ip, index) => (
                                        <SelectOption
                                            key={index}
                                            value={ip}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title="Sets what IP addresses, either IPv4 or IPv6, for machines the root user is not allowed to use to access the Directory Server. Wildcards are accepted. Any IP addresses not listed are implicitly allowed (rootdn-deny-ip) If an IP address is listed in both the rootdn-allow-ip and rootdn-deny-ip attributes, it is denied access.">
                            <GridItem className="ds-label" span={2}>
                                Deny IP address
                            </GridItem>
                            <GridItem span={10}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an IP address"
                                    onToggle={this.onDenyIPToggle}
                                    onSelect={this.onDenyIPSelect}
                                    onClear={this.onDenyIPClear}
                                    selections={denyIP}
                                    isOpen={this.state.isDenyIPOpen}
                                    aria-labelledby="typeAhead-deny-ip"
                                    placeholderText="Type an IP address ..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={this.onDenyIPCreateOption}
                                >
                                    {this.state.denyIPOptions.map((ip, index) => (
                                        <SelectOption
                                            key={index}
                                            value={ip}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title="Sets part of a time period or range when the root user is allowed to access the Directory Server. This sets when the time-based access begins (rootdn-open-time)">
                            <GridItem className="ds-label" span={2}>
                                Open Time
                            </GridItem>
                            <GridItem span={10}>
                                <TimePicker
                                    time={openTime}
                                    onChange={(str) => { this.handleTimeChange("openTime", str) }}
                                    is24Hour
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Sets part of a time period or range when the root user is allowed to access the Directory Server. This sets when the time-based access ends (rootdn-close-time)">
                            <GridItem className="ds-label" span={2}>
                                Close Time
                            </GridItem>
                            <GridItem span={10}>
                                <TimePicker
                                    time={closeTime}
                                    onChange={(str) => { this.handleTimeChange("closeTime", str) }}
                                    is24Hour
                                />
                            </GridItem>
                        </Grid>

                        <Grid title="Gives a comma-separated list of what days the root user is allowed to use to access the Directory Server. Any days listed are implicitly denied (rootdn-days-allowed)">
                            <GridItem span={12} className="ds-label">
                                Days To Allow Access
                            </GridItem>
                            <GridItem className="ds-margin-left" span={9}>
                                <Grid className="ds-margin-top-lg">
                                    <GridItem span={3}>
                                        <Checkbox
                                            id="allowMon"
                                            onChange={(checked, e) => {
                                                this.handleFieldChange(e);
                                            }}
                                            name={name}
                                            isChecked={allowMon}
                                            label="Monday"
                                        />
                                    </GridItem>
                                    <GridItem span={3}>
                                        <Checkbox
                                            id="allowFri"
                                            onChange={(checked, e) => {
                                                this.handleFieldChange(e);
                                            }}
                                            name={name}
                                            isChecked={allowFri}
                                            label="Friday"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem span={3}>
                                        <Checkbox
                                            id="allowTue"
                                            onChange={(checked, e) => {
                                                this.handleFieldChange(e);
                                            }}
                                            name={name}
                                            isChecked={allowTue}
                                            label="Tuesday"
                                        />
                                    </GridItem>
                                    <GridItem span={3}>
                                        <Checkbox
                                            id="allowSat"
                                            onChange={(checked, e) => {
                                                this.handleFieldChange(e);
                                            }}
                                            name={name}
                                            isChecked={allowSat}
                                            label="Saturday"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem span={3}>
                                        <Checkbox
                                            id="allowWed"
                                            onChange={(checked, e) => {
                                                this.handleFieldChange(e);
                                            }}
                                            name={name}
                                            isChecked={allowWed}
                                            label="Wednesday"
                                        />
                                    </GridItem>
                                    <GridItem span={3}>
                                        <Checkbox
                                            id="allowSun"
                                            onChange={(checked, e) => {
                                                this.handleFieldChange(e);
                                            }}
                                            name={name}
                                            isChecked={allowSun}
                                            label="Sunday"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem span={3}>
                                        <Checkbox
                                            id="allowThu"
                                            onChange={(checked, e) => {
                                                this.handleFieldChange(e);
                                            }}
                                            name={name}
                                            isChecked={allowThu}
                                            label="Thursday"
                                        />
                                    </GridItem>
                                </Grid>
                                <FormHelperText isError isHidden={!error.daysAllowed}>
                                    You must set at least one day
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                    </Form>
                    <Button
                        className="ds-margin-top-lg"
                        variant="primary"
                        onClick={this.savePlugin}
                        isDisabled={saveBtnDisabled || saving}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>
                </PluginBasicConfig>
            </div>
        );
    }
}

RootDNAccessControl.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

RootDNAccessControl.defaultProps = {
    rows: [],
    serverId: "",
};

export default RootDNAccessControl;
