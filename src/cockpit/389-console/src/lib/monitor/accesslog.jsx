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

export class AccessLogMonitor extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            accesslogData: "",
            accessReloading: false,
            accesslog_cont_refresh: "",
            accessRefreshing: false,
            accessLines: "50",
        };

        this.refreshAccessLog = this.refreshAccessLog.bind(this);
        this.handleAccessChange = this.handleAccessChange.bind(this);
        this.accessRefreshCont = this.accessRefreshCont.bind(this);
    }

    componentDidUpdate () {
        // Set the textarea to be scrolled down to the bottom
        const textarea = document.getElementById('accesslog-area');
        textarea.scrollTop = textarea.scrollHeight;
    }

    componentDidMount() {
        this.props.enableTree();
        this.refreshAccessLog();
    }

    componentWillUnmount() {
        // Stop the continuous log refresh interval
        clearInterval(this.state.accesslog_cont_refresh);
    }

    accessRefreshCont(e) {
        if (e.target.checked) {
            this.state.accesslog_cont_refresh = setInterval(this.refreshAccessLog, 2000);
        } else {
            clearInterval(this.state.accesslog_cont_refresh);
        }
        this.setState({
            accessRefreshing: e.target.checked,
        });
    }

    handleAccessChange(e) {
        const value = e.target.value;
        this.setState(() => (
            {
                accessLines: value
            }
        ), this.refreshAccessLog);
    }

    refreshAccessLog () {
        this.setState({
            accessReloading: true
        });
        const cmd = ["tail", "-" + this.state.accessLines, this.props.logLocation];
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
        let spinner = "";
        if (this.state.accessReloading) {
            spinner =
                <div>
                    <Spinner isSVG size="sm" />
                    Reloading access log...
                </div>;
        }

        return (
            <div id="monitor-log-access-page">
                <Grid>
                    <GridItem span={3}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Access Log <FontAwesomeIcon
                                    size="lg"
                                    className="ds-left-margin ds-refresh"
                                    icon={faSyncAlt}
                                    title="Refresh access log"
                                    onClick={this.refreshAccessLog}
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
                            value={this.state.accessLines}
                            onChange={(value, event) => {
                                this.handleAccessChange(event);
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
                                id="accessRefreshing"
                                isChecked={this.state.accessRefreshing}
                                onChange={(checked, e) => { this.accessRefreshCont(e) }}
                                label="Continuously Refresh"
                            />
                        </div>
                    </GridItem>
                    <TextArea
                        id="accesslog-area"
                        resizeOrientation="vertical"
                        className="ds-logarea ds-margin-top-lg"
                        value={this.state.accesslogData}
                        aria-label="text area example"
                    />
                </Grid>
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
