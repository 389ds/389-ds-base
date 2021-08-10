import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    ExpandableSection,
    Form,
    Grid,
    GridItem,
    Spinner,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions,
    noop
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';

const tuning_attrs = [
    'nsslapd-ndn-cache-enabled',
    'nsslapd-ignore-virtual-attrs',
    'nsslapd-connection-nocanon',
    'nsslapd-enable-turbo-mode',
    'nsslapd-threadnumber',
    'nsslapd-maxdescriptors',
    'nsslapd-timelimit',
    'nsslapd-sizelimit',
    'nsslapd-pagedsizelimit',
    'nsslapd-idletimeout',
    'nsslapd-ioblocktimeout',
    'nsslapd-outbound-ldap-io-timeout',
    'nsslapd-maxbersize',
    'nsslapd-maxsasliosize',
    'nsslapd-listen-backlog-size',
    'nsslapd-max-filter-nest-level',
    'nsslapd-ndn-cache-max-size',
];

export class ServerTuning extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            loading: false,
            loaded: false,
            activeKey: 1,
            saveDisabled: true,
            errObj: {},
            attrs: this.props.attrs,
            isExpanded: false,
        };

        this.onToggle = (isExpanded) => {
            this.setState({
                isExpanded
            });
        };

        this.handleChange = this.handleChange.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
    }

    componentDidMount() {
        if (!this.state.loaded) {
            this.loadConfig();
        } else {
            this.props.enableTree();
        }
    }

    handleChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;
        let valueErr = false;
        let errObj = this.state.errObj;

        // Check if a setting was changed, if so enable the save button
        for (let tuning_attr of tuning_attrs) {
            if (attr == tuning_attr && this.state['_' + tuning_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let tuning_attr of tuning_attrs) {
            if (attr != tuning_attr && this.state['_' + tuning_attr] != this.state[tuning_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        if (value == "" && e.target.type !== 'checkbox') {
            valueErr = true;
            disableSaveBtn = true;
        }
        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            saveDisabled: disableSaveBtn,
            errObj: errObj,
        });
    }

    loadConfig(reloading) {
        if (reloading) {
            this.setState({
                loaded: false
            });
        }

        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("loadConfig", "Load server tuning configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    let ndnEnabled = false;
                    let ignoreVirtAttrs = false;
                    let connNoCannon = false;
                    let turboMode = false;

                    if (attrs['nsslapd-ndn-cache-enabled'][0] == "on") {
                        ndnEnabled = true;
                    }
                    if (attrs['nsslapd-ignore-virtual-attrs'][0] == "on") {
                        ignoreVirtAttrs = true;
                    }
                    if (attrs['nsslapd-connection-nocanon'][0] == "on") {
                        connNoCannon = true;
                    }
                    if (attrs['nsslapd-enable-turbo-mode'][0] == "on") {
                        turboMode = true;
                    }
                    this.setState({
                        loaded: true,
                        loading: false,
                        saveDisabled: true,
                        // Settings
                        'nsslapd-ndn-cache-enabled': ndnEnabled,
                        'nsslapd-ignore-virtual-attrs': ignoreVirtAttrs,
                        'nsslapd-connection-nocanon': connNoCannon,
                        'nsslapd-enable-turbo-mode': turboMode,
                        'nsslapd-threadnumber': attrs['nsslapd-threadnumber'][0],
                        'nsslapd-maxdescriptors': attrs['nsslapd-maxdescriptors'][0],
                        'nsslapd-timelimit': attrs['nsslapd-timelimit'][0],
                        'nsslapd-sizelimit': attrs['nsslapd-sizelimit'][0],
                        'nsslapd-pagedsizelimit': attrs['nsslapd-pagedsizelimit'][0],
                        'nsslapd-idletimeout': attrs['nsslapd-idletimeout'][0],
                        'nsslapd-ioblocktimeout': attrs['nsslapd-ioblocktimeout'][0],
                        'nsslapd-outbound-ldap-io-timeout': attrs['nsslapd-outbound-ldap-io-timeout'][0],
                        'nsslapd-maxbersize': attrs['nsslapd-maxbersize'][0],
                        'nsslapd-maxsasliosize': attrs['nsslapd-maxsasliosize'][0],
                        'nsslapd-listen-backlog-size': attrs['nsslapd-listen-backlog-size'][0],
                        'nsslapd-max-filter-nest-level': attrs['nsslapd-max-filter-nest-level'][0],
                        'nsslapd-ndn-cache-max-size': attrs['nsslapd-ndn-cache-max-size'][0],
                        // Record original values
                        '_nsslapd-ndn-cache-enabled': ndnEnabled,
                        '_nsslapd-ignore-virtual-attrs': ignoreVirtAttrs,
                        '_nsslapd-connection-nocanon': connNoCannon,
                        '_nsslapd-enable-turbo-mode': turboMode,
                        '_nsslapd-threadnumber': attrs['nsslapd-threadnumber'][0],
                        '_nsslapd-maxdescriptors': attrs['nsslapd-maxdescriptors'][0],
                        '_nsslapd-timelimit': attrs['nsslapd-timelimit'][0],
                        '_nsslapd-sizelimit': attrs['nsslapd-sizelimit'][0],
                        '_nsslapd-pagedsizelimit': attrs['nsslapd-pagedsizelimit'][0],
                        '_nsslapd-idletimeout': attrs['nsslapd-idletimeout'][0],
                        '_nsslapd-ioblocktimeout': attrs['nsslapd-ioblocktimeout'][0],
                        '_nsslapd-outbound-ldap-io-timeout': attrs['nsslapd-outbound-ldap-io-timeout'][0],
                        '_nsslapd-maxbersize': attrs['nsslapd-maxbersize'][0],
                        '_nsslapd-maxsasliosize': attrs['nsslapd-maxsasliosize'][0],
                        '_nsslapd-listen-backlog-size': attrs['nsslapd-listen-backlog-size'][0],
                        '_nsslapd-max-filter-nest-level': attrs['nsslapd-max-filter-nest-level'][0],
                        '_nsslapd-ndn-cache-max-size': attrs['nsslapd-ndn-cache-max-size'][0],
                    }, this.props.enableTree());
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.setState({
                        loaded: true
                    });
                    this.props.addNotification(
                        "error",
                        `Error loading server configuration - ${errMsg.desc}`
                    );
                });
    }

    saveConfig() {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];

        this.setState({
            loading: true
        });

        for (let attr of tuning_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                let val = this.state[attr];
                if (typeof val === "boolean") {
                    if (val) {
                        val = "on";
                    } else {
                        val = "off";
                    }
                }
                cmd.push(attr + "=" + val);
            }
        }

        log_cmd("saveConfig", "Saving Tuning configuration", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadConfig();
                    this.props.addNotification(
                        "success",
                        "Successfully updated tuning configuration"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.loadConfig();
                    this.props.addNotification(
                        "error",
                        `Error updating tuning configuration - ${errMsg.desc}`
                    );
                });
    }

    render () {
        let body = "";
        let saveBtnName = "Save Settings";
        let extraPrimaryProps = {};
        if (this.state.loading) {
            saveBtnName = "Saving settings ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        if (!this.state.loaded) {
            body =
                <div className="ds-loading-spinner ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>Loading Tuning Configuration ...</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="lg" />
                </div>;
        } else {
            body =
                <div className={this.state.loading ? "ds-disabled" : ""}>
                    <Grid>
                        <GridItem span={3}>
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Tuning & Limits <FontAwesomeIcon
                                        size="lg"
                                        className="ds-left-margin ds-refresh"
                                        icon={faSyncAlt}
                                        title="Refresh settings"
                                        onClick={() => {
                                            this.loadConfig(1);
                                        }}
                                    />
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>
                    <Form className="ds-left-margin" isHorizontal>
                        <Grid
                            className="ds-margin-top-xlg"
                            title="The number of worker threads that handle database operations (nsslapd-threadnumber)."
                        >
                            <GridItem className="ds-label" span={3}>
                                Number Of Worker Threads
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state['nsslapd-threadnumber']}
                                    type="number"
                                    id="nsslapd-threadnumber"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="threadnumber"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    validated={this.state.errObj['nsslapd-threadnumber'] ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The maximum number of seconds allocated for a search request (nsslapd-timelimit)."
                        >
                            <GridItem className="ds-label" span={3}>
                                Search Time Limit
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state['nsslapd-timelimit']}
                                    type="number"
                                    id="nsslapd-timelimit"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="timelimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    validated={this.state.errObj['nsslapd-timelimit'] ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The maximum number of entries to return from a search operation (nsslapd-sizelimit)."
                        >
                            <GridItem className="ds-label" span={3}>
                                Search Size Limit
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state['nsslapd-sizelimit']}
                                    type="number"
                                    id="nsslapd-sizelimit"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="sizelimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    validated={this.state.errObj['nsslapd-sizelimit'] ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The maximum number of entries to return from a paged search operation (nsslapd-pagedsizelimit)."
                        >
                            <GridItem className="ds-label" span={3}>
                                Paged Search Size Limit
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state['nsslapd-sizelimit']}
                                    type="number"
                                    id="nsslapd-pagedsizelimit"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="pagedsizelimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    validated={this.state.errObj['nsslapd-pagedsizelimit'] ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="Sets the amount of time in seconds after which an idle LDAP client connection is closed by the server (nsslapd-idletimeout)."
                        >
                            <GridItem className="ds-label" span={3}>
                                Idle Connection Timeout
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state['nsslapd-idletimeout']}
                                    type="number"
                                    id="nsslapd-idletimeout"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="idletimeout"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    validated={this.state.errObj['nsslapd-idletimeout'] ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="Sets the amount of time in milliseconds after which the connection to a stalled LDAP client is closed (nsslapd-ioblocktimeout)."
                        >
                            <GridItem className="ds-label" span={3}>
                                I/O Block Timeout
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state['nsslapd-ioblocktimeout']}
                                    type="number"
                                    id="nsslapd-ioblocktimeout"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="ioblocktimeout"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    validated={this.state.errObj['nsslapd-ioblocktimeout'] ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                    <ExpandableSection
                        className="ds-margin-top-xlg"
                        toggleText={this.state.isExpanded ? 'Hide Advanced Settings' : 'Show Advanced Settings'}
                        onToggle={this.onToggle}
                        isExpanded={this.state.isExpanded}
                    >
                        <div className="ds-margin-top ds-indent">
                            <Form isHorizontal>
                                <Grid
                                    className="ds-margin-top"
                                    title="Sets the I/O wait time for all outbound LDAP connections (nsslapd-outbound-ldap-io-timeout)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Outbound IO Timeout
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state['nsslapd-outbound-ldap-io-timeout']}
                                            type="number"
                                            id="nsslapd-outbound-ldap-io-timeout"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="outbound-ldap-io-timeout"
                                            onChange={(str, e) => {
                                                this.handleChange(e);
                                            }}
                                            validated={this.state.errObj['nsslapd-outbound-ldap-io-timeout'] ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="The maximum size in bytes allowed for an incoming message (nsslapd-maxbersize)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Maximum BER Size
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state['nsslapd-maxbersize']}
                                            type="number"
                                            id="nsslapd-maxbersize"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="maxbersize"
                                            onChange={(str, e) => {
                                                this.handleChange(e);
                                            }}
                                            validated={this.state.errObj['nsslapd-maxbersize'] ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="The maximum allowed SASL IO packet size that the server will accept (nsslapd-maxsasliosize)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Maximum SASL IO Size
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state['nsslapd-maxsasliosize']}
                                            type="number"
                                            id="nsslapd-maxsasliosize"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="maxsasliosize"
                                            onChange={(str, e) => {
                                                this.handleChange(e);
                                            }}
                                            validated={this.state.errObj['nsslapd-maxsasliosize'] ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="The maximum length for how long the connection queue for the socket can grow before refusing connections (nsslapd-listen-backlog-size)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Listen Backlog Size
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state['nsslapd-listen-backlog-size']}
                                            type="number"
                                            id="nsslapd-listen-backlog-size"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="listen-backlog-size"
                                            onChange={(str, e) => {
                                                this.handleChange(e);
                                            }}
                                            validated={this.state.errObj['nsslapd-listen-backlog-size'] ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="Sets how deep a nested search filter is analysed (nsslapd-max-filter-nest-level)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Maximum Nested Filter Level
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state['nsslapd-max-filter-nest-level']}
                                            type="number"
                                            id="nsslapd-max-filter-nest-level"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="max-filter-nest-level"
                                            onChange={(str, e) => {
                                                this.handleChange(e);
                                            }}
                                            validated={this.state.errObj['nsslapd-max-filter-nest-level'] ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="Disable DNS reverse entries for outgoing connections (nsslapd-connection-nocanon)."
                                >
                                    <GridItem className="ds-label" span={4}>
                                        <Checkbox
                                            id="nsslapd-connection-nocanon"
                                            isChecked={this.state['nsslapd-connection-nocanon']}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            label="Disable Reverse DNS Lookups"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="Sets the worker threads to continuously read a connection without passing it back to the polling mechanism. (nsslapd-enable-turbo-mode)."
                                >
                                    <GridItem className="ds-label" span={4}>
                                        <Checkbox
                                            id="nsslapd-enable-turbo-mode"
                                            isChecked={this.state['nsslapd-enable-turbo-mode']}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            label="Enable Connection Turbo Mode"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="Disable the virtual attribute lookup in a search entry (nsslapd-ignore-virtual-attrs)."
                                >
                                    <GridItem className="ds-label" span={4}>
                                        <Checkbox
                                            id="nsslapd-ignore-virtual-attrs"
                                            isChecked={this.state['nsslapd-ignore-virtual-attrs']}
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            label="Disable Virtual Attribute Lookups"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="Enable the normalized DN cache.  Each thread has its own cache (nsslapd-ndn-cache-enabled)."
                                >
                                    <GridItem className="ds-label" span={4}>
                                        <Checkbox
                                            isChecked={this.state['nsslapd-ndn-cache-enabled']}
                                            id="nsslapd-ndn-cache-enabled"
                                            onChange={(checked, e) => {
                                                this.handleChange(e);
                                            }}
                                            label="Enable Normalized DN Cache"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="The max NDN cache size in bytes (nsslapd-ndn-cache-max-size)."
                                    className="ds-left-indent-lg"
                                >
                                    <GridItem className="ds-label" span={2}>
                                        NDN Max Cache Size
                                    </GridItem>
                                    <GridItem span={2}>
                                        <TextInput
                                            isDisabled={!this.state['nsslapd-ndn-cache-enabled']}
                                            value={this.state['nsslapd-ndn-cache-max-size']}
                                            type="number"
                                            id="nsslapd-ndn-cache-max-size"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="ndn-cache-max-size"
                                            onChange={(str, e) => {
                                                this.handleChange(e);
                                            }}
                                            validated={this.state.errObj['nsslapd-ndn-cache-max-size'] ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                        </div>
                    </ExpandableSection>
                    <Button
                        isDisabled={this.state.saveDisabled}
                        variant="primary"
                        className="ds-margin-top-xlg"
                        onClick={this.saveConfig}
                        isLoading={this.state.loading}
                        spinnerAriaValueText={this.state.loading ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>
                    <hr />
                </div>;
        }

        return (
            <div id="tuning-content">
                {body}
            </div>
        );
    }
}

// Property types and defaults

ServerTuning.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerTuning.defaultProps = {
    addNotification: noop,
    serverId: "",
    attrs: {},
};
