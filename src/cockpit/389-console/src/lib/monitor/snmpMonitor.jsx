import React from "react";
import PropTypes from "prop-types";
import "../../css/ds.css";
import {
    Row,
    Col,
    Form,
    Icon,
    ControlLabel,
    noop
} from "patternfly-react";

export class SNMPMonitor extends React.Component {
    componentDidMount() {
        this.props.enableTree();
    }

    render() {
        return (
            <div id="snmp-page">
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
                <Form horizontal className="ds-margin-top-lg">
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Anonymous Binds
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.anonymousbinds} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Referrals
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.referrals} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Unauthenticated Binds
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.anonymousbinds} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Returned Referrals
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.referralsreturned} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Simple Auth Binds
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.simpleauthbinds} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Bind Security Errors
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.bindsecurityerrors} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Strong Auth Binds
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.strongauthbinds} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Security Errors
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.securityerrors} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Initiated Operations
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.inops} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Errors
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.errors} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Compare Operations
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.compareops} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Current Connections
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.connections} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Add Operations
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.addentryops} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Total Connections
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.connectionseq} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Delete Operations
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.removeentryops} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Conns in Max Threads
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.connectionsinmaxthreads} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Modify Operation
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.modifyentryops} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Conns Exceeded Max Threads
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.connectionsmaxthreadscount} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            ModRDN Operations
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.modifyrdnops} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Bytes Received
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.bytesrecv} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Search Operations
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.searchops} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Bytes Sent
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.bytessent} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            One Level Searches
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.onelevelsearchops} size="10" readOnly />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Entries Sent
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.entriesreturned} size="10" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Whole Tree Searches
                        </Col>
                        <Col sm={2}>
                            <input type="text" value={this.props.data.wholesubtreesearchops} size="10" readOnly />
                        </Col>

                    </Row>
                </Form>
            </div>
        );
    }
}

// Prop types and defaults

SNMPMonitor.propTypes = {
    data: PropTypes.object,
    reload: PropTypes.func,
    enableTree: PropTypes.func,
};

SNMPMonitor.defaultProps = {
    data: {},
    reload: noop,
    enableTree: noop,
};

export default SNMPMonitor;
