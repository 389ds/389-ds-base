import cockpit from "cockpit";
import React from "react";
import {
    Icon,
    Modal,
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
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

class USN extends React.Component {
    componentWillMount() {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.updateSwitch();
            }
        }
    }

    constructor(props) {
        super(props);

        this.runCleanup = this.runCleanup.bind(this);
        this.toggleCleanupModal = this.toggleCleanupModal.bind(this);
        this.updateSwitch = this.updateSwitch.bind(this);
        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);

        this.state = {
            firstLoad: true,
            globalMode: false,
            disableSwitch: false,
            cleanupModalShow: false,
            cleanupSuffix: "",
            cleanupBackend: "",
            cleanupMaxUSN: ""
        };
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    handleSwitchChange(value) {
        const { serverId, addNotification, toggleLoadingHandler } = this.props;
        const new_status = this.state.globalMode ? "off" : "on";
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "plugin",
            "usn",
            "global",
            new_status
        ];

        toggleLoadingHandler();
        this.setState({ disableSwitch: true });
        log_cmd("handleSwitchChange", "Switch global USN mode from the USN plugin tab", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    console.info("savePlugin", "Result", content);
                    this.updateSwitch();
                    addNotification(
                        "success",
                        `Global USN mode was successfully set to ${new_status}.`
                    );
                    toggleLoadingHandler();
                    this.setState({ disableSwitch: false });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    addNotification(
                        "error",
                        `Error during global USN mode modification - ${errMsg.desc}`
                    );
                    toggleLoadingHandler();
                    this.setState({ disableSwitch: false });
                });
    }

    updateSwitch() {
        this.setState({
            firstLoad: false,
            disableSwitch: true
        });
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config",
            "get",
            "nsslapd-entryusn-global"
        ];
        this.props.toggleLoadingHandler();
        log_cmd("updateSwitch", "Get global USN status", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    let usnGlobalAttr = myObject.attrs["nsslapd-entryusn-global"][0];
                    this.setState({
                        globalMode: !(usnGlobalAttr == "off")
                    });
                    this.setState({
                        disableSwitch: false
                    });
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    if (err != 0) {
                        let errMsg = JSON.parse(err);
                        console.log("Get global USN failed", errMsg.desc);
                    }
                    this.setState({
                        disableSwitch: false
                    });
                    this.props.toggleLoadingHandler();
                });
    }

    toggleCleanupModal() {
        this.setState(prevState => ({
            cleanupModalShow: !prevState.cleanupModalShow,
            cleanupSuffix: "",
            cleanupBackend: "",
            cleanupMaxUSN: ""
        }));
    }

    runCleanup() {
        if (!this.state.cleanupSuffix && !this.state.cleanupBackend) {
            this.props.addNotification("warning", "Suffix or backend name is required.");
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "usn",
                "cleanup"
            ];

            if (this.state.cleanupSuffix) {
                cmd = [...cmd, "--suffix", this.state.cleanupSuffix];
            }
            if (this.state.cleanupBackend) {
                cmd = [...cmd, "--backend", this.state.cleanupBackend];
            }
            if (this.state.cleanupMaxUSN) {
                cmd = [...cmd, "--max-usn", this.state.cleanupMaxUSN];
            }

            this.props.toggleLoadingHandler();
            log_cmd("runCleanup", "Run cleanup USN tombstones", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        this.props.addNotification(
                            "success",
                            `Cleanup USN Tombstones task was successfull`
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            cleanupModalShow: false
                        });
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Cleanup USN Tombstones task has failed ${errMsg.desc}`
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            cleanupModalShow: false
                        });
                    });
        }
    }

    render() {
        const {
            globalMode,
            disableSwitch,
            cleanupModalShow,
            cleanupSuffix,
            cleanupBackend,
            cleanupMaxUSN
        } = this.state;

        return (
            <div>
                <Modal show={cleanupModalShow} onHide={this.toggleCleanupModal}>
                    <div className="ds-no-horizontal-scrollbar">
                        <Modal.Header>
                            <button
                                className="close"
                                onClick={this.toggleCleanupModal}
                                aria-hidden="true"
                                aria-label="Close"
                            >
                                <Icon type="pf" name="close" />
                            </button>
                            <Modal.Title>Fixup MemberOf Task</Modal.Title>
                        </Modal.Header>
                        <Modal.Body>
                            <Row>
                                <Col sm={12}>
                                    <Form horizontal>
                                        <FormGroup controlId="cleanupSuffix" key="cleanupSuffix">
                                            <Col sm={3}>
                                                <ControlLabel title="Gives the suffix or subtree in the Directory Server to run the cleanup operation against">
                                                    Cleanup Suffix
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={cleanupSuffix}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup controlId="cleanupBackend" key="cleanupBackend">
                                            <Col sm={3}>
                                                <ControlLabel title="Gives the Directory Server instance back end, or database, to run the cleanup operation against">
                                                    Cleanup Backend
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={cleanupBackend}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                        <FormGroup controlId="cleanupMaxUSN" key="cleanupMaxUSN">
                                            <Col sm={3}>
                                                <ControlLabel title="Gives the highest USN value to delete when removing tombstone entries. All tombstone entries up to and including that number are deleted. Tombstone entries with higher USN values (that means more recent entries) are not deleted">
                                                    Cleanup Max USN
                                                </ControlLabel>
                                            </Col>
                                            <Col sm={9}>
                                                <FormControl
                                                    type="text"
                                                    value={cleanupMaxUSN}
                                                    onChange={this.handleFieldChange}
                                                />
                                            </Col>
                                        </FormGroup>
                                    </Form>
                                </Col>
                            </Row>
                        </Modal.Body>
                        <Modal.Footer>
                            <Button
                                bsStyle="default"
                                className="btn-cancel"
                                onClick={this.toggleCleanupModal}
                            >
                                Cancel
                            </Button>
                            <Button bsStyle="primary" onClick={this.runCleanup}>
                                Run
                            </Button>
                        </Modal.Footer>
                    </div>
                </Modal>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="USN"
                    pluginName="Update Sequence Numbers"
                    cmdName="usn"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={9}>
                            <FormGroup key="globalMode" controlId="globalMode">
                                <Col
                                    componentClass={ControlLabel}
                                    sm={3}
                                    title="Defines if the USN plug-in assigns unique update sequence numbers (USN) across all back end databases or to each database individually"
                                >
                                    USN Global
                                </Col>
                                <Col sm={3}>
                                    <Switch
                                        bsSize="normal"
                                        title="normal"
                                        id="bsSize-example"
                                        value={globalMode}
                                        onChange={() => this.handleSwitchChange(globalMode)}
                                        animate={false}
                                        disabled={disableSwitch}
                                    />
                                </Col>
                            </FormGroup>
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={9}>
                            <Button
                                className="ds-margin-top"
                                bsStyle="primary"
                                onClick={this.toggleCleanupModal}
                            >
                                Run Fixup Task
                            </Button>
                        </Col>
                    </Row>
                </PluginBasicConfig>
            </div>
        );
    }
}

USN.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

USN.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default USN;
