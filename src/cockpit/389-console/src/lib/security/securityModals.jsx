import React from "react";
import {
    Modal,
    Row,
    Col,
    ControlLabel,
    Checkbox,
    FormControl,
    Icon,
    Button,
    Form,
    Spinner,
    noop
} from "patternfly-react";
import PropTypes from "prop-types";
import "../../css/ds.css";

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
                        <Spinner loading inline size="lg" />Adding CA certificate...
                    </div>
                </Row>;
        }

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
                            Add Certificate Authority
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
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
                            <p />
                            <Row title="Enter name/nickname of the certificate">
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
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Add Certificate
                        </Button>
                    </Modal.Footer>
                </div>
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
                        <Spinner loading inline size="lg" />Adding certificate...
                    </div>
                </Row>;
        }

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
                            Add Certificate
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
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
                            <p />
                            <Row title="Enter name/nickname of the certificate">
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
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Add Certificate
                        </Button>
                    </Modal.Footer>
                </div>
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
                        <Spinner loading inline size="lg" />Enabling security...
                    </div>
                </Row>;
        }

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
                            Enable Security
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
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
                            <p />
                            {spinner}
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Enable Security
                        </Button>
                    </Modal.Footer>
                </div>
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
                        <Spinner loading inline size="lg" />Saving certificate...
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
                            Edit Certificate Trust Flags
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Flags</ControlLabel>
                                </Col>
                                <Col sm={2}>
                                    <ControlLabel>SSL</ControlLabel>
                                </Col>
                                <Col sm={2}>
                                    <ControlLabel>Email</ControlLabel>
                                </Col>
                                <Col sm={3}>
                                    <ControlLabel>Object Signing</ControlLabel>
                                </Col>
                            </Row>
                            <hr />
                            <Row>
                                <Col sm={4} title="Trusted CA (flag 'C', also implies 'c' flag)">
                                    (C) - Trusted CA
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="CflagSSL"
                                        checked={CSSLChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="CflagEmail"
                                        checked={CEmailChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="CflagOS"
                                        checked={COSChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={4} title="Trusted CA for client authentication (flag 'T')">
                                    (T) - Trusted CA Client Auth
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="TflagSSL"
                                        checked={TSSLChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="TflagEmail"
                                        checked={TEmailChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="TflagOS"
                                        checked={TOSChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={4} title="Valid CA (flag 'c')">
                                    (c) - Valid CA
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="cflagSSL"
                                        checked={cSSLChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="cflagEmail"
                                        checked={cEmailChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="cflagOS"
                                        checked={cOSChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={4} title="Trusted Peer (flag 'P', implies flag 'p')">
                                    (P) - Trusted Peer
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="PflagSSL"
                                        checked={PSSLChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="PflagEmail"
                                        checked={PEmailChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="PflagOS"
                                        checked={POSChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={4} title="Valid Peer (flag 'p')">
                                    (p) - Valid Peer
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="pflagSSL"
                                        checked={pSSLChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="pflagEmail"
                                        checked={pEmailChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="pflagOS"
                                        checked={pOSChecked}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </Row>
                            <hr />
                            <Row>
                                <Col sm={4} title="A private key is associated with the certificate. This is a dynamic flag and you cannot adjust it.">
                                    (u) - Private Key
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="uflagSSL"
                                        checked={uSSLChecked}
                                        disabled
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="uflagEmail"
                                        checked={uEmailChecked}
                                        disabled
                                    />
                                </Col>
                                <Col sm={2}>
                                    <Checkbox
                                        id="uflagOS"
                                        checked={uOSChecked}
                                        disabled
                                    />
                                </Col>
                            </Row>
                            <p />
                            {spinner}
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Save
                        </Button>
                    </Modal.Footer>
                </div>
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
