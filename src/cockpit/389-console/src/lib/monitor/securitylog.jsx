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

export class SecurityLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            securitylogData: "",
            securityReloading: false,
            securitylog_cont_refresh: "",
            securityRefreshing: false,
            securityLines: "50",
            isTextWrapped: false,
        };

        this.handleRefreshSecurityLog = this.handleRefreshSecurityLog.bind(this);
        this.handleSecurityChange = this.handleSecurityChange.bind(this);
        this.securityRefreshCont = this.securityRefreshCont.bind(this);
        this.handleTextWrappedChange = this.handleTextWrappedChange.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
        this.handleRefreshSecurityLog();
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.securitylog_cont_refresh);
    }

    securityRefreshCont(e) {
        if (e.target.checked) {
            this.setState({
                securitylog_cont_refresh: setInterval(this.handleRefreshSecurityLog, 2000),
                securityRefreshing: e.target.checked,
            });
        } else {
            clearInterval(this.state.securitylog_cont_refresh);
            this.setState({
                securityRefreshing: e.target.checked,
            });
        }
    }

    handleSecurityChange(e) {
        const value = e.target.value;
        this.setState(() => (
            {
                securityLines: value
            }
        ), this.handleRefreshSecurityLog);
    }

    handleTextWrappedChange(_event, checked) {
        this.setState({
            isTextWrapped: checked
        });
    }

    handleRefreshSecurityLog () {
        this.setState({
            securityReloading: true
        });

        // Use different command when "no-limit" is selected
        let cmd;
        if (this.state.securityLines === "no-limit") {
            cmd = ["cat", this.props.logLocation];
        } else {
            cmd = ["tail", "-" + this.state.securityLines, this.props.logLocation];
        }

        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        securitylogData: content,
                        securityReloading: false
                    }));
                })
                .fail((err_msg) => {
                    cockpit.error(err_msg);
                    this.setState({
                        securityReloading: false,
                    });
                });
    }

    render() {
        let spinner = null;
        if (this.state.securityReloading) {
            spinner = (
                <ToolbarItem>
                    <Spinner size="sm" />
                    {_("Reloading security log...")}
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
                            id="securityLines"
                            value={this.state.securityLines}
                            onChange={(event) => {
                                this.handleSecurityChange(event);
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
                            id="securityRefreshing"
                            isChecked={this.state.securityRefreshing}
                            onChange={(e) => { this.securityRefreshCont(e) }}
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
            <div id="monitor-log-security-page">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Security Log")}
                        <Button
                            variant="plain"
                            aria-label={_("Refresh security log")}
                            onClick={this.handleRefreshSecurityLog}
                        >
                            <SyncAltIcon />
                        </Button>
                    </Text>
                </TextContent>
                <div className="ds-margin-top-lg">
                    <LogViewer
                        data={this.state.securitylogData}
                        isTextWrapped={this.state.isTextWrapped}
                        toolbar={logViewerToolbar}
                        scrollToRow={this.state.securitylogData.split('\n').length}
                        height="600px"
                    />
                </div>
            </div>
        );
    }
}

// Props and defaultProps

SecurityLogMonitor.propTypes = {
    logLocation: PropTypes.string,
    enableTree: PropTypes.func,
};

SecurityLogMonitor.defaultProps = {
    logLocation: "",
};

export default SecurityLogMonitor;
