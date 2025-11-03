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

export class AuditFailLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            auditfaillogData: "",
            auditfailReloading: false,
            auditfaillog_cont_refresh: "",
            auditfailRefreshing: false,
            auditfailLines: "50",
            isTextWrapped: false,
        };

        this.handleRefreshAuditFailLog = this.handleRefreshAuditFailLog.bind(this);
        this.handleAuditFailChange = this.handleAuditFailChange.bind(this);
        this.auditFailRefreshCont = this.auditFailRefreshCont.bind(this);
        this.handleTextWrappedChange = this.handleTextWrappedChange.bind(this);
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.auditfaillog_cont_refresh);
    }

    componentDidMount() {
        this.props.enableTree();
        this.handleRefreshAuditFailLog();
    }

    handleRefreshAuditFailLog () {
        this.setState({
            auditfailReloading: true
        });

        // Use different command when "no-limit" is selected
        let cmd;
        if (this.state.auditfailLines === "no-limit") {
            cmd = ["cat", this.props.logLocation];
        } else {
            cmd = ["tail", "-" + this.state.auditfailLines, this.props.logLocation];
        }

        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        auditfaillogData: content,
                        auditfailReloading: false
                    }));
                })
                .fail((err_msg) => {
                    cockpit.error(err_msg);
                    this.setState({ auditfailReloading: false });
                });
    }

    auditFailRefreshCont(e) {
        if (e.target.checked) {
            this.setState({
                auditfaillog_cont_refresh: setInterval(this.handleRefreshAuditFailLog, 2000),
                auditfailRefreshing: e.target.checked,
            });
        } else {
            clearInterval(this.state.auditfaillog_cont_refresh);
            this.setState({
                auditfailRefreshing: e.target.checked,
            });
        }
    }

    handleAuditFailChange(e) {
        const value = e.target.value;
        this.setState(() => (
            {
                auditfailLines: value
            }
        ), this.handleRefreshAuditFailLog);
    }

    handleTextWrappedChange(_event, checked) {
        this.setState({
            isTextWrapped: checked
        });
    }

    render() {
        let spinner = null;
        if (this.state.auditfailReloading) {
            spinner = (
                <ToolbarItem>
                    <Spinner size="sm" />
                    {_("Reloading audit failure log...")}
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
                            id="auditfailLines"
                            value={this.state.auditfailLines}
                            onChange={(event) => {
                                this.handleAuditFailChange(event);
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
                            id="auditfailRefreshing"
                            isChecked={this.state.auditfailRefreshing}
                            onChange={(e) => { this.auditFailRefreshCont(e) }}
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
            <div id="monitor-log-auditfail-page">
                <TextContent>
                    <Text component={TextVariants.h3}>
                        {_("Audit Failure Log")}
                        <Button
                            variant="plain"
                            aria-label={_("Refresh audit failure log")}
                            onClick={this.handleRefreshAuditFailLog}
                        >
                            <SyncAltIcon />
                        </Button>
                    </Text>
                </TextContent>
                <div className="ds-margin-top-lg">
                    <LogViewer
                        data={this.state.auditfaillogData}
                        isTextWrapped={this.state.isTextWrapped}
                        toolbar={logViewerToolbar}
                        scrollToRow={this.state.auditfaillogData.split('\n').length}
                        height="600px"
                    />
                </div>
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
};

export default AuditFailLogMonitor;
