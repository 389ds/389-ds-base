import React from "react";
import PropTypes from "prop-types";
import "../../css/ds.css";
import {
    Row,
    Col,
    Icon,
    ControlLabel,
    noop
} from "patternfly-react";

export class SNMPMonitor extends React.Component {
    render() {
        return (
            <div className="container-fluid" id="db-global-page">
                <Row>
                    <Col sm={12} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header">
                            SNMP Counters
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh SNMP monitor"
                                onClick={this.props.reload}
                            />
                        </ControlLabel>
                    </Col>
                </Row>
                <div className="ds-margin-top-med">
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Anonymous Binds</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.anonymousbinds} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Referrals</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.referrals} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Unauthenticated Binds</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.anonymousbinds} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Returned Referrals</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.referralsreturned} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Simple Auth Binds</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.simpleauthbinds} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Bind Security Errors</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.bindsecurityerrors} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Strong Auth Binds</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.strongauthbinds} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Security Errors</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.securityerrors} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Initiated Operations</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.inops} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Errors</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.errors} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Compare Operations</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.compareops} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Current Connections</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.connections} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Add Operations</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.addentryops} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Total Connections</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.connectionseq} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Delete Operations</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.removeentryops} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Conns in Max Threads</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.connectionsinmaxthreads} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Modify Operation</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.modifyentryops} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Conns Hit Max Threads</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.connectionsmaxthreadscount} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>ModRDN Operations</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.modifyrdnops} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Bytes Received</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.bytesrecv} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Search Operations</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.searchops} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Bytes Sent</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.bytessent} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>One Level Searches</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.onelevelsearchops} size="10" readOnly />
                        </Col>
                        <Col sm={3}>
                            <ControlLabel>Entries Sent</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.entriesreturned} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>Whole Tree Searches</ControlLabel>
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.wholesubtreesearchops} size="10" readOnly />
                        </Col>

                    </Row>
                </div>
            </div>
        );
    }
}

// Prop types and defaults

SNMPMonitor.propTypes = {
    data: PropTypes.object,
    reload: PropTypes.func
};

SNMPMonitor.defaultProps = {
    data: {},
    reload: noop
};

export default SNMPMonitor;
