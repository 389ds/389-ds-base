import React from "react";
import PropTypes from "prop-types";
import {
    DonutChart,
    PieChart,
    Nav,
    NavItem,
    TabContent,
    TabPane,
    TabContainer,
    Row,
    Col,
    ControlLabel,
    Form,
    Icon,
    noop,
} from "patternfly-react";
import d3 from "d3";

export class DatabaseMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeKey: 1,
            disableTabs: false,
        };
        this.handleNavSelect = this.handleNavSelect.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    render() {
        let badColor = "#d01c8b";
        let warningColor = "#ffc107";
        let goodColor = "#4dac26";
        let emptyColor = "#d3d3d3";
        let donutColorDB = goodColor;
        let donutColorNDN = goodColor;
        let donutColorNDNUtil = goodColor;
        let donutColorNDNMiss = emptyColor;
        let donutColorDBMiss = emptyColor;
        let dbcachehit = parseInt(this.props.data.dbcachehitratio[0]);
        let ndncachehit = parseInt(this.props.data.normalizeddncachehitratio[0]);
        let ndncachemax = parseInt(this.props.data.maxnormalizeddncachesize[0]);
        let ndncachecurr = parseInt(this.props.data.currentnormalizeddncachesize[0]);
        let utilratio = Math.round((ndncachecurr / ndncachemax) * 100);

        // Database cache
        if (dbcachehit > 89) {
            donutColorDB = goodColor;
        } else if (dbcachehit > 74) {
            donutColorDB = warningColor;
        } else {
            if (dbcachehit < 50) {
                // Pie chart shows higher catagory, so we need to highlight the misses
                donutColorDBMiss = badColor;
            } else {
                donutColorDB = badColor;
            }
        }
        // NDN cache ratio
        if (ndncachehit > 89) {
            donutColorNDN = goodColor;
        } else if (ndncachehit > 74) {
            donutColorNDN = warningColor;
        } else {
            if (ndncachehit < 50) {
                // Pie chart shows higher catagory, so we need to highlight the misses
                donutColorNDNMiss = badColor;
            } else {
                donutColorNDN = badColor;
            }
        }
        // NDN cache utilization
        if (ndncachehit < 90) {
            if (utilratio > 95) {
                donutColorNDNUtil = badColor;
            } else if (utilratio > 90) {
                donutColorNDNUtil = warningColor;
            }
        }

        return (
            <div id="db-content">
                <Row>
                    <Col sm={12} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header">
                            Database Performance Statistics
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh database monitor"
                                onClick={this.props.reload}
                            />
                        </ControlLabel>
                    </Col>
                </Row>
                <TabContainer className="ds-margin-top-lg" id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div>
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: 'Database Cache'}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: 'Normalized DN Cache'}} />
                            </NavItem>
                        </Nav>
                        <TabContent>

                            <TabPane eventKey={1}>
                                <div className="ds-margin-top-lg ds-margin-left-piechart">
                                    <DonutChart
                                        id="monitor-db-cache-hitratio-chart"
                                        size={{width: 180, height: 120}}
                                        data={{
                                            columns: [['miss', 100 - dbcachehit], ['hit', dbcachehit]],
                                            colors: {
                                                'hit': donutColorDB,
                                                'miss': donutColorDBMiss,
                                            },
                                            order: null,
                                        }}
                                        title={{type: 'percent'}}
                                        legend={{show: true, position: 'right'}}
                                    />
                                    <b className="ds-left-margin">DB Cache Hit Ratio</b>
                                </div>
                                <hr />
                                <Form horizontal>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Database Cache Hit Ratio
                                        </Col>
                                        <Col sm={3}>
                                            <input type="text" value={this.props.data.dbcachehitratio} size="28" readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Database Cache Tries
                                        </Col>
                                        <Col sm={3}>
                                            <input type="text" value={this.props.data.dbcachetries} size="28" readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Database Cache Hits
                                        </Col>
                                        <Col sm={3}>
                                            <input type="text" value={this.props.data.dbcachehits} size="28" readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Cache Pages Read
                                        </Col>
                                        <Col sm={3}>
                                            <input type="text" value={this.props.data.dbcachepagein} size="28" readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Cache Pages Written
                                        </Col>
                                        <Col sm={3}>
                                            <input type="text" value={this.props.data.dbcachepageout} size="28" readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Read-Only Page Evictions
                                        </Col>
                                        <Col sm={3}>
                                            <input type="text" value={this.props.data.dbcacheroevict} size="28" readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={3}>
                                            Read-Write Page Evictions
                                        </Col>
                                        <Col sm={3}>
                                            <input type="text" value={this.props.data.dbcacherwevict} size="28" readOnly />
                                        </Col>
                                    </Row>
                                </Form>
                            </TabPane>

                            <TabPane eventKey={2}>
                                <div className="ds-margin-top-lg">
                                    <div className="ds-container">
                                        <div className="ds-divider" />
                                        <div className="ds-left-margin">
                                            <DonutChart
                                                id="monitor-db-cache-ndn-hitratio-chart"
                                                size={{width: 180, height: 120}}
                                                data={{
                                                    columns: [['miss', 100 - ndncachehit], ['hit', ndncachehit]],
                                                    colors: {
                                                        'hit': donutColorNDN,
                                                        'miss': donutColorNDNMiss,
                                                    },
                                                    order: null,
                                                }}
                                                title={{type: 'percent'}}
                                                legend={{show: true, position: 'right'}}
                                            />
                                            <b>NDN Cache Hit Ratio</b>
                                        </div>
                                        <div className="ds-divider" />
                                        <div className="ds-divider" />
                                        <div className="ds-chart-right">
                                            <PieChart
                                                id="monitor-db-cache-ndn-util-chart"
                                                size={{width: 180, height: 120}}
                                                data={{
                                                    columns: [
                                                        ['Used', utilratio],
                                                        ['Unused', 100 - utilratio],
                                                    ],
                                                    colors: {
                                                        'Used': donutColorNDNUtil,
                                                        'Unused': emptyColor,
                                                    },
                                                    order: null,
                                                }}
                                                pie={{
                                                    label: {
                                                        format: function (value, ratio, id) {
                                                            return d3.format(',%')(value / 100);
                                                        }
                                                    }
                                                }}
                                                title={{type: 'pie'}}
                                                legend={{show: true, position: 'right'}}
                                                unloadBeforeLoad
                                            />
                                            <b>NDN Cache Utilization</b>
                                            <div>
                                                (DN's in cache: <b>{this.props.data.currentnormalizeddncachecount}</b>)
                                            </div>
                                        </div>
                                    </div>
                                    <hr />
                                    <Form horizontal>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                NDN Cache Hit Ratio
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.normalizeddncachehitratio} size="28" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                NDN Cache Tries
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.normalizeddncachetries} size="28" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                NDN Cache Hits
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.normalizeddncachehits} size="28" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                NDN Cache Evictions
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.normalizeddncacheevictions} size="28" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                NDN Cache Max Size
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.maxnormalizeddncachesize} size="28" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                NDN Current Cache Size
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.currentnormalizeddncachesize} size="28" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                NDN Cache DN Count
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.currentnormalizeddncachecount} size="28" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                NDN Cache Thread Size
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.normalizeddncachethreadsize} size="28" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                NDN Cache Thread Slots
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.normalizeddncachethreadslots} size="28" readOnly />
                                            </Col>
                                        </Row>
                                    </Form>
                                </div>
                            </TabPane>
                        </TabContent>
                    </div>
                </TabContainer>
            </div>
        );
    }
}

// Prop types and defaults

DatabaseMonitor.propTypes = {
    data: PropTypes.object,
    reload: PropTypes.func,
    enableTree: PropTypes.func,
};

DatabaseMonitor.defaultProps = {
    data: {},
    reload: noop,
    enableTree: noop,
};

export default DatabaseMonitor;
