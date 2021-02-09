import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Spinner,
    ControlLabel,
    noop,
    Icon,
    Row,
    Col
} from "patternfly-react";

export class ErrorLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            errorlogData: "",
            errorReloading: false,
            errorlog_cont_refresh: "",
            errorRefreshing: false,
            errorSevLevel: "Everything",
            errorLines: "50",
        };

        // Build the log severity sev_levels
        let sev_emerg = " - EMERG - ";
        let sev_crit = " - CRIT - ";
        let sev_alert = " - ALERT - ";
        let sev_err = " - ERR - ";
        let sev_warn = " - WARN - ";
        let sev_notice = " - NOTICE - ";
        let sev_info = " - INFO - ";
        let sev_debug = " - DEBUG - ";
        this.sev_levels = {
            "Emergency": sev_emerg,
            "Critical": sev_crit,
            "Alert": sev_alert,
            "Error": sev_err,
            "Warning": sev_warn,
            "Notice": sev_notice,
            "Info": sev_info,
            "Debug": sev_debug
        };
        this.sev_all_errs = [sev_emerg, sev_crit, sev_alert, sev_err];
        this.sev_all_info = [sev_warn, sev_notice, sev_info, sev_debug];

        this.refreshErrorLog = this.refreshErrorLog.bind(this);
        this.errorRefreshCont = this.errorRefreshCont.bind(this);
        this.handleErrorChange = this.handleErrorChange.bind(this);
        this.handleSevChange = this.handleSevChange.bind(this);
    }

    componentDidUpdate () {
        // Set the textarea to be scrolled down to the bottom
        let textarea = document.getElementById('errorslog-area');
        textarea.scrollTop = textarea.scrollHeight;
    }

    componentDidMount() {
        this.props.enableTree();
        this.refreshErrorLog();
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.errorlog_cont_refresh);
    }

    refreshErrorLog () {
        this.setState({
            errorReloading: true
        });

        let cmd = ["tail", "-" + this.state.errorLines, this.props.logLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(data => {
                    if (this.state.errorSevLevel != "Everything") {
                        // Filter Data
                        let lines = data.split('\n');
                        let new_data = "";
                        for (let i = 0; i < lines.length; i++) {
                            let line = "";
                            if (this.state.errorSevLevel == "Error Messages") {
                                for (let lev of this.sev_all_errs) {
                                    if (lines[i].indexOf(lev) != -1) {
                                        line = lines[i] + "\n";
                                    }
                                }
                            } else if (this.state.errorSevLevel == "Info Messages") {
                                for (let lev of this.sev_all_info) {
                                    if (lines[i].indexOf(lev) != -1) {
                                        line = lines[i] + "\n";
                                    }
                                }
                            } else if (lines[i].indexOf(this.sev_levels[this.state.errorSevLevel]) != -1) {
                                line = lines[i] + "\n";
                            }
                            // Add the filtered line to new data
                            new_data += line;
                        }
                        data = new_data;
                    }

                    this.setState(() => ({
                        errorlogData: data,
                        errorReloading: false
                    }));
                });
    }

    errorRefreshCont(e) {
        if (e.target.checked) {
            this.state.errorlog_cont_refresh = setInterval(this.refreshErrorLog, 2000);
        } else {
            clearInterval(this.state.errorlog_cont_refresh);
        }
        this.setState({
            errorRefreshing: e.target.checked,
        });
    }

    handleErrorChange(e) {
        let value = e.target.value;
        this.setState(() => (
            {
                errorLines: value
            }
        ), this.refreshErrorLog);
    }

    handleSevChange(e) {
        const value = e.target.value;

        this.setState({
            errorSevLevel: value,
        }, this.refreshErrorLog);
    }

    render() {
        let spinner = "";
        if (this.state.errorReloading) {
            spinner =
                <div>
                    <Spinner inline loading size="sm" />
                    Reloading errors log...
                </div>;
        }
        let contRefreshCheckbox =
            <input type="checkbox" className="ds-sm-left-margin"
                onChange={this.errorRefreshCont}
            />;
        if (this.state.errorRefreshing) {
            contRefreshCheckbox =
                <input type="checkbox" className="ds-sm-left-margin"
                    defaultChecked onChange={this.errorRefreshCont}
                />;
        }

        let selectLines =
            <div>
                <label htmlFor="errorlog-lines"> Log Lines To Show</label><select
                    className="btn btn-default dropdown ds-left-margin"
                    onChange={this.handleErrorChange}
                    id="errorlog-lines" value={this.props.lines}>
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
            <div id="monitor-log-errors-page">
                <Row>
                    <Col sm={3}>
                        <ControlLabel className="ds-suffix-header">
                            Errors Log
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh errors log"
                                onClick={this.refreshErrorLog}
                            />
                        </ControlLabel>
                    </Col>
                    <Col sm={9} className="ds-float-left">
                        {spinner}
                    </Col>
                </Row>
                <Row className="ds-margin-top-lg">
                    <Col sm={5}>
                        {selectLines}
                    </Col>
                    <Col sm={4}>
                        <div className="dropdown">
                            <label htmlFor="errorslog-sev-level">Filter</label><select
                                className="btn btn-default dropdown ds-left-margin"
                                onChange={this.handleSevChange}>
                                <option>Everything</option>
                                <option>Error Messages</option>
                                <option>Info Messages</option>
                                <option disabled>──────────</option>
                                <option>Emergency</option>
                                <option>Alert</option>
                                <option>Critical</option>
                                <option>Error</option>
                                <option>Warning</option>
                                <option>Notice</option>
                                <option>Info</option>
                                <option>Debug</option>
                            </select>
                        </div>
                    </Col>
                    <Col sm={3}>
                        <div className="ds-float-right">
                            <label>
                                {contRefreshCheckbox} Continuously Refresh
                            </label>
                        </div>
                    </Col>
                </Row>
                <textarea id="errorslog-area" className="ds-logarea" value={this.state.errorlogData} readOnly />
            </div>
        );
    }
}

// Props and defaultProps

ErrorLogMonitor.propTypes = {
    logLocation: PropTypes.string,
    enableTree: PropTypes.func,
};

ErrorLogMonitor.defaultProps = {
    logLocation: "",
    enableTree: noop,
};

export default ErrorLogMonitor;
