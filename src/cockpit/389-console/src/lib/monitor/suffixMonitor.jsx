import React from "react";
import PropTypes from "prop-types";
import "../../css/ds.css";
import {
    DonutChart,
    PieChart,
    ControlLabel,
    Row,
    Col,
    Form,
    Icon,
    Nav,
    NavItem,
    TabContent,
    TabPane,
    TabContainer,
    noop
} from "patternfly-react";
import d3 from "d3";

export class SuffixMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeKey: 1,
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
        let donutColorEC = goodColor;
        let donutColorECMiss = emptyColor;
        let donutColorECUtil = goodColor;
        let donutColorDN = goodColor;
        let donutColorDNMiss = emptyColor;
        let donutColorDNUtil = goodColor;

        // Entry cache
        let cachehit = parseInt(this.props.data.entrycachehitratio[0]);
        let cachemax = parseInt(this.props.data.maxentrycachesize[0]);
        let cachecurr = parseInt(this.props.data.currententrycachesize[0]);
        let cachecount = parseInt(this.props.data.currententrycachecount[0]);
        let utilratio = Math.round((cachecurr / cachemax) * 100);
        // DN cache
        let dncachehit = parseInt(this.props.data.dncachehitratio[0]);
        let dncachemax = parseInt(this.props.data.maxdncachesize[0]);
        let dncachecurr = parseInt(this.props.data.currentdncachesize[0]);
        let dncachecount = parseInt(this.props.data.currentdncachecount[0]);
        let dnutilratio = Math.round((dncachecurr / dncachemax) * 100);

        // Adjust ratios if needed
        if (utilratio == 0) {
            utilratio = 1;
        }
        if (dnutilratio == 0) {
            dnutilratio = 1;
        }

        // Entry Cache
        if (cachehit > 89) {
            donutColorEC = goodColor;
        } else if (cachehit > 74) {
            donutColorEC = warningColor;
        } else {
            if (cachehit < 50) {
                // Pie chart shows higher catagory, so we need to highlight the misses
                donutColorECMiss = badColor;
            } else {
                donutColorEC = badColor;
            }
        }
        // Entry cache utilization
        if (cachehit < 90) {
            if (utilratio > 95) {
                donutColorECUtil = badColor;
            } else if (utilratio > 90) {
                donutColorECUtil = warningColor;
            }
        }
        // DN cache ratio
        if (dncachehit > 89) {
            donutColorDN = goodColor;
        } else if (dncachehit > 74) {
            donutColorDN = warningColor;
        } else {
            if (dncachehit < 50) {
                // Pie chart shows higher catagory, so we need to highlight the misses
                donutColorDNMiss = badColor;
            } else {
                donutColorDN = badColor;
            }
        }
        // DN cache utilization
        if (dncachehit < 90) {
            if (dnutilratio > 95) {
                donutColorDNUtil = badColor;
            } else if (dnutilratio > 90) {
                donutColorDNUtil = warningColor;
            }
        }

        let suffixIcon = "tree";
        if (this.props.dbtype == "subsuffix") {
            suffixIcon = "leaf";
        }

        return (
            <div id="monitor-suffix-page">
                <Row>
                    <Col sm={12} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header">
                            <Icon type="fa" name={suffixIcon} /> {this.props.suffix} (<i>{this.props.bename}</i>)
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh suffix monitor"
                                onClick={() => this.props.reload(this.props.suffix)}
                            />
                        </ControlLabel>
                    </Col>
                </Row>
                <TabContainer className="ds-margin-top-lg" id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div>
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: 'Entry Cache'}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: 'DN Cache'}} />
                            </NavItem>
                        </Nav>
                        <TabContent>

                            <TabPane eventKey={1}>
                                <div className="ds-margin-top-lg">
                                    <div className="ds-container">
                                        <div className="ds-divider" />
                                        <div className="ds-left-margin" title="The entry cache hit ratio.  If the chart is RED then the hit ratio is below 90% and might require further cache tuning">
                                            <DonutChart
                                                id="monitor-entry-cache-pie"
                                                size={{width: 180, height: 120}}
                                                data={{
                                                    columns: [['miss', 100 - cachehit], ['hit', cachehit]],
                                                    colors: {
                                                        'hit': donutColorEC,
                                                        'miss': donutColorECMiss,
                                                    },
                                                    order: null,
                                                    unload: true
                                                }}
                                                title={{type: 'percent'}}
                                                legend={{show: true, position: 'right'}}
                                            />
                                            <b>Entry Cache Hit Ratio</b>
                                        </div>
                                        <div className="ds-divider" />
                                        <div className="ds-divider" />
                                        <div className="ds-chart-right" title="How much of the allocated cache space is used (max size vs current size).  If the chart is RED then you should to increase the max cache size because the cache hit ratio is below 90%">
                                            <PieChart
                                                id="monitor-entry-util-pie"
                                                size={{width: 180, height: 120}}
                                                data={{
                                                    columns: [
                                                        ['Used', utilratio],
                                                        ['Unused', 100 - utilratio],
                                                    ],
                                                    colors: {
                                                        'Used': donutColorECUtil,
                                                        'Unused': emptyColor,
                                                    },
                                                    order: null,
                                                    type: 'pie'
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
                                            <b>Entry Cache Utilization</b>
                                            <div>
                                                (Entries in cache: <b>{cachecount}</b>)
                                            </div>
                                        </div>
                                    </div>
                                    <hr />
                                    <Form horizontal>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Entry Cache Hit Ratio
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.entrycachehitratio} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Entry Cache Tries
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.entrycachetries} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Entry Cache Hits
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.entrycachehits} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Entry Cache Max Size
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.maxentrycachesize} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Entry Cache Current Size
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.currententrycachesize} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Entry Cache Max Entries
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.maxentrycachecount} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                Entry Cache Count
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.currententrycachecount} size="35" readOnly />
                                            </Col>
                                        </Row>
                                    </Form>
                                </div>
                            </TabPane>

                            <TabPane eventKey={2}>
                                <div className="ds-margin-top-lg">
                                    <div className="ds-container">
                                        <div className="ds-divider" />
                                        <div className="ds-left-margin" title="The DN cache hit ratio.  If the chart is RED then the hit ratio is below 90% and might require further cache tuning">
                                            <DonutChart
                                                id="monitor-entry-cache-pie"
                                                size={{width: 180, height: 120}}
                                                data={{
                                                    columns: [['miss', 100 - dncachehit], ['hit', dncachehit]],
                                                    colors: {
                                                        'hit': donutColorDN,
                                                        'miss': donutColorDNMiss,
                                                    },
                                                    order: null,
                                                }}
                                                title={{type: 'percent'}}
                                                legend={{show: true, position: 'right'}}
                                            />
                                            <b className="ds-left-margin">DN Cache Hit Ratio</b>
                                        </div>
                                        <div className="ds-divider" />
                                        <div className="ds-divider" />
                                        <div className="ds-chart-right" title="How much of the allocated cache space is used (max size vs current size).  If the chart is RED then you should to increase the max cache size because the cache hit ratio is below 90%">
                                            <PieChart
                                                id="monitor-entry-util-pie"
                                                size={{width: 180, height: 120}}
                                                data={{
                                                    columns: [
                                                        ['Used', dnutilratio],
                                                        ['Unused', 100 - dnutilratio],
                                                    ],
                                                    colors: {
                                                        'Used': donutColorDNUtil,
                                                        'Unused': emptyColor,
                                                    },
                                                    order: null,
                                                    type: 'pie'
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
                                            <div className="ds-left-margin">
                                                <b>DN Cache Utilization</b>
                                                <div>
                                                    (DN's in cache: <b>{dncachecount}</b>)
                                                </div>
                                            </div>
                                        </div>
                                    </div>
                                    <hr />
                                    <Form horizontal>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                DN Cache Hit Ratio
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.dncachehitratio} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                DN Cache Tries
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.dncachetries} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                DN Cache Hits
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.dncachehits} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                DN Cache Max Size
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.maxdncachesize} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                DN Cache Current Size
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.currentdncachesize} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                DN Cache Max Count
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.maxdncachecount} size="35" readOnly />
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top">
                                            <Col componentClass={ControlLabel} sm={3}>
                                                DN Cache Current Count
                                            </Col>
                                            <Col sm={3}>
                                                <input type="text" value={this.props.data.currentdncachecount} size="35" readOnly />
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

SuffixMonitor.propTypes = {
    suffix: PropTypes.string,
    data: PropTypes.object,
    bename: PropTypes.string,
    reload: PropTypes.func,
    enableTree: PropTypes.func,
};

SuffixMonitor.defaultProps = {
    suffix: "",
    data: {},
    bename: "",
    reload: noop,
    enableTree: noop,
};

export default SuffixMonitor;
