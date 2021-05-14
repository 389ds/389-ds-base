import React from "react";
import {
    Row,
    Col,
    ControlLabel,
    FormControl,
    Form,
} from "patternfly-react";
import {
    Button,
    Checkbox,
    Grid,
    GridItem,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    Spinner,
    // TextInput,
    noop
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

        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-modal-spinner">
                        <Spinner inline size="lg" />Adding CA certificate...
                    </div>
                </Row>;
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Add Certificate Authority"
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={saveHandler}>
                        Add Certificate
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <h4>
                        Add CA certificate to the security database.
                    </h4>
                    <hr />
                    <Row title="Enter full path to and and including certificate file name">
                        <Col sm={4}>
                            <ControlLabel>Certificate File</ControlLabel>
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="certFile"
                                className={error.certFile ? "ds-input-bad" : ""}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Enter name/nickname of the certificate">
                        <Col sm={4}>
                            <ControlLabel>Certificate Nickname</ControlLabel>
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="certName"
                                className={error.certName ? "ds-input-bad" : ""}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    <p />
                    {spinner}
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

        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-modal-spinner">
                        <Spinner size="lg" />Adding certificate...
                    </div>
                </Row>;
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title="Add Certificate"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={saveHandler}>
                        Add Certificate
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <h4>
                        Add certificate to the security database.
                    </h4>
                    <hr />
                    <Row title="Enter full path to and and including certificate file name">
                        <Col sm={4}>
                            <ControlLabel>Certificate File</ControlLabel>
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="certFile"
                                className={error.certFile ? "ds-input-bad" : ""}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Enter name/nickname of the certificate">
                        <Col sm={4}>
                            <ControlLabel>Certificate Nickname</ControlLabel>
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="certName"
                                className={error.certName ? "ds-input-bad" : ""}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    {spinner}
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
        let certNames = [];
        for (let cert of certs) {
            certNames.push(cert.attrs['nickname']);
        }
        let certNameOptions = certNames.map((name) =>
            <option key={name} value={name}>{name}</option>
        );
        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-modal-spinner">
                        <Spinner size="lg" />Enabling security...
                    </div>
                </Row>;
        }

        return (
            <Modal
                variant={ModalVariant.small}
                aria-labelledby="ds-modal"
                title="Enable Security"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={saveHandler}>
                        Enable Security
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <h4>
                        You are choosing to enable security for the Directory Server which
                        allows the server to accept incoming client TLS connections.  Please
                        select which certificate the server should use.
                    </h4>
                    <hr />
                    <Row className="ds-margin-top" title="The server certificate the Directory Server will use">
                        <Col sm={4}>
                            <ControlLabel>Available Certificates</ControlLabel>
                        </Col>
                        <Col sm={8}>
                            <select id="certNameSelect" onChange={handleChange} defaultValue={primaryName}>
                                {certNameOptions}
                            </select>
                        </Col>
                    </Row>
                    {spinner}
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

        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-modal-spinner">
                        <Spinner size="lg" />Saving certificate...
                    </div>
                </Row>;
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
                    <Button key="confirm" variant="primary" onClick={saveHandler}>
                        Save
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
                    {spinner}
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
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
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
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
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
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
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
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    spinning: false,
    error: {},
};
