import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "./lib/tools.jsx";
import PropTypes from "prop-types";
import { ServerSettings } from "./lib/server/settings.jsx";
import { ServerTuning } from "./lib/server/tuning.jsx";
import { ServerSASL } from "./lib/server/sasl.jsx";
import { ServerLDAPI } from "./lib/server/ldapi.jsx";
import { ServerAccessLog } from "./lib/server/accessLog.jsx";
import { ServerAuditLog } from "./lib/server/auditLog.jsx";
import { ServerAuditFailLog } from "./lib/server/auditfailLog.jsx";
import { ServerErrorLog } from "./lib/server/errorLog.jsx";
import { ServerSecurityLog } from "./lib/server/securityLog.jsx";
import { Security } from "./security.jsx";
import {
    Spinner,
    TreeView,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faBook,
} from '@fortawesome/free-solid-svg-icons';
import {
    CatalogIcon,
    CogIcon,
    KeyIcon,
    TachometerAltIcon,
    LockIcon,
    RouteIcon
} from '@patternfly/react-icons';

export class Server extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            nodes: [],
            node_name: "settings-config",
            node_text: "",
            attrs: [],
            loaded: false,
            disableTree: false,
            activeItems: [
                {
                    name: "Server Settings",
                    id: "settings-config",
                    icon: <CogIcon />,
                }
            ],
        };

        this.loadTree = this.loadTree.bind(this);
        this.reloadConfig = this.reloadConfig.bind(this);
        this.enableTree = this.enableTree.bind(this);
        this.handleTreeClick = this.handleTreeClick.bind(this);
    }

    componentDidUpdate() {
        if (this.props.wasActiveList.includes(1)) {
            if (this.state.firstLoad) {
                this.loadConfig();
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
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("loadConfig", "Load server configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    this.setState(
                        {
                            loaded: true,
                            attrs: attrs
                        },
                        this.loadTree()
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.setState({
                        loaded: true
                    });
                    this.props.addNotification(
                        "error",
                        `Error loading server configuration - ${errMsg.desc}`
                    );
                });
    }

    reloadConfig() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("reloadConfig", "Reload server configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    this.setState({
                        attrs: attrs
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error reloading server configuration - ${errMsg.desc}`
                    );
                });
    }

    loadTree() {
        const basicData = [
            {
                name: "Server Settings",
                id: "settings-config",
                icon: <CogIcon />,

            },
            {
                name: "Tuning & Limits",
                icon: <TachometerAltIcon />,
                id: "tuning-config",
            },
            {
                name: "Security",
                icon: <LockIcon />,
                id: "security-config",
            },
            {
                name: "SASL Settings & Mappings",
                icon: <RouteIcon />,
                id: "sasl-config",
            },
            {
                name: "LDAPI & Autobind",
                icon: <KeyIcon />,
                id: "ldapi-config",
            },
            {
                name: "Logging",
                icon: <CatalogIcon />,
                id: "logging-config",
                children: [
                    {
                        name: "Access Log",
                        icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                        id: "access-log-config",
                    },
                    {
                        name: "Audit Log",
                        icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                        id: "audit-log-config",
                    },
                    {
                        name: "Audit Failure Log",
                        icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                        id: "auditfail-log-config",
                    },
                    {
                        name: "Errors Log",
                        icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                        id: "error-log-config",
                    },
                    {
                        name: "Security Log",
                        icon: <FontAwesomeIcon size="sm" icon={faBook} />,
                        id: "security-log-config",
                    }
                ],
                defaultExpanded: true
            }
        ];
        this.setState({
            nodes: basicData,
            node_name: this.state.node_name
        });
    }

    handleTreeClick(evt, treeViewItem, parentItem) {
        if (treeViewItem.id !== "logging-config" && treeViewItem.id !== this.state.node_name) {
            this.setState({
                activeItems: [treeViewItem, parentItem],
                node_name: treeViewItem.id,
                disableTree: true // Disable the tree to allow node to be fully loaded
            });
        }
    }

    render() {
        const { nodes } = this.state;
        let serverPage = (
            <div className="ds-margin-top-xlg ds-center">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Loading Server Configuration ...
                    </Text>
                </TextContent>
                <Spinner className="ds-margin-top-lg" size="xl" />
            </div>
        );

        let server_element = "";
        let disabled = "tree-view-container";
        if (this.state.disableTree) {
            disabled = "tree-view-container ds-disabled";
        }

        if (this.state.loaded) {
            if (this.state.node_name === "settings-config" || this.state.node_name === "") {
                server_element = (
                    <ServerSettings
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        version={this.props.version}
                        enableTree={this.enableTree}
                        addNotification={this.props.addNotification}
                    />
                );
            } else if (this.state.node_name === "tuning-config") {
                server_element = (
                    <ServerTuning
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                        addNotification={this.props.addNotification}
                    />
                );
            } else if (this.state.node_name === "sasl-config") {
                server_element = (
                    <ServerSASL
                        serverId={this.props.serverId}
                        enableTree={this.enableTree}
                        addNotification={this.props.addNotification}
                    />
                );
            } else if (this.state.node_name === "security-config") {
                server_element = (
                    <Security
                        addNotification={this.props.addNotification}
                        serverId={this.props.serverId}
                        enableTree={this.enableTree}
                        certDir={this.state.attrs['nsslapd-certdir']}
                    />
                );
            } else if (this.state.node_name === "ldapi-config") {
                server_element = (
                    <ServerLDAPI
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                        addNotification={this.props.addNotification}
                    />
                );
            } else if (this.state.node_name === "access-log-config") {
                server_element = (
                    <ServerAccessLog
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                        addNotification={this.props.addNotification}
                    />
                );
            } else if (this.state.node_name === "audit-log-config") {
                server_element = (
                    <ServerAuditLog
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                        addNotification={this.props.addNotification}
                    />
                );
            } else if (this.state.node_name === "auditfail-log-config") {
                server_element = (
                    <ServerAuditFailLog
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                        addNotification={this.props.addNotification}
                    />
                );
            } else if (this.state.node_name === "error-log-config") {
                server_element = (
                    <ServerErrorLog
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                        addNotification={this.props.addNotification}
                    />
                );
            } else if (this.state.node_name === "security-log-config") {
                server_element = (
                    <ServerSecurityLog
                        serverId={this.props.serverId}
                        attrs={this.state.attrs}
                        enableTree={this.enableTree}
                        addNotification={this.props.addNotification}
                    />
                );
            }

            serverPage = (
                <div className="container-fluid">
                    <div className="ds-container">
                        <div className="ds-tree">
                            <div
                                className={disabled}
                                id="server-tree"
                            >
                                <TreeView
                                    data={nodes}
                                    activeItems={this.state.activeItems}
                                    onSelect={this.handleTreeClick}
                                />
                            </div>
                        </div>
                        <div className="ds-tree-content">
                            {server_element}
                        </div>
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
    serverId: ""
};
