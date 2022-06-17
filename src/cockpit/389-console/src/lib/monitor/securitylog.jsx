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

export class SecurityLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            securitylogData: "",
            securityReloading: false,
            securitylog_cont_refresh: "",
            securityRefreshing: false,
            securityLines: "50",
        };

        this.refreshSecurityLog = this.refreshSecurityLog.bind(this);
        this.handleSecurityChange = this.handleSecurityChange.bind(this);
        this.securityRefreshCont = this.securityRefreshCont.bind(this);
    }

    componentDidUpdate () {
        // Set the textarea to be scrolled down to the bottom
        const textarea = document.getElementById('securitylog-area');
        textarea.scrollTop = textarea.scrollHeight;
    }

    componentDidMount() {
        this.props.enableTree();
        this.refreshSecurityLog();
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.securitylog_cont_refresh);
    }

    securityRefreshCont(e) {
        if (e.target.checked) {
            this.state.securitylog_cont_refresh = setInterval(this.refreshSecurityLog, 2000);
        } else {
            clearInterval(this.state.securitylog_cont_refresh);
        }
        this.setState({
            securityRefreshing: e.target.checked,
        });
    }

    handleSecurityChange(e) {
        const value = e.target.value;
        this.setState(() => (
            {
                securityLines: value
            }
        ), this.refreshSecurityLog);
    }

    refreshSecurityLog () {
        this.setState({
            securityReloading: true
        });
        const cmd = ["tail", "-" + this.state.securityLines, this.props.logLocation];
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.setState(() => ({
                        securitylogData: content,
                        securityReloading: false
                    }));
                })
                .fail(() => {
                    // Notification of failure (could only be server down)
                    this.setState({
                        securityReloading: false,
                    });
                });
    }

    render() {
        let spinner = "";
        if (this.state.securityReloading) {
            spinner =
                <div>
                    <Spinner isSVG size="sm" />
                    Reloading security log...
                </div>;
        }

        return (
            <div id="monitor-log-security-page">
                <Grid>
                    <GridItem span={3}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Security Log <FontAwesomeIcon
                                    size="lg"
                                    className="ds-left-margin ds-refresh"
                                    icon={faSyncAlt}
                                    title="Refresh security log"
                                    onClick={this.refreshSecurityLog}
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
                            id="securityLines"
                            value={this.state.securityLines}
                            onChange={(value, event) => {
                                this.handleSecurityChange(event);
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
                                id="securityRefreshing"
                                isChecked={this.state.securityRefreshing}
                                onChange={(checked, e) => { this.securityRefreshCont(e) }}
                                label="Continuously Refresh"
                            />
                        </div>
                    </GridItem>
                    <TextArea
                        id="securitylog-area"
                        resizeOrientation="vertical"
                        className="ds-logarea ds-margin-top-lg"
                        value={this.state.securitylogData}
                        aria-label="text area example"
                    />
                </Grid>
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
