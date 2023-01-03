import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { log_cmd } from "./lib/tools.jsx";
import { DoubleConfirmModal } from "./lib/notifications.jsx";
import {
    Grid,
    GridItem,
    Spinner,
    Nav,
    NavItem,
    NavList,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import {
    BanIcon,
    CheckCircleIcon,
    ListIcon,
    ResourcesEmptyIcon
} from '@patternfly/react-icons';

import { PluginTable } from "./lib/plugins/pluginTables.jsx";
import AccountPolicy from "./lib/plugins/accountPolicy.jsx";
import AttributeUniqueness from "./lib/plugins/attributeUniqueness.jsx";
import AutoMembership from "./lib/plugins/autoMembership.jsx";
import DNAPlugin from "./lib/plugins/dna.jsx";
import LinkedAttributes from "./lib/plugins/linkedAttributes.jsx";
import ManagedEntries from "./lib/plugins/managedEntries.jsx";
import MemberOf from "./lib/plugins/memberOf.jsx";
import PassthroughAuthentication from "./lib/plugins/passthroughAuthentication.jsx";
import PAMPassthroughAuthentication from "./lib/plugins/pamPassThru.jsx";
import ReferentialIntegrity from "./lib/plugins/referentialIntegrity.jsx";
import RetroChangelog from "./lib/plugins/retroChangelog.jsx";
import RootDNAccessControl from "./lib/plugins/rootDNAccessControl.jsx";
import USNPlugin from "./lib/plugins/usn.jsx";
import WinSync from "./lib/plugins/winsync.jsx";

export class Plugins extends React.Component {
    componentDidUpdate(prevProps) {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.pluginList();
            } else {
                if (this.props.serverId !== prevProps.serverId) {
                    this.pluginList();
                }
            }
        }
    }

    componentDidMount () {
        this.getSchema();
    }

    constructor(props) {
        super(props);

        this.state = {
            firstLoad: true,
            loading: false,
            showPluginModal: false,
            currentPluginTab: "",
            activePlugin: "All Plugins",
            togglePluginName: "",
            togglePluginEnabled: false,
            showConfirmToggle: false,
            toggleSpinning: false,
            modalChecked: false,
            modalSpinning: false,
            rows: [],
            pluginTableKey: 0,
            attributes: [],
            objectclasses: [],

            // Plugin attributes
            currentPluginName: "",
            currentPluginType: "",
            currentPluginEnabled: false,
            currentPluginPath: "",
            currentPluginInitfunc: "",
            currentPluginId: "",
            currentPluginVendor: "",
            currentPluginVersion: "",
            currentPluginDescription: "",
            currentPluginDependsOnType: "",
            currentPluginDependsOnNamed: "",
            currentPluginPrecedence: ""
        };

        this.handleSelect = result => {
            this.setState({
                activePlugin: result.itemId
            });
        };

        this.onFieldChange = this.onFieldChange.bind(this);
        this.pluginList = this.pluginList.bind(this);
        this.onChangeTab = this.onChangeTab.bind(this);
        this.savePlugin = this.savePlugin.bind(this);
        this.toggleLoading = this.toggleLoading.bind(this);
        this.getIconAndName = this.getIconAndName.bind(this);
        this.togglePlugin = this.togglePlugin.bind(this);
        this.showConfirmToggle = this.showConfirmToggle.bind(this);
        this.closeConfirmToggle = this.closeConfirmToggle.bind(this);
        this.getSchema = this.getSchema.bind(this);
    }

    getSchema() {
        const attr_cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "attributetypes",
            "list"
        ];
        log_cmd("getSchema", "Plugins Get attrs", attr_cmd);
        cockpit
                .spawn(attr_cmd, { superuser: true, err: "message" })
                .done(content => {
                    const attrContent = JSON.parse(content);
                    const attrs = [];
                    for (const content of attrContent.items) {
                        attrs.push(content.name[0]);
                    }

                    const oc_cmd = [
                        "dsconf",
                        "-j",
                        "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                        "schema",
                        "objectclasses",
                        "list"
                    ];
                    log_cmd("getSchema", "Get objectClasses", oc_cmd);
                    cockpit
                            .spawn(oc_cmd, { superuser: true, err: "message" })
                            .done(content => {
                                const ocContent = JSON.parse(content);
                                const ocs = [];
                                for (const content of ocContent.items) {
                                    ocs.push(content.name[0]);
                                }
                                this.setState({
                                    objectClasses: ocs,
                                    attributes: attrs,
                                });
                            })
                            .fail(err => {
                                const errMsg = JSON.parse(err);
                                this.props.addNotification("error", `Failed to get objectClasses - ${errMsg.desc}`);
                            });
                });
    }

    onChangeTab(event) {
        this.setState({ currentPluginTab: event.target.value });
    }

    onFieldChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    toggleLoading() {
        this.setState(prevState => ({
            loading: !prevState.loading,
        }));
    }

    pluginList() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "list"
        ];

        log_cmd("pluginList", "Get plugins for table rows", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    var myObject = JSON.parse(content);
                    const pluginTableKey = this.state.pluginTableKey + 1;
                    if (this.state.firstLoad) {
                        this.setState(prevState => ({
                            pluginTabs: {
                                ...prevState.pluginTabs,
                                basicConfig: true
                            },
                            rows: myObject.items,
                            firstLoad: false,
                            pluginTableKey: pluginTableKey,
                        }));
                    } else {
                        this.setState({
                            rows: myObject.items,
                            pluginTableKey: pluginTableKey,
                        });
                    }
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `${errMsg.desc} error during plugin loading`
                    );
                });
    }

    savePlugin(data) {
        let nothingToSetErr = false;
        let basicPluginSuccess = false;
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "set",
            data.name,
            "--type",
            data.type || "delete",
            "--path",
            data.path || "delete",
            "--initfunc",
            data.initfunc || "delete",
            "--id",
            data.id || "delete",
            "--vendor",
            data.vendor || "delete",
            "--version",
            data.version || "delete",
            "--description",
            data.description || "delete",
            "--depends-on-type",
            data.dependsOnType || "delete",
            "--depends-on-named",
            data.dependsOnNamed || "delete",
            "--precedence",
            data.precedence || "delete"
        ];

        if ("enabled" in data) {
            cmd = [...cmd, "--enabled", data.enabled ? "on" : "off"];
        }

        this.toggleLoading();

        log_cmd("savePlugin", "Edit the plugin", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    console.info("savePlugin", "Result", content);
                    basicPluginSuccess = true;
                    this.props.addNotification(
                        "success",
                        `Plugin ${data.name} was successfully modified`
                    );
                    this.pluginList();
                    this.closePluginModal();
                    this.toggleLoading();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (errMsg.desc.indexOf("nothing to set") >= 0) {
                        nothingToSetErr = true;
                    } else {
                        this.props.addNotification(
                            "error",
                            `${errMsg.desc} error during ${data.name} modification`
                        );
                    }
                    this.closePluginModal();
                    this.toggleLoading();
                })
                .always(() => {
                    if ("specificPluginCMD" in data && data.specificPluginCMD.length !== 0) {
                        this.toggleLoading();
                        log_cmd(
                            "savePlugin",
                            "Edit the plugin from the plugin config tab",
                            data.specificPluginCMD
                        );
                        cockpit
                                .spawn(data.specificPluginCMD, {
                                    superuser: true,
                                    err: "message"
                                })
                                .done(content => {
                                    // Notify success only one time
                                    if (!basicPluginSuccess) {
                                        this.props.addNotification(
                                            "success",
                                            `Plugin ${data.name} was successfully modified`
                                        );
                                    }
                                    this.pluginList();
                                    this.toggleLoading();
                                    console.info("savePlugin", "Result", content);
                                })
                                .fail(err => {
                                    const errMsg = JSON.parse(err);
                                    if (
                                        (errMsg.desc.indexOf("nothing to set") >= 0 && nothingToSetErr) ||
                                errMsg.desc.indexOf("nothing to set") < 0
                                    ) {
                                        if (basicPluginSuccess) {
                                            this.props.addNotification(
                                                "success",
                                                `Plugin ${data.name} was successfully modified`
                                            );
                                            this.pluginList();
                                        }
                                        this.props.addNotification(
                                            "error",
                                            `${errMsg.desc} error during ${data.name} modification`
                                        );
                                    }
                                    this.toggleLoading();
                                });
                    } else {
                        this.pluginList();
                    }
                });
    }

    showConfirmToggle(plugin_name, enabled) {
        this.setState({
            showConfirmToggle: true,
            togglePluginName: plugin_name,
            togglePluginEnabled: enabled,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmToggle() {
        this.setState({
            showConfirmToggle: false,
            togglePluginName: "",
            togglePluginEnabled: "",
        });
    }

    togglePlugin() {
        const new_status = this.state.togglePluginEnabled ? "off" : "on";
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "set",
            "--enabled=" + new_status,
            this.state.togglePluginName,
        ];

        this.setState({ modalSpinning: true });
        log_cmd("togglePlugin", "Switch plugin states from the plugin tab", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    console.info("savePlugin", "Result", content);
                    this.pluginList();
                    this.props.addNotification(
                        "warning",
                        `${this.state.togglePluginName} plugin was successfully set to "${new_status}".
                        Please, restart the instance.`
                    );
                    this.setState({
                        modalSpinning: false,
                        showConfirmToggle: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during ${this.state.togglePluginName} plugin modification - ${errMsg.desc}`
                    );
                    // toggleLoadingHandler();
                    this.setState({
                        modalSpinning: false,
                        showConfirmToggle: false
                    });
                });
    }

    getIconAndName(name, plugin_name) {
        const pluginRow = this.state.rows.find(row => row.cn[0] === plugin_name);
        if (pluginRow) {
            if (pluginRow['nsslapd-pluginEnabled'][0] === "on") {
                return <div className="ds-ok-icon"><CheckCircleIcon title="Plugin is enabled" className="ds-icon-sm" />{name}</div>;
            } else {
                return <div className="ds-disabled-icon"><BanIcon title="Plugin is disabled" className="ds-icon-sm" />{name}</div>;
            }
        } else {
            // Might be attribute uniqueness that just has individual entries
            const otherRows = this.state.rows.filter(row => row['nsslapd-pluginId'][0] === "NSUniqueAttr");
            if (otherRows.length > 0) {
                for (const plugin of otherRows) {
                    if (plugin['nsslapd-pluginEnabled'][0] === "on") {
                        return <div className="ds-ok-icon"><CheckCircleIcon title="Plugin is enabled" className="ds-icon-sm" />{name}</div>;
                    }
                }
                return <div className="ds-disabled-icon"><BanIcon title="Plugin is disabled" className="ds-icon-sm" />{name}</div>;
            } else {
                return <div className="ds-disabled-icon"><ResourcesEmptyIcon title="Plugin is not configured" className="ds-icon-sm" />{name}</div>;
            }
        }
    }

    render() {
        const selectPlugins = {
            allPlugins: {
                name: "All Plugins",
                icon: <div><ListIcon className="ds-icon-sm" />All Plugins</div>,
                component: (
                    <PluginTable
                        key={this.state.pluginTableKey}
                        rows={this.state.rows}
                        showConfirmToggle={this.showConfirmToggle}
                        spinning={this.state.modalSpinning}
                    />
                ),
            },
            accountPolicy: {
                name: "Account Policy",
                icon: this.getIconAndName("Account Policy", "Account Policy Plugin"),
                component: (
                    <AccountPolicy
                        key={this.state.rows}
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        attributes={this.state.attributes}
                    />
                )
            },
            attributeUniqueness: {
                name: "Attribute Uniqueness",
                icon: this.getIconAndName("Attribute Uniqueness", "attribute uniqueness"),
                component: (
                    <AttributeUniqueness
                        rows={this.state.rows}
                        key={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        attributes={this.state.attributes}
                        objectClasses={this.state.objectClasses}
                    />
                )
            },
            autoMembership: {
                name: "Auto Membership",
                icon: this.getIconAndName("Auto Membership", "Auto Membership Plugin"),
                component: (
                    <AutoMembership
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        attributes={this.state.attributes}
                        key={this.props.wasActiveList}
                    />
                )
            },
            dna: {
                name: "DNA",
                icon: this.getIconAndName("DNA", "Distributed Numeric Assignment Plugin"),
                component: (
                    <DNAPlugin
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        attributes={this.state.attributes}
                        key={this.props.wasActiveList}
                    />
                )
            },
            linkedAttributes: {
                name: "Linked Attributes",
                icon: this.getIconAndName("Linked Attributes", "Linked Attributes"),
                component: (
                    <LinkedAttributes
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        attributes={this.state.attributes}
                        key={this.props.wasActiveList}
                    />
                )
            },
            managedEntries: {
                name: "Managed Entries",
                icon: this.getIconAndName("Managed Entries", "Managed Entries"),
                component: (
                    <ManagedEntries
                        rows={this.state.rows}
                        key={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        attributes={this.state.attributes}
                        addNotification={this.props.addNotification}
                    />
                )
            },
            memberOf: {
                name: "MemberOf",
                icon: this.getIconAndName("MemberOf", "MemberOf Plugin"),
                component: (
                    <MemberOf
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        objectClasses={this.state.objectClasses}
                        key={this.props.wasActiveList}
                    />
                )
            },
            passthroughAuthentication: {
                name: "LDAP Pass Through Auth",
                icon: this.getIconAndName("LDAP Pass Through Auth", "Pass Through Authentication"),
                component: (
                    <PassthroughAuthentication
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        attributes={this.state.attributes}
                        key={this.props.wasActiveList}
                    />
                )
            },
            pamPassthroughAuthentication: {
                name: "PAM Pass Through Auth",
                icon: this.getIconAndName("PAM Pass Through Auth", "PAM Pass Through Auth"),
                component: (
                    <PAMPassthroughAuthentication
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        attributes={this.state.attributes}
                        key={this.props.wasActiveList}
                    />
                )
            },
            winsync: {
                name: "Posix Winsync",
                icon: this.getIconAndName("Posix Winsync", "Posix Winsync API"),
                component: (
                    <WinSync
                        rows={this.state.rows}
                        key={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        attributes={this.state.attributes}
                        toggleLoadingHandler={this.toggleLoading}
                    />
                )
            },
            referentialIntegrity: {
                name: "Referential Integrity",
                icon: this.getIconAndName("Referential Integrity", "referential integrity postoperation"),
                component: (
                    <ReferentialIntegrity
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        attributes={this.state.attributes}
                        key={this.props.wasActiveList}
                    />
                )
            },
            retroChangelog: {
                name: "Retro Changelog",
                icon: this.getIconAndName("Retro Changelog", "Retro Changelog Plugin"),
                component: (
                    <RetroChangelog
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        attributes={this.state.attributes}
                        key={this.props.wasActiveList}
                    />
                )
            },
            rootDnaAccessControl: {
                name: "RootDN Access Control",
                icon: this.getIconAndName("RootDN Access Control", "RootDN Access Control"),
                component: (
                    <RootDNAccessControl
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        wasActiveList={this.props.wasActiveList}
                        attributes={this.state.attributes}
                        toggleLoadingHandler={this.toggleLoading}
                    />
                )
            },
            usn: {
                name: "USN",
                icon: this.getIconAndName("USN", "USN"),
                component: (
                    <USNPlugin
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                        wasActiveList={this.props.wasActiveList}
                        key={this.props.wasActiveList}
                    />
                )
            }
        };

        return (
            <div className="container-fluid">
                <div className="ds-margin-top-xlg ds-center" hidden={!this.state.firstLoad}>
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Loading Plugins ...
                        </Text>
                    </TextContent>
                    <Spinner className="ds-margin-top-lg" size="xl" />
                </div>
                <div hidden={this.state.firstLoad} className={this.state.loading ? "ds-disabled" : ""}>
                    <Grid className="ds-margin-top-xlg" hasGutter>
                        <GridItem span={3} className="ds-vert-scroll">
                            <Nav key={this.state.pluginTableKey} theme="light" onSelect={this.handleSelect}>
                                <NavList>
                                    {Object.entries(selectPlugins).map(([id, item]) => (
                                        <NavItem key={item.name} itemId={item.name} isActive={this.state.activePlugin === item.name}>
                                            {item.icon}
                                        </NavItem>
                                    ))}
                                </NavList>
                            </Nav>
                        </GridItem>
                        <GridItem className="ds-indent-md" span={9}>
                            {Object.entries(selectPlugins).filter(plugin => plugin[1].name === this.state.activePlugin)
                                    .map(filteredPlugin => (
                                        <div key={filteredPlugin[1].name} className="ds-margin-top">
                                            {filteredPlugin[1].component}
                                        </div>
                                    ))}
                        </GridItem>
                    </Grid>
                    <DoubleConfirmModal
                        showModal={this.state.showConfirmToggle}
                        closeHandler={this.closeConfirmToggle}
                        handleChange={this.onFieldChange}
                        actionHandler={this.togglePlugin}
                        spinning={this.state.modalSpinning}
                        item={this.state.togglePluginName}
                        checked={this.state.modalChecked}
                        mTitle={this.state.togglePluginEnabled ? "Disable Plugin" : "Enable Plugin"}
                        mMsg={this.state.togglePluginEnabled
                            ? "Are you really sure you want to disable this plugin?  Disabling some plugins can cause the server to not start, please use caution."
                            : "Are you sure you want to enable this plugin?"}
                        mSpinningMsg={this.state.togglePluginEnabled
                            ? "Disabling plugin ..." : "Enabling plugin ..."}
                        mBtnName={this.state.togglePluginEnabled
                            ? "Disable Plugin" : "Enable Plugin"}
                    />
                </div>
            </div>
        );
    }
}

Plugins.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string
};

Plugins.defaultProps = {
    serverId: ""
};
