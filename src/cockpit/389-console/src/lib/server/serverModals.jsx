import React from "react";
import {
    Button,
    // Checkbox,
    Col,
    ControlLabel,
    Form,
    FormControl,
    Icon,
    Modal,
    Row,
    Spinner,
    noop
} from "patternfly-react";
import PropTypes from "prop-types";

export class SASLMappingModal extends React.Component {
    render() {
        let title = this.props.type;
        let btnText = "Create";
        let spinning = "";
        if (title != "Create") {
            btnText = "Save";
        }
        if (this.props.spinning) {
            spinning = <Spinner className="ds-margin-top-lg" loading size="md" />;
        }

        return (
            <Modal show={this.props.showModal} onHide={this.props.closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={this.props.closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            {title} SASL Mapping
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
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
                        {spinning}
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={this.props.closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            disabled={this.props.saveDisabled}
                            onClick={() => {
                                this.props.saveHandler(this.props.name);
                            }}
                        >
                            {btnText} Mapping
                        </Button>
                    </Modal.Footer>
                </div>
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
