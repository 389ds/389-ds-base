import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Card,
    CardBody,
    Grid,
    GridItem,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import {
    Chart,
    ChartArea,
    ChartAxis,
    ChartGroup,
    ChartThemeColor,
    ChartVoronoiContainer
} from '@patternfly/react-charts';
import { numToCommas, displayBytes } from "../tools.jsx";

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
            ndnCount: 5,
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
                { name: "", x: "1", y: 0 },
                { name: "", x: "2", y: 0 },
                { name: "", x: "3", y: 0 },
                { name: "", x: "4", y: 0 },
                { name: "", x: "5", y: 0 },
                { name: "", x: "6", y: 0 },
                { name: "", x: "7", y: 0 },
                { name: "", x: "8", y: 0 },
                { name: "", x: "9", y: 0 },
                { name: "", x: "10", y: 0 },
            ],
            ndnCacheList: [
                { name: "", x: "1", y: 0 },
                { name: "", x: "2", y: 0 },
                { name: "", x: "3", y: 0 },
                { name: "", x: "4", y: 0 },
                { name: "", x: "5", y: 0 },
            ],
            ndnCacheUtilList: [
                { name: "", x: "1", y: 0 },
                { name: "", x: "2", y: 0 },
                { name: "", x: "3", y: 0 },
                { name: "", x: "4", y: 0 },
                { name: "", x: "5", y: 0 },
            ],
        });
    }

    refreshCache() {
        // Search for db cache stat and update state
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "ldbm"
        ];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let count = this.state.count + 1;
                    const ndnCount = this.state.ndnCount + 1;
                    if (count > 100) {
                        // Keep progress count in check
                        count = 1;
                    }

                    // Build up the DB Cache chart data
                    const dbratio = config.attrs.dbcachehitratio[0];
                    const chart_data = this.state.dbCacheList;
                    chart_data.shift();
                    chart_data.push({ name: "Cache Hit Ratio", x: count.toString(), y: parseInt(dbratio) });

                    // Build up the NDN Cache chart data
                    const ndnratio = config.attrs.normalizeddncachehitratio[0];
                    const ndn_chart_data = this.state.ndnCacheList;
                    ndn_chart_data.shift();
                    ndn_chart_data.push({ name: "Cache Hit Ratio", x: count.toString(), y: parseInt(ndnratio) });

                    // Build up the DB Cache Util chart data
                    const ndn_util_chart_data = this.state.ndnCacheUtilList;
                    const currNDNSize = parseInt(config.attrs.currentnormalizeddncachesize[0]);
                    const maxNDNSize = parseInt(config.attrs.maxnormalizeddncachesize[0]);
                    const ndn_utilization = (currNDNSize / maxNDNSize) * 100;
                    ndn_util_chart_data.shift();
                    ndn_util_chart_data.push({ name: "Cache Utilization", x: ndnCount.toString(), y: parseInt(ndn_utilization) });

                    this.setState({
                        data: config.attrs,
                        loading: false,
                        dbCacheList: chart_data,
                        ndnCacheList: ndn_chart_data,
                        ndnCacheUtilList: ndn_util_chart_data,
                        count: count,
                        ndnCount: ndnCount
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
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Loading database monitor information ...
                    </Text>
                </TextContent>
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
                    <Tab eventKey={0} title={<TabTitleText>Database Cache</TabTitleText>}>
                        <div className="ds-margin-top">
                            <Card isSelectable>
                                <CardBody>
                                    <div className="ds-container">
                                        <div className="ds-center">
                                            <TextContent className="ds-margin-top-xlg" title="The database cache hit ratio (dbcachehitratio).">
                                                <Text component={TextVariants.h3}>
                                                    Cache Hit Ratio
                                                </Text>
                                            </TextContent>
                                            <TextContent>
                                                <Text component={TextVariants.h2}>
                                                    <b>{dbcachehit}%</b>
                                                </Text>
                                            </TextContent>
                                        </div>
                                        <div className="ds-margin-left" style={{ height: '200px', width: '500px' }}>
                                            <Chart
                                                ariaDesc="Database Cache"
                                                ariaTitle="Live Database Cache Statistics"
                                                containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                height={200}
                                                maxDomain={{ y: 100 }}
                                                minDomain={{ y: 0 }}
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
                                <b>{this.state.data.dbcachehitratio}%</b>
                            </GridItem>
                            <GridItem span={3}>
                                Database Cache Tries:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.state.data.dbcachetries)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Database Cache Hits:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.state.data.dbcachehits)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Cache Pages Read:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.state.data.dbcachepagein)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Cache Pages Written:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.state.data.dbcachepageout)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Read-Only Page Evictions:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.state.data.dbcacheroevict)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                Read-Write Page Evictions:
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.state.data.dbcacherwevict)}</b>
                            </GridItem>
                        </Grid>
                    </Tab>

                    <Tab eventKey={1} title={<TabTitleText>Normalized DN Cache</TabTitleText>}>
                        <div className="ds-margin-top-lg">
                            <Grid hasGutter>
                                <GridItem span={6}>
                                    <Card isSelectable>
                                        <CardBody>
                                            <div className="ds-container">
                                                <div className="ds-center">
                                                    <TextContent className="ds-margin-top-xlg"  title="The normalized DN cache hit ratio (normalizeddncachehitratio).">
                                                        <Text component={TextVariants.h3}>
                                                            Cache Hit Ratio
                                                        </Text>
                                                    </TextContent>
                                                    <TextContent>
                                                        <Text component={TextVariants.h2}>
                                                            <b>{ndncachehit}%</b>
                                                        </Text>
                                                    </TextContent>
                                                </div>
                                                <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                    <Chart
                                                        ariaDesc="NDN Cache"
                                                        ariaTitle="Live Normalized DN Cache Statistics"
                                                        containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                        height={200}
                                                        maxDomain={{ y: 100 }}
                                                        minDomain={{ y: 0 }}
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
                                    <Card isSelectable>
                                        <CardBody>
                                            <div className="ds-container">
                                                <div className="ds-center">
                                                    <TextContent className="ds-margin-top-lg" title="The amount of the cache that is being used: max size (maxnormalizeddncachesize) vs current size (currentnormalizeddncachesize)">
                                                        <Text component={TextVariants.h2}>
                                                            Cache Utilization
                                                        </Text>
                                                    </TextContent>
                                                    <TextContent>
                                                        <Text component={TextVariants.h3}>
                                                            <b>{utilratio}%</b>
                                                        </Text>
                                                    </TextContent>
                                                    <TextContent className="ds-margin-top-xlg">
                                                        <Text component={TextVariants.h5}>
                                                            Cached DN's
                                                        </Text>
                                                    </TextContent>
                                                    <b>{numToCommas(this.state.data.currentnormalizeddncachecount[0])}</b>
                                                </div>
                                                <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                    <Chart
                                                        ariaDesc="NDN Cache Utilization"
                                                        ariaTitle="Live Normalized DN Cache Utilization Statistics"
                                                        containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                        height={200}
                                                        maxDomain={{ y: 100 }}
                                                        minDomain={{ y: 0 }}
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
                                    <b>{this.state.data.normalizeddncachehitratio}%</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Max Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{displayBytes(this.state.data.maxnormalizeddncachesize)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Tries:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncachetries)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Current Cache Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{displayBytes(this.state.data.currentnormalizeddncachesize)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Hits:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncachehits)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache DN Count:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.currentnormalizeddncachecount)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Evictions:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncacheevictions)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Thread Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncachethreadsize)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    NDN Cache Thread Slots:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncachethreadslots)}</b>
                                </GridItem>
                            </Grid>
                        </div>
                    </Tab>
                </Tabs>;
        }

        return (
            <div id="db-content">
                <TextContent>
                    <Text className="ds-sub-header" component={TextVariants.h2}>
                        Database Performance Statistics
                    </Text>
                </TextContent>
                <div className="ds-margin-top-lg">
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
};

export default DatabaseMonitor;
