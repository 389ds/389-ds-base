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

export class AuditLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            auditlogData: "",
            auditReloading: false,
            auditlog_cont_refresh: "",
            auditRefreshing: false,
            auditLines: "50",
            isTextWrapped: false,
        };
        this.handleRefreshAuditLog = this.handleRefreshAuditLog.bind(this);
        this.handleAuditChange = this.handleAuditChange.bind(this);
        this.auditRefreshCont = this.auditRefreshCont.bind(this);
        this.handleTextWrappedChange = this.handleTextWrappedChange.bind(this);
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.auditlog_cont_refresh);
    }

    componentDidMount() {
        this.props.enableTree();
        this.handleRefreshAuditLog();
    }

    handleRefreshAuditLog () {
        this.setState({
            auditReloading: true
        });
        
        // Use different command when "no-limit" is selected
        let cmd;
        if (this.state.auditLines === "no-limit") {
            cmd = ["cat", this.props.logLocation];
        } else {
            cmd = ["tail", "-" + this.state.auditLines, this.props.logLocation];
        }
        
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
        if (e.target.checked) {
            this.setState({
                auditlog_cont_refresh: setInterval(this.handleRefreshAuditLog, 2000),
                auditRefreshing: e.target.checked,
            });
        } else {
            clearInterval(this.state.auditlog_cont_refresh);
            this.setState({
                auditRefreshing: e.target.checked,
            });
        }
    }

    handleAuditChange(e) {
        const value = e.target.value;
        this.setState(() => (
            {
                auditLines: value
            }
        ), this.handleRefreshAuditLog);
    }

    handleTextWrappedChange(_event, checked) {
        this.setState({
            isTextWrapped: checked
        });
    }

    render() {
        let spinner = null;
        if (this.state.auditReloading) {
            spinner = (
                <ToolbarItem>
                    <Spinner size="sm" />
                    {_("Reloading audit log...")}
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
                            id="auditLines"
                            value={this.state.auditLines}
                            onChange={(event) => {
                                this.handleAuditChange(event);
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
                            id="auditRefreshing"
                            isChecked={this.state.auditRefreshing}
                            onChange={(e) => { this.auditRefreshCont(e) }}
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
            <div id="monitor-log-audit-page">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Audit Log")}
                        <Button 
                            variant="plain"
                            aria-label={_("Refresh audit log")}
                            onClick={this.handleRefreshAuditLog}
                        >
                            <SyncAltIcon />
                        </Button>
                    </Text>
                </TextContent>
                <div className="ds-margin-top-lg">
                    <LogViewer
                        data={this.state.auditlogData}
                        isTextWrapped={this.state.isTextWrapped}
                        toolbar={logViewerToolbar}
                        scrollToRow={this.state.auditlogData.split('\n').length}
                        height="600px"
                    />
                </div>
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
};

export default AuditLogMonitor;
