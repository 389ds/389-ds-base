import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Form,
    FormHelperText,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    TextInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import PropTypes from "prop-types";

const _ = cockpit.gettext;

export class SASLMappingModal extends React.Component {
    render() {
        let title = this.props.type;
        let btnText = _("Create Mapping");
        const extraPrimaryProps = {};
        if (title !== "Create") {
            btnText = _("Save Mapping");
        }
        title = cockpit.format(_("$0 SASL Mapping"), title);
        if (this.props.spinning) {
            btnText = _("Saving...");
            extraPrimaryProps.spinnerAriaValueText = _("Loading");
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title={title}
                isOpen={this.props.showModal}
                onClose={this.props.handleClose}
                actions={[
                    <Button
                        key="confirm"
                        isDisabled={this.props.saveDisabled || this.props.spinning}
                        variant="primary"
                        isLoading={this.props.spinning}
                        spinnerAriaValueText={this.props.spinning ? _("Loading") : undefined}
                        onClick={() => {
                            this.props.saveHandler(this.props.name);
                        }}
                        {...extraPrimaryProps}
                    >
                        {btnText}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={this.props.handleClose}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid
                        title={_("SASL Mapping entry name")}
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("SASL Mapping Name")}
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
                                isDisabled={this.props.type === "Edit"}
                            />
                            <FormHelperText isError isHidden={!this.props.error.saslMapName}>
                                {_("You must provide a name for this mapping")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid
                        title={_("SASL Mapping Regular Expression")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("Regular Expression")}
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
                            <FormHelperText isError isHidden={!this.props.error.saslMapRegex}>
                                {_("You must provide a valid regular expression")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid
                        title={_("Test Regular Expression")}
                    >
                        <GridItem className="ds-label" span={3}>
                            <font size="2">{_("* Test Regex")}</font>
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
                                placeholder={_("Enter text to test regex")}
                            />
                        </GridItem>
                        <GridItem span={4}>
                            <Button
                                className="ds-left-margin"
                                isDisabled={this.props.testBtnDisabled || this.props.error.saslMapRegex}
                                variant="primary"
                                onClick={this.props.handleTestRegex}
                            >
                                {_("Test It")}
                            </Button>
                        </GridItem>
                    </Grid>
                    <Grid
                        title={_("The search base or a specific entry DN to match against the constructed DN")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("SASL Mapping Base")}
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
                            <FormHelperText isError isHidden={!this.props.error.saslBase}>
                                {_("You must provide a search base")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid
                        title={_("SASL mapping search filter")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("SASL Mapping Filter")}
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
                            <FormHelperText isError isHidden={!this.props.error.saslFilter}>
                                {_("You must provide an LDAP search filter")}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                    <Grid
                        title={_("Set the mapping priority for which mappings should be tried first")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("SASL Mapping Priority")}
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
                            <FormHelperText isError isHidden={!this.props.error.saslPriority}>
                                {_("Priority must be between 1 and 100")}
                            </FormHelperText>
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
    error: {},
    name: "",
    regex: "",
    testText: "",
    base: "",
    filter: "",
    priority: "",
    spinning: PropTypes.bool,
};
