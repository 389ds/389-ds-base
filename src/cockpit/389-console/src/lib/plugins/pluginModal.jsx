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

class PluginEditModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            handleSwitchChange,
            savePluginHandler
        } = this.props;
        const {
            currentPluginName,
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
            currentPluginPrecedence
        } = this.props.pluginData;
        const modalFields = {
            currentPluginType: currentPluginType,
            currentPluginPath: currentPluginPath,
            currentPluginInitfunc: currentPluginInitfunc,
            currentPluginId: currentPluginId,
            currentPluginVendor: currentPluginVendor,
            currentPluginVersion: currentPluginVersion,
            currentPluginDescription: currentPluginDescription,
            currentPluginPrecedence: currentPluginPrecedence
        };

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
                        <Modal.Title>Edit Plugin - {currentPluginName}</Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal>
                            <FormGroup
                                key="currentPluginEnabled"
                                controlId="currentPluginEnabled"
                                disabled={false}
                            >
                                <Col componentClass={ControlLabel} sm={5}>
                                    Plugin Status
                                </Col>
                                <Col sm={6}>
                                    <Switch
                                        bsSize="normal"
                                        title="normal"
                                        id="pluginEnableSwitch"
                                        value={currentPluginEnabled}
                                        onChange={() => handleSwitchChange(currentPluginEnabled)}
                                        animate={false}
                                    />
                                </Col>
                            </FormGroup>
                            {Object.entries(modalFields).map(([id, value]) => (
                                <FormGroup key={id} controlId={id} disabled={false}>
                                    <Col componentClass={ControlLabel} sm={5}>
                                        Plugin {id.replace("currentPlugin", "")}
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={value}
                                            onChange={handleChange}
                                        />
                                    </Col>
                                </FormGroup>
                            ))}
                            <FormGroup
                                key="currentPluginDependsOnType"
                                controlId="currentPluginDependsOnType"
                                disabled={false}
                            >
                                <Col componentClass={ControlLabel} sm={5}>
                                    Plugin Depends On Type
                                </Col>
                                <Col sm={6}>
                                    <FormControl
                                        type="text"
                                        value={currentPluginDependsOnType}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup
                                key="currentPluginDependsOnNamed"
                                controlId="currentPluginDependsOnNamed"
                                disabled={false}
                            >
                                <Col componentClass={ControlLabel} sm={5}>
                                    Plugin Depends On Named
                                </Col>
                                <Col sm={6}>
                                    <FormControl
                                        type="text"
                                        value={currentPluginDependsOnNamed}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </FormGroup>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button bsStyle="default" className="btn-cancel" onClick={closeHandler}>
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={() =>
                                savePluginHandler({
                                    name: currentPluginName,
                                    enabled: currentPluginEnabled,
                                    type: currentPluginType,
                                    path: currentPluginPath,
                                    initfunc: currentPluginInitfunc,
                                    id: currentPluginId,
                                    vendor: currentPluginVendor,
                                    version: currentPluginVersion,
                                    description: currentPluginDescription,
                                    dependsOnType: currentPluginDependsOnType,
                                    dependsOnNamed: currentPluginDependsOnNamed,
                                    precedence: currentPluginPrecedence
                                })
                            }
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
    pluginData: PropTypes.exact({
        currentPluginName: PropTypes.string,
        currentPluginType: PropTypes.string,
        currentPluginEnabled: PropTypes.bool,
        currentPluginPath: PropTypes.string,
        currentPluginInitfunc: PropTypes.string,
        currentPluginId: PropTypes.string,
        currentPluginVendor: PropTypes.string,
        currentPluginVersion: PropTypes.string,
        currentPluginDescription: PropTypes.string,
        currentPluginDependsOnType: PropTypes.string,
        currentPluginDependsOnNamed: PropTypes.string,
        currentPluginPrecedence: PropTypes.string
    }),
    closeHandler: PropTypes.func,
    savePluginHandler: PropTypes.func,
    showModal: PropTypes.bool
};

PluginEditModal.defaultProps = {
    handleChange: noop,
    handleSwitchChange: noop,
    pluginData: {
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
    },
    closeHandler: noop,
    savePluginHandler: noop,
    showModal: false
};

export default PluginEditModal;
