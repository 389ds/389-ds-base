import cockpit from "cockpit";
import React from "react";
import {
    ExpandableSection,
    Form,
    Grid,
    GridItem,
    Switch,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";

const _ = cockpit.gettext;

class PluginBasicConfig extends React.Component {
    constructor(props) {
        super(props);

        this.updateFields = this.updateFields.bind(this);
        this.updateSwitch = this.updateSwitch.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleSwitchChange = this.handleSwitchChange.bind(this);

        this.state = {
            disableSwitch: false,
            currentPluginEnabled: true,
            currentPluginType: "",
            currentPluginPath: "",
            currentPluginInitfunc: "",
            currentPluginId: "",
            currentPluginVendor: "",
            currentPluginVersion: "",
            currentPluginDescription: "",
            currentPluginDependsOnType: "",
            currentPluginDependsOnNamed: "",
            currentPluginPrecedence: "",
            isExpanded: false,
        };

        this.handleToggle = (_event, isExpanded) => {
            this.setState({
                isExpanded
            });
        };
    }

    componentDidMount(prevProps) {
        this.updateFields();
    }

    componentDidUpdate(prevProps) {
        if (this.props.rows !== prevProps.rows) {
            this.updateFields();
        }
    }

    handleFieldChange(e) {
        e.preventDefault();
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    handleSwitchChange() {
        const {
            pluginName,
            cmdName,
            serverId,
            pluginListHandler,
            addNotification,
            toggleLoadingHandler
        } = this.props;
        const new_status = this.state.currentPluginEnabled ? "disable" : "enable";
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "plugin",
            cmdName,
            new_status
        ];

        toggleLoadingHandler();
        this.setState({ disableSwitch: true });
        log_cmd("handleSwitchChange", "Switch plugin states from the plugin tab", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    console.info("savePlugin", "Result", content);
                    pluginListHandler();
                    const successCheckCMD = [
                        "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
                        "config", "get", "nsslapd-dynamic-plugins"
                    ];
                    log_cmd("handleSwitchChange", "Get Dynamic Plugins attribute", successCheckCMD);
                    cockpit
                            .spawn(successCheckCMD, { superuser: true, err: "message" })
                            .done(content => {
                                const config = JSON.parse(content);
                                let dynamicPluginEnabled;
                                if (config.attrs["nsslapd-dynamic-plugins"][0] === "on") {
                                    dynamicPluginEnabled = true;
                                } else if (config.attrs["nsslapd-dynamic-plugins"][0] === "off") {
                                    dynamicPluginEnabled = false;
                                } else {
                                    console.error(
                                        "handleSwitchChange failed",
                                        "wrong nsslapd-dynamic-pluginc attribute value",
                                        config.attrs["nsslapd-dynamic-plugins"][0]
                                    );
                                }
                                addNotification(
                                    `${!dynamicPluginEnabled ? 'warning' : 'success'}`,
                                    cockpit.format(_("$0 plugin was successfully $1d."), pluginName, new_status) +
                                    `${!dynamicPluginEnabled ? _("Please, restart the instance.") : ''}`
                                );
                                toggleLoadingHandler();
                            })
                            .fail(() => {
                                console.error(
                                    "handleSwitchChange failed",
                                    "Failed to get nsslapd-dynamic-pluginc attribute value"
                                );
                                addNotification(
                                    "warning",
                                    cockpit.format(_("$0 plugin was successfully $1d. Please, restart the instance."), pluginName, new_status)
                                );
                                toggleLoadingHandler();
                            });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    addNotification(
                        "error",
                        cockpit.format(_("Error during $0 plugin modification - $1"), pluginName, errMsg.desc)
                    );
                    toggleLoadingHandler();
                    this.setState({ disableSwitch: false });
                });
    }

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === this.props.cn);
            if (pluginRow) {
                this.setState({
                    currentPluginType: pluginRow["nsslapd-pluginType"][0],
                    currentPluginPath: pluginRow["nsslapd-pluginPath"][0],
                    currentPluginInitfunc: pluginRow["nsslapd-pluginInitfunc"][0],
                    currentPluginId: pluginRow["nsslapd-pluginId"][0],
                    currentPluginVendor: pluginRow["nsslapd-pluginVendor"][0],
                    currentPluginVersion: pluginRow["nsslapd-pluginVersion"][0],
                    currentPluginDescription: pluginRow["nsslapd-pluginDescription"][0],
                    currentPluginDependsOnType:
                        pluginRow["nsslapd-plugin-depends-on-type"] === undefined
                            ? ""
                            : pluginRow["nsslapd-plugin-depends-on-type"][0],
                    currentPluginDependsOnNamed:
                        pluginRow["nsslapd-plugin-depends-on-named"] === undefined
                            ? ""
                            : pluginRow["nsslapd-plugin-depends-on-named"][0],
                    currentPluginPrecedence:
                        pluginRow["nsslapd-pluginprecedence"] === undefined
                            ? ""
                            : pluginRow["nsslapd-pluginprecedence"][0]
                });
            }
        }
        this.updateSwitch();
    }

    updateSwitch() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === this.props.cn);
            if (pluginRow) {
                let pluginEnabled = false;
                if (pluginRow["nsslapd-pluginEnabled"][0] === "on") {
                    pluginEnabled = true;
                } else if (pluginRow["nsslapd-pluginEnabled"][0] === "off") {
                    pluginEnabled = false;
                } else {
                    console.error(
                        "openPluginModal failed",
                        "wrong nsslapd-pluginenabled attribute value",
                        pluginRow["nsslapd-pluginEnabled"][0]
                    );
                }

                this.setState({
                    currentPluginEnabled: pluginEnabled,
                    disableSwitch: false
                });
            }
        }
    }

    render() {
        const {
            disableSwitch
        } = this.state;

        const enabled = <i>{_("Plugin is enabled")}</i>;
        const disabled = <i>{_("Plugin is disabled")}</i>;

        return (
            <div>
                <Form isHorizontal>
                    <Grid>
                        <GridItem span={9} className="ds-margin-top">
                            <TextContent>
                                <Text component={TextVariants.h2}>
                                    {this.props.pluginName} {_("Plugin")}
                                </Text>
                            </TextContent>
                        </GridItem>
                        {this.props.removeSwitch || (
                            <GridItem span={3} className="ds-float-right ds-margin-top">
                                <Switch
                                    id="this.props.pluginName"
                                    label={enabled}
                                    labelOff={disabled}
                                    isChecked={this.state.currentPluginEnabled}
                                    onChange={(event, str) => this.handleSwitchChange(event, str)}
                                    isDisabled={disableSwitch}
                                />
                            </GridItem>
                        )}
                        <hr />
                    </Grid>
                </Form>
                <div className="ds-indent">
                    {this.props.children}
                </div>
                {this.state.currentPluginPath !== "" &&
                    <ExpandableSection
                        className="ds-margin-top-lg"
                        toggleText={this.state.isExpanded ? _("Hide Plugin Details") : _("Show Plugin Details")}
                        onToggle={(event, isOpen) => this.handleToggle(event, isOpen)}
                        isExpanded={this.state.isExpanded}
                    >
                        <Grid className="ds-margin-left">
                            <GridItem span={12}>
                                <Form isHorizontal>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin Type")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginType}</i>
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin Path")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginPath}</i>
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin Initfunc")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginInitfunc}</i>
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin Depends On Type")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginDependsOnType}</i>
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin Depends On Named")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginDependsOnNamed}</i>
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin Vendor")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginVendor}</i>
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin Version")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginVersion}</i>
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin Description")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginDescription}</i>
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin ID")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginId}</i>
                                        </GridItem>
                                    </Grid>
                                    <Grid>
                                        <GridItem span={3}>
                                            <b>{_("Plugin Precedence")}</b>
                                        </GridItem>
                                        <GridItem span={6}>
                                            <i>{this.state.currentPluginPrecedence}</i>
                                        </GridItem>
                                    </Grid>
                                    <hr />
                                </Form>
                            </GridItem>
                        </Grid>
                    </ExpandableSection>}
            </div>
        );
    }
}

PluginBasicConfig.propTypes = {
    children: PropTypes.any.isRequired,
    rows: PropTypes.array,
    serverId: PropTypes.string,
    cn: PropTypes.string,
    pluginName: PropTypes.string,
    cmdName: PropTypes.string,
    removeSwitch: PropTypes.bool,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func,
};

PluginBasicConfig.defaultProps = {
    rows: [],
    serverId: "",
    cn: "",
    pluginName: "",
    cmdName: "",
    removeSwitch: false,
};

export default PluginBasicConfig;
