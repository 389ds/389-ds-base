import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import {
    Button,
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
import { SyncAltIcon } from '@patternfly/react-icons';

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
        };

        this.handleRefreshAuditFailLog = this.handleRefreshAuditFailLog.bind(this);
        this.handleAuditFailChange = this.handleAuditFailChange.bind(this);
        this.auditFailRefreshCont = this.auditFailRefreshCont.bind(this);
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.auditfaillog_cont_refresh);
    }

    componentDidUpdate () {
        // Set the textarea to be scrolled down to the bottom
        const textarea = document.getElementById('auditfaillog-area');
        textarea.scrollTop = textarea.scrollHeight;
    }

    componentDidMount() {
        this.props.enableTree();
        this.handleRefreshAuditFailLog();
    }

    handleRefreshAuditFailLog () {
        this.setState({
            auditfailReloading: true
        });
        const cmd = ["tail", "-" + this.state.auditfailLines, this.props.logLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        auditfaillogData: content,
                        auditfailReloading: false
                    }));
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

    render() {
        let spinner = "";
        if (this.state.auditfailReloading) {
            spinner = (
                <div>
                    <Spinner size="sm" />
                    {_("Reloading audit failure log...")}
                </div>
            );
        }

        return (
            <div id="monitor-log-auditfail-page">
                <Grid>
                    <GridItem span={3}>
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
                    </GridItem>
                    <GridItem span={9} className="ds-float-left">
                        {spinner}
                    </GridItem>
                </Grid>

                <Grid className="ds-margin-top-lg ds-indent">
                    <GridItem span={2}>
                        <FormSelect
                            id="auditfailLines"
                            value={this.state.auditfailLines}
                            onChange={(event, value) => {
                                this.handleAuditFailChange(event);
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
                    <GridItem span={10}>
                        <div className="ds-float-right">
                            <Checkbox
                                id="auditfailRefreshing"
                                isChecked={this.state.auditfailRefreshing}
                                onChange={(e, checked) => { this.auditFailRefreshCont(e) }}
                                label={_("Continuously Refresh")}
                            />
                        </div>
                    </GridItem>
                    <TextArea
                        id="auditfaillog-area"
                        resizeOrientation="vertical"
                        className="ds-logarea ds-margin-top-lg"
                        value={this.state.auditfaillogData}
                        aria-label="text area example"
                    />
                </Grid>
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
