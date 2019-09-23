import React from "react";
import PropTypes from "prop-types";
import "../../css/ds.css";
import {
    Spinner,
    ControlLabel,
    noop,
    Icon,
    Row,
    Col
} from "patternfly-react";

export class ErrorLogMonitor extends React.Component {
    componentDidUpdate () {
        // Set the textarea to be scrolled down to the bottom
        let textarea = document.getElementById('errorslog-area');
        textarea.scrollTop = textarea.scrollHeight;
    }

    componentDidMount() {
        this.props.enableTree();
    }

    render() {
        let spinner = "";
        if (this.props.reloading) {
            spinner =
                <div>
                    <Spinner inline loading size="sm" />
                    Reloading errors log...
                </div>;
        }
        let contRefreshCheckbox =
            <input type="checkbox" className="ds-sm-left-margin"
                onChange={this.props.handleRefresh}
            />;
        if (this.props.refreshing) {
            contRefreshCheckbox =
                <input type="checkbox" className="ds-sm-left-margin"
                    defaultChecked onChange={this.props.handleRefresh}
                />;
        }

        let selectLines =
            <div>
                <label htmlFor="errorlog-lines"> Log Lines To Show</label><select
                    className="btn btn-default dropdown ds-left-margin"
                    onChange={this.props.handleChange}
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
                                onClick={() => this.props.reload(this.props.reload)}
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
                                onChange={this.props.handleSevLevel}>
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
                <textarea id="errorslog-area" className="ds-logarea" value={this.props.data} readOnly />
            </div>
        );
    }
}

// Props and defaultProps

ErrorLogMonitor.propTypes = {
    data: PropTypes.string,
    handleChange: PropTypes.func,
    reload: PropTypes.func,
    reloading: PropTypes.bool,
    handleSevLevel: PropTypes.func,
    refreshing: PropTypes.bool,
    handleRefresh: PropTypes.func,
    lines: PropTypes.string,
    enableTree: PropTypes.func,
};

ErrorLogMonitor.defaultProps = {
    data: "",
    handleChange: noop,
    reload: noop,
    reloading: false,
    handleSevLevel: noop,
    refreshing: false,
    handleRefresh: noop,
    lines: "50",
    enableTree: noop,
};

export default ErrorLogMonitor;
