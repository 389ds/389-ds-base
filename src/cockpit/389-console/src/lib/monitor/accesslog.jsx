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

export class AccessLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            accesslogData: "",
            accessReloading: false,
            accesslog_cont_refresh: "",
            accessRefreshing: false,
            accessLines: "50",
            isTextWrapped: false,
        };

        this.handleRefreshAccessLog = this.handleRefreshAccessLog.bind(this);
        this.handleAccessChange = this.handleAccessChange.bind(this);
        this.accessRefreshCont = this.accessRefreshCont.bind(this);
        this.handleTextWrappedChange = this.handleTextWrappedChange.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
        this.handleRefreshAccessLog();
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.accesslog_cont_refresh);
    }

    accessRefreshCont(e) {
        if (e.target.checked) {
            this.setState({
                accesslog_cont_refresh: setInterval(this.handleRefreshAccessLog, 2000),
                accessRefreshing: e.target.checked,
            });
        } else {
            clearInterval(this.state.accesslog_cont_refresh);
            this.setState({
                accessRefreshing: e.target.checked,
            });
        }
    }

    handleAccessChange(e) {
        const value = e.target.value;
        this.setState(() => (
            {
                accessLines: value
            }
        ), this.handleRefreshAccessLog);
    }

    handleTextWrappedChange(_event, checked) {
        this.setState({
            isTextWrapped: checked
        });
    }

    handleRefreshAccessLog () {
        this.setState({
            accessReloading: true
        });
        
        // Use different command when "no-limit" is selected
        let cmd;
        if (this.state.accessLines === "no-limit") {
            cmd = ["cat", this.props.logLocation];
        } else {
            cmd = ["tail", "-" + this.state.accessLines, this.props.logLocation];
        }
        
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
        let spinner = null;
        if (this.state.accessReloading) {
            spinner = (
                <ToolbarItem>
                    <Spinner size="sm" />
                    {_("Reloading access log...")}
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
                            id="accessLines"
                            value={this.state.accessLines}
                            onChange={(event) => {
                                this.handleAccessChange(event);
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
                    <ToolbarItem alignSelf="center" className="ds-float-right">
                        <Checkbox
                            id="accessRefreshing"
                            isChecked={this.state.accessRefreshing}
                            onChange={(e) => { this.accessRefreshCont(e) }}
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
            <div id="monitor-log-access-page">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Access Log")}
                        <Button 
                            variant="plain"
                            aria-label={_("Refresh access log")}
                            onClick={this.handleRefreshAccessLog}
                        >
                            <SyncAltIcon />
                        </Button>
                    </Text>
                </TextContent>
                <div className="ds-margin-top-lg">
                    <LogViewer
                        data={this.state.accesslogData}
                        isTextWrapped={this.state.isTextWrapped}
                        toolbar={logViewerToolbar}
                        scrollToRow={this.state.accesslogData.split('\n').length}
                        height="600px"
                    />
                </div>
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
};

export default AccessLogMonitor;
