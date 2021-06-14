import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { log_cmd, get_date_string, get_date_diff, displayKBytes } from "../tools.jsx";
import {
    ConnectionTable,
    DiskTable,
} from "./monitorTables.jsx";
import {
    Nav,
    NavItem,
    TabContent,
    TabPane,
    TabContainer,
} from "patternfly-react";
import {
    Button,
    Card,
    CardBody,
    Grid,
    GridItem,
    noop
} from '@patternfly/react-core';
import {
    Chart,
    ChartArea,
    ChartAxis,
    ChartGroup,
    ChartThemeColor,
    ChartVoronoiContainer,
} from '@patternfly/react-charts';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';

export class ServerMonitor extends React.Component {
    constructor (props) {
        super(props);

        let initChart = [];
        for (let idx = 1; idx <= 10; idx++) {
            initChart.push({name: '', x: idx.toString(), y: 0});
        }

        this.state = {
            activeKey: 1,
            count: 10,
            port: 389,
            secure_port: 636,
            conn_highmark: 1000,
            cpu_tick_values: [25, 50, 75, 100],
            mem_tick_values: [25, 50, 75, 100],
            conn_tick_values: [250, 500, 750, 1000],
            mem_ratio: 0,
            chart_refresh: "",
            initChart: initChart,
            cpuChart: [...initChart],
            memVirtChart: [...initChart],
            memResChart: [...initChart],
            connChart: [...initChart],
        };
        this.handleNavSelect = this.handleNavSelect.bind(this);
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

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    getPorts() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get", "nsslapd-port", "nsslapd-secureport"
        ];
        log_cmd("getPorts", "Get the server port numbers", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    this.setState({
                        port: config.attrs['nsslapd-port'][0],
                        secure_port:  config.attrs['nsslapd-secureport'] === undefined
                            ? "-1"
                            : config.attrs['nsslapd-secureport'][0],
                    });
                });
    }

    resetChartData() {
        this.setState({
            conn_highmark: 1000,
            cpu_tick_values: [25, 50, 75, 100],
            mem_tick_values: [],
            conn_tick_values: [250, 500, 750, 1000],
            cpuChart: [...this.state.initChart],
            memVirtChart: [...this.state.initChart],
            memResChart: [...this.state.initChart],
            connChart: [...this.state.initChart],
        });
    }

    refreshCharts() {
        let cmd = "ps -ef | grep slapd-" + this.props.serverId + " | grep dirsrv";
        let cpu = 0;
        let pid = 0;
        let max_mem = 0;
        let virt_mem = 0;
        let res_mem = 0;
        let current_conns = 0;
        let conn_highmark = this.state.conn_highmark;
        let cpu_tick_values = this.state.cpu_tick_values;
        let conn_tick_values = this.state.conn_tick_values;

        log_cmd("refreshCharts", "Get server pid", [cmd]);
        cockpit
                .script(cmd, [], { superuser: true, err: "message" })
                .done(output => {
                    pid = output.split(/\s+/)[1];
                    let cpu_cmd = "top -n 1 -b -p " + pid + " | tail -1";
                    log_cmd("refreshCharts", "Get cpu and memory stats", [cpu_cmd]);
                    cockpit
                            .script(cpu_cmd, [], { superuser: true, err: "message" })
                            .done(top_output => {
                                let top_parts = top_output.trim().split(/\s+/);
                                virt_mem = top_parts[4];
                                res_mem = top_parts[5];
                                cpu = parseInt(top_parts[8]);
                                let mem_cmd = "awk '/MemTotal/{print $2}' /proc/meminfo";
                                log_cmd("refreshCharts", "Get total memory", [mem_cmd]);
                                cockpit
                                        .script(mem_cmd, [], { superuser: true, err: "message" })
                                        .done(mem_output => {
                                            max_mem = parseInt(mem_output);
                                            let conn_cmd = "netstat -anp | grep ':" + this.state.port + "\\|:" + this.state.secure_port +
                                                "' | grep ESTABLISHED | grep ns-slapd | wc -l";
                                            log_cmd("refreshCharts", "Get current count", [conn_cmd]);
                                            cockpit
                                                    .script(conn_cmd, [], { superuser: true, err: "message" })
                                                    .done(conn_output => {
                                                        current_conns = parseInt(conn_output);
                                                        let count = this.state.count + 1; // This is used by all the charts
                                                        if (count == 100) {
                                                            // Keep progress count in check
                                                            count = 1;
                                                        }

                                                        // Adjust CPU chart ticks if CPU goes above 100%
                                                        if (cpu > 100) {
                                                            let resize = true;
                                                            for (let stat of this.state.cpuChart) {
                                                                if (stat.y > cpu) {
                                                                    // There is already a higher CPU in the data
                                                                    resize = false;
                                                                    break;
                                                                }
                                                            }
                                                            if (resize) {
                                                                let incr = Math.ceil(cpu / 4);
                                                                cpu_tick_values = [incr, incr += incr, incr += incr, cpu];
                                                            }
                                                        } else {
                                                            let okToReset = true;
                                                            for (let stat of this.state.cpuChart) {
                                                                if (stat.y > 100) {
                                                                    okToReset = false;
                                                                    break;
                                                                }
                                                            }
                                                            if (okToReset) {
                                                                cpu_tick_values = [25, 50, 75, 100];
                                                            }
                                                        }

                                                        // Set conn tick values
                                                        if (current_conns > conn_highmark) {
                                                            conn_highmark = Math.ceil(current_conns / 1000) * 1000;
                                                            let conn_incr = Math.ceil(conn_highmark / 4);
                                                            let tick = conn_incr;
                                                            conn_tick_values = [
                                                                tick,
                                                                tick += conn_incr,
                                                                tick += conn_incr,
                                                                tick += conn_incr
                                                            ];
                                                        }

                                                        let cpuChart = this.state.cpuChart;
                                                        cpuChart.shift();
                                                        cpuChart.push({name: "CPU", x: count.toString(), y: parseInt(cpu)});

                                                        let memVirtChart = this.state.memVirtChart;
                                                        memVirtChart.shift();
                                                        memVirtChart.push({name: "Virtual Memory", x: count.toString(), y: parseInt(Math.round((virt_mem / max_mem) * 100))});

                                                        let memResChart = this.state.memResChart;
                                                        memResChart.shift();
                                                        memResChart.push({name: "Resident Memory", x: count.toString(), y: parseInt(Math.round((res_mem / max_mem) * 100))});

                                                        let connChart = this.state.connChart;
                                                        connChart.shift();
                                                        connChart.push({name: "Connections", x: count.toString(), y: parseInt(current_conns)});

                                                        this.setState({
                                                            count: count,
                                                            cpu_tick_values: cpu_tick_values,
                                                            conn_tick_values: conn_tick_values,
                                                            cpuChart: cpuChart,
                                                            memVirtChart: memVirtChart,
                                                            memResChart: memResChart,
                                                            connChart: connChart,
                                                            conn_highmark: conn_highmark,
                                                            current_conns: current_conns,
                                                            mem_virt_size: virt_mem,
                                                            mem_res_size: res_mem,
                                                            mem_ratio: Math.round((virt_mem / max_mem) * 100),
                                                            cpu: cpu,
                                                        });
                                                    })
                                                    .fail(() => {
                                                        this.resetChartData();
                                                    });
                                        })
                                        .fail(() => {
                                            this.resetChartData();
                                        });
                            })
                            .fail(() => {
                                this.resetChartData();
                            });
                })
                .fail(() => {
                    this.resetChartData();
                });
    }

    startRefresh() {
        this.state.chart_refresh = setInterval(this.refreshCharts, 3000);
    }

    stopRefresh() {
        clearInterval(this.state.chart_refresh);
    }

    render() {
        const {
            cpu,
            connChart,
            cpuChart,
            current_conns,
            memResChart,
            memVirtChart,
            mem_virt_size,
            mem_res_size,
            mem_ratio,
        } = this.state;

        // Generate start time and uptime
        let startTime = this.props.data.starttime[0];
        let currTime = this.props.data.currenttime[0];
        let startDate = get_date_string(this.props.data.starttime[0]);
        let uptime = get_date_diff(startTime, currTime);
        let conn_tick_values = this.state.conn_tick_values;
        let cpu_tick_values = this.state.cpu_tick_values;
        let mem_tick_values = this.state.mem_tick_values;

        // Adjust chart if CPU goes above 100%
        if (cpu > 100) {
            let incr = Math.ceil(cpu / 4);
            cpu_tick_values = [incr, incr += incr, incr += incr, incr += incr];
        } else {
            let okToReset = true;
            for (let stat of cpuChart) {
                if (stat.y > 100) {
                    okToReset = false;
                    break;
                }
            }
            if (okToReset) {
                cpu_tick_values = [25, 50, 75, 100];
            }
        }

        return (
            <div id="monitor-server-page">
                <Grid>
                    <GridItem span={9}>
                        <h4>Server Statistics <FontAwesomeIcon
                            size="lg"
                            className="ds-left-margin ds-refresh"
                            icon={faSyncAlt}
                            title="Refresh suffix monitor"
                            onClick={this.props.reload}
                        />
                        </h4>
                    </GridItem>
                </Grid>
                <TabContainer className="ds-margin-top-xlg" id="server-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div>
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: 'Resource Charts'}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: 'Server Stats'}} />
                            </NavItem>
                            <NavItem eventKey={3}>
                                <div dangerouslySetInnerHTML={{__html: 'Connection Table'}} />
                            </NavItem>
                            <NavItem eventKey={4}>
                                <div dangerouslySetInnerHTML={{__html: 'Disk Space'}} />
                            </NavItem>
                            <NavItem eventKey={5}>
                                <div dangerouslySetInnerHTML={{__html: 'SNMP Counters'}} />
                            </NavItem>
                        </Nav>
                        <TabContent>
                            <TabPane eventKey={1}>
                                <Card className="ds-margin-top-lg" isHoverable>
                                    <CardBody>
                                        <Grid>
                                            <GridItem span="2" className="ds-center" title="Established client connections to the server">
                                                <h6 className="ds-margin-top">Connections</h6>
                                                <h3><b>{current_conns}</b></h3>
                                            </GridItem>
                                            <GridItem span="10" style={{ height: '175px', width: '600px' }}>
                                                <Chart
                                                    ariaDesc="connection stats"
                                                    ariaTitle="Live Connection Statistics"
                                                    containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                    height={175}
                                                    minDomain={{y: 0}}
                                                    padding={{
                                                        bottom: 30,
                                                        left: 50,
                                                        top: 10,
                                                        right: 15,
                                                    }}
                                                    width={600}
                                                >
                                                    <ChartAxis />
                                                    <ChartAxis dependentAxis showGrid tickValues={conn_tick_values} />
                                                    <ChartGroup>
                                                        <ChartArea
                                                            data={connChart}
                                                        />
                                                    </ChartGroup>
                                                </Chart>
                                            </GridItem>
                                        </Grid>
                                    </CardBody>
                                </Card>
                                <Grid className="ds-margin-top-lg" hasGutter>
                                    <GridItem span={6}>
                                        <Card isHoverable>
                                            <CardBody>
                                                <Grid>
                                                    <GridItem className="ds-center" span="4">
                                                        <h5 className="ds-margin-top">Memory Usage</h5>
                                                        <b>{mem_ratio}%</b>
                                                        <h6 className="ds-margin-top-xlg">Virtual Size</h6>
                                                        <b>{displayKBytes(mem_virt_size)}</b>
                                                        <h6 className="ds-margin-top-xlg">Resident Size</h6>
                                                        <b>{displayKBytes(mem_res_size)}</b>
                                                    </GridItem>
                                                    <GridItem span="8" style={{ height: '225px' }}>
                                                        <Chart
                                                            ariaDesc="Server Memory Utilization"
                                                            ariaTitle="Live Server Memory Statistics"
                                                            containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                            height={225}
                                                            minDomain={{y: 0}}
                                                            padding={{
                                                                bottom: 30,
                                                                left: 40,
                                                                top: 10,
                                                                right: 15,
                                                            }}
                                                            themeColor={ChartThemeColor.multiUnordered}
                                                        >
                                                            <ChartAxis />
                                                            <ChartAxis dependentAxis showGrid tickValues={mem_tick_values} />
                                                            <ChartGroup>
                                                                <ChartArea
                                                                    data={memVirtChart}
                                                                    interpolation="monotoneX"
                                                                />
                                                                <ChartArea
                                                                    data={memResChart}
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
                                        <Card isHoverable>
                                            <CardBody>
                                                <Grid>
                                                    <GridItem span="4" className="ds-center">
                                                        <h6 className="ds-margin-top-xlg">CPU Usage</h6>
                                                        <h4><b>{cpu}%</b></h4>
                                                    </GridItem>
                                                    <GridItem span="8" style={{ height: '225px' }}>
                                                        <Chart
                                                            ariaDesc="cpu"
                                                            ariaTitle="Server CPU Usage"
                                                            containerComponent={<ChartVoronoiContainer labels={({ datum }) => `${datum.name}: ${datum.y}`} constrainToVisibleArea />}
                                                            height={225}
                                                            minDomain={{y: 0}}
                                                            padding={{
                                                                bottom: 30,
                                                                left: 40,
                                                                top: 10,
                                                                right: 15,
                                                            }}
                                                            themeColor={ChartThemeColor.multiUnordered}
                                                        >
                                                            <ChartAxis />
                                                            <ChartAxis dependentAxis showGrid tickValues={cpu_tick_values} />
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
                            </TabPane>

                            <TabPane eventKey={2}>
                                <Grid hasGutter className="ds-margin-top-xlg">
                                    <GridItem span={3}>
                                        Server Instance
                                    </GridItem>
                                    <GridItem span={9}>
                                        <b>{"slapd-" + this.props.serverId}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Version
                                    </GridItem>
                                    <GridItem span={9}>
                                        <b>{this.props.data.version}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Server Started
                                    </GridItem>
                                    <GridItem span={9}>
                                        <b>{startDate}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Server Uptime
                                    </GridItem>
                                    <GridItem span={9}>
                                        <b>{uptime}</b>
                                    </GridItem>
                                    <hr />
                                    <GridItem span={3}>
                                        Worker Threads
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.threads}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Threads Waiting To Read
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.readwaiters}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Conns At Max Threads
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.currentconnectionsatmaxthreads}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Conns Exceeded Max Threads
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.maxthreadsperconnhits}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Total Connections
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.totalconnections}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Current Connections
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.currentconnections}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Operations Started
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.opsinitiated}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Operations Completed
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.opscompleted}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Entries Returned To Clients
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.entriessent}</b>
                                    </GridItem>
                                    <GridItem span={3}>
                                        Bytes Sent to Clients
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.data.bytessent}</b>
                                    </GridItem>
                                </Grid>
                            </TabPane>
                            <TabPane eventKey={3}>
                                <ConnectionTable conns={this.props.data.connection} />
                            </TabPane>
                            <TabPane eventKey={4}>
                                <DiskTable
                                    rows={this.props.disks}
                                />
                                <Button
                                    className="ds-margin-top"
                                    onClick={this.props.reloadDisks}
                                >
                                    Refresh
                                </Button>
                            </TabPane>
                            <TabPane eventKey={5}>
                                <Grid className="ds-margin-top-xlg" hasGutter>
                                    <GridItem span={4}>
                                        Anonymous Binds
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.anonymousbinds}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Referrals
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.referrals}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Unauthenticated Binds
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.unauthbinds}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Returned Referrals
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.referralsreturned}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Simple Auth Binds
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.simpleauthbinds}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Bind Security Errors
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.bindsecurityerrors}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Strong Auth Binds
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.strongauthbinds}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Security Errors
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.securityerrors}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Initiated Operations
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.inops}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Errors
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.errors}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Compare Operations
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.compareops}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Current Connections
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.connections}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Add Operations
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.addentryops}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Total Connections
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.connectionseq}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Delete Operations
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.removeentryops}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Conns in Max Threads
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.connectionsinmaxthreads}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Modify Operations
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.modifyentryops}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Conns Exceeded Max Threads
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.connectionsmaxthreadscount}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        ModRDN Operations
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.modifyrdnops}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Bytes Received
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.bytesrecv}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Search Operations
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.searchops}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Bytes Sent
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.bytessent}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        One Level Searches
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.onelevelsearchops}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Entries Returned
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.entriesreturned}</b>
                                    </GridItem>
                                    <GridItem span={4}>
                                        Whole Tree Searches
                                    </GridItem>
                                    <GridItem span={2}>
                                        <b>{this.props.snmpData.wholesubtreesearchops}</b>
                                    </GridItem>
                                </Grid>
                            </TabPane>
                        </TabContent>
                    </div>
                </TabContainer>
            </div>
        );
    }
}

ServerMonitor.propTypes = {
    serverId: PropTypes.string,
    data: PropTypes.object,
    reload: PropTypes.func,
    enableTree: PropTypes.func,
};

ServerMonitor.defaultProps = {
    serverId: "",
    data: {},
    reload: noop,
    enableTree: noop,
};

export default ServerMonitor;
