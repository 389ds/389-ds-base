import cockpit from "cockpit";
import React from "react";
import {
    Modal,
    Icon,
    Button,
    Switch,
    noop,
    Form,
    FormControl,
    FormGroup,
    ControlLabel,
    Col
} from "patternfly-react";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

var cmd;

class PluginEditModal extends React.Component {
    constructor(props) {
        super(props);
        this.savePlugin = this.savePlugin.bind(this);
    }

    savePlugin() {
        const {
            currentPluginName,
            currentPluginType,
            currentPluginEnabled,
            currentPluginPath,
            currentPluginInitfunc,
            currentPluginId,
            currentPluginVendor,
            currentPluginVersion,
            currentPluginDescription,
            pluginListHandler,
            closeHandler,
            addNotification
        } = this.props;
        cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "edit",
            currentPluginName,
            "--enabled",
            currentPluginEnabled ? "on" : "off",
            "--type",
            currentPluginType,
            "--path",
            currentPluginPath,
            "--initfunc",
            currentPluginInitfunc,
            "--id",
            currentPluginId,
            "--vendor",
            currentPluginVendor,
            "--version",
            currentPluginVersion,
            "--description",
            currentPluginDescription
        ];
        log_cmd("savePlugin", "Edit the plugin from the modal form", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    console.info("savePlugin", "Result", content);
                    addNotification(
                        "success",
                        `Plugin ${currentPluginName} was successfully modified`
                    );
                    pluginListHandler();
                    closeHandler();
                })
                .fail(err => {
                    addNotification(
                        "error",
                        `Error during plugin ${currentPluginName} modification - ${err}`
                    );
                    closeHandler();
                });
    }

    render() {
        const modalFields = {
            currentPluginType: this.props.currentPluginType,
            currentPluginPath: this.props.currentPluginPath,
            currentPluginInitfunc: this.props.currentPluginInitfunc,
            currentPluginId: this.props.currentPluginId,
            currentPluginVendor: this.props.currentPluginVendor,
            currentPluginVersion: this.props.currentPluginVersion,
            currentPluginDescription: this.props.currentPluginDescription
        };
        const {
            showModal,
            closeHandler,
            currentPluginName,
            currentPluginEnabled,
            handleChange,
            handleSwitchChange
        } = this.props;
        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Edit Plugin - {currentPluginName}
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal>
                            <FormGroup
                                key="currentPluginEnabled"
                                controlId="currentPluginEnabled"
                                disabled={false}
                            >
                                <Col componentClass={ControlLabel} sm={4}>
                                    Plugin Status
                                </Col>
                                <Col sm={7}>
                                    <Switch
                                        bsSize="normal"
                                        title="normal"
                                        id="bsSize-example"
                                        value={currentPluginEnabled}
                                        onChange={() =>
                                            handleSwitchChange(
                                                currentPluginEnabled
                                            )
                                        }
                                        animate={false}
                                    />
                                </Col>
                            </FormGroup>
                            {Object.entries(modalFields).map(([id, value]) => (
                                <FormGroup
                                    key={id}
                                    controlId={id}
                                    disabled={false}
                                >
                                    <Col componentClass={ControlLabel} sm={4}>
                                        Plugin {id.replace("currentPlugin", "")}
                                    </Col>
                                    <Col sm={7}>
                                        <FormControl
                                            type="text"
                                            value={value}
                                            onChange={handleChange}
                                        />
                                    </Col>
                                </FormGroup>
                            ))}
                        </Form>
                    </Modal.Body>
                    <Modal.Footer className="ds-modal-footer">
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button bsStyle="primary" onClick={this.savePlugin}>
                            Save
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

PluginEditModal.propTypes = {
    handleChange: PropTypes.func,
    handleSwitchChange: PropTypes.func,
    currentPluginName: PropTypes.string,
    currentPluginType: PropTypes.string,
    currentPluginEnabled: PropTypes.bool,
    currentPluginPath: PropTypes.string,
    currentPluginInitfunc: PropTypes.string,
    currentPluginId: PropTypes.string,
    currentPluginVendor: PropTypes.string,
    currentPluginVersion: PropTypes.string,
    currentPluginDescription: PropTypes.string,
    serverId: PropTypes.string,
    closeHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    showModal: PropTypes.bool
};

PluginEditModal.defaultProps = {
    handleChange: noop,
    handleSwitchChange: noop,
    currentPluginName: "",
    currentPluginType: "",
    currentPluginEnabled: false,
    currentPluginPath: "",
    currentPluginInitfunc: "",
    currentPluginId: "",
    currentPluginVendor: "",
    currentPluginVersion: "",
    currentPluginDescription: "",
    serverId: "",
    closeHandler: noop,
    pluginListHandler: noop,
    showModal: false
};

export default PluginEditModal;
