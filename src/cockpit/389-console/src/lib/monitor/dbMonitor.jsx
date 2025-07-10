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

const _ = cockpit.gettext;

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
            ndnCacheUtilList: [],
            ndn_cache_enabled: false
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
            ndn_cache_enabled: false
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
                    chart_data.push({ name: _("Cache Hit Ratio"), x: count.toString(), y: parseInt(dbratio) });

                    // Check if NDN cache is enabled
                    const ndn_cache_enabled = config.attrs.normalizeddncachehitratio &&
                                             config.attrs.normalizeddncachehitratio.length > 0;
                    let ndn_chart_data = this.state.ndnCacheList;
                    let ndn_util_chart_data = this.state.ndnCacheUtilList;

                    // Only build NDN cache chart data if NDN cache is enabled
                    if (ndn_cache_enabled) {
                        const ndnratio = config.attrs.normalizeddncachehitratio[0];
                        ndn_chart_data = this.state.ndnCacheList;
                        ndn_chart_data.shift();
                        ndn_chart_data.push({ name: _("Cache Hit Ratio"), x: count.toString(), y: parseInt(ndnratio) });

                        // Build up the NDN Cache Util chart data
                        ndn_util_chart_data = this.state.ndnCacheUtilList;
                        const currNDNSize = parseInt(config.attrs.currentnormalizeddncachesize[0]);
                        const maxNDNSize = parseInt(config.attrs.maxnormalizeddncachesize[0]);
                        const ndn_utilization = (currNDNSize / maxNDNSize) * 100;
                        ndn_util_chart_data.shift();
                        ndn_util_chart_data.push({ name: _("Cache Utilization"), x: ndnCount.toString(), y: parseInt(ndn_utilization) });
                    }

                    this.setState({
                        data: config.attrs,
                        loading: false,
                        dbCacheList: chart_data,
                        ndnCacheList: ndn_chart_data,
                        ndnCacheUtilList: ndn_util_chart_data,
                        count,
                        ndnCount,
                        ndn_cache_enabled
                    });
                })
                .fail(() => {
                    this.resetChartData();
                });
    }

    startCacheRefresh() {
        this.setState({
            cache_refresh: setInterval(this.refreshCache, 2000),
        });
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
        let content = (
            <div className="ds-margin-top-xlg ds-center">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Loading database monitor information ...")}
                    </Text>
                </TextContent>
                <Spinner className="ds-margin-top-lg" size="xl" />
            </div>
        );

        if (!this.state.loading) {
            dbcachehit = parseInt(this.state.data.dbcachehitratio[0]);

            // Check if NDN cache is enabled
            const ndn_cache_enabled = this.state.data.normalizeddncachehitratio &&
                                     this.state.data.normalizeddncachehitratio.length > 0;

            if (ndn_cache_enabled) {
                ndncachehit = parseInt(this.state.data.normalizeddncachehitratio[0]);
                ndncachemax = parseInt(this.state.data.maxnormalizeddncachesize[0]);
                ndncachecurr = parseInt(this.state.data.currentnormalizeddncachesize[0]);
                utilratio = Math.round((ndncachecurr / ndncachemax) * 100);
                if (utilratio === 0) {
                    // Just round up to 1
                    utilratio = 1;
                }
            }

            // Database cache
            if (dbcachehit > 89) {
                chartColor = ChartThemeColor.green;
            } else if (dbcachehit > 74) {
                chartColor = ChartThemeColor.orange;
            } else {
                chartColor = ChartThemeColor.purple;
            }

            // NDN cache colors only if enabled
            if (ndn_cache_enabled) {
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
            }

            // Create tabs based on what caches are available
            const tabs = [];

            // Database Cache tab is always available
            tabs.push(
                <Tab eventKey={0} key="db-cache-tab" title={<TabTitleText>{_("Database Cache")}</TabTitleText>}>
                    <div className="ds-margin-top">
                        <Card isSelectable>
                            <CardBody>
                                <div className="ds-container">
                                    <div className="ds-center">
                                        <TextContent className="ds-margin-top-xlg" title={_("The database cache hit ratio (dbcachehitratio).")}>
                                            <Text component={TextVariants.h3}>
                                                {_("Cache Hit Ratio")}
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
                                            ariaTitle={_("Live Database Cache Statistics")}
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
                            {_("Database Cache Hit Ratio:")}
                        </GridItem>
                        <GridItem span={2}>
                            <b>{this.state.data.dbcachehitratio}%</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Database Cache Tries:")}
                        </GridItem>
                        <GridItem span={2}>
                            <b>{numToCommas(this.state.data.dbcachetries)}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Database Cache Hits:")}
                        </GridItem>
                        <GridItem span={2}>
                            <b>{numToCommas(this.state.data.dbcachehits)}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Cache Pages Read:")}
                        </GridItem>
                        <GridItem span={2}>
                            <b>{numToCommas(this.state.data.dbcachepagein)}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Cache Pages Written:")}
                        </GridItem>
                        <GridItem span={2}>
                            <b>{numToCommas(this.state.data.dbcachepageout)}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Read-Only Page Evictions:")}
                        </GridItem>
                        <GridItem span={2}>
                            <b>{numToCommas(this.state.data.dbcacheroevict)}</b>
                        </GridItem>
                        <GridItem span={3}>
                            {_("Read-Write Page Evictions:")}
                        </GridItem>
                        <GridItem span={2}>
                            <b>{numToCommas(this.state.data.dbcacherwevict)}</b>
                        </GridItem>
                    </Grid>
                </Tab>
            );

            // Only add NDN Cache tab if NDN cache is enabled
            if (ndn_cache_enabled) {
                tabs.push(
                    <Tab eventKey={1} key="ndn-cache-tab" title={<TabTitleText>{_("Normalized DN Cache")}</TabTitleText>}>
                        <div className="ds-margin-top-lg">
                            <Grid hasGutter>
                                <GridItem span={6}>
                                    <Card isSelectable>
                                        <CardBody>
                                            <div className="ds-container">
                                                <div className="ds-center">
                                                    <TextContent className="ds-margin-top-xlg" title={_("The normalized DN cache hit ratio (normalizeddncachehitratio).")}>
                                                        <Text component={TextVariants.h3}>
                                                            {_("Cache Hit Ratio")}
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
                                                        ariaTitle={_("Live Normalized DN Cache Statistics")}
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
                                                    <TextContent className="ds-margin-top-lg" title={_("The amount of the cache that is being used: max size (maxnormalizeddncachesize) vs current size (currentnormalizeddncachesize)")}>
                                                        <Text component={TextVariants.h2}>
                                                            {_("Cache Utilization")}
                                                        </Text>
                                                    </TextContent>
                                                    <TextContent>
                                                        <Text component={TextVariants.h3}>
                                                            <b>{utilratio}%</b>
                                                        </Text>
                                                    </TextContent>
                                                    <TextContent className="ds-margin-top-xlg">
                                                        <Text component={TextVariants.h5}>
                                                            {_("Cached DN's")}
                                                        </Text>
                                                    </TextContent>
                                                    <b>{numToCommas(this.state.data.currentnormalizeddncachecount[0])}</b>
                                                </div>
                                                <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                    <Chart
                                                        ariaDesc="NDN Cache Utilization"
                                                        ariaTitle={_("Live Normalized DN Cache Utilization Statistics")}
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
                                    {_("NDN Cache Hit Ratio:")}
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.normalizeddncachehitratio}%</b>
                                </GridItem>
                                <GridItem span={3}>
                                    {_("NDN Cache Max Size:")}
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{displayBytes(this.state.data.maxnormalizeddncachesize)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    {_("NDN Cache Tries:")}
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncachetries)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    {_("NDN Current Cache Size:")}
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{displayBytes(this.state.data.currentnormalizeddncachesize)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    {_("NDN Cache Hits:")}
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncachehits)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    {_("NDN Cache DN Count:")}
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.currentnormalizeddncachecount)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    {_("NDN Cache Evictions:")}
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncacheevictions)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    {_("NDN Cache Thread Size:")}
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncachethreadsize)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    {_("NDN Cache Thread Slots:")}
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.normalizeddncachethreadslots)}</b>
                                </GridItem>
                            </Grid>
                        </div>
                    </Tab>
                );
            }

            content = (
                <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    {tabs}
                </Tabs>
            );
        }

        return (
            <div id="db-content">
                <TextContent>
                    <Text className="ds-sub-header" component={TextVariants.h2}>
                        {_("Database Performance Statistics")}
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
    data: PropTypes.object,
    serverId: PropTypes.string,
    enableTree: PropTypes.func,
};

DatabaseMonitor.defaultProps = {
    data: {},
    serverId: "",
};

export class DatabaseMonitorMDB extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeTabKey: 0,
            data: {},
            loading: true,
            // refresh chart
            cache_refresh: "",
            count: 10,
            ndnCount: 5,
            dbCacheList: [],
            ndnCacheList: [],
            ndnCacheUtilList: [],
            ndn_cache_enabled: false
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
            ndn_cache_enabled: false
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

                    // Check if NDN cache is enabled
                    const ndn_cache_enabled = config.attrs.normalizeddncachehitratio &&
                                             config.attrs.normalizeddncachehitratio.length > 0;
                    let ndn_chart_data = this.state.ndnCacheList;
                    let ndn_util_chart_data = this.state.ndnCacheUtilList;

                    // Only build NDN cache chart data if NDN cache is enabled
                    if (ndn_cache_enabled) {
                        // Build up the NDN Cache chart data
                        const ndnratio = config.attrs.normalizeddncachehitratio[0];
                        ndn_chart_data = this.state.ndnCacheList;
                        ndn_chart_data.shift();
                        ndn_chart_data.push({ name: _("Cache Hit Ratio"), x: count.toString(), y: parseInt(ndnratio) });

                        // Build up the DB Cache Util chart data
                        ndn_util_chart_data = this.state.ndnCacheUtilList;
                        const currNDNSize = parseInt(config.attrs.currentnormalizeddncachesize[0]);
                        const maxNDNSize = parseInt(config.attrs.maxnormalizeddncachesize[0]);
                        const ndn_utilization = (currNDNSize / maxNDNSize) * 100;
                        ndn_util_chart_data.shift();
                        ndn_util_chart_data.push({ name: _("Cache Utilization"), x: ndnCount.toString(), y: parseInt(ndn_utilization) });
                    }

                    this.setState({
                        data: config.attrs,
                        loading: false,
                        ndnCacheList: ndn_chart_data,
                        ndnCacheUtilList: ndn_util_chart_data,
                        count,
                        ndnCount,
                        ndn_cache_enabled
                    });
                })
                .fail(() => {
                    this.resetChartData();
                });
    }

    startCacheRefresh() {
        this.setState({
            cache_refresh: setInterval(this.refreshCache, 2000),
        });
    }

    stopCacheRefresh() {
        clearInterval(this.state.cache_refresh);
    }

    render() {
        let ndnChartColor = ChartThemeColor.green;
        let ndnUtilColor = ChartThemeColor.green;
        let ndncachehit = 0;
        let ndncachemax = 0;
        let ndncachecurr = 0;
        let utilratio = 0;
        let content = (
            <div className="ds-margin-top-xlg ds-center">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Loading database monitor information ...")}
                    </Text>
                </TextContent>
                <Spinner className="ds-margin-top-lg" size="xl" />
            </div>
        );

        if (!this.state.loading) {
            // Check if NDN cache is enabled
            const ndn_cache_enabled = this.state.data.normalizeddncachehitratio &&
                                     this.state.data.normalizeddncachehitratio.length > 0;

            if (ndn_cache_enabled) {
                ndncachehit = parseInt(this.state.data.normalizeddncachehitratio[0]);
                ndncachemax = parseInt(this.state.data.maxnormalizeddncachesize[0]);
                ndncachecurr = parseInt(this.state.data.currentnormalizeddncachesize[0]);
                utilratio = Math.round((ndncachecurr / ndncachemax) * 100);
                if (utilratio === 0) {
                    // Just round up to 1
                    utilratio = 1;
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

                content = (
                    <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={0} title={<TabTitleText>{_("Normalized DN Cache")}</TabTitleText>}>
                            <div className="ds-margin-top-lg">
                                <Grid hasGutter>
                                    <GridItem span={6}>
                                        <Card isSelectable>
                                            <CardBody>
                                                <div className="ds-container">
                                                    <div className="ds-center">
                                                        <TextContent className="ds-margin-top-xlg" title={_("The normalized DN cache hit ratio (normalizeddncachehitratio).")}>
                                                            <Text component={TextVariants.h3}>
                                                                {_("Cache Hit Ratio")}
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
                                                            ariaTitle={_("Live Normalized DN Cache Statistics")}
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
                                                        <TextContent className="ds-margin-top-lg" title={_("The amount of the cache that is being used: max size (maxnormalizeddncachesize) vs current size (currentnormalizeddncachesize)")}>
                                                            <Text component={TextVariants.h2}>
                                                                {_("Cache Utilization")}
                                                            </Text>
                                                        </TextContent>
                                                        <TextContent>
                                                            <Text component={TextVariants.h3}>
                                                                <b>{utilratio}%</b>
                                                            </Text>
                                                        </TextContent>
                                                        <TextContent className="ds-margin-top-xlg">
                                                            <Text component={TextVariants.h5}>
                                                                {_("Cached DN's")}
                                                            </Text>
                                                        </TextContent>
                                                        <b>{numToCommas(this.state.data.currentnormalizeddncachecount[0])}</b>
                                                    </div>
                                                    <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                        <Chart
                                                            ariaDesc="NDN Cache Utilization"
                                                            ariaTitle={_("Live Normalized DN Cache Utilization Statistics")}
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
                                        {_("NDN Cache Hit Ratio:")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.state.data.normalizeddncachehitratio}%</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        {_("NDN Cache Max Size:")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{displayBytes(this.state.data.maxnormalizeddncachesize)}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        {_("NDN Cache Tries:")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{numToCommas(this.state.data.normalizeddncachetries)}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        {_("NDN Current Cache Size:")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{displayBytes(this.state.data.currentnormalizeddncachesize)}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        {_("NDN Cache Hits:")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{numToCommas(this.state.data.normalizeddncachehits)}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        {_("NDN Cache DN Count:")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{numToCommas(this.state.data.currentnormalizeddncachecount)}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        {_("NDN Cache Evictions:")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{numToCommas(this.state.data.normalizeddncacheevictions)}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        {_("NDN Cache Thread Size:")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{numToCommas(this.state.data.normalizeddncachethreadsize)}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        {_("NDN Cache Thread Slots:")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{numToCommas(this.state.data.normalizeddncachethreadslots)}</b>
                                    </GridItem>
                                </Grid>
                            </div>
                        </Tab>
                    </Tabs>
                );
            } else {
                // No NDN cache available
                content = (
                    <div className="ds-margin-top-xlg ds-center">
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                {_("Normalized DN Cache is disabled")}
                            </Text>
                        </TextContent>
                    </div>
                );
            }
        }

        return (
            <div id="db-content">
                <TextContent>
                    <Text className="ds-sub-header" component={TextVariants.h2}>
                        {_("Database Performance Statistics")}
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

DatabaseMonitorMDB.propTypes = {
    data: PropTypes.object,
    serverId: PropTypes.string,
    enableTree: PropTypes.func,
};

DatabaseMonitorMDB.defaultProps = {
    data: {},
    serverId: "",
};
