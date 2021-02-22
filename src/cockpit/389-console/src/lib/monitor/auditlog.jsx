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

export class AuditLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            auditlogData: "",
            auditReloading: false,
            auditlog_cont_refresh: "",
            auditRefreshing: false,
            auditLines: "50",
        };
        this.refreshAuditLog = this.refreshAuditLog.bind(this);
        this.handleAuditChange = this.handleAuditChange.bind(this);
        this.auditRefreshCont = this.auditRefreshCont.bind(this);
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.auditlog_cont_refresh);
    }

    componentDidUpdate () {
        // Set the textarea to be scrolled down to the bottom
        let textarea = document.getElementById('auditlog-area');
        textarea.scrollTop = textarea.scrollHeight;
    }

    componentDidMount() {
        this.props.enableTree();
        this.refreshAuditLog();
    }

    refreshAuditLog () {
        console.log("MARK refreshing");
        this.setState({
            auditReloading: true
        });
        let cmd = ["tail", "-" + this.state.auditLines, this.props.logLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        auditlogData: content,
                        auditReloading: false
                    }));
                });
    }

    auditRefreshCont(e) {
        console.log("MARK setting continuous: ", e.target.checked);
        if (e.target.checked) {
            this.state.auditlog_cont_refresh = setInterval(this.refreshAuditLog, 2000);
        } else {
            clearInterval(this.state.auditlog_cont_refresh);
        }
        this.setState({
            auditRefreshing: e.target.checked,
        });
    }

    handleAuditChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                auditLines: value
            }
        ), this.refreshAuditLog);
    }

    render() {
        let spinner = "";
        if (this.state.auditReloading) {
            spinner =
                <div>
                    <Spinner inline loading size="sm" />
                    Reloading audit log...
                </div>;
        }
        let contRefreshCheckbox =
            <input type="checkbox" className="ds-sm-left-margin"
                onChange={this.auditRefreshCont}
            />;
        if (this.state.auditRefreshing) {
            contRefreshCheckbox =
                <input type="checkbox" className="ds-sm-left-margin"
                    defaultChecked onChange={this.auditRefreshCont}
                />;
        }

        let selectLines =
            <div>
                <label htmlFor="auditlog-lines"> Log Lines To Show</label><select
                    className="btn btn-default dropdown ds-left-margin"
                    onChange={this.handleAuditChange}
                    id="auditlog-lines" value={this.state.auditLines}>
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
            <div id="monitor-log-audit-page">
                <Row>
                    <Col sm={4}>
                        <ControlLabel className="ds-suffix-header">
                            Audit Log
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh audit log"
                                onClick={this.refreshAuditLog}
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
                <textarea id="auditlog-area" className="ds-logarea" value={this.state.auditlogData} readOnly />
            </div>
        );
    }
}

// Props and defaultProps

AuditLogMonitor.propTypes = {
    logLocation: PropTypes.string,
    enableTree: PropTypes.func,
};

AuditLogMonitor.defaultProps = {
    logLocation: "",
    enableTree: noop,
};

export default AuditLogMonitor;
