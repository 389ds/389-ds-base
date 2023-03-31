import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { ReferralTable } from "./databaseTables.jsx";
import {
    Button,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    TextInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import { log_cmd, valid_port, valid_dn } from "../tools.jsx";
import PropTypes from "prop-types";

export class SuffixReferrals extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            showConfirmRefDelete: false,
            showRefModal: false,
            removeRef: "",
            refProtocol: "ldap://",
            refHost: "",
            refPort: "",
            refSuffix: "",
            refFilter: "",
            refScope: "sub",
            refAttrs: "",
            refValue: "",
            errObj: {},
            saving: false,
            saveBtnDisabled: true,
            modalSpinning: false,
            modalChecked: false,
        };

        // Delete referral and confirmation
        this.showConfirmRefDelete = this.showConfirmRefDelete.bind(this);
        this.closeConfirmRefDelete = this.closeConfirmRefDelete.bind(this);
        this.deleteRef = this.deleteRef.bind(this);
        this.saveRef = this.saveRef.bind(this);
        // Referral modal
        this.showRefModal = this.showRefModal.bind(this);
        this.closeRefModal = this.closeRefModal.bind(this);
        this.handleRefChange = this.handleRefChange.bind(this);
        this.buildRef = this.buildRef.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleScopeChange = this.handleScopeChange.bind(this);
        this.handleLdapChange = this.handleLdapChange.bind(this);
    }

    handleScopeChange(value, event) {
        this.setState({
            refScope: value,
        });
    }

    handleLdapChange(value, event) {
        this.setState({
            refProtocol: value,
        });
    }

    showConfirmRefDelete(item) {
        this.setState({
            removeRef: item,
            showConfirmRefDelete: true,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    showRefModal() {
        this.setState({
            showRefModal: true,
            errObj: {},
            refValue: ""
        });
    }

    closeConfirmRefDelete() {
        this.setState({
            showConfirmRefDelete: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    closeRefModal() {
        this.setState({
            showRefModal: false
        });
    }

    deleteRef() {
        // take state.removeRef and remove it from ldap
        this.setState({
            modalSpinning: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "suffix", "set", "--del-referral=" + this.state.removeRef, this.props.suffix
        ];
        log_cmd("deleteRef", "Delete suffix referral", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        `Referral successfully deleted`
                    );
                    this.setState({
                        modalSpinning: false,
                        showConfirmRefDelete: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failure deleting referral - ${errMsg.desc}`
                    );
                    this.setState({
                        modalSpinning: false,
                        showConfirmRefDelete: false
                    });
                });
    }

    // Create referral
    saveRef() {
        this.setState({
            saving: true
        });

        // Add referral
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "suffix", "set", "--add-referral=" + this.state.refValue, this.props.suffix
        ];
        log_cmd("saveRef", "Add referral", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.closeRefModal();
                    this.props.addNotification(
                        "success",
                        `Referral successfully created`
                    );
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.closeRefModal();
                    this.props.addNotification(
                        "error",
                        `Failure creating referral - ${errMsg.desc}`
                    );
                    this.setState({
                        saving: false
                    });
                });
    }

    buildRef() {
        let previewRef = this.state.refProtocol + this.state.refHost;
        const ref_port = this.state.refPort;
        const ref_suffix = this.state.refSuffix;
        const ref_attrs = this.state.refAttrs;
        const ref_filter = this.state.refFilter;
        const ref_scope = this.state.refScope;

        if (ref_port != "") {
            previewRef += ":" + ref_port;
        }
        if (ref_suffix != "" || ref_attrs != "" || ref_filter != "" || ref_scope != "") {
            previewRef += "/" + encodeURIComponent(ref_suffix);
            if (ref_attrs != "") {
                previewRef += "?" + encodeURIComponent(ref_attrs);
            } else if (ref_filter != "" || ref_scope != "") {
                previewRef += "?";
            }
            if (ref_scope != "") {
                previewRef += "?" + encodeURIComponent(ref_scope);
            } else if (ref_filter != "") {
                previewRef += "?";
            }
            if (ref_filter != "") {
                previewRef += "?" + encodeURIComponent(ref_filter);
            }
        }

        // Update referral value
        this.setState({
            refValue: previewRef
        });
    }

    handleRefChange(e) {
        const value = e.target.value;
        const attr = e.target.id;
        const errObj = this.state.errObj;
        let valueErr = false;
        let saveBtnDisabled = false;

        // Check previously set values
        const check_attrs = ['refHost', 'refPort'];
        for (const check_attr of check_attrs) {
            if (attr != check_attr) {
                if (this.state[check_attr] == "") {
                    saveBtnDisabled = true;
                }
            }
        }
        // Check changes
        if (attr == "refHost" && value == "") {
            saveBtnDisabled = true;
            valueErr = true;
        }
        if (attr == "refPort" && (value == "" || !valid_port(value))) {
            saveBtnDisabled = true;
            valueErr = true;
        }
        if (attr == "refSuffix" && (value != "" && !valid_dn(value))) {
            saveBtnDisabled = true;
            valueErr = true;
        }

        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj,
            saveBtnDisabled: saveBtnDisabled
        }, this.buildRef);
    }

    handleChange(e) {
        // Basic handler, no error checking
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value,
        });
    }

    render() {
        return (
            <div className="ds-sub-header ds-margin-bottom-md">
                <ReferralTable
                    key={this.props.rows}
                    rows={this.props.rows}
                    deleteRef={this.showConfirmRefDelete}
                />
                <Button variant="primary" onClick={this.showRefModal}>
                    Create Referral
                </Button>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmRefDelete}
                    closeHandler={this.closeConfirmRefDelete}
                    handleChange={this.handleChange}
                    actionHandler={this.deleteRef}
                    spinning={this.state.modalSpinning}
                    item={this.state.removeRef}
                    checked={this.state.modalChecked}
                    mTitle="Delete Referral"
                    mMsg="Are you sure you want to delete this referral?"
                    mSpinningMsg="Deleting ..."
                    mBtnName="Delete"
                />
                <AddReferralModal
                    showModal={this.state.showRefModal}
                    closeHandler={this.closeRefModal}
                    handleChange={this.handleRefChange}
                    handleScopeChange={this.handleScopeChange}
                    handleLdapChange={this.handleLdapChange}
                    saveHandler={this.saveRef}
                    previewValue={this.state.refValue}
                    refProtocol={this.state.refProtocol}
                    refScope={this.state.refScope}
                    error={this.state.errObj}
                    saving={this.state.saving}
                    saveBtnDisabled={this.state.saveBtnDisabled}
                />
            </div>
        );
    }
}

class AddReferralModal extends React.Component {
    render() {
        let {
            showModal,
            closeHandler,
            handleChange,
            handleScopeChange,
            handleLdapChange,
            saveHandler,
            previewValue,
            error,
            saving,
            saveBtnDisabled,
            refProtocol,
            refScope,
        } = this.props;

        if (previewValue == "") {
            previewValue = "ldap://";
        }
        let saveBtnName = "Create Referral";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Creating ...";
            extraPrimaryProps.spinnerAriaValueText = "Creating";
        }
        return (
            <Modal
                variant={ModalVariant.medium}
                title="Add Database Referral"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isDisabled={saveBtnDisabled || saving}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form className="ds-margin-top" isHorizontal autoComplete="off">
                    <Grid>
                        <GridItem className="ds-label" span={3}>
                            Protocol
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect id="refProtocol" value={refProtocol} onChange={handleLdapChange} aria-label="FormSelect Input">
                                <FormSelectOption key={1} value="ldap://" label="ldap://" />
                                <FormSelectOption key={2} value="ldaps://" label="ldaps://" />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem className="ds-label" span={3}>
                            Host Name
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="refHost"
                                aria-describedby="horizontal-form-name-helper"
                                name="refHost"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                validated={error.refHost ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem className="ds-label" span={3}>
                            Port Number
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="number"
                                id="refPort"
                                aria-describedby="horizontal-form-name-helper"
                                name="refPort"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                validated={error.refPort ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem className="ds-label" span={3}>
                            Suffix
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="refSuffix"
                                aria-describedby="horizontal-form-name-helper"
                                name="refSuffix"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                validated={error.refSuffix ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="Comma separated list of attributes to return">
                        <GridItem className="ds-label" span={3}>
                            Attributes
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="refAttrs"
                                aria-describedby="horizontal-form-name-helper"
                                name="refAttrs"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem className="ds-label" span={3}>
                            Filter
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="refFilter"
                                aria-describedby="horizontal-form-name-helper"
                                name="refFilter"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem className="ds-label" span={3}>
                            Scope
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect id="refScope" value={refScope} onChange={handleScopeChange} aria-label="FormSelect Input">
                                <FormSelectOption key={1} value="sub" label="sub" />
                                <FormSelectOption key={2} value="one" label="one" />
                                <FormSelectOption key={3} value="base" label="base" />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <hr />
                    <Grid>
                        <GridItem span={3}>
                            Computed Referral
                        </GridItem>
                        <GridItem span={9}>
                            <b>{previewValue}</b>
                        </GridItem>
                    </Grid>
                    <hr />
                </Form>
            </Modal>
        );
    }
}

// Property types and defaults

SuffixReferrals.propTypes = {
    rows: PropTypes.array,
    suffix: PropTypes.string,
    reload: PropTypes.func,
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
};

SuffixReferrals.defaultProps = {
    rows: [],
    suffix: "",
    serverId: "",
};

AddReferralModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    previewValue: PropTypes.string,
    error: PropTypes.object,
};

AddReferralModal.defaultProps = {
    showModal: false,
    previewValue: "",
    error: {},
};
