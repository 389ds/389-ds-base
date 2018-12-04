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
import "../../css/ds.css";

class PluginEditModal extends React.Component {
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
            currentPluginType,
            currentPluginPath,
            currentPluginInitfunc,
            currentPluginId,
            currentPluginVendor,
            currentPluginVersion,
            currentPluginDescription,
            handleChange,
            handleSwitchChange,
            savePluginHandler
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
                                        id="pluginEnableSwitch"
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
                        <Button
                            bsStyle="primary"
                            onClick={() => savePluginHandler({
                                name: currentPluginName,
                                enabled: currentPluginEnabled,
                                type: currentPluginType,
                                path: currentPluginPath,
                                initfunc: currentPluginInitfunc,
                                id: currentPluginId,
                                vendor: currentPluginVendor,
                                version: currentPluginVersion,
                                description: currentPluginDescription
                            })}
                        >
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
    closeHandler: PropTypes.func,
    savePluginHandler: PropTypes.func,
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
    closeHandler: noop,
    savePluginHandler: noop,
    showModal: false
};

export default PluginEditModal;
