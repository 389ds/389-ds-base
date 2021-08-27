import React from "react";
import {
    Button,
    Form,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    TextInput,
    ValidatedOptions,
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
                <Form isHorizontal>
                    <Grid
                        title="SASL Mapping entry name"
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            SASL Mapping Name
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.props.name}
                                type="text"
                                id="saslMapName"
                                aria-describedby="horizontal-form-name-helper"
                                name="saslMapName"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                                validated={this.props.error.saslMapName ? ValidatedOptions.error : ValidatedOptions.default}
                                isRequired
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="SASL mapping Regular Expression"
                    >
                        <GridItem className="ds-label" span={3}>
                            SASL mapping Regular Expression
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.props.regex}
                                type="text"
                                id="saslMapRegex"
                                aria-describedby="horizontal-form-name-helper"
                                name="saslMapRegex"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                                isRequired
                                validated={this.props.error.saslMapRegex ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Test Regular Expression"
                    >
                        <GridItem className="ds-label" span={3}>
                            <font size="2">* Test Regex</font>
                        </GridItem>
                        <GridItem span={5}>
                            <TextInput
                                value={this.props.testText}
                                type="text"
                                id="saslTestText"
                                aria-describedby="horizontal-form-name-helper"
                                name="saslTestText"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                                placeholder="Enter text to test regex"
                            />
                        </GridItem>
                        <GridItem span={4}>
                            <Button
                                className="ds-left-margin"
                                isDisabled={this.props.testBtnDisabled}
                                variant="primary"
                                onClick={this.props.handleTestRegex}
                            >
                                Test It
                            </Button>
                        </GridItem>
                    </Grid>
                    <Grid
                        title="The search base or a specific entry DN to match against the constructed DN"
                    >
                        <GridItem className="ds-label" span={3}>
                            SASL Mapping Base
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.props.base}
                                type="text"
                                id="saslBase"
                                aria-describedby="horizontal-form-name-helper"
                                name="saslBase"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                                isRequired
                                validated={this.props.error.saslBase ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="SASL mapping search filter"
                    >
                        <GridItem className="ds-label" span={3}>
                            SASL Mapping Filter
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.props.filter}
                                type="text"
                                id="saslFilter"
                                aria-describedby="horizontal-form-name-helper"
                                name="saslFilter"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                                isRequired
                                validated={this.props.error.saslFilter ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Set the mapping priority for which mappings should be tried first"
                    >
                        <GridItem className="ds-label" span={3}>
                            SASL Mapping Priority
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.props.priority}
                                type="number"
                                id="saslPriority"
                                aria-describedby="horizontal-form-name-helper"
                                name="saslPriority"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                                validated={this.props.error.saslPriority ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
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
