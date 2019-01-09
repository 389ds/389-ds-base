import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Row,
    Col,
    Form,
    Switch,
    noop,
    FormGroup,
    FormControl,
    ControlLabel
} from "patternfly-react";
import PropTypes from "prop-types";
import CustomCollapse from "../customCollapse.jsx";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

class PluginBasicConfig extends React.Component {
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

        this.updateFields = this.updateFields.bind(this);
        this.updateSwitch = this.updateSwitch.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);

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
            currentPluginDependsOnNamed: ""
        };
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    handleSwitchChange(value) {
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
                    addNotification(
                        "success",
                        `${pluginName} plugin was successfully ${new_status}d.
                        Please, restart the instance.`
                    );
                    toggleLoadingHandler();
                })
                .fail(err => {
                    addNotification(
                        "error",
                        `Error during ${pluginName} plugin modification - ${err}`
                    );
                    toggleLoadingHandler();
                    this.setState({ disableSwitch: false });
                });
    }

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === this.props.cn);

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
                        : pluginRow["nsslapd-plugin-depends-on-named"][0]
            });
        }
        this.updateSwitch();
    }

    updateSwitch() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === this.props.cn);

            var pluginEnabled;
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

    render() {
        const {
            currentPluginEnabled,
            currentPluginType,
            currentPluginPath,
            currentPluginInitfunc,
            currentPluginId,
            currentPluginVendor,
            currentPluginVersion,
            currentPluginDescription,
            currentPluginDependsOnType,
            currentPluginDependsOnNamed,
            disableSwitch
        } = this.state;

        const modalFieldsCol1 = {
            currentPluginType: this.state.currentPluginType,
            currentPluginPath: this.state.currentPluginPath,
            currentPluginInitfunc: this.state.currentPluginInitfunc
        };
        const modalFieldsCol2 = {
            currentPluginVendor: this.state.currentPluginVendor,
            currentPluginVersion: this.state.currentPluginVersion,
            currentPluginDescription: this.state.currentPluginDescription,
            currentPluginId: this.state.currentPluginId
        };
        return (
            <div>
                <Form inline>
                    <Row>
                        <Col sm={6}>
                            <h3>
                                <ControlLabel className="ds-plugin-tab-header">
                                    {this.props.pluginName}
                                </ControlLabel>
                            </h3>
                        </Col>
                        <Col smOffset={1} sm={3}>
                            <FormGroup key="switchPluginStatus" controlId="switchPluginStatus">
                                <ControlLabel
                                    className="toolbar-pf-find ds-float-left ds-right-indent"
                                >
                                    Status
                                </ControlLabel>
                                <Switch
                                    bsSize="normal"
                                    title="normal"
                                    id="bsSize-example"
                                    value={currentPluginEnabled}
                                    onChange={() => this.handleSwitchChange(currentPluginEnabled)}
                                    animate={false}
                                    disabled={disableSwitch}
                                />
                            </FormGroup>
                        </Col>
                    </Row>
                </Form>
                {this.props.children}
                <CustomCollapse>
                    <Row>
                        <Col sm={4}>
                            <Form horizontal>
                                {Object.entries(modalFieldsCol1).map(([id, value]) => (
                                    <FormGroup key={id} controlId={id} disabled={false}>
                                        <Col componentClass={ControlLabel} sm={6}>
                                            {this.props.memberOfAttr} Plugin{" "}
                                            {id.replace("currentPlugin", "")}
                                        </Col>
                                        <Col sm={6}>
                                            <FormControl
                                                type="text"
                                                value={value}
                                                onChange={this.handleFieldChange}
                                            />
                                        </Col>
                                    </FormGroup>
                                ))}
                                <FormGroup
                                    key="currentPluginDependsOnType"
                                    controlId="currentPluginDependsOnType"
                                    disabled={false}
                                >
                                    <Col componentClass={ControlLabel} sm={6}>
                                        Plugin Depends On Type
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={this.state.currentPluginDependsOnType}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="currentPluginDependsOnNamed"
                                    controlId="currentPluginDependsOnNamed"
                                    disabled={false}
                                >
                                    <Col componentClass={ControlLabel} sm={6}>
                                        Plugin Depends On Named
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={this.state.currentPluginDependsOnNamed}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                        <Col sm={4}>
                            <Form horizontal>
                                {Object.entries(modalFieldsCol2).map(([id, value]) => (
                                    <FormGroup key={id} controlId={id} disabled={false}>
                                        <Col componentClass={ControlLabel} sm={5}>
                                            Plugin {id.replace("currentPlugin", "")}
                                        </Col>
                                        <Col sm={7}>
                                            <FormControl
                                                type="text"
                                                value={value}
                                                onChange={this.handleFieldChange}
                                            />
                                        </Col>
                                    </FormGroup>
                                ))}
                            </Form>
                        </Col>
                    </Row>
                </CustomCollapse>
                <Row>
                    <Col smOffset={7} sm={1}>
                        <Button
                            bsSize="large"
                            bsStyle="primary"
                            onClick={() =>
                                this.props.savePluginHandler({
                                    name: this.props.cn,
                                    type: currentPluginType,
                                    path: currentPluginPath,
                                    initfunc: currentPluginInitfunc,
                                    id: currentPluginId,
                                    vendor: currentPluginVendor,
                                    version: currentPluginVersion,
                                    description: currentPluginDescription,
                                    dependsOnType: currentPluginDependsOnType,
                                    dependsOnNamed: currentPluginDependsOnNamed,
                                    specificPluginCMD: this.props.specificPluginCMD
                                })
                            }
                        >
                            Save Config
                        </Button>
                    </Col>
                </Row>
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
    specificPluginCMD: PropTypes.array,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

PluginBasicConfig.defaultProps = {
    rows: [],
    serverId: "",
    cn: "",
    pluginName: "",
    cmdName: "",
    specificPluginCMD: [],
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default PluginBasicConfig;
