import React from "react";
import {
    Modal,
    Row,
    Col,
    ControlLabel,
    Icon,
    Button,
    Form,
    noop,
    Spinner,
} from "patternfly-react";
import PropTypes from "prop-types";
import { get_date_string } from "../tools.jsx";
import { LagReportTable } from "./monitorTables.jsx";
import "../../css/ds.css";

class ReplLoginModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            doReport,
            spinning,
            error
        } = this.props;

        let spinner = "";
        if (spinning) {
            spinner =
                <Row className="ds-margin-top">
                    <hr />
                    <div className="ds-modal-spinner">
                        <Spinner loading inline size="lg" />Authenticating to all the replicas ...
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
                            Replication Login Credentials
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <p>
                                In order to get the replication agreement lag times and state the
                                authentication credentials to the remote replicas must be provided.
                                This only works if the bind credentials used are valid on all the
                                replicas.
                            </p>
                            <hr />
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>
                                        Bind DN
                                    </ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <input
                                        className={error.binddn ? "ds-input-auto-bad" : "ds-input-auto"}
                                        onChange={handleChange} defaultValue="cn=Directory Manager"
                                        type="text" id="binddn"
                                    />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>
                                        Password
                                    </ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <input
                                        className={error.bindpw ? "ds-input-auto-bad" : "ds-input-auto"}
                                        onChange={handleChange} type="password" id="bindpw"
                                    />
                                </Col>
                            </Row>
                            {spinner}
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Close
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={doReport}
                        >
                            Get Report
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class ReplLagReportModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            agmts,
            pokeAgmt,
            viewAgmt
        } = this.props;

        return (
            <Modal backdrop="static" contentClassName="ds-lag-report" show={showModal} onHide={closeHandler}>
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
                        Replication Lag Report
                    </Modal.Title>
                </Modal.Header>
                <Modal.Body>
                    <LagReportTable
                        agmts={agmts}
                        pokeAgmt={pokeAgmt}
                        viewAgmt={viewAgmt}
                    />
                </Modal.Body>
                <Modal.Footer>
                    <Button
                        bsStyle="default"
                        className="btn-cancel"
                        onClick={closeHandler}
                    >
                        Close
                    </Button>
                </Modal.Footer>
            </Modal>
        );
    }
}

class TaskLogModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            logData,
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
                            Task Log
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <div>
                                <textarea className="ds-logarea" value={logData} readOnly />
                            </div>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Close
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class AgmtDetailsModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            agmt,
        } = this.props;

        // Format status dates from agmt
        let convertedDate = {};
        let dateAttrs = ['last-update-start', 'last-update-end',
            'last-init-start', 'last-init-end'];
        for (let attr of dateAttrs) {
            if (agmt[attr] == "19700101000000Z") {
                convertedDate[attr] = "Unavailable";
            } else {
                convertedDate[attr] = get_date_string(agmt[attr]);
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
                            Replication Agreement Details ({agmt['agmt-name']})
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Replica</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['replica']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Agreement Enabled</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['replica-enabled']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Init Started</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={convertedDate['last-init-start']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Init Ended</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={convertedDate['last-init-end']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Initialization Status</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <textarea value={agmt['last-init-status']} rows="5" className="ds-agmt-textarea" readOnly />
                                </Col>
                            </Row>

                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Replication In Progress</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['update-in-progress']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Changes Sent</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['number-changes-sent']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Update Started</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={convertedDate['last-update-start']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Update Ended</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={convertedDate['last-update-end']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Update Status</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <textarea value={agmt['last-update-status']} rows="5" className="ds-agmt-textarea" readOnly />
                                </Col>
                            </Row>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-primary ds-float-left"
                            onClick={this.props.initAgmt}
                        >
                            Initialize Agreement
                        </Button>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Close
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class WinsyncAgmtDetailsModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            agmt,
        } = this.props;

        // Format status dates from agmt
        let dateAttrs = ['last-update-start', 'last-update-end',
            'last-init-start', 'last-init-end'];
        for (let attr of dateAttrs) {
            if (agmt[attr] == "19700101000000Z") {
                agmt[attr] = "Unavailable";
            } else {
                agmt[attr] = get_date_string(agmt[attr]);
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
                            Replication Winsync Agreement Details ({agmt['agmt-name']})
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Windows Replica</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['replica']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Agreement Enabled</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['replica-enabled']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Init Started</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['last-init-start']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Init Ended</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['last-init-end']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Initialization Status</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <textarea value={agmt['last-init-status']} rows="5" className="ds-agmt-textarea" readOnly />
                                </Col>
                            </Row>

                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Replication In Progress</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['update-in-progress']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Changes Sent</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['number-changes-sent']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Update Started</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['last-update-start']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Update Ended</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <input className="ds-input-auto" type="text" size="22" value={agmt['last-update-end']} readOnly />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col sm={4}>
                                    <ControlLabel>Last Update Status</ControlLabel>
                                </Col>
                                <Col sm={8}>
                                    <textarea value={agmt['last-update-status']} rows="5" className="ds-agmt-textarea" readOnly />
                                </Col>
                            </Row>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-primary ds-float-left"
                            onClick={this.props.initAgmt}
                        >
                            Initialize Agreement
                        </Button>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Close
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

// Prototypes and defaultProps
AgmtDetailsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    agmt: PropTypes.object,
    initAgmt: PropTypes.func,
};

AgmtDetailsModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    agmt: {},
    initAgmt: noop,
};

WinsyncAgmtDetailsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    agmt: PropTypes.object,
    initAgmt: PropTypes.func,
};

WinsyncAgmtDetailsModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    agmt: {},
    initAgmt: noop,
};

TaskLogModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    logData: PropTypes.string
};

TaskLogModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    agreement: "",
};

ReplLoginModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    doReport: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
};

ReplLoginModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    doReport: noop,
    spinning: false,
    error: {},
};

export {
    TaskLogModal,
    AgmtDetailsModal,
    ReplLagReportModal,
    ReplLoginModal,
    WinsyncAgmtDetailsModal,
};
