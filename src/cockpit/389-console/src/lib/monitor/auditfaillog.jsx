import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Spinner,
    noop,
    Row,
    Col,
    Icon,
    ControlLabel
} from "patternfly-react";

export class AuditFailLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            auditfaillogData: "",
            auditfailReloading: false,
            auditfaillog_cont_refresh: "",
            auditfailRefreshing: false,
            auditfailLines: "50",
        };

        this.refreshAuditFailLog = this.refreshAuditFailLog.bind(this);
        this.handleAuditFailChange = this.handleAuditFailChange.bind(this);
        this.auditFailRefreshCont = this.auditFailRefreshCont.bind(this);
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.auditfaillog_cont_refresh);
    }

    componentDidUpdate () {
        // Set the textarea to be scrolled down to the bottom
        let textarea = document.getElementById('auditfaillog-area');
        textarea.scrollTop = textarea.scrollHeight;
    }

    componentDidMount() {
        this.props.enableTree();
        this.refreshAuditFailLog();
    }

    refreshAuditFailLog () {
        this.setState({
            auditfailReloading: true
        });
        let cmd = ["tail", "-" + this.state.auditfailLines, this.props.logLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        auditfaillogData: content,
                        auditfailReloading: false
                    }));
                });
    }

    auditFailRefreshCont(e) {
        if (e.target.checked) {
            this.state.auditfaillog_cont_refresh = setInterval(this.refreshAuditFailLog, 2000);
        } else {
            clearInterval(this.state.auditfaillog_cont_refresh);
        }
        this.setState({
            auditfailRefreshing: e.target.checked,
        });
    }

    handleAuditFailChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                auditfailLines: value
            }
        ), this.refreshAuditFailLog);
    }

    render() {
        let spinner = "";
        if (this.state.auditfailReloading) {
            spinner =
                <div>
                    <Spinner inline loading size="sm" />
                    Reloading audit failure log...
                </div>;
        }
        let contRefreshCheckbox =
            <input type="checkbox" className="ds-sm-left-margin"
                onChange={this.auditFailRefreshCont}
            />;
        if (this.state.auditfailRefreshing) {
            contRefreshCheckbox =
                <input type="checkbox" className="ds-sm-left-margin"
                    defaultChecked onChange={this.auditFailRefreshCont}
                />;
        }

        let selectLines =
            <div>
                <label htmlFor="auditfaillog-lines"> Log Lines To Show</label><select
                    className="btn btn-default dropdown ds-left-margin"
                    onChange={this.handleAuditFailChange}
                    id="auditfaillog-lines" value={this.state.auditfailLines}>
                    <option>50</option>
                    <option>100</option>
                    <option>200</option>
                    <option>300</option>
                    <option>400</option>
                    <option>500</option>
                    <option>1000</option>
                    <option>2000</option>
                    <option>5000</option>
                    <option>10000</option>
                    <option>50000</option>
                </select>
            </div>;

        return (
            <div id="monitor-log-auditfail-page">
                <Row>
                    <Col sm={4}>
                        <ControlLabel className="ds-suffix-header">
                            Audit Failure Log
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh audit failure log"
                                onClick={this.refreshAuditFailLog}
                            />
                        </ControlLabel>
                    </Col>
                    <Col sm={8} className="ds-float-left">
                        {spinner}
                    </Col>
                </Row>
                <Row className="ds-margin-top-lg">
                    <Col sm={6}>
                        {selectLines}
                    </Col>
                    <Col sm={6}>
                        <div className="ds-float-right">
                            <label>
                                {contRefreshCheckbox} Continuously Refresh
                            </label>
                        </div>
                    </Col>
                </Row>
                <textarea id="auditfaillog-area" className="ds-logarea" value={this.state.auditfaillogData} readOnly />
            </div>
        );
    }
}

// Props and defaultProps

AuditFailLogMonitor.propTypes = {
    logLocation: PropTypes.string,
    enableTree: PropTypes.func,
};

AuditFailLogMonitor.defaultProps = {
    logLocation: "",
    enableTree: noop,
};

export default AuditFailLogMonitor;
