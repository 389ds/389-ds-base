import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup } from "../notifications.jsx";
import { ReferralTable } from "./databaseTables.jsx";
import {
    Modal,
    Row,
    ControlLabel,
    Col,
    Icon,
    Button,
    Form,
    noop
} from "patternfly-react";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import "../../css/ds.css";

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
            refScope: "",
            refAttrs: "",
            refValue: "",
            errObj: {},
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
    }

    showConfirmRefDelete(item) {
        this.setState({
            removeRef: item.name,
            showConfirmRefDelete: true
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
            showConfirmRefDelete: false
        });
    }

    closeRefModal() {
        this.setState({
            showRefModal: false
        });
    }

    deleteRef() {
        // take state.removeRef and remove it from ldap
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
                })
                .fail(err => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failure deleting referral - ${err}`
                    );
                });
    }

    // Create referral
    saveRef() {
        // validate
        let missingArgs = {
            refHost: false,
            refPort: false
        };
        let errors = false;
        if (this.state.refHost == "") {
            this.props.addNotification(
                "warning",
                `Missing hostname for LDAP URL`
            );
            missingArgs.refHost = true;
            errors = true;
        }
        if (this.state.refPort == "") {
            this.props.addNotification(
                "warning",
                `Missing port for LDAP URL`
            );
            missingArgs.refPort = true;
            errors = true;
        }
        if (errors) {
            this.setState({
                errObj: missingArgs
            });
            return;
        }

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
                })
                .fail(err => {
                    this.props.reload(this.props.suffix);
                    this.closeRefModal();
                    this.props.addNotification(
                        "error",
                        `Failure creating referral - ${err}`
                    );
                });
    }

    buildRef() {
        let previewRef = this.state.refProtocol + this.state.refHost;
        let ref_port = this.state.refPort;
        let ref_suffix = this.state.refSuffix;
        let ref_attrs = this.state.refAttrs;
        let ref_filter = this.state.refFilter;
        let ref_scope = this.state.refScope;

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
        const key = e.target.id;
        let errObj = this.state.errObj;
        let valueErr = false;

        if (value == "") {
            if (key == "refHost" || key == "refPort") {
                valueErr = true;
            }
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        }, this.buildRef);
    }

    render() {
        let refs = [];
        for (let row of this.props.rows) {
            refs.push({name: row});
        }

        return (
            <div className="ds-sub-header">
                <ReferralTable
                    rows={refs}
                    loadModalHandler={this.showConfirmRefDelete}
                />
                <button className="btn btn-primary ds-margin-top" onClick={this.showRefModal} type="button">Create Referral</button>
                <ConfirmPopup
                    showModal={this.state.showConfirmRefDelete}
                    closeHandler={this.closeConfirmRefDelete}
                    actionFunc={this.deleteRef}
                    actionParam={this.state.removeRef}
                    msg="Are you sure you want to delete this referral?"
                    msgContent={this.state.removeRef}
                />
                <AddReferralModal
                    showModal={this.state.showRefModal}
                    closeHandler={this.closeRefModal}
                    handleChange={this.handleRefChange}
                    saveHandler={this.saveRef}
                    previewValue={this.state.refValue}
                    error={this.state.errObj}
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
            saveHandler,
            previewValue,
            error
        } = this.props;

        if (previewValue == "") {
            previewValue = "ldap://";
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
                            Add Database Referral
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>Protocol</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <select
                                        defaultValue="ldap://" onChange={this.props.handleChange} className="btn btn-default dropdown" id="refProtocol">
                                        <option>ldap://</option>
                                        <option>ldaps://</option>
                                    </select>
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>Host Name</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <input className={error.refHost ? "ds-input-auto-bad" : "ds-input-auto"} type="text" onChange={handleChange} id="refHost" />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>Port Number</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <input className={error.refPort ? "ds-input-auto-bad" : "ds-input-auto"} type="text" onChange={handleChange} id="refPort" />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>Suffix</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <input className="ds-input-auto" onChange={handleChange} type="text" id="refSuffix" />
                                </Col>
                            </Row>
                            <p />
                            <Row title="Comma separated list of attributes to return">
                                <Col sm={3}>
                                    <ControlLabel>Attributes</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <input className="ds-input-auto" onChange={handleChange} type="text" id="refAttrs" />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>Filter</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <input onChange={handleChange} className="ds-input-auto" type="text" id="refFilter" />
                                </Col>
                            </Row>
                            <p />
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>Scope</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <select className="btn btn-default dropdown" onChange={handleChange} defaultValue="" name="refScope">
                                        <option />
                                        <option>sub</option>
                                        <option>one</option>
                                        <option>base</option>
                                    </select>
                                </Col>
                            </Row>
                            <hr />
                            <Row>
                                <Col sm={3}>
                                    <label className="ds-label-sm">Computed Referral</label>
                                </Col>
                                <Col sm={9}>
                                    <input className="ds-input-auto" value={previewValue} readOnly />
                                </Col>
                            </Row>
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
                            Create Referral
                        </Button>
                    </Modal.Footer>
                </div>
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
    reload: noop,
    addNotification: noop,
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
    showModal: noop,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    previewValue: "",
    error: {},
};
