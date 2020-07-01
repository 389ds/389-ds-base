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
    FormGroup,
    FormControl,
    Spinner,
    Checkbox
} from "patternfly-react";
import PropTypes from "prop-types";
import { get_date_string } from "../tools.jsx";
import { ReportSingleTable, ReportConsumersTable } from "./monitorTables.jsx";

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
            if (agmt[attr][0] == "19700101000000Z") {
                convertedDate[attr] = "Unavailable";
            } else {
                convertedDate[attr] = get_date_string(agmt[attr][0]);
            }
        }
        let initButton = null;
        if (!this.props.isRemoteAgmt) {
            initButton = <Button
                bsStyle="default"
                className="btn-primary ds-float-left"
                onClick={this.props.initAgmt}
            >
                Initialize Agreement
            </Button>;
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
                        {initButton}
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
            if (agmt[attr][0] == "19700101000000Z") {
                agmt[attr] = "Unavailable";
            } else {
                agmt[attr] = get_date_string(agmt[attr][0]);
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

class ConflictCompareModal extends React.Component {
    render() {
        const {
            showModal,
            conflictEntry,
            validEntry,
            swapFunc,
            convertFunc,
            deleteFunc,
            handleConvertChange,
            closeHandler,
        } = this.props;

        let ignoreAttrs = ['createtimestamp', 'creatorsname', 'modifytimestamp',
            'modifiersname', 'entryid', 'entrydn', 'parentid', 'numsubordinates'];
        let conflict = "dn: " + conflictEntry.dn + "\n";
        let valid = "dn: " + validEntry.dn + "\n";
        let conflictChildren = "0";
        let validChildren = "0";

        for (const key in conflictEntry.attrs) {
            if (key == "numsubordinates") {
                conflictChildren = conflictEntry.attrs[key];
            }
            if (!ignoreAttrs.includes(key)) {
                for (let attr of conflictEntry.attrs[key]) {
                    conflict += key + ": " + attr + "\n";
                }
            }
        }
        for (const key in validEntry.attrs) {
            if (key == "numsubordinates") {
                validChildren = <font color="red">{validEntry.attrs[key]}</font>;
            }
            if (!ignoreAttrs.includes(key)) {
                for (let attr of validEntry.attrs[key]) {
                    valid += key + ": " + attr + "\n";
                }
            }
        }

        return (
            <Modal show={showModal} className="ds-modal-wide" onHide={closeHandler}>
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
                            Resolve Replication Conflict
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <div className="ds-modal-row">
                                <Row>
                                    <Col sm={5}>
                                        <Row>
                                            <h3>Conflict Entry</h3>
                                        </Row>
                                        <Row>
                                            <textarea className="ds-conflict" value={conflict} readOnly />
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <p>Child Entries: <b>{conflictChildren}</b></p>
                                        </Row>
                                    </Col>
                                    <Col sm={1} />
                                    <Col sm={5}>
                                        <Row>
                                            <h3>Valid Entry</h3>
                                        </Row>
                                        <Row>
                                            <textarea className="ds-conflict" value={valid} readOnly />
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <p>Child Entries: <b>{validChildren}</b></p>
                                        </Row>
                                    </Col>
                                </Row>
                                <hr />
                                <Row>
                                    <h4>You can convert the <b>Conflict Entry</b> into a new valid entry by providing a new RDN value below, like "<i>cn=NEW_RDN</i>"</h4>
                                </Row>
                                <Row>
                                    <Col sm={3}>
                                        <Button
                                            bsStyle="primary"
                                            className="ds-conflict-btn"
                                            onClick={() => {
                                                convertFunc(conflictEntry.dn);
                                            }}
                                        >
                                            Convert Conflict
                                        </Button>
                                    </Col>
                                    <Col sm={4}>
                                        <input onChange={handleConvertChange} type="text" placeholder="Enter new RDN here" size="30" />
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top">
                                    <h4>Or, you can replace, or swap, the <b>Valid Entry</b> (and its child entries) with the <b>Conflict Entry</b></h4>
                                </Row>
                                <Row>
                                    <Col sm={3}>
                                        <Button
                                            bsStyle="primary"
                                            className="ds-conflict-btn"
                                            onClick={() => {
                                                swapFunc(conflictEntry.dn);
                                            }}
                                        >
                                            Swap Entries
                                        </Button>
                                    </Col>
                                </Row>
                                <Row className="ds-margin-top">
                                    <h4>Or, you can delete the <b>Conflict Entry</b></h4>
                                </Row>
                                <Row>
                                    <Col sm={3}>
                                        <Button
                                            bsStyle="primary"
                                            className="ds-conflict-btn"
                                            onClick={() => {
                                                deleteFunc(conflictEntry.dn);
                                            }}
                                        >
                                            Delete Conflict
                                        </Button>
                                    </Col>
                                </Row>
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

class ReportCredentialsModal extends React.Component {
    render() {
        const {
            handleFieldChange,
            showModal,
            closeHandler,
            newEntry,
            hostname,
            port,
            binddn,
            pwInputInterractive,
            bindpw,
            addConfig,
            editConfig
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
                        <Modal.Title>{newEntry ? "Add" : "Edit"} Report Credentials</Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Row>
                            <Col sm={12}>
                                <Form horizontal autoComplete="off">
                                    <FormGroup controlId="credsHostname">
                                        <Col sm={3}>
                                            <ControlLabel title="A regex for hostname">
                                                Hostname
                                            </ControlLabel>
                                        </Col>
                                        <Col sm={9}>
                                            <FormControl
                                                type="text"
                                                value={hostname}
                                                onChange={handleFieldChange}
                                            />
                                        </Col>
                                    </FormGroup>
                                    <FormGroup controlId="credsPort">
                                        <Col sm={3}>
                                            <ControlLabel title="A regex for port">
                                                Port
                                            </ControlLabel>
                                        </Col>
                                        <Col sm={9}>
                                            <FormControl
                                                type="text"
                                                value={port}
                                                onChange={handleFieldChange}
                                            />
                                        </Col>
                                    </FormGroup>
                                    <FormGroup controlId="credsBinddn">
                                        <Col sm={3}>
                                            <ControlLabel title="Bind DN for the specified instances">
                                                Bind DN
                                            </ControlLabel>
                                        </Col>
                                        <Col sm={9}>
                                            <FormControl
                                                type="text"
                                                value={binddn}
                                                onChange={handleFieldChange}
                                            />
                                        </Col>
                                    </FormGroup>
                                    <FormGroup controlId="credsBindpw">
                                        <Col sm={3}>
                                            <ControlLabel title="Bind password for the specified instances">
                                                Password
                                            </ControlLabel>
                                        </Col>
                                        <Col sm={9}>
                                            <FormControl
                                                type="password"
                                                value={bindpw}
                                                onChange={handleFieldChange}
                                                disabled={pwInputInterractive}
                                            />
                                        </Col>
                                    </FormGroup>
                                    <FormGroup controlId="interractiveInput">
                                        <Col sm={3}>
                                            <ControlLabel title="Input the password interactively">
                                                Interractive Input
                                            </ControlLabel>
                                        </Col>
                                        <Col sm={9}>
                                            <Checkbox
                                                checked={pwInputInterractive}
                                                id="pwInputInterractive"
                                                onChange={handleFieldChange}
                                            />
                                        </Col>
                                    </FormGroup>
                                </Form>
                            </Col>
                        </Row>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button bsStyle="default" className="btn-cancel" onClick={closeHandler}>
                            Cancel
                        </Button>
                        <Button bsStyle="primary" onClick={newEntry ? addConfig : editConfig}>
                            Save
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class ReportAliasesModal extends React.Component {
    render() {
        const {
            handleFieldChange,
            showModal,
            closeHandler,
            newEntry,
            hostname,
            port,
            alias,
            addConfig,
            editConfig
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
                        <Modal.Title>{newEntry ? "Add" : "Edit"} Report Credentials</Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Row>
                            <Col sm={12}>
                                <Form horizontal>
                                    <FormGroup controlId="aliasName">
                                        <Col sm={3}>
                                            <ControlLabel title="Alias name for the instance">
                                                Alias
                                            </ControlLabel>
                                        </Col>
                                        <Col sm={9}>
                                            <FormControl
                                                type="text"
                                                value={alias}
                                                onChange={handleFieldChange}
                                            />
                                        </Col>
                                    </FormGroup>
                                    <FormGroup controlId="aliasHostname">
                                        <Col sm={3}>
                                            <ControlLabel title="An instance hostname">
                                                Hostname
                                            </ControlLabel>
                                        </Col>
                                        <Col sm={9}>
                                            <FormControl
                                                type="text"
                                                value={hostname}
                                                onChange={handleFieldChange}
                                            />
                                        </Col>
                                    </FormGroup>
                                    <FormGroup controlId="aliasPort">
                                        <Col sm={3}>
                                            <ControlLabel title="An instance port">
                                                Port
                                            </ControlLabel>
                                        </Col>
                                        <Col sm={9}>
                                            <FormControl
                                                type="number"
                                                value={port}
                                                onChange={handleFieldChange}
                                            />
                                        </Col>
                                    </FormGroup>
                                </Form>
                            </Col>
                        </Row>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button bsStyle="default" className="btn-cancel" onClick={closeHandler}>
                            Cancel
                        </Button>
                        <Button bsStyle="primary" onClick={newEntry ? addConfig : editConfig}>
                            Save
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class ReportLoginModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            processCredsInput,
            instanceName,
            disableBinddn,
            loginBinddn,
            loginBindpw
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
                        <Modal.Title>Replication Login Credentials for {instanceName}</Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <p>
                                In order to get the replication agreement lag times and state the
                                authentication credentials to the remote replicas must be provided.
                            </p>
                            <hr />
                            <p>
                                Bind DN was acquired from <b>Replica Credentials</b> table. If you want
                                to bind as another user, change or remove the Bind DN there.
                            </p>
                            <br />
                            <FormGroup controlId="loginBinddn">
                                <Col sm={3}>
                                    <ControlLabel title="Bind DN for the instance">
                                        Bind DN
                                    </ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <FormControl
                                        type="text"
                                        value={loginBinddn}
                                        onChange={handleChange}
                                        disabled={disableBinddn}
                                    />
                                </Col>
                            </FormGroup>
                            <FormGroup controlId="loginBindpw">
                                <Col sm={3}>
                                    <ControlLabel title="Password for the Bind DN">
                                        Password
                                    </ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <FormControl
                                        type="password"
                                        value={loginBindpw}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </FormGroup>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button bsStyle="default" className="btn-cancel" onClick={closeHandler}>
                            Close
                        </Button>
                        <Button bsStyle="primary" onClick={processCredsInput}>
                            Confirm Credentials Input
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class FullReportContent extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            oneTableReport: false,
            showDisabledAgreements: false
        };

        this.handleSwitchChange = this.handleSwitchChange.bind(this);
    }

    handleSwitchChange(e) {
        if (typeof e === "boolean") {
            // Handle Switch object
            this.setState({
                oneTableReport: e
            });
        } else {
            this.setState({
                [e.target.id]: e.target.checked
            });
        }
    }

    render() {
        const {
            reportData,
            handleRefresh,
            reportRefreshing,
            reportLoading
        } = this.props;

        let suppliers = [];
        let supplierName;
        let supplierData;
        let resultRows = [];
        let spinner = <ControlLabel />;
        if (reportLoading) {
            spinner = (
                <div>
                    <ControlLabel title="Do the refresh every few seconds">
                        {reportRefreshing ? "Refreshing" : "Loading"} the report...
                    </ControlLabel>
                    <Spinner inline loading size="sm" />
                </div>
            );
        }
        let reportHeader = "";
        if (reportData.length > 0) {
            reportHeader = (
                <Form horizontal autoComplete="off">
                    <FormGroup controlId="showDisabledAgreements">
                        <Col sm={8}>
                            <Checkbox
                                checked={this.state.showDisabledAgreements}
                                id="showDisabledAgreements"
                                onChange={this.handleSwitchChange}
                                title="Display all agreements including the disabled ones and the ones we failed to connect to"
                            >
                                Show All (Including Disabled Agreements)
                            </Checkbox>
                        </Col>
                    </FormGroup>
                    <FormGroup controlId="oneTableReport">
                        <Col sm={6} title="Show all data in one table (it makes it easier to check lag times)">
                            <Checkbox
                                checked={this.state.oneTableReport}
                                onChange={this.handleSwitchChange}
                                id="oneTableReport"
                                title="Display all agreements including the disabled ones and the ones we failed to connect to"
                            >
                                Table View
                            </Checkbox>
                        </Col>
                    </FormGroup>
                    <Button
                        className="ds-margin-top"
                        bsStyle="default"
                        onClick={handleRefresh}
                    >
                        Refresh Report
                    </Button>
                    <hr />
                </Form>
            );
        } else {
            reportHeader = spinner;
        }
        if (this.state.oneTableReport) {
            for (let supplier of reportData) {
                for (let replica of supplier.data) {
                    resultRows = resultRows.concat(replica.agmts_status);
                }
                suppliers.push(supplierData);
            }
            suppliers = [(<div>
                <ReportSingleTable
                    rows={resultRows}
                    viewAgmt={this.props.viewAgmt}
                />
            </div>
            )];
        } else {
            for (let supplier of reportData) {
                let s_data = supplier.data;
                if (s_data.length === 1 && s_data[0].replica_status.startsWith("Unavailable")) {
                    supplierData = (
                        <div>
                            <h4>
                                <b>Can not get replication information from Replica</b>
                            </h4>
                            <h4 title="Supplier availability status">
                                <b>Replica Status:</b> {s_data[0].replica_status}
                            </h4>
                        </div>
                    );
                } else {
                    supplierData = supplier.data.map(replica => (
                        <div key={replica.replica_root + replica.replica_id}>
                            <h4 title="Replica Root suffix">
                                <b>Replica Root:</b> {replica.replica_root}
                            </h4>
                            <h4 title="Replica ID">
                                <b>Replica ID:</b> {replica.replica_id}
                            </h4>
                            <h4 title="Replica Status">
                                <b>Replica Status:</b> {replica.replica_status}
                            </h4>
                            <h4 title="Max CSN">
                                <b>Max CSN:</b> {replica.maxcsn}
                            </h4>
                            {"agmts_status" in replica &&
                            replica.agmts_status.length > 0 &&
                            "agmt-name" in replica.agmts_status[0] ? (
                                <ReportConsumersTable
                                    rows={replica.agmts_status}
                                    viewAgmt={this.props.viewAgmt}
                                    />
                                ) : (
                                    <h4>
                                        <b>No Agreements Were Found</b>
                                    </h4>
                                )}
                        </div>
                    ));
                }
                supplierName = (
                    <div key={supplier.name}>
                        <center>
                            <h2 title="Supplier host:port (and alias if applicable)">
                                <b>Supplier:</b> {supplier.name}
                            </h2>
                        </center>
                        <hr />
                        {supplierData}
                    </div>
                );
                suppliers.push(supplierName);
            }
        }

        let report = suppliers.map(supplier => (
            <div key={supplier.key}>
                {supplier}
                <hr />
            </div>
        ));
        if (reportLoading) {
            report =
                <Col sm={12} className="ds-center ds-margin-top">
                    {spinner}
                </Col>;
        }

        return (
            <div>
                {reportHeader}
                {report}
            </div>
        );
    }
}
// Prototypes and defaultProps
AgmtDetailsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    agmt: PropTypes.object,
    initAgmt: PropTypes.func,
    isRemoteAgmt: PropTypes.bool
};

AgmtDetailsModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    agmt: {},
    initAgmt: noop,
    isRemoteAgmt: false
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

ConflictCompareModal.propTypes = {
    showModal: PropTypes.bool,
    conflictEntry: PropTypes.object,
    validEntry: PropTypes.object,
    swapFunc: PropTypes.func,
    convertFunc: PropTypes.func,
    closeHandler: PropTypes.func,
};

ConflictCompareModal.defaultProps = {
    showModal: false,
    conflictEntry: {},
    validEntry: {},
    swapFunc: noop,
    convertFunc: noop,
    closeHandler: noop,
};

ReportCredentialsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleFieldChange: PropTypes.func,
    hostname: PropTypes.string,
    port: PropTypes.string,
    binddn: PropTypes.string,
    bindpw: PropTypes.string,
    pwInputInterractive: PropTypes.bool,
    newEntry: PropTypes.bool,
    addConfig: PropTypes.func,
    editConfig: PropTypes.func
};

ReportCredentialsModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleFieldChange: noop,
    hostname: "",
    port: "",
    binddn: "",
    bindpw: "",
    pwInputInterractive: false,
    newEntry: false,
    addConfig: noop,
    editConfig: noop,
};

ReportAliasesModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleFieldChange: PropTypes.func,
    hostname: PropTypes.string,
    port: PropTypes.number,
    alias: PropTypes.string,
    newEntry: PropTypes.bool,
    addConfig: PropTypes.func,
    editConfig: PropTypes.func
};

ReportAliasesModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleFieldChange: noop,
    hostname: "",
    port: 389,
    alias: "",
    newEntry: false,
    addConfig: noop,
    editConfig: noop,
};

ReportLoginModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    processCredsInput: PropTypes.func,
    instanceName: PropTypes.string,
    disableBinddn: PropTypes.bool,
    loginBinddn: PropTypes.string,
    loginBindpw: PropTypes.string
};

ReportLoginModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    processCredsInput: noop,
    instanceName: "",
    disableBinddn: false,
    loginBinddn: "",
    loginBindpw: ""
};

FullReportContent.propTypes = {
    reportData: PropTypes.array,
    handleRefresh: PropTypes.func,
    reportRefreshing: PropTypes.bool
};

FullReportContent.defaultProps = {
    handleFieldChange: noop,
    reportData: [],
    handleRefresh: noop,
    reportRefreshTimeout: 5,
    reportRefreshing: false
};

export {
    TaskLogModal,
    AgmtDetailsModal,
    WinsyncAgmtDetailsModal,
    ConflictCompareModal,
    ReportCredentialsModal,
    ReportAliasesModal,
    ReportLoginModal,
    FullReportContent
};
