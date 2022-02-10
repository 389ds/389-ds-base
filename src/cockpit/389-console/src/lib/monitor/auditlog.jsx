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

export class AuditLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            auditlogData: "",
            auditReloading: false,
            auditlog_cont_refresh: "",
            auditRefreshing: false,
            auditLines: "50",
        };
        this.refreshAuditLog = this.refreshAuditLog.bind(this);
        this.handleAuditChange = this.handleAuditChange.bind(this);
        this.auditRefreshCont = this.auditRefreshCont.bind(this);
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.auditlog_cont_refresh);
    }

    componentDidUpdate () {
        // Set the textarea to be scrolled down to the bottom
        const textarea = document.getElementById('auditlog-area');
        textarea.scrollTop = textarea.scrollHeight;
    }

    componentDidMount() {
        this.props.enableTree();
        this.refreshAuditLog();
    }

    refreshAuditLog () {
        this.setState({
            auditReloading: true
        });
        const cmd = ["tail", "-" + this.state.auditLines, this.props.logLocation];
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
            this.state.auditlog_cont_refresh = setInterval(this.refreshAuditLog, 2000);
        } else {
            clearInterval(this.state.auditlog_cont_refresh);
        }
        this.setState({
            auditRefreshing: e.target.checked,
        });
    }

    handleAuditChange(e) {
        const value = e.target.value;
        this.setState(() => (
            {
                auditLines: value
            }
        ), this.refreshAuditLog);
    }

    render() {
        let spinner = "";
        if (this.state.auditReloading) {
            spinner =
                <div>
                    <Spinner size="sm" />
                    Reloading audit log...
                </div>;
        }

        return (
            <div id="monitor-log-audit-page">
                <Grid>
                    <GridItem span={3}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Audit Log <FontAwesomeIcon
                                    size="lg"
                                    className="ds-left-margin ds-refresh"
                                    icon={faSyncAlt}
                                    title="Refresh audit log"
                                    onClick={this.refreshAuditLog}
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
                            id="accessLines"
                            value={this.state.auditLines}
                            onChange={(value, event) => {
                                this.handleAuditChange(event);
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
                                id="auditRefreshing"
                                isChecked={this.state.auditRefreshing}
                                onChange={(checked, e) => { this.auditRefreshCont(e) }}
                                label="Continuously Refresh"
                            />
                        </div>
                    </GridItem>
                    <TextArea
                        id="auditlog-area"
                        resizeOrientation="vertical"
                        className="ds-logarea ds-margin-top-lg"
                        value={this.state.auditlogData}
                        aria-label="text area example"
                    />
                </Grid>
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
