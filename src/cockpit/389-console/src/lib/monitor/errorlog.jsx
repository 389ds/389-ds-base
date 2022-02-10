import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Checkbox,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Spinner,
    TextArea,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';

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
        const sev_emerg = " - EMERG - ";
        const sev_crit = " - CRIT - ";
        const sev_alert = " - ALERT - ";
        const sev_err = " - ERR - ";
        const sev_warn = " - WARN - ";
        const sev_notice = " - NOTICE - ";
        const sev_info = " - INFO - ";
        const sev_debug = " - DEBUG - ";
        this.sev_levels = {
            Emergency: sev_emerg,
            Critical: sev_crit,
            Alert: sev_alert,
            Error: sev_err,
            Warning: sev_warn,
            Notice: sev_notice,
            Info: sev_info,
            Debug: sev_debug
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
        const textarea = document.getElementById('errorslog-area');
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

        const cmd = ["tail", "-" + this.state.errorLines, this.props.logLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(data => {
                    if (this.state.errorSevLevel != "Everything") {
                        // Filter Data
                        const lines = data.split('\n');
                        let new_data = "";
                        for (let i = 0; i < lines.length; i++) {
                            let line = "";
                            if (this.state.errorSevLevel == "Error Messages") {
                                for (const lev of this.sev_all_errs) {
                                    if (lines[i].indexOf(lev) != -1) {
                                        line = lines[i] + "\n";
                                    }
                                }
                            } else if (this.state.errorSevLevel == "Info Messages") {
                                for (const lev of this.sev_all_info) {
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
        const value = e.target.value;
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
                    <Spinner size="sm" />
                    Reloading errors log...
                </div>;
        }

        return (
            <div id="monitor-log-errors-page">
                <Grid>
                    <GridItem span={3}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Errors Log <FontAwesomeIcon
                                    size="lg"
                                    className="ds-left-margin ds-refresh"
                                    icon={faSyncAlt}
                                    title="Refresh error log"
                                    onClick={this.refreshErrorLog}
                                />
                            </Text>
                        </TextContent>
                    </GridItem>
                    <GridItem span={9} className="ds-float-left">
                        {spinner}
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top-lg ds-indent">
                    <GridItem span={2}>
                        <FormSelect
                            id="errorLines"
                            value={this.state.errorLines}
                            onChange={(value, event) => {
                                this.handleErrorChange(event);
                            }}
                            aria-label="FormSelect Input"
                        >
                            <FormSelectOption key="50" value="50" label="50" />
                            <FormSelectOption key="100" value="100" label="100" />
                            <FormSelectOption key="200" value="200" label="200" />
                            <FormSelectOption key="300" value="300" label="300" />
                            <FormSelectOption key="400" value="400" label="400" />
                            <FormSelectOption key="500" value="500" label="500" />
                            <FormSelectOption key="1000" value="1000" label="1000" />
                            <FormSelectOption key="2000" value="2000" label="2000" />
                            <FormSelectOption key="5000" value="5000" label="5000" />
                            <FormSelectOption key="10000" value="10000" label="10000" />
                            <FormSelectOption key="50000" value="50000" label="50000" />
                        </FormSelect>
                    </GridItem>
                    <GridItem offset={4} span={4}>
                        <div className="ds-container">
                            <div className="ds-label">
                                Filter
                            </div>
                            <FormSelect
                                className="ds-left-margin"
                                value={this.state.errorSevLevel}
                                onChange={(value, event) => {
                                    this.handleSevChange(event);
                                }}
                                aria-label="FormSelect Input"
                            >
                                <FormSelectOption key="Everything" value="Everything" label="Everything" />
                                <FormSelectOption key="Error Messages" value="Error Messages" label="Error Messages" />
                                <FormSelectOption key="Info Messages" value="Info Messages" label="Info Messages" />
                                <FormSelectOption isDisabled key="disabled" value="disabled" label="---------------" />
                                <FormSelectOption key="Emergency" value="Emergency" label="Emergency" />
                                <FormSelectOption key="Alert" value="Alert" label="Alert" />
                                <FormSelectOption key="Critical" value="Critical" label="Critical" />
                                <FormSelectOption key="Warning" value="Warning" label="Warning" />
                                <FormSelectOption key="Notice" value="Notice" label="Notice" />
                                <FormSelectOption key="Info" value="Info" label="Info" />
                                <FormSelectOption key="Debug" value="Debug" label="Debug" />
                            </FormSelect>
                        </div>
                    </GridItem>
                    <GridItem span={4}>
                        <div className="ds-float-right">
                            <Checkbox
                                id="errorRefreshing"
                                isChecked={this.state.errorRefreshing}
                                onChange={(checked, e) => { this.errorRefreshCont(e) }}
                                label="Continuously Refresh"
                            />
                        </div>
                    </GridItem>
                    <TextArea
                        id="errorslog-area"
                        resizeOrientation="vertical"
                        className="ds-logarea ds-margin-top-lg"
                        value={this.state.errorlogData}
                        aria-label="text area example"
                    />
                </Grid>
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
};

export default ErrorLogMonitor;
