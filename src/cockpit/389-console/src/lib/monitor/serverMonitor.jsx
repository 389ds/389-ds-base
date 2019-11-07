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
    Form,
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

    componentDidMount() {
        this.props.enableTree();
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
            <div id="monitor-server-page">
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

                <TabContainer className="ds-margin-top-lg" id="server-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
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
                                <Form horizontal className="ds-margin-top-lg">
                                    <Row>
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Server Instance
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-serverid" value={"slapd-" + this.props.serverId} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Version
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-version" value={this.props.data.version} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Server Started
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-starttime" value={startDate} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Server Uptime
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-uptime" value={uptime} readOnly />
                                        </Col>
                                    </Row>
                                </Form>
                                <hr />
                                <Form horizontal>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Worker Threads
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-threads" value={this.props.data.threads} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Threads Waiting To Read
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-readwaiters" value={this.props.data.readwaiters} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Conns At Max Threads
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-currentconnectionsatmaxthreads" value={this.props.data.currentconnectionsatmaxthreads} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Conns Exceeded Max Threads
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-maxthreadsperconnhits" value={this.props.data.maxthreadsperconnhits} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Total Connections
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-totalconnections" value={this.props.data.totalconnections} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Current Connections
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-currentconnections" value={this.props.data.currentconnections} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Operations Started
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-opsinitiated" value={this.props.data.opsinitiated} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Operations Completed
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-opscompleted" value={this.props.data.opscompleted} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Entries Returned To Clients
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-entriessent" value={this.props.data.entriessent} readOnly />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Bytes Sent to Clients
                                        </Col>
                                        <Col sm={8}>
                                            <input type="text" className="ds-input-auto" id="monitor-server-bytessent" value={this.props.data.bytessent} readOnly />
                                        </Col>
                                    </Row>
                                </Form>
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
