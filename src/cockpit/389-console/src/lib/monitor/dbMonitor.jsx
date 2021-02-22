import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import {
    Card,
    CardBody,
    Grid,
    GridItem,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    noop
} from "@patternfly/react-core";
import {
    Chart,
    ChartArea,
    ChartAxis,
    ChartGroup,
    ChartThemeColor,
    ChartVoronoiContainer
} from '@patternfly/react-charts';

export class DatabaseMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeTabKey: 0,
            data: {},
            loading: true,
            // refresh charts
            cache_refresh: "",
            count: 10,
            dbCacheList: [],
            ndnCacheList: [],
            ndnCacheUtilList: []
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.startCacheRefresh = this.startCacheRefresh.bind(this);
        this.refreshCache = this.refreshCache.bind(this);
    }

    componentDidMount() {
        this.resetChartData();
        this.refreshCache();
        this.startCacheRefresh();
        this.props.enableTree();
    }

    componentWillUnmount() {
        this.stopCacheRefresh();
    }

    resetChartData() {
        this.setState({
            data: {
                dbcachehitratio: [0],
                dbcachetries: [0],
                dbcachehits: [0],
                dbcachepagein: [0],
                dbcachepageout: [0],
                dbcacheroevict: [0],
                dbcacherwevict: [0],
                normalizeddncachehitratio: [0],
                maxnormalizeddncachesize: [0],
                currentnormalizeddncachesize: [0],
                normalizeddncachetries: [0],
                normalizeddncachehits: [0],
                normalizeddncacheevictions: [0],
                currentnormalizeddncachecount: [0],
                normalizeddncachethreadsize: [0],
                normalizeddncachethreadslots: [0],
            },
            dbCacheList: [
                {name: "", x: "1", y: 0},
                {name: "", x: "2", y: 0},
                {name: "", x: "3", y: 0},
                {name: "", x: "4", y: 0},
                {name: "", x: "5", y: 0},
                {name: "", x: "6", y: 0},
                {name: "", x: "7", y: 0},
                {name: "", x: "8", y: 0},
                {name: "", x: "9", y: 0},
                {name: "", x: "10", y: 0},
            ],
            ndnCacheList: [
                {name: "", x: "1", y: 0},
                {name: "", x: "2", y: 0},
                {name: "", x: "3", y: 0},
                {name: "", x: "4", y: 0},
                {name: "", x: "5", y: 0},
            ],
            ndnCacheUtilList: [
                {name: "", x: "1", y: 0},
                {name: "", x: "2", y: 0},
                {name: "", x: "3", y: 0},
                {name: "", x: "4", y: 0},
                {name: "", x: "5", y: 0},
            ],
        });
    }

    refreshCache() {
        // Search for db cache stat and update state
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "ldbm"
        ];
        log_cmd("refreshCache", "Load database monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let count = this.state.count + 1; // This is used by all the charts
                    if (count > 100) {
                        // Keep progress count in check
                        count = 1;
                    }

                    // Build up the DB Cache chart data
                    let dbratio = config.attrs.dbcachehitratio[0];
                    let chart_data = this.state.dbCacheList;
                    chart_data.shift();
                    chart_data.push({name: "Cache Hit Ratio", x: count.toString(), y: parseInt(dbratio)});

                    // Build up the NDN Cache chart data
                    let ndnratio = config.attrs.normalizeddncachehitratio[0];
                    let ndn_chart_data = this.state.ndnCacheList;
                    ndn_chart_data.shift();
                    ndn_chart_data.push({name: "Cache Hit Ratio", x: count.toString(), y: parseInt(ndnratio)});

                    // Build up the DB Cache Util chart data
                    let ndn_util_chart_data = this.state.ndnCacheUtilList;
                    let currNDNSize = parseInt(config.attrs.currentnormalizeddncachesize[0]);
                    let maxNDNSize = parseInt(config.attrs.maxnormalizeddncachesize[0]);
                    let ndn_utilization = (currNDNSize / maxNDNSize) * 100;
                    ndn_util_chart_data.shift();
                    ndn_util_chart_data.push({name: "Cache Utilization", x: count.toString(), y: parseInt(ndn_utilization)});

                    this.setState({
                        data: config.attrs,
                        loading: false,
                        dbCacheList: chart_data,
                        ndnCacheList: ndn_chart_data,
                        ndnCacheUtilList: ndn_util_chart_data,
                        count: count
                    });
                })
                .fail(() => {
                    this.resetChartData();
                });
    }

    startCacheRefresh() {
        this.state.cache_refresh = setInterval(this.refreshCache, 2000);
    }

    stopCacheRefresh() {
        clearInterval(this.state.cache_refresh);
    }

    render() {
        let chartColor = ChartThemeColor.green;
        let ndnChartColor = ChartThemeColor.green;
        let ndnUtilColor = ChartThemeColor.green;
        let dbcachehit = 0;
        let ndncachehit = 0;
        let ndncachemax = 0;
        let ndncachecurr = 0;
        let utilratio = 0;
        let content =
            <div className="ds-margin-top-xlg ds-center">
                <h4>Loading database monitor information ...</h4>
                <Spinner className="ds-margin-top-lg" size="xl" />
            </div>;

        if (!this.state.loading) {
            dbcachehit = parseInt(this.state.data.dbcachehitratio[0]);
            ndncachehit = parseInt(this.state.data.normalizeddncachehitratio[0]);
            ndncachemax = parseInt(this.state.data.maxnormalizeddncachesize[0]);
            ndncachecurr = parseInt(this.state.data.currentnormalizeddncachesize[0]);
            utilratio = Math.round((ndncachecurr / ndncachemax) * 100);
            if (utilratio == 0) {
                // Just round up to 1
                utilratio = 1;
            }

            // Database cache
            if (dbcachehit > 89) {
                chartColor = ChartThemeColor.green;
            } else if (dbcachehit > 74) {
                chartColor = ChartThemeColor.orange;
            } else {
                chartColor = ChartThemeColor.purple;
            }
            // NDN cache ratio
            if (ndncachehit > 89) {
                ndnChartColor = ChartThemeColor.green;
            } else if (ndncachehit > 74) {
                ndnChartColor = ChartThemeColor.orange;
            } else {
                ndnChartColor = ChartThemeColor.purple;
            }
            // NDN cache utilization
            if (utilratio > 95) {
                ndnUtilColor = ChartThemeColor.purple;
            } else if (utilratio > 90) {
                ndnUtilColor = ChartThemeColor.orange;
            } else {
                ndnUtilColor = ChartThemeColor.green;
            }

            content =
                <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText><b>Database Cache</b></TabTitleText>}>
                        <div className="ds-margin-top">
                            <Card>
                                <CardBody>
                                    <div className="ds-container">
                                        <div className="ds-center">
                                            <h4 className="ds-margin-top-xlg">Cache Hit Ratio</h4>
                                            <h3><b>{dbcachehit}%</b></h3>
                                        </div>
                                        <div className="ds-margin-left" style={{ height: '200px', width: '500px' }}>
                                            <Chart
                                                ariaDesc="Database Cache"
                                                ariaTitle="Live Database Cache Statistics"
                                                containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                height={200}
                                                maxDomain={{y: 100}}
                                                minDomain={{y: 0}}
                                                padding={{
                                                    bottom: 30,
                                                    left: 40,
                                                    top: 10,
                                                    right: 10,
                                                }}
                                                width={500}
                                                themeColor={chartColor}
                                            >
                                                <ChartAxis />
                                                <ChartAxis dependentAxis showGrid tickValues={[25, 50, 75, 100]} />
                                                <ChartGroup>
                                                    <ChartArea
                                                        data={this.state.dbCacheList}
                                                    />
                                                </ChartGroup>
                                            </Chart>
                                        </div>
                                    </div>
                                </CardBody>
                            </Card>
                        </div>

                        <Grid hasGutter className="ds-margin-top-xlg">
                            <GridItem span={3}>
                                Database Cache Hit Ratio:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{this.state.data.dbcachehitratio}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Database Cache Tries:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{this.state.data.dbcachetries}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Database Cache Hits:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{this.state.data.dbcachehits}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Cache Pages Read:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{this.state.data.dbcachepagein}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Cache Pages Written:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{this.state.data.dbcachepageout}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Read-Only Page Evictions:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{this.state.data.dbcacheroevict}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Read-Write Page Evictions:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{this.state.data.dbcacherwevict}</b>
                            </GridItem>
                        </Grid>
                    </Tab>

                    <Tab eventKey={1} title={<TabTitleText><b>Normalized DN Cache</b></TabTitleText>}>
                        <div className="ds-margin-top-lg">
                            <Grid hasGutter>
                                <GridItem span={6}>
                                    <Card>
                                        <CardBody>
                                            <div className="ds-container">
                                                <div className="ds-center">
                                                    <h4 className="ds-margin-top-lg">Cache Hit Ratio</h4>
                                                    <h3><b>{ndncachehit}%</b></h3>
                                                </div>
                                                <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                    <Chart
                                                        ariaDesc="NDN Cache"
                                                        ariaTitle="Live Normalized DN Cache Statistics"
                                                        containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                        height={200}
                                                        maxDomain={{y: 100}}
                                                        minDomain={{y: 0}}
                                                        padding={{
                                                            bottom: 40,
                                                            left: 60,
                                                            top: 10,
                                                            right: 15,
                                                        }}
                                                        width={350}
                                                        themeColor={ndnChartColor}
                                                    >
                                                        <ChartAxis />
                                                        <ChartAxis dependentAxis showGrid tickValues={[25, 50, 75, 100]} />
                                                        <ChartGroup>
                                                            <ChartArea
                                                                data={this.state.ndnCacheList}
                                                            />
                                                        </ChartGroup>
                                                    </Chart>
                                                </div>
                                            </div>
                                        </CardBody>
                                    </Card>
                                </GridItem>
                                <GridItem span={6}>
                                    <Card>
                                        <CardBody>
                                            <div className="ds-container">
                                                <div className="ds-center">
                                                    <h4 className="ds-margin-top-lg">Cache Utilization</h4>
                                                    <h3><b>{utilratio}%</b></h3>
                                                    <h6 className="ds-margin-top-xlg">Cached DN's</h6>
                                                    <b>{this.state.data.currentnormalizeddncachecount[0]}</b>
                                                </div>
                                                <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                    <Chart
                                                        ariaDesc="NDN Cache Utilization"
                                                        ariaTitle="Live Normalized DN Cache Utilization Statistics"
                                                        containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                        height={200}
                                                        maxDomain={{y: 100}}
                                                        minDomain={{y: 0}}
                                                        padding={{
                                                            bottom: 40,
                                                            left: 60,
                                                            top: 10,
                                                            right: 15,
                                                        }}
                                                        width={350}
                                                        themeColor={ndnUtilColor}
                                                    >
                                                        <ChartAxis />
                                                        <ChartAxis dependentAxis showGrid tickValues={[25, 50, 75, 100]} />
                                                        <ChartGroup>
                                                            <ChartArea
                                                                data={this.state.ndnCacheUtilList}
                                                            />
                                                        </ChartGroup>
                                                    </Chart>
                                                </div>
                                            </div>
                                        </CardBody>
                                    </Card>
                                </GridItem>
                            </Grid>

                            <Grid hasGutter className="ds-margin-top-xlg">
                                <GridItem span={3}>
                                    NDN Cache Hit Ratio:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.normalizeddncachehitratio}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Max Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.maxnormalizeddncachesize}</b>
                                </GridItem>

                                <GridItem span={3}>
                                    NDN Cache Tries:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.normalizeddncachetries}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Current Cache Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.currentnormalizeddncachesize}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Hits:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.normalizeddncachehits}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache DN Count:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.currentnormalizeddncachecount}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Evictions:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.normalizeddncacheevictions}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Thread Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.normalizeddncachethreadsize}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Thread Slots:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.normalizeddncachethreadslots}</b>
                                </GridItem>
                            </Grid>
                        </div>
                    </Tab>
                </Tabs>;
        }

        return (
            <div id="db-content">
                <h3>
                    Database Performance Statistics
                </h3>
                <div className="ds-margin-top-xlg">
                    {content}
                </div>

            </div>
        );
    }
}

// Prop types and defaults

DatabaseMonitor.propTypes = {
    serverId: PropTypes.string,
    enableTree: PropTypes.func,
};

DatabaseMonitor.defaultProps = {
    serverId: "",
    enableTree: noop,
};

export default DatabaseMonitor;
