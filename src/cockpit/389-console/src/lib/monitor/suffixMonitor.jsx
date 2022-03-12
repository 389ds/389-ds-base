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
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faLeaf,
    faTree,
} from '@fortawesome/free-solid-svg-icons';
import { numToCommas, displayBytes, log_cmd } from '../tools.jsx';

export class SuffixMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeTabKey: 0,
            data: {},
            loading: true,
            // refresh charts
            cache_refresh: "",
            count: 10,
            utilCount: 5,
            entryCacheList: [],
            entryUtilCacheList: [],
            dnCacheList: [],
            dnCacheUtilList: []
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.startCacheRefresh = this.startCacheRefresh.bind(this);
        this.refreshSuffixCache = this.refreshSuffixCache.bind(this);
    }

    componentDidMount() {
        this.resetChartData();
        this.refreshSuffixCache();
        this.startCacheRefresh();
        this.props.enableTree();
    }

    componentWillUnmount() {
        this.stopCacheRefresh();
    }

    resetChartData() {
        this.setState({
            data: {
                // Entry cache
                entrycachehitratio: [0],
                entrycachetries: [0],
                entrycachehits: [0],
                maxentrycachesize: [0],
                currententrycachesize: [0],
                maxentrycachecount: [0],
                currententrycachecount: [0],
                // DN cache
                dncachehitratio: [0],
                dncachetries: [0],
                dncachehits: [0],
                maxdncachesize: [0],
                currentdncachesize: [0],
                maxdncachecount: [0],
                currentdncachecount: [0],
            },
            entryCacheList: [
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
            entryUtilCacheList: [
                { name: "", x: "1", y: 0 },
                { name: "", x: "2", y: 0 },
                { name: "", x: "3", y: 0 },
                { name: "", x: "4", y: 0 },
                { name: "", x: "5", y: 0 },
            ],
            dnCacheList: [
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
            dnCacheUtilList: [
                { name: "", x: "1", y: 0 },
                { name: "", x: "2", y: 0 },
                { name: "", x: "3", y: 0 },
                { name: "", x: "4", y: 0 },
                { name: "", x: "5", y: 0 },
            ],
        });
    }

    refreshSuffixCache() {
        // Search for db cache stat and update state
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "monitor", "backend", this.props.suffix
        ];
        log_cmd("refreshSuffixCache", "Get suffix monitor", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let count = this.state.count + 1;
                    const utilCount = this.state.utilCount + 1;
                    if (count > 100) {
                        // Keep progress count in check
                        count = 1;
                    }

                    // Build up the Entry Cache chart data
                    const entryRatio = config.attrs.entrycachehitratio[0];
                    const entry_data = this.state.entryCacheList;
                    entry_data.shift();
                    entry_data.push({ name: "Cache Hit Ratio", x: count.toString(), y: parseInt(entryRatio) });

                    // Build up the Entry Util chart data
                    const entry_util_data = this.state.entryUtilCacheList;
                    let maxsize = config.attrs.maxentrycachesize[0];
                    let currsize = config.attrs.currententrycachesize[0];
                    let utilratio = Math.round((currsize / maxsize) * 100);
                    if (utilratio == 0) {
                        utilratio = 1;
                    }
                    entry_util_data.shift();
                    entry_util_data.push({ name: "Cache Utilization", x: utilCount.toString(), y: parseInt(utilratio) });

                    // Build up the DN Cache chart data
                    const dnratio = config.attrs.dncachehitratio[0];
                    const dn_data = this.state.dnCacheList;
                    dn_data.shift();
                    dn_data.push({ name: "Cache Hit Ratio", x: count.toString(), y: parseInt(dnratio) });

                    // Build up the DN Cache Util chart data
                    const dn_util_data = this.state.dnCacheUtilList;
                    currsize = parseInt(config.attrs.currentdncachesize[0]);
                    maxsize = parseInt(config.attrs.maxdncachesize[0]);
                    utilratio = (currsize / maxsize) * 100;
                    if (utilratio == 0) {
                        utilratio = 1;
                    }
                    dn_util_data.shift();
                    dn_util_data.push({ name: "Cache Utilization", x: utilCount.toString(), y: parseInt(utilratio) });

                    this.setState({
                        data: config.attrs,
                        loading: false,
                        entryCacheList: entry_data,
                        entryUtilCacheList: entry_util_data,
                        dnCacheList: dn_data,
                        dnCacheUtilList: dn_util_data,
                        count: count,
                        utilCount: utilCount
                    });
                })
                .fail(() => {
                    this.resetChartData();
                });
    }

    startCacheRefresh() {
        this.state.cache_refresh = setInterval(this.refreshSuffixCache, 2000);
    }

    stopCacheRefresh() {
        clearInterval(this.state.cache_refresh);
    }

    render() {
        let entryChartColor = ChartThemeColor.green;
        let entryUtilChartColor = ChartThemeColor.green;
        let dnChartColor = ChartThemeColor.green;
        let dnUtilChartColor = ChartThemeColor.green;
        let cachehit = 1;
        let cachemax = 0;
        let cachecurr = 0;
        let cachecount = 0;
        let utilratio = 1;
        // DN cache
        let dncachehit = 0;
        let dncachemax = 0;
        let dncachecurr = 0;
        let dncachecount = 0;
        let dnutilratio = 1;
        let suffixIcon = faTree;

        if (this.props.dbtype == "subsuffix") {
            suffixIcon = faLeaf;
        }

        let content =
            <div className="ds-margin-top-xlg ds-center">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        Loading Suffix Monitor Information ...
                    </Text>
                </TextContent>
                <Spinner className="ds-margin-top-lg" size="xl" />
            </div>;

        if (!this.state.loading) {
            // Entry cache
            cachehit = parseInt(this.state.data.entrycachehitratio[0]);
            cachemax = parseInt(this.state.data.maxentrycachesize[0]);
            cachecurr = parseInt(this.state.data.currententrycachesize[0]);
            cachecount = parseInt(this.state.data.currententrycachecount[0]);
            utilratio = Math.round((cachecurr / cachemax) * 100);
            // DN cache
            dncachehit = parseInt(this.state.data.dncachehitratio[0]);
            dncachemax = parseInt(this.state.data.maxdncachesize[0]);
            dncachecurr = parseInt(this.state.data.currentdncachesize[0]);
            dncachecount = parseInt(this.state.data.currentdncachecount[0]);
            dnutilratio = Math.round((dncachecurr / dncachemax) * 100);

            // Adjust ratios if needed
            if (utilratio == 0) {
                utilratio = 1;
            }
            if (dnutilratio == 0) {
                dnutilratio = 1;
            }

            // Entry cache chart color
            if (cachehit > 89) {
                entryChartColor = ChartThemeColor.green;
            } else if (cachehit > 74) {
                entryChartColor = ChartThemeColor.orange;
            } else {
                entryChartColor = ChartThemeColor.purple;
            }
            // Entry cache utilization
            if (utilratio > 95) {
                entryUtilChartColor = ChartThemeColor.purple;
            } else if (utilratio > 90) {
                entryUtilChartColor = ChartThemeColor.orange;
            } else {
                entryUtilChartColor = ChartThemeColor.green;
            }
            // DN cache chart color
            if (dncachehit > 89) {
                dnChartColor = ChartThemeColor.green;
            } else if (dncachehit > 74) {
                dnChartColor = ChartThemeColor.orange;
            } else {
                dnChartColor = ChartThemeColor.purple;
            }
            // DN cache utilization
            if (dnutilratio > 95) {
                dnUtilChartColor = ChartThemeColor.purple;
            } else if (dnutilratio > 90) {
                dnUtilChartColor = ChartThemeColor.orange;
            } else {
                dnUtilChartColor = ChartThemeColor.green;
            }

            content =
                <div id="monitor-suffix-page">
                    <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={0} title={<TabTitleText>Entry Cache</TabTitleText>}>
                            <div className="ds-margin-top">
                                <Grid hasGutter>
                                    <GridItem span={6}>
                                        <Card isSelectable>
                                            <CardBody>
                                                <div className="ds-container">
                                                    <div className="ds-center">
                                                        <TextContent title="The entry cache hit ratio (entrycachehitratio)">
                                                            <Text className="ds-margin-top" component={TextVariants.h3}>
                                                                Cache Hit Ratio
                                                            </Text>
                                                        </TextContent>
                                                        <TextContent>
                                                            <Text className="ds-margin-top" component={TextVariants.h2}>
                                                                <b>{cachehit}%</b>
                                                            </Text>
                                                        </TextContent>
                                                    </div>
                                                    <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                        <Chart
                                                            ariaDesc="Entry Cache"
                                                            ariaTitle="Live Entry Cache Statistics"
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
                                                            themeColor={entryChartColor}
                                                        >
                                                            <ChartAxis />
                                                            <ChartAxis dependentAxis showGrid tickValues={[25, 50, 75, 100]} />
                                                            <ChartGroup>
                                                                <ChartArea
                                                                    data={this.state.entryCacheList}
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
                                                        <TextContent title="The amount of the cache that is being used: max size (maxentrycachesize) vs current size (currententrycachesize)">
                                                            <Text className="ds-margin-top" component={TextVariants.h3}>
                                                                Cache Utilization
                                                            </Text>
                                                        </TextContent>
                                                        <TextContent>
                                                            <Text component={TextVariants.h2}>
                                                                <b>{utilratio}%</b>
                                                            </Text>
                                                        </TextContent>
                                                        <TextContent>
                                                            <Text className="ds-margin-top-lg" component={TextVariants.h5}>
                                                                Cached Entries
                                                            </Text>
                                                        </TextContent>
                                                        <b>{cachecount}</b>
                                                    </div>
                                                    <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                        <Chart
                                                            ariaDesc="Entry Cache Utilization"
                                                            ariaTitle="Live Entry Cache Utilization Statistics"
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
                                                            themeColor={entryUtilChartColor}
                                                        >
                                                            <ChartAxis />
                                                            <ChartAxis dependentAxis showGrid tickValues={[25, 50, 75, 100]} />
                                                            <ChartGroup>
                                                                <ChartArea
                                                                    data={this.state.entryUtilCacheList}
                                                                />
                                                            </ChartGroup>
                                                        </Chart>
                                                    </div>
                                                </div>
                                            </CardBody>
                                        </Card>
                                    </GridItem>
                                </Grid>
                            </div>
                            <Grid hasGutter className="ds-margin-top-xlg">
                                <GridItem span={3}>
                                    Entry Cache Hit Ratio:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.entrycachehitratio[0]}%</b>
                                </GridItem>
                                <GridItem span={3}>
                                    Entry Cache Max Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{displayBytes(cachemax)} </b>
                                </GridItem>

                                <GridItem span={3}>
                                    Entry Cache Hits:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.entrycachehits[0])}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    Entry Cache Current Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{displayBytes(cachecurr)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    Entry Cache Tries:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.entrycachetries[0])}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    Entry Cache Max Entries:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.maxentrycachecount[0])}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    Entry Cache Count:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.currententrycachecount[0])}</b>
                                </GridItem>
                            </Grid>
                        </Tab>
                        <Tab eventKey={1} title={<TabTitleText>DN Cache</TabTitleText>}>
                            <div className="ds-margin-top">
                                <Grid hasGutter>
                                    <GridItem span={6}>
                                        <Card isSelectable>
                                            <CardBody>
                                                <div className="ds-container">
                                                    <div className="ds-center">
                                                        <TextContent title="The DN cache hit ratio (dncachehitratio)">
                                                            <Text className="ds-margin-top" component={TextVariants.h3}>
                                                                Cache Hit Ratio
                                                            </Text>
                                                        </TextContent>
                                                        <TextContent>
                                                            <Text className="ds-margin-top" component={TextVariants.h2}>
                                                                <b>{dncachehit}%</b>
                                                            </Text>
                                                        </TextContent>
                                                    </div>
                                                    <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                        <Chart
                                                            ariaDesc="DN Cache"
                                                            ariaTitle="Live DN Cache Statistics"
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
                                                            themeColor={dnChartColor}
                                                        >
                                                            <ChartAxis />
                                                            <ChartAxis dependentAxis showGrid tickValues={[25, 50, 75, 100]} />
                                                            <ChartGroup>
                                                                <ChartArea
                                                                    data={this.state.dnCacheList}
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
                                                        <TextContent title="The amount of the cache that is being used: max size (maxdncachesize) vs current size (currentdncachesize)">
                                                            <Text className="ds-margin-top" component={TextVariants.h3}>
                                                                Cache Utilization
                                                            </Text>
                                                        </TextContent>
                                                        <TextContent>
                                                            <Text component={TextVariants.h2}>
                                                                <b>{dnutilratio}%</b>
                                                            </Text>
                                                        </TextContent>
                                                        <TextContent>
                                                            <Text className="ds-margin-top-lg" component={TextVariants.h5}>
                                                                Cached DN's
                                                            </Text>
                                                        </TextContent>
                                                        <b>{numToCommas(dncachecount)}</b>
                                                    </div>
                                                    <div className="ds-margin-left" style={{ height: '200px', width: '350px' }}>
                                                        <Chart
                                                            ariaDesc="DN Cache Utilization"
                                                            ariaTitle="Live DN Cache Utilization Statistics"
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
                                                            themeColor={dnUtilChartColor}
                                                        >
                                                            <ChartAxis />
                                                            <ChartAxis dependentAxis showGrid tickValues={[25, 50, 75, 100]} />
                                                            <ChartGroup>
                                                                <ChartArea
                                                                    data={this.state.dnCacheUtilList}
                                                                />
                                                            </ChartGroup>
                                                        </Chart>
                                                    </div>
                                                </div>
                                            </CardBody>
                                        </Card>
                                    </GridItem>
                                </Grid>
                            </div>
                            <Grid hasGutter className="ds-margin-top-xlg">
                                <GridItem span={3}>
                                    DN Cache Hit Ratio:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{this.state.data.dncachehitratio[0]}%</b>
                                </GridItem>
                                <GridItem span={3}>
                                    DN Cache Max Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{displayBytes(dncachemax)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    DN Cache Hits:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.dncachehits[0])}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    DN Cache Current Size:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{displayBytes(dncachecurr)}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    DN Cache Tries:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.dncachetries[0])}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    DN Cache Max Count:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.maxdncachecount[0])}</b>
                                </GridItem>
                                <GridItem span={3}>
                                    DN Cache Current Count:
                                </GridItem>
                                <GridItem span={2}>
                                    <b>{numToCommas(this.state.data.currentdncachecount[0])}</b>
                                </GridItem>
                            </Grid>
                        </Tab>
                    </Tabs>
                </div>;
        }

        return (
            <div>
                <TextContent>
                    <Text component={TextVariants.h2}>
                        <FontAwesomeIcon size="sm" icon={suffixIcon} /> {this.props.suffix} (<b>{this.props.bename}</b>)
                    </Text>
                </TextContent>
                <div className="ds-margin-top-lg">
                    {content}
                </div>
            </div>
        );
    }
}

SuffixMonitor.propTypes = {
    serverId: PropTypes.string,
    suffix: PropTypes.string,
    bename: PropTypes.string,
    enableTree: PropTypes.func,
};

SuffixMonitor.defaultProps = {
    serverId: "",
    suffix: "",
    bename: "",
};

export default SuffixMonitor;
