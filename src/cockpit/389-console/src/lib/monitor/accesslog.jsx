import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Spinner,
    noop,
    Row,
    Col,
    ControlLabel,
    Icon,
} from "patternfly-react";

export class AccessLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            accesslogData: "",
            accessReloading: false,
            accesslog_cont_refresh: "",
            accessRefreshing: false,
            accessLines: "50",
        };

        this.refreshAccessLog = this.refreshAccessLog.bind(this);
        this.handleAccessChange = this.handleAccessChange.bind(this);
        this.accessRefreshCont = this.accessRefreshCont.bind(this);
    }

    componentDidUpdate () {
        // Set the textarea to be scrolled down to the bottom
        let textarea = document.getElementById('accesslog-area');
        textarea.scrollTop = textarea.scrollHeight;
    }

    componentDidMount() {
        this.props.enableTree();
        this.refreshAccessLog();
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.accesslog_cont_refresh);
    }

    accessRefreshCont(e) {
        if (e.target.checked) {
            this.state.accesslog_cont_refresh = setInterval(this.refreshAccessLog, 2000);
        } else {
            clearInterval(this.state.accesslog_cont_refresh);
        }
        this.setState({
            accessRefreshing: e.target.checked,
        });
    }

    handleAccessChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                accessLines: value
            }
        ), this.refreshAccessLog);
    }

    refreshAccessLog () {
        this.setState({
            accessReloading: true
        });
        let cmd = ["tail", "-" + this.state.accessLines, this.props.logLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        accesslogData: content,
                        accessReloading: false
                    }));
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        accessReloading: false,
                    });
                });
    }

    render() {
        let spinner = "";
        if (this.state.accessReloading) {
            spinner =
                <div>
                    <Spinner inline loading size="sm" />
                    Reloading access log...
                </div>;
        }
        let contRefreshCheckbox =
            <input type="checkbox" className="ds-sm-left-margin"
                onChange={this.accessRefreshCont}
            />;
        if (this.state.accessRefreshing) {
            contRefreshCheckbox =
                <input type="checkbox" className="ds-sm-left-margin"
                    defaultChecked onChange={this.accessRefreshCont}
                />;
        }

        let selectLines =
            <div>
                <label htmlFor="accesslog-lines"> Log Lines To Show</label><select
                    className="btn btn-default dropdown ds-left-margin"
                    onChange={this.handleAccessChange}
                    id="accesslog-lines" value={this.state.accessLines}>
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
            <div id="monitor-log-access-page">
                <Row>
                    <Col sm={3}>
                        <ControlLabel className="ds-suffix-header">
                            Access Log
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh access log"
                                onClick={this.refreshAccessLog}
                            />
                        </ControlLabel>
                    </Col>
                    <Col sm={9} className="ds-float-left">
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
                <textarea id="accesslog-area" className="ds-logarea" value={this.state.accesslogData} readOnly />
            </div>
        );
    }
}

// Props and defaultProps

AccessLogMonitor.propTypes = {
    logLocation: PropTypes.string,
    enableTree: PropTypes.func,
};

AccessLogMonitor.defaultProps = {
    logLocation: "",
    enableTree: noop,
};

export default AccessLogMonitor;
