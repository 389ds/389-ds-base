import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    log_cmd,
    get_date_string,
    get_date_diff,
    displayBytes,
    numToCommas
} from "../tools.jsx";
import {
    ConnectionTable,
    DiskTable,
} from "./monitorTables.jsx";
import {
    Button,
    Card,
    CardBody,
    Divider,
    Grid,
    GridItem,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
} from '@patternfly/react-core';
import {
    Chart,
    ChartArea,
    ChartAxis,
    ChartGroup,
    ChartThemeColor,
    ChartVoronoiContainer,
} from '@patternfly/react-charts';
import { SyncAltIcon } from "@patternfly/react-icons";

const _ = cockpit.gettext;
const refresh_interval = 10000; // 10 seconds

export class ServerMonitor extends React.Component {
    constructor (props) {
        super(props);

        const initCPUChart = [];
        const initVirtChart = [];
        const initResChart = [];
        const initSwapChart = [];
        const initConnChart = [];
        const initConnEstablishedChart = [];
        const initConnTimeWaitChart = [];
        const initConnCloseWaitChart = [];
        for (let idx = 0; idx <= 5; idx++) {
            const value = refresh_interval / 1000;
            const x_value = "0:00:" + (idx === 0 ? "00" : value * idx).toString();
            initCPUChart.push({ name: 'CPU', x: x_value, y: 0 });
            initResChart.push({ name: 'Resident', x: x_value, y: 0 });
            initVirtChart.push({ name: 'Virtual', x: x_value, y: 0 });
            initSwapChart.push({ name: 'Swap', x: x_value, y: 0 });
            initConnChart.push({ name: 'Connections', x: x_value, y: 0 });
            initConnTimeWaitChart.push({ name: 'Connections time wait', x: x_value, y: 0 });
            initConnCloseWaitChart.push({ name: 'Connections close wait', x: x_value, y: 0 });
            initConnEstablishedChart.push({ name: 'Connections established', x: x_value, y: 0 });
        }

        this.state = {
            activeKey: this.props.serverTab,
            port: 389,
            secure_port: 636,
            conn_highmark: 1000,
            cpu_tick_values: [25, 50, 75, 100],
            mem_tick_values: [25, 50, 75, 100],
            conn_tick_values: [250, 500, 750, 1000],
            mem_ratio: 0,
            total_threads: 0,
            chart_refresh: "",
            initCPUChart,
            initVirtChart,
            initResChart,
            initSwapChart,
            initConnChart,
            initConnEstablishedChart,
            initConnTimeWaitChart,
            initConnCloseWaitChart,
            cpuChart: [...initCPUChart],
            memVirtChart: [...initVirtChart],
            memResChart: [...initResChart],
            swapChart: [...initSwapChart],
            connChart: [...initConnChart],
            connEstablishedChart: [...initConnEstablishedChart],
            connTimeWaitChart: [...initConnTimeWaitChart],
            connCloseWaitChart: [...initConnCloseWaitChart],
        };

        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeKey: tabIndex
            });
        };

        this.startRefresh = this.startRefresh.bind(this);
        this.refreshCharts = this.refreshCharts.bind(this);
        this.stopRefresh = this.stopRefresh.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
        this.refreshCharts();
        this.startRefresh();
    }

    componentWillUnmount() {
        this.stopRefresh();
    }

    resetChartData() {
        this.setState({
            conn_highmark: 1000,
            cpu_tick_values: [25, 50, 75, 100],
            mem_tick_values: [],
            conn_tick_values: [250, 500, 750, 1000],
            cpuChart: [...this.state.initCPUChart],
            memVirtChart: [...this.state.initVirtChart],
            memResChart: [...this.state.initResChart],
            swapChart: [...this.state.initSwapChart],
            connChart: [...this.state.initConnChart],
            connCloseWaitChart: [...this.state.initConnCloseWaitChart],
            connTimeWaitChart: [...this.state.initConnTimeWaitChart],
            connEstablishedChart: [...this.state.initConnEstablishedChart],
        });
    }

    refreshCharts() {
        let cpu = 0;
        let mem_total = 0;
        let mem_rss_usage = 0;
        let mem_vms_usage = 0;
        let mem_swap_usage = 0;
        let virt_mem = 0;
        let res_mem = 0;
        let swap_mem = 0;
        let current_conns = 0;
        let conn_established = 0;
        let conn_close_wait = 0;
        let conn_time_wait = 0;
        let total_threads = 0;
        let conn_highmark = this.state.conn_highmark;
        let cpu_tick_values = this.state.cpu_tick_values;
        let conn_tick_values = this.state.conn_tick_values;
        let interval = "0:00:00";
        const cmd = [
            "dsconf", "-j", this.props.serverId,
            "monitor", "server", "--just-resources"
        ];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const data = JSON.parse(content);
                    const attrs = data.attrs;
                    const date = new Date;

                    cpu = attrs['cpu_usage'][0];
                    mem_rss_usage = attrs['mem_rss_percent'][0];
                    mem_vms_usage = attrs['mem_vms_percent'][0];
                    mem_swap_usage = attrs['mem_swap_percent'][0];
                    virt_mem = attrs['vms'][0];
                    res_mem = attrs['rss'][0];
                    swap_mem = attrs['swap'][0];
                    current_conns = attrs['connection_count'][0];
                    conn_established = attrs['connection_established_count'][0];
                    conn_close_wait = attrs['connection_close_wait_count'][0];
                    conn_time_wait = attrs['connection_time_wait_count'][0];
                    total_threads = attrs['total_threads'][0];
                    mem_total = attrs['total_mem'][0];

                    // Build time interval
                    let hour = date.getHours().toString();
                    let min = date.getMinutes();
                    let sec = date.getSeconds();

                    if (min < 10) {
                        min = "0" + min;
                    }
                    if (sec < 10) {
                        sec = "0" + sec;
                    }
                    interval = hour + ":" + min + ":" + sec;

                    // Set conn tick values
                    if (current_conns > conn_highmark) {
                        conn_highmark = Math.ceil(current_conns / 1000) * 1000;
                        const conn_incr = Math.ceil(conn_highmark / 4);
                        let tick = conn_incr;
                        conn_tick_values = [
                            tick,
                            tick += conn_incr,
                            tick += conn_incr,
                            tick += conn_incr
                        ];
                    }

                    const cpuChart = this.state.cpuChart;
                    cpuChart.shift();
                    cpuChart.push({ name: _("CPU"), x: interval, y: parseInt(cpu) });

                    const memVirtChart = this.state.memVirtChart;
                    memVirtChart.shift();
                    memVirtChart.push({ name: _("Virtual Memory"), x: interval, y: parseInt(mem_vms_usage) });

                    const memResChart = this.state.memResChart;
                    memResChart.shift();
                    memResChart.push({ name: _("Resident Memory"), x: interval, y: parseInt(mem_rss_usage) });

                    const swapChart = this.state.swapChart;
                    swapChart.shift();
                    swapChart.push({ name: "Swap", x: interval, y: parseInt(mem_swap_usage) });

                    const connChart = this.state.connChart;
                    connChart.shift();
                    connChart.push({ name: _("Connections"), x: interval, y: parseInt(current_conns) });

                    const connEstablishedChart = this.state.connEstablishedChart;
                    connEstablishedChart.shift();
                    connEstablishedChart.push({ name: _("Connections established"), x: interval, y: parseInt(conn_established) });

                    const connTimeWaitChart = this.state.connTimeWaitChart;
                    connTimeWaitChart.shift();
                    connTimeWaitChart.push({ name: _("Connections time wait"), x: interval, y: parseInt(conn_time_wait) });

                    const connCloseWaitChart = this.state.connCloseWaitChart;
                    connCloseWaitChart.shift();
                    connCloseWaitChart.push({ name: _("Connections close wait"), x: interval, y: parseInt(conn_close_wait) });

                    this.setState({
                        cpu_tick_values,
                        conn_tick_values,
                        cpuChart,
                        memVirtChart,
                        memResChart,
                        swapChart,
                        connChart,
                        connTimeWaitChart,
                        connCloseWaitChart,
                        connEstablishedChart,
                        conn_highmark,
                        current_conns,
                        conn_close_wait,
                        conn_time_wait,
                        conn_established,
                        mem_virt_size: virt_mem,
                        mem_res_size: res_mem,
                        mem_swap_size: swap_mem,
                        mem_rss_ratio: mem_rss_usage,
                        mem_vms_ratio: mem_vms_usage,
                        mem_swap_ratio: mem_swap_usage,
                        mem_total,
                        cpu,
                        total_threads,
                    });
                })
                .fail(() => {
                    this.resetChartData();
                });
    }

    startRefresh() {
        this.setState({
            chart_refresh: setInterval(this.refreshCharts, refresh_interval),
        });
    }

    stopRefresh() {
        clearInterval(this.state.chart_refresh);
    }

    render() {
        const {
            cpu,
            connChart,
            connTimeWaitChart,
            connCloseWaitChart,
            connEstablishedChart,
            cpuChart,
            current_conns,
            conn_established,
            conn_close_wait,
            conn_time_wait,
            memResChart,
            memVirtChart,
            swapChart,
            mem_virt_size,
            mem_res_size,
            mem_swap_size,
            mem_rss_ratio,
            mem_vms_ratio,
            mem_swap_ratio,
            mem_total,
            total_threads,
        } = this.state;

        // Generate start time and uptime
        const startTime = this.props.data.starttime[0];
        const currTime = this.props.data.currenttime[0];
        const startDate = get_date_string(this.props.data.starttime[0]);
        const uptime = get_date_diff(startTime, currTime);
        const conn_tick_values = this.state.conn_tick_values;
        let cpu_tick_values = this.state.cpu_tick_values;
        let mem_tick_values = this.state.mem_tick_values;

        // Adjust chart if CPU goes above 100%
        if (cpu > 100) {
            let tick = (Math.ceil(cpu/100)*100)/4;
            let incr = tick;
            cpu_tick_values = [tick, tick += incr, tick += incr, tick += incr];
        } else {
            cpu_tick_values = [25, 50, 75, 100];
        }

        // Adjust chart if memory usage goes above 100%
        if (mem_vms_ratio > 100 || mem_rss_ratio > 100 || mem_swap_ratio > 100) {
            // Find the highest ratio to resize the chart with
            let mem_ratio;
            if (mem_vms_ratio >= mem_rss_ratio && mem_vms_ratio >= mem_swap_ratio) {
                mem_ratio = mem_vms_ratio;
            } else if (mem_rss_ratio >= mem_vms_ratio && mem_rss_ratio >= mem_swap_ratio) {
                mem_ratio = mem_rss_ratio;
            } else {
                mem_ratio = mem_swap_ratio;
            }
            let tick = (Math.ceil(mem_ratio/100)*100)/4;
            let incr = tick;
            mem_tick_values = [tick, tick += incr, tick += incr, tick += incr];
        } else {
            mem_tick_values = [25, 50, 75, 100];
        }

        return (
            <div id="monitor-server-page">
                <Grid>
                    <GridItem span={9}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                {_("Server Statistics")}
                                <Button
                                    variant="plain"
                                    aria-label={_("Refresh suffix monitor")}
                                    onClick={() => this.props.handleReload(this.state.activeKey)}
                                >
                                    <SyncAltIcon />
                                </Button>
                            </Text>
                        </TextContent>
                    </GridItem>
                </Grid>
                <Tabs isFilled className="ds-margin-top-lg" activeKey={this.state.activeKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>{_("Resource Charts")}</TabTitleText>}>
                        <Grid className="ds-margin-top-lg" hasGutter>
                            <GridItem span="6">
                                <Card className="ds-margin-top-lg">
                                    <CardBody>
                                        <Grid>
                                            <GridItem span="4" title={_("Established client connections to the server")}>
                                                <div className="ds-center" >
                                                    <TextContent>
                                                        <Text component={TextVariants.h2}>
                                                            {_("Connections")}
                                                        </Text>
                                                    </TextContent>
                                                    <TextContent>
                                                        <Text component={TextVariants.h6}>
                                                            <b>{numToCommas(current_conns)}</b>
                                                        </Text>
                                                    </TextContent>
                                                    <Divider className="ds-margin-top ds-margin-bottom"/>
                                                </div>
                                                <TextContent className="ds-margin-top-lg" title="Connections that are in an ESTABLISHED state">
                                                    <Text component={TextVariants.p}>
                                                        Established: &nbsp;&nbsp;<b>{numToCommas(conn_established)}</b>
                                                    </Text>
                                                </TextContent>
                                                <TextContent className="ds-margin-top-lg" title="Connections that are in a CLOSE_WAIT state">
                                                    <Text component={TextVariants.p}>
                                                        Close wait: &nbsp;&nbsp;<b>{numToCommas(conn_close_wait)}</b>
                                                    </Text>
                                                </TextContent>
                                                <TextContent className="ds-margin-top-lg" title="Connections that are in a TIME_WAIT state">
                                                    <Text component={TextVariants.p}>
                                                        Time wait: &nbsp;&nbsp;<b>{numToCommas(conn_time_wait)}</b>
                                                    </Text>
                                                </TextContent>
                                            </GridItem>
                                            <GridItem span="8">
                                                <Chart
                                                    ariaDesc="connection stats"
                                                    ariaTitle={_("Live Connection Statistics")}
                                                    containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                    height={220}
                                                    minDomain={{ y: 0 }}
                                                    padding={{
                                                        bottom: 30,
                                                        left: 60,
                                                        top: 10,
                                                        right: 30,
                                                    }}
                                                >
                                                    <ChartAxis />
                                                    <ChartAxis dependentAxis showGrid tickValues={conn_tick_values} />
                                                    <ChartGroup>
                                                        <ChartArea
                                                            data={connChart}
                                                        />
                                                        <ChartArea
                                                            data={connEstablishedChart}
                                                            interpolation="monotoneX"
                                                        />
                                                        <ChartArea
                                                            data={connTimeWaitChart}
                                                            interpolation="monotoneX"
                                                        />
                                                        <ChartArea
                                                            data={connCloseWaitChart}
                                                            interpolation="monotoneX"
                                                        />
                                                    </ChartGroup>
                                                </Chart>
                                            </GridItem>
                                        </Grid>
                                    </CardBody>
                                </Card>
                            </GridItem>
                            <GridItem span={6}>
                                <Card className="ds-margin-top-lg">
                                    <CardBody>
                                        <Grid>
                                            <GridItem span="4" className="ds-center">
                                                <TextContent>
                                                    <Text className="ds-margin-top-xlg" component={TextVariants.h3}>
                                                        {_("CPU Usage")}
                                                    </Text>
                                                </TextContent>
                                                <TextContent>
                                                    <Text component={TextVariants.h6}>
                                                        <b>{cpu}%</b>
                                                    </Text>
                                                </TextContent>
                                            </GridItem>
                                            <GridItem span="8">
                                                <Chart
                                                    ariaDesc="cpu"
                                                    ariaTitle={_("Server CPU Usage")}
                                                    containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}%`} constrainToVisibleArea />}
                                                    height={220}
                                                    minDomain={{ y: 0 }}
                                                    padding={{
                                                        bottom: 30,
                                                        left: 55,
                                                        top: 10,
                                                        right: 25,
                                                    }}
                                                    themeColor={ChartThemeColor.multiUnordered}
                                                >
                                                    <ChartAxis />
                                                    <ChartAxis
                                                        dependentAxis
                                                        showGrid
                                                        tickFormat={(value) => `${value}%`}
                                                        tickValues={cpu_tick_values}
                                                    />
                                                    <ChartGroup>
                                                        <ChartArea
                                                            data={cpuChart}
                                                            interpolation="monotoneX"
                                                        />
                                                    </ChartGroup>
                                                </Chart>
                                            </GridItem>
                                        </Grid>
                                    </CardBody>
                                </Card>
                            </GridItem>
                        </Grid>

                        <Card className="ds-margin-top-lg ds-margin-bottom-md">
                            <CardBody>
                                <Grid hasGutter>
                                    <GridItem className="ds-center ds-margin-top-xl" span="3">
                                        <TextContent className="ds-margin-top" title="The percentage of system memory used by Directory Server">
                                            <Text component={TextVariants.h3}>
                                                Total System Memory
                                            </Text>
                                        </TextContent>
                                        <TextContent>
                                            <Text component={TextVariants.h6}>
                                                <b>{displayBytes(mem_total)}</b>
                                            </Text>
                                        </TextContent>
                                        <Divider className="ds-margin-top ds-margin-bottom"/>
                                        <TextContent className="ds-margin-top-lg">
                                            <Text component={TextVariants.h3}>
                                                {_("Resident Size")}
                                            </Text>
                                        </TextContent>
                                        <TextContent>
                                            <Text component={TextVariants.h6}>
                                                <b>{displayBytes(mem_res_size)}</b> ({mem_rss_ratio}%)
                                            </Text>
                                        </TextContent>
                                        <TextContent>
                                            <Text className="ds-margin-top-lg" component={TextVariants.h3}>
                                                {_("Virtual Size")}
                                            </Text>
                                        </TextContent>
                                        <TextContent>
                                            <Text component={TextVariants.h6}>
                                                <b>{displayBytes(mem_virt_size)}</b> ({mem_vms_ratio}%)
                                            </Text>
                                        </TextContent>
                                        <TextContent className="ds-margin-top-lg">
                                            <Text component={TextVariants.h3}>
                                                Swap Size
                                            </Text>
                                        </TextContent>
                                        <TextContent>
                                            <Text component={TextVariants.h6}>
                                                <b>{displayBytes(mem_swap_size)}</b> ({mem_swap_ratio}%)
                                            </Text>
                                        </TextContent>
                                    </GridItem>
                                    <GridItem span="9">
                                        <Chart
                                            ariaDesc="Server Memory Utilization"
                                            ariaTitle={_("Live Server Memory Statistics")}
                                            containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}%`} constrainToVisibleArea />}
                                            height={125}
                                            minDomain={{ y: 0 }}
                                            padding={{
                                                bottom: 30,
                                                left: 60,
                                                top: 10,
                                                right: 30,
                                            }}
                                            themeColor={ChartThemeColor.multiUnordered}
                                        >
                                            <ChartAxis />
                                            <ChartAxis
                                                dependentAxis
                                                showGrid
                                                style={{
                                                    tickLabels: {
                                                        fontSize: 8,
                                                    }
                                                }}
                                                tickFormat={(value) => `${value}%`} tickValues={mem_tick_values}
                                            />
                                            <ChartGroup>
                                                <ChartArea
                                                    data={memVirtChart}
                                                    interpolation="monotoneX"
                                                    themeColor={ChartThemeColor.blue}
                                                />
                                                <ChartArea
                                                    data={memResChart}
                                                    interpolation="monotoneX"
                                                    themeColor={ChartThemeColor.green}
                                                />
                                                <ChartArea
                                                    data={swapChart}
                                                    interpolation="monotoneX"
                                                    themeColor={ChartThemeColor.red}
                                                />
                                            </ChartGroup>
                                        </Chart>
                                    </GridItem>
                                </Grid>
                            </CardBody>
                        </Card>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>{_("Server Stats")}</TabTitleText>}>
                        <Grid hasGutter className="ds-margin-top-xlg">
                            <GridItem span={3}>
                                {_("Server Instance")}
                            </GridItem>
                            <GridItem span={9}>
                                <b>{"slapd-" + this.props.serverId}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Version")}
                            </GridItem>
                            <GridItem span={9}>
                                <b>{this.props.data.version}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Server Started")}
                            </GridItem>
                            <GridItem span={9}>
                                <b>{startDate}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Server Uptime")}
                            </GridItem>
                            <GridItem span={9}>
                                <b>{uptime}</b>
                            </GridItem>
                            <hr />
                            <GridItem span={3} title="Active threads includes worker threads, listener threads, tasks, and persistent searches">
                                {_("Active Threads")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{this.props.data.threads}</b>
                            </GridItem>

                            <GridItem span={3} title="Count of all the threads the server has created. Includes replication agrements, housekeeping, worker threads, tasks, etc">
                                {_("Total Threads")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{total_threads}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Threads Waiting To Read")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.data.readwaiters)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Conns At Max Threads")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.data.currentconnectionsatmaxthreads)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Conns Exceeded Max Threads")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.data.maxthreadsperconnhits)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Total Connections")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.data.totalconnections)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Current Connections")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.data.currentconnections)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Operations Started")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.data.opsinitiated)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Operations Completed")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.data.opscompleted)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Entries Returned To Clients")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.data.entriessent)}</b>
                            </GridItem>
                            <GridItem span={3}>
                                {_("Data Sent to Clients")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{displayBytes(this.props.data.bytessent)}</b>
                            </GridItem>
                        </Grid>
                    </Tab>
                    <Tab eventKey={2} title={<TabTitleText>{_("Connection Table")}</TabTitleText>}>
                        <ConnectionTable conns={this.props.data.connection} />
                    </Tab>
                    <Tab eventKey={3} title={<TabTitleText>{_("Disk Space")}</TabTitleText>}>
                        <DiskTable
                            rows={this.props.disks}
                        />
                        <Button
                            className="ds-margin-top-lg"
                            variant="secondary"
                            onClick={this.props.handleReloadDisks}
                            isLoading={this.props.diskReloadSpinning}
                            spinnerAriaValueText={this.props.diskReloadSpinning ? "Refreshing" : undefined}
                            isDisabled={this.props.diskReloadSpinning}
                        >
                            {this.props.diskReloadSpinning ? "Refreshing" : _("Refresh")}
                        </Button>
                    </Tab>
                    <Tab eventKey={4} title={<TabTitleText>{_("SNMP Counters")}</TabTitleText>}>
                        <Grid className="ds-margin-top-xlg" hasGutter>
                            <GridItem span={4}>
                                {_("Anonymous Binds")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.anonymousbinds[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Referrals")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.referrals[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Unauthenticated Binds")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.unauthbinds[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Returned Referrals")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.referralsreturned[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Simple Auth Binds")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.simpleauthbinds[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Bind Security Errors")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.bindsecurityerrors[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Strong Auth Binds")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.strongauthbinds[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Security Errors")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.securityerrors[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Initiated Operations")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.inops[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Errors")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.errors[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Compare Operations")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.compareops[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Current Connections")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.connections[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Add Operations")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.addentryops[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Total Connections")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.connectionseq[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Delete Operations")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.removeentryops[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Conns in Max Threads")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.connectionsinmaxthreads[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Modify Operations")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.modifyentryops[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Conns Exceeded Max Threads")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.connectionsmaxthreadscount[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("ModRDN Operations")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.modifyrdnops[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Data Received")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{displayBytes(this.props.snmpData.bytesrecv[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Search Operations")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.searchops[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Data Sent")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{displayBytes(this.props.snmpData.bytessent[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("One Level Searches")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.onelevelsearchops[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Entries Returned")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.entriesreturned[0])}</b>
                            </GridItem>
                            <GridItem span={4}>
                                {_("Whole Tree Searches")}
                            </GridItem>
                            <GridItem span={2}>
                                <b>{numToCommas(this.props.snmpData.wholesubtreesearchops[0])}</b>
                            </GridItem>
                        </Grid>
                    </Tab>
                </Tabs>
            </div>
        );
    }
}

ServerMonitor.propTypes = {
    serverId: PropTypes.string,
    data: PropTypes.object,
    handleReload: PropTypes.func,
    enableTree: PropTypes.func,
};

ServerMonitor.defaultProps = {
    serverId: "",
    data: {},
};

export default ServerMonitor;
