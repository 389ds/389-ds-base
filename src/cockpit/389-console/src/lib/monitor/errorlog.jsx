import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Button,
    Checkbox,
    FormSelect,
    FormSelectOption,
    Toolbar,
    ToolbarContent,
    ToolbarItem,
    Spinner,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { SyncAltIcon } from '@patternfly/react-icons';
import { LogViewer, LogViewerSearch } from '@patternfly/react-log-viewer';

const _ = cockpit.gettext;

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
            isTextWrapped: false,
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

        this.handleRefreshErrorLog = this.handleRefreshErrorLog.bind(this);
        this.errorRefreshCont = this.errorRefreshCont.bind(this);
        this.handleErrorChange = this.handleErrorChange.bind(this);
        this.handleSevChange = this.handleSevChange.bind(this);
        this.handleTextWrappedChange = this.handleTextWrappedChange.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
        this.handleRefreshErrorLog();
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.errorlog_cont_refresh);
    }

    handleRefreshErrorLog () {
        this.setState({
            errorReloading: true
        });

        // Use different command when "no-limit" is selected
        let cmd;
        if (this.state.errorLines === "no-limit") {
            cmd = ["cat", this.props.logLocation];
        } else {
            cmd = ["tail", "-" + this.state.errorLines, this.props.logLocation];
        }
        
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(data => {
                    if (this.state.errorSevLevel !== "Everything") {
                        // Filter Data
                        const lines = data.split('\n');
                        let new_data = "";
                        for (let i = 0; i < lines.length; i++) {
                            let line = "";
                            if (this.state.errorSevLevel === "Error Messages") {
                                for (const lev of this.sev_all_errs) {
                                    if (lines[i].indexOf(lev) !== -1) {
                                        line = lines[i] + "\n";
                                    }
                                }
                            } else if (this.state.errorSevLevel === "Info Messages") {
                                for (const lev of this.sev_all_info) {
                                    if (lines[i].indexOf(lev) !== -1) {
                                        line = lines[i] + "\n";
                                    }
                                }
                            } else if (lines[i].indexOf(this.sev_levels[this.state.errorSevLevel]) !== -1) {
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
            this.setState({
                errorlog_cont_refresh: setInterval(this.handleRefreshErrorLog, 2000),
                errorRefreshing: e.target.checked,
            });
        } else {
            clearInterval(this.state.errorlog_cont_refresh);
            this.setState({
                errorRefreshing: e.target.checked,
            });
        }
    }

    handleErrorChange(e) {
        const value = e.target.value;
        this.setState(() => (
            {
                errorLines: value
            }
        ), this.handleRefreshErrorLog);
    }

    handleSevChange(e) {
        const value = e.target.value;

        this.setState({
            errorSevLevel: value,
        }, this.handleRefreshErrorLog);
    }

    handleTextWrappedChange(_event, checked) {
        this.setState({
            isTextWrapped: checked
        });
    }

    render() {
        let spinner = null;
        if (this.state.errorReloading) {
            spinner = (
                <ToolbarItem>
                    <Spinner size="sm" />
                    {_("Reloading errors log...")}
                </ToolbarItem>
            );
        }

        const logViewerToolbar = (
            <Toolbar>
                <ToolbarContent>
                    <ToolbarItem>
                        <LogViewerSearch placeholder={_("Search")} />
                    </ToolbarItem>
                    <ToolbarItem>
                        <FormSelect
                            id="errorLines"
                            value={this.state.errorLines}
                            onChange={(event) => {
                                this.handleErrorChange(event);
                            }}
                            aria-label={_("Lines to display")}
                            style={{ minWidth: '150px', width: 'auto' }}
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
                            <FormSelectOption key="no-limit" value="no-limit" label={_("No Limit")} />
                        </FormSelect>
                    </ToolbarItem>
                    <ToolbarItem>
                        <div className="ds-container">
                            <div className="ds-label">
                                {_("Filter")}
                            </div>
                            <FormSelect
                                className="ds-left-margin"
                                value={this.state.errorSevLevel}
                                onChange={(event) => {
                                    this.handleSevChange(event);
                                }}
                                aria-label={_("Filter by severity")}
                                style={{ minWidth: '150px', width: 'auto' }}
                            >
                                <FormSelectOption key="Everything" value="Everything" label={_("Everything")} />
                                <FormSelectOption key="Error Messages" value="Error Messages" label={_("Error Messages")} />
                                <FormSelectOption key="Info Messages" value="Info Messages" label={_("Info Messages")} />
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
                    </ToolbarItem>
                    <ToolbarItem alignSelf="center" className="ds-float-right">
                        <Checkbox
                            id="errorRefreshing"
                            isChecked={this.state.errorRefreshing}
                            onChange={(e) => { this.errorRefreshCont(e) }}
                            label={_("Continuously Refresh")}
                        />
                    </ToolbarItem>
                    <ToolbarItem alignSelf="center">
                        <Checkbox
                            id="wrapText"
                            isChecked={this.state.isTextWrapped}
                            onChange={this.handleTextWrappedChange}
                            label={_("Wrap text")}
                            aria-label="wrap text checkbox"
                        />
                    </ToolbarItem>
                    {spinner}
                </ToolbarContent>
            </Toolbar>
        );

        return (
            <div id="monitor-log-errors-page">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Errors Log")}
                        <Button 
                            variant="plain"
                            aria-label={_("Refresh error log")}
                            onClick={this.handleRefreshErrorLog}
                        >
                            <SyncAltIcon />
                        </Button>
                    </Text>
                </TextContent>
                <div className="ds-margin-top-lg">
                    <LogViewer
                        data={this.state.errorlogData}
                        isTextWrapped={this.state.isTextWrapped}
                        toolbar={logViewerToolbar}
                        scrollToRow={this.state.errorlogData.split('\n').length}
                        height="600px"
                    />
                </div>
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
