import React from "react";
import {
    Col,
    ControlLabel,
    Form,
    FormControl,
    Row,
} from "patternfly-react";
import {
    Button,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    // TextInput,
    noop
} from "@patternfly/react-core";
import PropTypes from "prop-types";

export class SASLMappingModal extends React.Component {
    render() {
        let title = this.props.type;
        let btnText = "Create Mapping";
        let extraPrimaryProps = {};
        if (title != "Create") {
            btnText = "Save Mapping";
        }
        title = title + " SASL Mapping";
        if (this.props.spinning) {
            btnText = "Saving...";
            extraPrimaryProps.spinnerAriaValueText = "Loading";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title={title}
                isOpen={this.props.showModal}
                onClose={this.props.closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        isDisabled={this.props.saveDisabled}
                        variant="primary"
                        isLoading={this.props.spinning}
                        spinnerAriaValueText={this.props.spinning ? "Loading" : undefined}
                        onClick={() => {
                            this.props.saveHandler(this.props.name);
                        }}
                        {...extraPrimaryProps}
                    >
                        {btnText}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={this.props.closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal>
                    <Row
                        className="ds-margin-top"
                        title="SASL Mapping entry name"
                    >
                        <Col componentClass={ControlLabel} sm={5}>
                            SASL Mapping Name
                        </Col>
                        <Col sm={5}>
                            <FormControl
                                id="saslMapName"
                                type="text"
                                onChange={this.props.handleChange}
                                className={this.props.error.saslMapName ? "ds-input-bad" : ""}
                                defaultValue={this.props.name}
                            />
                        </Col>
                    </Row>
                    <Row
                        className="ds-margin-top"
                        title="SASL mapping Regular Expression"
                    >
                        <Col componentClass={ControlLabel} sm={5}>
                            SASL Mapping Regex
                        </Col>
                        <Col sm={5}>
                            <FormControl
                                id="saslMapRegex"
                                type="text"
                                onChange={this.props.handleChange}
                                className={this.props.error.saslMapRegex ? "ds-input-bad" : ""}
                                defaultValue={this.props.regex}
                            />
                        </Col>
                    </Row>
                    <Row
                        className="ds-margin-top"
                        title="Test Regular Expression"
                    >
                        <Col componentClass={ControlLabel} sm={5}>
                            <font size="2">* Test Regex</font>
                        </Col>
                        <Col sm={5}>
                            <FormControl
                                id="saslTestText"
                                type="text"
                                onChange={this.props.handleChange}
                                defaultValue={this.props.testText}
                                placeholder="Enter text to test regex"
                            />
                        </Col>
                        <Col sm={1}>
                            <Button
                                disabled={this.props.testBtnDisabled}
                                bsStyle="primary"
                                onClick={this.props.handleTestRegex}
                            >
                                Test It
                            </Button>
                        </Col>
                    </Row>
                    <Row
                        className="ds-margin-top"
                        title="The search base or a specific entry DN to match against the constructed DN"
                    >
                        <Col componentClass={ControlLabel} sm={5}>
                            SASL Mapping Base
                        </Col>
                        <Col sm={5}>
                            <FormControl
                                id="saslBase"
                                type="text"
                                onChange={this.props.handleChange}
                                className={this.props.error.saslBase ? "ds-input-bad" : ""}
                                defaultValue={this.props.base}
                            />
                        </Col>
                    </Row>
                    <Row
                        className="ds-margin-top"
                        title="SASL mapping search filter"
                    >
                        <Col componentClass={ControlLabel} sm={5}>
                            SASL Mapping Filter
                        </Col>
                        <Col sm={5}>
                            <FormControl
                                id="saslFilter"
                                type="text"
                                onChange={this.props.handleChange}
                                className={this.props.error.saslFilter ? "ds-input-bad" : ""}
                                defaultValue={this.props.filter}
                            />
                        </Col>
                    </Row>
                    <Row
                        className="ds-margin-top"
                        title="Set the mapping priority for which mappins should be tried first"
                    >
                        <Col componentClass={ControlLabel} sm={5}>
                            SASL Mapping Priority
                        </Col>
                        <Col sm={5}>
                            <FormControl
                                id="saslPriority"
                                type="number"
                                min="1"
                                max="100"
                                value={this.props.priority}
                                onChange={this.props.handleChange}
                                className={this.props.error.saslPriority ? "ds-input-bad" : ""}
                            />
                        </Col>
                    </Row>
                </Form>
            </Modal>
        );
    }
}

// Types and defaults

SASLMappingModal.propTypes = {
    showModal: PropTypes.bool,
    testBtnDisabled: PropTypes.bool,
    saveDisabled: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    handleTestRegex: PropTypes.func,
    saveHandler: PropTypes.func,
    error: PropTypes.object,
    name: PropTypes.string,
    regex: PropTypes.string,
    testText: PropTypes.string,
    base: PropTypes.string,
    filter: PropTypes.string,
    priority: PropTypes.string,
    spinning: PropTypes.bool,
};

SASLMappingModal.defaultProps = {
    showModal: false,
    testBtnDisabled: true,
    saveDisabled: true,
    closeHandler: noop,
    handleChange: noop,
    handleTestRegex: noop,
    saveHandler: noop,
    error: {},
    name: "",
    regex: "",
    testText: "",
    base: "",
    filter: "",
    priority: "",
    spinning: PropTypes.bool,
};
