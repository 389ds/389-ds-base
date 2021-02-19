import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { log_cmd } from "./lib/tools.jsx";
import { Col, Row, Tab, Nav, NavItem, noop } from "patternfly-react";
import { Spinner } from "@patternfly/react-core";
import PluginViewModal from "./lib/plugins/pluginModal.jsx";
import { PluginTable } from "./lib/plugins/pluginTables.jsx";
import AccountPolicy from "./lib/plugins/accountPolicy.jsx";
import AttributeUniqueness from "./lib/plugins/attributeUniqueness.jsx";
import AutoMembership from "./lib/plugins/autoMembership.jsx";
import DNA from "./lib/plugins/dna.jsx";
import LinkedAttributes from "./lib/plugins/linkedAttributes.jsx";
import ManagedEntries from "./lib/plugins/managedEntries.jsx";
import MemberOf from "./lib/plugins/memberOf.jsx";
import PassthroughAuthentication from "./lib/plugins/passthroughAuthentication.jsx";
import ReferentialIntegrity from "./lib/plugins/referentialIntegrity.jsx";
import RetroChangelog from "./lib/plugins/retroChangelog.jsx";
import RootDNAccessControl from "./lib/plugins/rootDNAccessControl.jsx";
import USN from "./lib/plugins/usn.jsx";
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

    constructor() {
        super();

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.openPluginModal = this.openPluginModal.bind(this);
        this.closePluginModal = this.closePluginModal.bind(this);
        this.pluginList = this.pluginList.bind(this);
        this.onChangeTab = this.onChangeTab.bind(this);
        this.savePlugin = this.savePlugin.bind(this);
        this.toggleLoading = this.toggleLoading.bind(this);

        this.state = {
            firstLoad: true,
            loading: false,
            showPluginModal: false,
            currentPluginTab: "",
            rows: [],

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
    }

    onChangeTab(event) {
        this.setState({ currentPluginTab: event.target.value });
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    handleSwitchChange(value) {
        this.setState({
            currentPluginEnabled: !value
        });
    }

    closePluginModal() {
        this.setState({
            showPluginModal: false
        });
    }

    toggleLoading() {
        this.setState(prevState => ({
            loading: !prevState.loading,
        }));
    }

    openPluginModal(rowData) {
        let pluginEnabled;
        if (rowData["nsslapd-pluginEnabled"][0] === "on") {
            pluginEnabled = true;
        } else if (rowData["nsslapd-pluginEnabled"][0] === "off") {
            pluginEnabled = false;
        } else {
            console.error(
                "openPluginModal failed",
                "wrong nsslapd-pluginenabled attribute value",
                rowData["nsslapd-pluginEnabled"][0]
            );
        }
        this.setState({
            currentPluginName: rowData.cn[0],
            currentPluginType: rowData["nsslapd-pluginType"][0],
            currentPluginEnabled: pluginEnabled,
            currentPluginPath: rowData["nsslapd-pluginPath"][0],
            currentPluginInitfunc: rowData["nsslapd-pluginInitfunc"][0],
            currentPluginId: rowData["nsslapd-pluginId"][0],
            currentPluginVendor: rowData["nsslapd-pluginVendor"][0],
            currentPluginVersion: rowData["nsslapd-pluginVersion"][0],
            currentPluginDescription: rowData["nsslapd-pluginDescription"][0],
            currentPluginDependsOnType:
                rowData["nsslapd-plugin-depends-on-type"] === undefined
                    ? ""
                    : rowData["nsslapd-plugin-depends-on-type"][0],
            currentPluginDependsOnNamed:
                rowData["nsslapd-plugin-depends-on-named"] === undefined
                    ? ""
                    : rowData["nsslapd-plugin-depends-on-named"][0],
            currentPluginPrecedence:
                rowData["nsslapd-pluginprecedence"] === undefined
                    ? ""
                    : rowData["nsslapd-pluginprecedence"][0],
            showPluginModal: true
        });
    }

    pluginList() {
        let cmd = [
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
                    if (this.state.firstLoad) {
                        this.setState(prevState => ({
                            pluginTabs: {
                                ...prevState.pluginTabs,
                                basicConfig: true
                            },
                            rows: myObject.items,
                            firstLoad: false,
                        }));
                    } else {
                        this.setState({
                            rows: myObject.items
                        });
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
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
                    let errMsg = JSON.parse(err);
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
                    if ("specificPluginCMD" in data && data.specificPluginCMD.length != 0) {
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
                                    let errMsg = JSON.parse(err);
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

    render() {
        const selectPlugins = {
            allPlugins: {
                name: "All Plugins",
                component: (
                    <PluginTable rows={this.state.rows} loadModalHandler={this.openPluginModal} />
                )
            },
            accountPolicy: {
                name: "Account Policy",
                component: (
                    <AccountPolicy
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                    />
                )
            },
            attributeUniqueness: {
                name: "Attribute Uniqueness",
                component: (
                    <AttributeUniqueness
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
            },
            linkedAttributes: {
                name: "Linked Attributes",
                component: (
                    <LinkedAttributes
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
            },
            dna: {
                name: "DNA",
                component: (
                    <DNA
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        wasActiveList={this.props.wasActiveList}
                        key={this.props.wasActiveList}
                    />
                )
            },
            autoMembership: {
                name: "Auto Membership",
                component: (
                    <AutoMembership
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
            },
            memberOf: {
                name: "MemberOf",
                component: (
                    <MemberOf
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
            },
            managedEntries: {
                name: "Managed Entries",
                component: (
                    <ManagedEntries
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                    />
                )
            },
            passthroughAuthentication: {
                name: "Passthrough Authentication",
                component: (
                    <PassthroughAuthentication
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
            },
            winsync: {
                name: "Posix Winsync",
                component: (
                    <WinSync
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                    />
                )
            },
            referentialIntegrity: {
                name: "Referential Integrity",
                component: (
                    <ReferentialIntegrity
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
            },
            retroChangelog: {
                name: "Retro Changelog",
                component: (
                    <RetroChangelog
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
            },
            rootDnaAccessControl: {
                name: "RootDN Access Control",
                component: (
                    <RootDNAccessControl
                        rows={this.state.rows}
                        serverId={this.props.serverId}
                        savePluginHandler={this.savePlugin}
                        pluginListHandler={this.pluginList}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                    />
                )
            },
            usn: {
                name: "USN",
                component: (
                    <USN
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
                <div className="ds-margin-top-lg ds-center" hidden={!this.state.firstLoad}>
                    <h4>Loading Plugins ...</h4>
                    <Spinner className="ds-margin-top-lg" size="xl" />
                </div>
                <div hidden={this.state.firstLoad}>
                    <Row className="clearfix" hidden={!this.state.loading}>
                        <Col sm={12}>
                            <Spinner
                                className="ds-float-left ds-plugin-spinner"
                                size="md"
                            />
                        </Col>
                    </Row>
                    <Tab.Container
                        id="left-tabs-example"
                        defaultActiveKey={Object.keys(selectPlugins)[0]}
                    >
                        <Row className="clearfix">
                            <Col sm={3}>
                                <Nav bsStyle="pills" stacked>
                                    {Object.entries(selectPlugins).map(([id, item]) => (
                                        <NavItem key={id} eventKey={id}>
                                            {item.name}
                                        </NavItem>
                                    ))}
                                </Nav>
                            </Col>
                            <Col sm={9}>
                                <Tab.Content animation={false}>
                                    {Object.entries(selectPlugins).map(([id, item]) => (
                                        <Tab.Pane key={id} eventKey={id}>
                                            {item.component}
                                        </Tab.Pane>
                                    ))}
                                </Tab.Content>
                            </Col>
                        </Row>
                    </Tab.Container>
                    <PluginViewModal
                        handleChange={this.handleFieldChange}
                        handleSwitchChange={this.handleSwitchChange}
                        pluginData={{
                            currentPluginName: this.state.currentPluginName,
                            currentPluginType: this.state.currentPluginType,
                            currentPluginEnabled: this.state.currentPluginEnabled,
                            currentPluginPath: this.state.currentPluginPath,
                            currentPluginInitfunc: this.state.currentPluginInitfunc,
                            currentPluginId: this.state.currentPluginId,
                            currentPluginVendor: this.state.currentPluginVendor,
                            currentPluginVersion: this.state.currentPluginVersion,
                            currentPluginDescription: this.state.currentPluginDescription,
                            currentPluginDependsOnType: this.state.currentPluginDependsOnType,
                            currentPluginDependsOnNamed: this.state.currentPluginDependsOnNamed,
                            currentPluginPrecedence: this.state.currentPluginPrecedence
                        }}
                        closeHandler={this.closePluginModal}
                        showModal={this.state.showPluginModal}
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
    addNotification: noop,
    serverId: ""
};
