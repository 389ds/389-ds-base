import React from "react";
import { noop, FormGroup, FormControl, Row, Col, Form, ControlLabel } from "patternfly-react";
import { Typeahead } from "react-bootstrap-typeahead";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import "../../css/ds.css";

class RootDNAccessControl extends React.Component {
    componentWillMount(prevProps) {
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
            allowHost: [],
            denyHost: [],
            allowIP: [],
            denyIP: [],
            openTime: "",
            closeTime: "",
            daysAllowed: ""
        };

        this.updateFields = this.updateFields.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    updateFields() {
        let allowHostList = [];
        let denyHostList = [];
        let allowIPList = [];
        let denyIPList = [];

        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "RootDN Access Control");
            this.setState({
                openTime:
                    pluginRow["rootdn-open-time"] === undefined
                        ? ""
                        : pluginRow["rootdn-open-time"][0],
                closeTime:
                    pluginRow["rootdn-close-time"] === undefined
                        ? ""
                        : pluginRow["rootdn-close-time"][0],
                daysAllowed:
                    pluginRow["rootdn-days-allowed"] === undefined
                        ? ""
                        : pluginRow["rootdn-days-allowed"][0]
            });

            if (pluginRow["rootdn-allow-host"] === undefined) {
                this.setState({ allowHost: [] });
            } else {
                for (let value of pluginRow["rootdn-allow-host"]) {
                    allowHostList = [...allowHostList, { id: value, label: value }];
                }
                this.setState({ allowHost: allowHostList });
            }
            if (pluginRow["rootdn-deny-host"] === undefined) {
                this.setState({ denyHost: [] });
            } else {
                for (let value of pluginRow["rootdn-deny-host"]) {
                    denyHostList = [...denyHostList, { id: value, label: value }];
                }
                this.setState({ denyHost: denyHostList });
            }
            if (pluginRow["rootdn-allow-ip"] === undefined) {
                this.setState({ allowIP: [] });
            } else {
                for (let value of pluginRow["rootdn-allow-ip"]) {
                    allowIPList = [...allowIPList, { id: value, label: value }];
                }
                this.setState({ allowIP: allowIPList });
            }
            if (pluginRow["rootdn-deny-ip"] === undefined) {
                this.setState({ denyIP: [] });
            } else {
                for (let value of pluginRow["rootdn-deny-ip"]) {
                    denyIPList = [...denyIPList, { id: value, label: value }];
                }
                this.setState({ denyIP: denyIPList });
            }
        }
    }

    render() {
        const {
            allowHost,
            denyHost,
            allowIP,
            denyIP,
            openTime,
            closeTime,
            daysAllowed
        } = this.state;

        let specificPluginCMD = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "root-dn",
            "set",
            "--open-time",
            openTime || "delete",
            "--close-time",
            closeTime || "delete",
            "--days-allowed",
            daysAllowed || "delete"
        ];

        // Delete attributes if the user set an empty value to the field
        specificPluginCMD = [...specificPluginCMD, "--allow-host"];
        if (allowHost.length != 0) {
            for (let value of allowHost) {
                specificPluginCMD = [...specificPluginCMD, value.label];
            }
        } else {
            specificPluginCMD = [...specificPluginCMD, "delete"];
        }
        specificPluginCMD = [...specificPluginCMD, "--deny-host"];
        if (denyHost.length != 0) {
            for (let value of denyHost) {
                specificPluginCMD = [...specificPluginCMD, value.label];
            }
        } else {
            specificPluginCMD = [...specificPluginCMD, "delete"];
        }
        specificPluginCMD = [...specificPluginCMD, "--allow-ip"];
        if (allowIP.length != 0) {
            for (let value of allowIP) {
                specificPluginCMD = [...specificPluginCMD, value.label];
            }
        } else {
            specificPluginCMD = [...specificPluginCMD, "delete"];
        }
        specificPluginCMD = [...specificPluginCMD, "--allow-host"];
        if (allowHost.length != 0) {
            for (let value of allowHost) {
                specificPluginCMD = [...specificPluginCMD, value.label];
            }
        } else {
            specificPluginCMD = [...specificPluginCMD, "delete"];
        }

        return (
            <div>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="RootDN Access Control"
                    pluginName="RootDN Access Control"
                    cmdName="root-dn"
                    specificPluginCMD={specificPluginCMD}
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup key="allowHost" controlId="allowHost">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Sets what hosts, by fully-qualified domain name, the root user is allowed to use to access the Directory Server. Any hosts not listed are implicitly denied (rootdn-allow-host)"
                                    >
                                        Allow Host
                                    </Col>
                                    <Col sm={6}>
                                        <Typeahead
                                            allowNew
                                            multiple
                                            onChange={value => {
                                                this.setState({
                                                    allowHost: value
                                                });
                                            }}
                                            selected={allowHost}
                                            options={[]}
                                            newSelectionPrefix="Add a host to allow: "
                                            placeholder="Type a hostname (wild cards are allowed)..."
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="denyHost" controlId="denyHost">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Sets what hosts, by fully-qualified domain name, the root user is not allowed to use to access the Directory Server Any hosts not listed are implicitly allowed (rootdn-deny-host). If an host address is listed in both the rootdn-allow-host and rootdn-deny-host attributes, it is denied access."
                                    >
                                        Deny Host
                                    </Col>
                                    <Col sm={6}>
                                        <Typeahead
                                            allowNew
                                            multiple
                                            onChange={value => {
                                                this.setState({
                                                    denyHost: value
                                                });
                                            }}
                                            selected={denyHost}
                                            options={[]}
                                            newSelectionPrefix="Add a host to deny: "
                                            placeholder="Type a hostname (wild cards are allowed)..."
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="allowIP" controlId="allowIP">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Sets what IP addresses, either IPv4 or IPv6, for machines the root user is allowed to use to access the Directory Server Any IP addresses not listed are implicitly denied (rootdn-allow-ip)"
                                    >
                                        Allow IP address
                                    </Col>
                                    <Col sm={6}>
                                        <Typeahead
                                            allowNew
                                            multiple
                                            onChange={value => {
                                                this.setState({
                                                    allowIP: value
                                                });
                                            }}
                                            selected={allowIP}
                                            options={[]}
                                            newSelectionPrefix="Add an IP address to allow: "
                                            placeholder="Type an IP address (wild cards are allowed)..."
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="denyIP" controlId="denyIP">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Sets what IP addresses, either IPv4 or IPv6, for machines the root user is not allowed to use to access the Directory Server. Any IP addresses not listed are implicitly allowed (rootdn-deny-ip) If an IP address is listed in both the rootdn-allow-ip and rootdn-deny-ip attributes, it is denied access."
                                    >
                                        Deny IP address
                                    </Col>
                                    <Col sm={6}>
                                        <Typeahead
                                            allowNew
                                            multiple
                                            onChange={value => {
                                                this.setState({
                                                    denyIP: value
                                                });
                                            }}
                                            selected={denyIP}
                                            options={[]}
                                            newSelectionPrefix="Add an IP address to deny: "
                                            placeholder="Type an IP address (wild cards are allowed)..."
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="openTime" controlId="openTime">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Sets part of a time period or range when the root user is allowed to access the Directory Server. This sets when the time-based access begins (rootdn-open-time)"
                                    >
                                        Open Time
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={openTime}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="closeTime" controlId="closeTime">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Sets part of a time period or range when the root user is allowed to access the Directory Server. This sets when the time-based access ends (rootdn-close-time)"
                                    >
                                        Close Time
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={closeTime}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="daysAllowed" controlId="daysAllowed">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Gives a comma-separated list of what days the root user is allowed to use to access the Directory Server. Any days listed are implicitly denied (rootdn-days-allowed)"
                                    >
                                        Days Allowed
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={daysAllowed}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
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
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default RootDNAccessControl;
