import React from "react";
import {
    Button,
    Checkbox,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Text,
    TextContent,
    TextVariants,
    TextInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import PropTypes from "prop-types";

export class SecurityAddCACertModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            spinning,
            error
        } = this.props;
        let saveBtnName = "Add Certificate";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Adding Certificate ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Add Certificate Authority"
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={error.certFile || error.certName}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text component={TextVariants.h4}>
                            Add a CA Certificate to the security database.
                        </Text>
                    </TextContent>
                    <hr />
                    <Grid title="Enter full path to and and including certificate file name">
                        <GridItem className="ds-label" span={3}>
                            Certificate File
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="certFile"
                                aria-describedby="horizontal-form-name-helper"
                                name="certFile"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.certFile ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Enter name/nickname of the certificate"
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            Certificate Nickname
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="certName"
                                aria-describedby="horizontal-form-name-helper"
                                name="certName"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.certName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

export class SecurityAddCertModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            spinning,
            error
        } = this.props;

        let saveBtnName = "Add Certificate";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Adding Certificate ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title="Add Certificate"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={error.certFile || error.certName}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal>
                    <TextContent>
                        <Text component={TextVariants.h4}>
                            Add A Certificate To The Security Database.
                        </Text>
                    </TextContent>
                    <hr />
                    <Grid title="Enter full path to and and including certificate file name">
                        <GridItem className="ds-label" span={3}>
                            Certificate File
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="certFile"
                                aria-describedby="horizontal-form-name-helper"
                                name="certFile"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.certFile ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Enter name/nickname of the certificate"
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            Certificate Nickname
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="certName"
                                aria-describedby="horizontal-form-name-helper"
                                name="certName"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.certName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

export class SecurityEnableModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            primaryName,
            certs,
            spinning
        } = this.props;

        // Build list of cert names for the select list
        const certNames = [];
        for (const cert of certs) {
            certNames.push(cert.attrs.nickname);
        }

        let saveBtnName = "Enable Security";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Enabling Security ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <Modal
                variant={ModalVariant.small}
                aria-labelledby="ds-modal"
                title="Enable Security"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal>
                    <TextContent>
                        <Text component={TextVariants.h4}>
                            You are choosing to enable security for the Directory Server which
                            allows the server to accept incoming client TLS connections.  Please
                            select which certificate the server should use.
                        </Text>
                    </TextContent>
                    <hr />
                    <Grid className="ds-margin-top" title="The server certificate the Directory Server will use">
                        <GridItem className="ds-label" span={4}>
                            Available Certificates
                        </GridItem>
                        <GridItem sm={8}>
                            <FormSelect
                                value={primaryName}
                                id="certNameSelect"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                aria-label="FormSelect Input"
                            >
                                {certNames.map((option) => (
                                    <FormSelectOption key={option} value={option} label={option} />
                                ))}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

export class EditCertModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            flags,
            spinning
        } = this.props;

        let saveBtnName = "Save Flags";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Saving flags ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        // Process the cert flags
        let CSSLChecked = false;
        let CEmailChecked = false;
        let COSChecked = false;
        let TSSLChecked = false;
        let TEmailChecked = false;
        let TOSChecked = false;
        let cSSLChecked = false;
        let cEmailChecked = false;
        let cOSChecked = false;
        let PSSLChecked = false;
        let PEmailChecked = false;
        let POSChecked = false;
        let pSSLChecked = false;
        let pEmailChecked = false;
        let pOSChecked = false;
        let uSSLChecked = false;
        let uEmailChecked = false;
        let uOSChecked = false;
        let SSLFlags = '';
        let EmailFlags = '';
        let OSFlags = '';

        if (flags != "") {
            [SSLFlags, EmailFlags, OSFlags] = flags.split(',');
            if (SSLFlags.includes('T')) {
                TSSLChecked = true;
            }
            if (EmailFlags.includes('T')) {
                TEmailChecked = true;
            }
            if (OSFlags.includes('T')) {
                TOSChecked = true;
            }
            if (SSLFlags.includes('C')) {
                CSSLChecked = true;
            }
            if (EmailFlags.includes('C')) {
                CEmailChecked = true;
            }
            if (OSFlags.includes('C')) {
                COSChecked = true;
            }
            if (SSLFlags.includes('c')) {
                cSSLChecked = true;
            }
            if (EmailFlags.includes('c')) {
                cEmailChecked = true;
            }
            if (OSFlags.includes('c')) {
                cOSChecked = true;
            }
            if (SSLFlags.includes('P')) {
                PSSLChecked = true;
            }
            if (EmailFlags.includes('P')) {
                PEmailChecked = true;
            }
            if (OSFlags.includes('P')) {
                POSChecked = true;
            }
            if (SSLFlags.includes('p')) {
                pSSLChecked = true;
            }
            if (EmailFlags.includes('p')) {
                pEmailChecked = true;
            }
            if (OSFlags.includes('p')) {
                pOSChecked = true;
            }
            if (SSLFlags.includes('u')) {
                uSSLChecked = true;
            }
            if (EmailFlags.includes('u')) {
                uEmailChecked = true;
            }
            if (OSFlags.includes('u')) {
                uOSChecked = true;
            }
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title="Edit Certificate Trust Flags"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.disableSaveBtn}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Grid className="ds-margin-top">
                    <GridItem span={6}>
                        Flags
                    </GridItem>
                    <GridItem span={2}>
                        SSL
                    </GridItem>
                    <GridItem span={2}>
                        Email
                    </GridItem>
                    <GridItem span={2}>
                        Object Signing
                    </GridItem>
                    <hr />
                    <GridItem span={6} title="Trusted CA (flag 'C', also implies 'c' flag)">
                        (C) - Trusted CA
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="CflagSSL"
                            isChecked={CSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="CflagEmail"
                            isChecked={CEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="CflagOS"
                            isChecked={COSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title="Trusted CA for client authentication (flag 'T')">
                        (T) - Trusted CA Client Auth
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="TflagSSL"
                            isChecked={TSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="TflagEmail"
                            isChecked={TEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="TflagOS"
                            isChecked={TOSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title="Valid CA (flag 'c')">
                        (c) - Valid CA
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="cflagSSL"
                            isChecked={cSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="cflagEmail"
                            isChecked={cEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="cflagOS"
                            isChecked={cOSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title="Trusted Peer (flag 'P', implies flag 'p')">
                        (P) - Trusted Peer
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="PflagSSL"
                            isChecked={PSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="PflagEmail"
                            isChecked={PEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="PflagOS"
                            isChecked={POSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>

                    <GridItem span={6} title="Valid Peer (flag 'p')">
                        (p) - Valid Peer
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="pflagSSL"
                            isChecked={pSSLChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="pflagEmail"
                            isChecked={pEmailChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="pflagOS"
                            isChecked={pOSChecked}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                        />
                    </GridItem>
                    <hr />
                    <GridItem span={6} title="A private key is associated with the certificate. This is a dynamic flag and you cannot adjust it.">
                        (u) - Private Key
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="uflagSSL"
                            isChecked={uSSLChecked}
                            disabled
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="uflagEmail"
                            isChecked={uEmailChecked}
                            disabled
                        />
                    </GridItem>
                    <GridItem span={2}>
                        <Checkbox
                            id="uflagOS"
                            isChecked={uOSChecked}
                            disabled
                        />
                    </GridItem>
                </Grid>
            </Modal>
        );
    }
}

SecurityEnableModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    primaryName: PropTypes.string,
    certs: PropTypes.array,
    spinning: PropTypes.bool,
};

SecurityEnableModal.defaultProps = {
    showModal: false,
    primaryName: "",
    certs: [],
    spinning: false,
};

EditCertModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    flags: PropTypes.string,
    spinning: PropTypes.bool,
};

EditCertModal.defaultProps = {
    showModal: false,
    flags: "",
    spinning: false,
};

SecurityAddCertModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
};

SecurityAddCertModal.defaultProps = {
    showModal: false,
    spinning: false,
    error: {},
};

SecurityAddCACertModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
};

SecurityAddCACertModal.defaultProps = {
    showModal: false,
    spinning: false,
    error: {},
};
