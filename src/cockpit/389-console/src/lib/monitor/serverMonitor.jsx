import React from "react";
import PropTypes from "prop-types";
import "../../css/ds.css";
import { get_date_string, get_date_diff } from "../tools.jsx";
import {
    ConnectionTable
} from "./monitorTables.jsx";
import {
    Nav,
    NavItem,
    TabContent,
    TabPane,
    TabContainer,
    Row,
    Col,
    ControlLabel,
    Icon,
    noop
} from "patternfly-react";

export class ServerMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeKey: 1,
        };
        this.handleNavSelect = this.handleNavSelect.bind(this);
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    render() {
        // Generate start time and Uptime
        let startTime = this.props.data.starttime[0];
        let currTime = this.props.data.currenttime[0];
        let startDate = get_date_string(this.props.data.starttime[0]);
        let uptime = get_date_diff(startTime, currTime);

        return (
            <div id="monitor-server-page" className="container-fluid">
                <Row>
                    <Col sm={12} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header">
                            Server Statistics
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh server monitor"
                                onClick={this.props.reload}
                            />
                        </ControlLabel>
                    </Col>
                </Row>

                <TabContainer id="server-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div>
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: 'Server Information'}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: 'Connection Table'}} />
                            </NavItem>
                        </Nav>
                        <TabContent>

                            <TabPane eventKey={1}>
                                <div className="ds-container ds-margin-top-lg">
                                    <div className="ds-inline">
                                        <div>
                                            <label htmlFor="monitor-serverid" className="ds-label-xsm">Server Instance</label><input type="text"
                                                className="ds-input" id="monitor-serverid" value={"slapd-" + this.props.serverId} size="50" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-version" className="ds-label-xsm">Version</label><input type="text"
                                                className="ds-input" id="monitor-version" value={this.props.data.version} size="50" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-starttime" className="ds-label-xsm">Server Started</label><input type="text"
                                                className="ds-input" id="monitor-server-starttime" value={startDate} size="50" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-uptime" className="ds-label-xsm">Server Uptime</label><input type="text"
                                                className="ds-input" id="monitor-server-uptime" value={uptime} size="50" readOnly />
                                        </div>
                                    </div>
                                </div>
                                <hr />
                                <div className="ds-container">
                                    <div className="ds-inline">
                                        <div>
                                            <label htmlFor="monitor-server-threads" className="ds-monitor-label">Threads</label><input type="text"
                                                className="ds-input" id="monitor-server-threads" value={this.props.data.threads} size="12" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-totalconnections" className="ds-monitor-label">Total Connections</label><input type="text"
                                                className="ds-input" id="monitor-server-totalconnections" value={this.props.data.totalconnections} size="12" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-currentconnections" className="ds-monitor-label">Current Conections</label><input type="text"
                                                className="ds-input" id="monitor-server-currentconnections" value={this.props.data.currentconnections} size="12" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-currentconnectionsatmaxthreads" className="ds-monitor-label">Conns At Max Threads</label><input type="text"
                                                className="ds-input" id="monitor-server-currentconnectionsatmaxthreads" value={this.props.data.currentconnectionsatmaxthreads} size="12" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-maxthreadsperconnhits" className="ds-monitor-label">Conns Hit Max Threads</label><input type="text"
                                                className="ds-input" id="monitor-server-maxthreadsperconnhits" value={this.props.data.maxthreadsperconnhits} size="12" readOnly />
                                        </div>
                                    </div>
                                    <div className="ds-divider" />
                                    <div className="ds-inline">
                                        <div>
                                            <label htmlFor="monitor-server-readwaiters" className="ds-monitor-label-med">Threads Waiting To Read</label><input type="text"
                                                className="ds-input" id="monitor-server-readwaiters" value={this.props.data.readwaiters} size="12" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-opsinitiated" className="ds-monitor-label-med">Operations Started</label><input type="text"
                                                className="ds-input" id="monitor-server-opsinitiated" value={this.props.data.opsinitiated} size="12" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-opscompleted" className="ds-monitor-label-med">Operations Completed</label><input type="text"
                                            className="ds-input" id="monitor-server-opscompleted" value={this.props.data.opscompleted} size="12" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-entriessent" className="ds-monitor-label-med">Entries Returned To Clients</label><input type="text"
                                            className="ds-input" id="monitor-server-entriessent" value={this.props.data.entriessent} size="12" readOnly />
                                        </div>
                                        <div>
                                            <label htmlFor="monitor-server-bytessent" className="ds-monitor-label-med">Bytes Sent to Clients</label><input type="text"
                                                className="ds-input" id="monitor-server-bytessent" value={this.props.data.bytessent} size="12" readOnly />
                                        </div>

                                    </div>
                                </div>
                            </TabPane>
                            <TabPane eventKey={2}>
                                <ConnectionTable conns={this.props.data.connection} />
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
    reload: PropTypes.func
};

ServerMonitor.defaultProps = {
    serverId: "",
    data: {},
    reload: noop
};

export default ServerMonitor;
