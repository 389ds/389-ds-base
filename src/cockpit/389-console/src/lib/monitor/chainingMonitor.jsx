import React from "react";
import PropTypes from "prop-types";
import {
    Row,
    Col,
    ControlLabel,
    Icon,
    noop
} from "patternfly-react";

export class ChainingMonitor extends React.Component {
    componentDidMount() {
        this.props.enableTree();
    }

    render() {
        return (
            <div id="monitor-server-page" className="container-fluid">
                <Row>
                    <Col sm={12} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header">
                            <Icon type="fa" name="link" /> {this.props.suffix} (<i>{this.props.bename}</i>)
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh chaining monitor"
                                onClick={() => this.props.reload(this.props.suffix)}
                            />
                        </ControlLabel>
                    </Col>
                </Row>
                <div className="ds-margin-top-lg">
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Add Operations
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nsaddcount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Delete Operations
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nsdeletecount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Modify Operations
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nsmodifycount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                ModRDN Operations
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nsrenamecount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Base Searches
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nssearchbasecount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                One-Level Searches
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nssearchonelevelcount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Subtree Searches
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nssearchsubtreecount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Abandon Operations
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nsabandoncount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Bind Operations
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nsbindcount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Unbind Operations
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nsunbindcount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Compare Operations
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nscomparecount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Outgoing Connections
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nsopenopconnectioncount} size="35" readOnly />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col sm={3}>
                            <ControlLabel>
                                Outgoing Bind Connections
                            </ControlLabel>
                        </Col>
                        <Col sm={3}>
                            <input type="text" value={this.props.data.nsopenbindconnectioncount} size="35" readOnly />
                        </Col>
                    </Row>
                </div>
            </div>
        );
    }
}

ChainingMonitor.propTypes = {
    suffix: PropTypes.string,
    bename: PropTypes.string,
    data: PropTypes.object,
    enableTree: PropTypes.func,
};

ChainingMonitor.defaultProps = {
    suffix: "",
    bename: "",
    data: {},
    enableTree: noop,
};

export default ChainingMonitor;
