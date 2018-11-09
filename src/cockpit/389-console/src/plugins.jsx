import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { log_cmd } from "./lib/tools.jsx";
import PluginEditModal from "./lib/plugins/pluginModal.jsx";
import PluginTable from "./lib/plugins/pluginTable.jsx";
import NotificationController from "./lib/notifications.jsx";
import "./css/ds.css";

var cmd;

export class Plugins extends React.Component {
    componentWillMount() {
        this.pluginList();
    }

    componentDidUpdate(prevProps) {
        if (this.props.serverId !== prevProps.serverId) {
            this.pluginList();
        }
    }

    constructor() {
        super();

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.openPluginModal = this.openPluginModal.bind(this);
        this.closePluginModal = this.closePluginModal.bind(this);
        this.pluginList = this.pluginList.bind(this);
        this.removeNotification = this.removeNotification.bind(this);
        this.addNotification = this.addNotification.bind(this);

        this.state = {
            notifications: [],
            loading: false,
            showPluginModal: false,
            // Sort the first column in an ascending way by default.
            // rows and row selection state
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
            currentPluginDescription: ""
        };
    }

    addNotification(type, message, timerdelay, persistent) {
        this.setState(prevState => ({
            notifications: [
                ...prevState.notifications,
                {
                    key: prevState.notifications.length + 1,
                    type: type,
                    persistent: persistent,
                    timerdelay: timerdelay,
                    message: message
                }
            ]
        }));
    }

    removeNotification(notificationToRemove) {
        this.setState({
            notifications: this.state.notifications.filter(
                notification => notificationToRemove.key !== notification.key
            )
        });
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

    openPluginModal(rowData) {
        var pluginEnabled;
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
            showPluginModal: true
        });
    }

    pluginList() {
        this.setState({
            loading: true
        });
        cmd = [
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
                    this.setState({
                        rows: myObject.items,
                        loading: false
                    });
                })
                .fail(err => {
                    if (err != 0) {
                        console.error("pluginList failed", err);
                    }
                    this.setState({
                        loading: false
                    });
                });
    }

    render() {
        return (
            <div className="container-fluid">
                <NotificationController
                    notifications={this.state.notifications}
                    removeNotificationAction={this.removeNotification}
                />
                <h2>Plugins</h2>
                <PluginEditModal
                    addNotification={this.addNotification}
                    handleChange={this.handleFieldChange}
                    handleSwitchChange={this.handleSwitchChange}
                    currentPluginName={this.state.currentPluginName}
                    currentPluginType={this.state.currentPluginType}
                    currentPluginEnabled={this.state.currentPluginEnabled}
                    currentPluginPath={this.state.currentPluginPath}
                    currentPluginInitfunc={this.state.currentPluginInitfunc}
                    currentPluginId={this.state.currentPluginId}
                    currentPluginVendor={this.state.currentPluginVendor}
                    currentPluginVersion={this.state.currentPluginVersion}
                    currentPluginDescription={
                        this.state.currentPluginDescription
                    }
                    serverId={this.props.serverId}
                    closeHandler={this.closePluginModal}
                    pluginListHandler={this.pluginList}
                    showModal={this.state.showPluginModal}
                />
                <PluginTable
                    rows={this.state.rows}
                    loadModalHandler={this.openPluginModal}
                    loading={this.state.loading}
                />
            </div>
        );
    }
}

Plugins.propTypes = {
    serverId: PropTypes.string
};

Plugins.defaultProps = {
    serverId: ""
};
