import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "./lib/tools.jsx";
import { TreeView, Spinner, noop } from "patternfly-react";
import PropTypes from "prop-types";
import { ServerSettings } from "./lib/server/settings.jsx";
import { ServerTuning } from "./lib/server/tuning.jsx";
import { ServerSASL } from "./lib/server/sasl.jsx";
import { ServerLDAPI } from "./lib/server/ldapi.jsx";
import { ServerAccessLog } from "./lib/server/accessLog.jsx";
import { ServerAuditLog } from "./lib/server/auditLog.jsx";
import { ServerAuditFailLog } from "./lib/server/auditfailLog.jsx";
import { ServerErrorLog } from "./lib/server/errorLog.jsx";
import { Security } from "./security.jsx";

const treeViewContainerStyles = {
    width: "295px"
};

export class Server extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            nodes: [],
            node_name: "",
            node_text: "",
            attrs: [],
            loaded: false,
            disableTree: false
        };

        this.loadTree = this.loadTree.bind(this);
        this.enableTree = this.enableTree.bind(this);
        this.selectNode = this.selectNode.bind(this);
    }

    componentDidUpdate(prevProps) {
        if (this.props.wasActiveList.includes(1)) {
            if (this.state.firstLoad) {
                this.loadConfig();
            } else {
                if (this.props.serverId !== prevProps.serverId) {
                    this.loadConfig();
                }
            }
        }
    }

    enableTree() {
        this.setState({
            disableTree: false
        });
    }

    loadConfig() {
        this.setState({
            loaded: false,
            firstLoad: false
        });
        let cmd = ["dsconf", "-j", this.props.serverId, "config", "get"];
        log_cmd("loadConfig", "Load server configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    this.setState(
                        {
                            loaded: true,
                            attrs: attrs
                        },
                        this.loadTree()
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.setState({
                        loaded: true
                    });
                    this.props.addNotification(
                        "error",
                        `Error loading server configuration - ${errMsg.desc}`
                    );
                });
    }

    loadTree() {
        let basicData = [
            {
                text: "Server Settings",
                selectable: true,
                selected: true,
                icon: "pficon-settings",
                state: { expanded: true },
                id: "settings-config",
                nodes: []
            },
            {
                text: "Tuning & Limits",
                selectable: true,
                icon: "fa fa-tachometer",
                id: "tuning-config",
                nodes: []
            },
            {
                text: "Security",
                selectable: true,
                icon: "pficon-locked",
                id: "security-config",
                nodes: []
            },
            {
                text: "SASL Settings & Mappings",
                selectable: true,
                icon: "glyphicon glyphicon-map-marker",
                id: "sasl-config",
                nodes: []
            },
            {
                text: "LDAPI & Autobind",
                selectable: true,
                icon: "glyphicon glyphicon-flash",
                id: "ldapi-config",
                nodes: []
            },
            {
                text: "Logging",
                icon: "pficon-catalog",
                selectable: false,
                id: "logging-config",
                state: { expanded: true },
                nodes: [
                    {
                        text: "Access Log",
                        icon: "glyphicon glyphicon-book",
                        selectable: true,
                        id: "access-log-config",
                        type: "log"
                    },
                    {
                        text: "Audit Log",
                        icon: "glyphicon glyphicon-book",
                        selectable: true,
                        id: "audit-log-config",
                        type: "log"
                    },
                    {
                        text: "Audit Failure Log",
                        icon: "glyphicon glyphicon-book",
                        selectable: true,
                        id: "auditfail-log-config",
                        type: "log"
                    },
                    {
                        text: "Errors Log",
                        icon: "glyphicon glyphicon-book",
                        selectable: true,
                        id: "error-log-config",
                        type: "log"
                    }
                ]
            }
        ];
        this.setState({
            nodes: basicData,
            node_name: this.state.node_name
        });
    }

    selectNode(selectedNode) {
        if (selectedNode.selected) {
            return;
        }
        this.setState({
            disableTree: true // Disable the tree to allow node to be fully loaded
        });

        this.setState(prevState => {
            return {
                nodes: this.nodeSelector(prevState.nodes, selectedNode),
                node_name: selectedNode.id,
                node_text: selectedNode.text,
                bename: ""
            };
        });
    }

    nodeSelector(nodes, targetNode) {
        return nodes.map(node => {
            if (node.nodes) {
                return {
                    ...node,
                    nodes: this.nodeSelector(node.nodes, targetNode),
                    selected: node.id === targetNode.id ? !node.selected : false
                };
            } else if (node.id === targetNode.id) {
                return { ...node, selected: !node.selected };
            } else if (node.id !== targetNode.id && node.selected) {
                return { ...node, selected: false };
            } else {
                return node;
            }
        });
    }

    render() {
        const { nodes } = this.state;
        let serverPage = (
            <div className="ds-loading-spinner ds-center">
                <p />
                <h4>Loading server configuration ...</h4>
                <Spinner className="ds-margin-top-lg" loading size="md" />
            </div>
        );

        let server_element = "";
        let disabled = "tree-view-container";
        if (this.state.disableTree) {
            disabled = "tree-view-container ds-disabled";
        }

        if (this.state.loaded) {
            if (this.state.node_name == "settings-config" || this.state.node_name == "") {
                server_element = (
                    <ServerSettings
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                    />
                );
            } else if (this.state.node_name == "tuning-config") {
                server_element = (
                    <ServerTuning
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                    />
                );
            } else if (this.state.node_name == "sasl-config") {
                server_element = (
                    <ServerSASL serverId={this.props.serverId} enableTree={this.enableTree} />
                );
            } else if (this.state.node_name == "security-config") {
                server_element = (
                    <Security
                        addNotification={this.props.addNotification}
                        serverId={this.props.serverId}
                        enableTree={this.enableTree}
                    />
                );
            } else if (this.state.node_name == "ldapi-config") {
                server_element = (
                    <ServerLDAPI
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                    />
                );
            } else if (this.state.node_name == "access-log-config") {
                server_element = (
                    <ServerAccessLog
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                    />
                );
            } else if (this.state.node_name == "audit-log-config") {
                server_element = (
                    <ServerAuditLog
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                    />
                );
            } else if (this.state.node_name == "auditfail-log-config") {
                server_element = (
                    <ServerAuditFailLog
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                    />
                );
            } else if (this.state.node_name == "error-log-config") {
                server_element = (
                    <ServerErrorLog
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                    />
                );
            }

            serverPage = (
                <div className="container-fluid">
                    <div className="ds-container">
                        <div>
                            <div className="ds-tree">
                                <div
                                    className={disabled}
                                    id="server-tree"
                                    style={treeViewContainerStyles}
                                >
                                    <TreeView
                                        nodes={nodes}
                                        highlightOnHover
                                        highlightOnSelect
                                        selectNode={this.selectNode}
                                        key={this.state.node_text}
                                    />
                                </div>
                            </div>
                        </div>
                        <div className="ds-tree-content">{server_element}</div>
                    </div>
                </div>
            );
        }

        return <div>{serverPage}</div>;
    }
}

// Property types and defaults

Server.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string
};

Server.defaultProps = {
    addNotification: noop,
    serverId: ""
};
