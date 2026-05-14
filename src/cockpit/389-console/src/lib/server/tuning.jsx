import cockpit from "cockpit";
import React from "react";
import { log_cmd, getApiErrorMessage } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Divider,
    Form,
    Grid,
    GridItem,
    NumberInput,
    Spinner,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import { SyncAltIcon } from '@patternfly/react-icons';

const tuning_attrs = [
    'nsslapd-ndn-cache-enabled',
    'nsslapd-ignore-virtual-attrs',
    'nsslapd-connection-nocanon',
    'nsslapd-enable-turbo-mode',
    'nsslapd-threadnumber',
    'nsslapd-maxthreadsperconn',
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
    'nsslapd-maxcontrolsperop',
    'nsslapd-tcp-fin-timeout',
    'nsslapd-tcp-keepalive-time',
    'nsslapd-maxsimplepaged-per-conn'
];

const _ = cockpit.gettext;

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
        };

        this.maxValue = 2000000000;
        this.onMinusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validateSaveBtn() });
        };
        this.onConfigChange = (event, id, min, max) => {
            let maxValue = this.maxValue;
            if (max !== 0) {
                maxValue = max;
            }
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > maxValue ? maxValue : newValue < min ? min : newValue
            }, () => { this.validateSaveBtn() });
        };
        this.onPlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validateSaveBtn() });
        };

        this.validateSaveBtn = this.validateSaveBtn.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.handleSaveConfig = this.handleSaveConfig.bind(this);
    }

    componentDidMount() {
        if (!this.state.loaded) {
            this.loadConfig();
        } else {
            this.props.enableTree();
        }
    }

    validateSaveBtn() {
        let saveBtnDisabled = true;
        // Check if a setting was changed, if so enable the save button
        for (const config_attr of tuning_attrs) {
            if (this.state[config_attr].toString() !== this.state['_' + config_attr].toString()) {
                saveBtnDisabled = false;
                break;
            }
        }
        this.setState({
            saveDisabled: saveBtnDisabled,
        });
    }

    handleChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;

        this.setState({
            [attr]: value,
        }, () => { this.validateSaveBtn() });
    }

    loadConfig(reloading) {
        if (reloading) {
            this.setState({
                loaded: false
            });
        }

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("loadConfig", "Load server tuning configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    let ndnEnabled = false;
                    let ignoreVirtAttrs = false;
                    let connNoCannon = false;
                    let turboMode = false;

                    if (attrs['nsslapd-ndn-cache-enabled'][0] === "on") {
                        ndnEnabled = true;
                    }
                    if (attrs['nsslapd-ignore-virtual-attrs'][0] === "on") {
                        ignoreVirtAttrs = true;
                    }
                    if (attrs['nsslapd-connection-nocanon'][0] === "on") {
                        connNoCannon = true;
                    }
                    if (attrs['nsslapd-enable-turbo-mode'][0] === "on") {
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
                        'nsslapd-threadnumber': parseInt(attrs['nsslapd-threadnumber'][0]),
                        'nsslapd-maxthreadsperconn': parseInt(attrs['nsslapd-maxthreadsperconn'][0]),
                        'nsslapd-maxdescriptors': parseInt(attrs['nsslapd-maxdescriptors'][0]),
                        'nsslapd-timelimit': parseInt(attrs['nsslapd-timelimit'][0]),
                        'nsslapd-sizelimit': parseInt(attrs['nsslapd-sizelimit'][0]),
                        'nsslapd-pagedsizelimit': parseInt(attrs['nsslapd-pagedsizelimit'][0]),
                        'nsslapd-idletimeout': parseInt(attrs['nsslapd-idletimeout'][0]),
                        'nsslapd-ioblocktimeout': parseInt(attrs['nsslapd-ioblocktimeout'][0]),
                        'nsslapd-outbound-ldap-io-timeout': parseInt(attrs['nsslapd-outbound-ldap-io-timeout'][0]),
                        'nsslapd-maxbersize': parseInt(attrs['nsslapd-maxbersize'][0]),
                        'nsslapd-maxsasliosize': parseInt(attrs['nsslapd-maxsasliosize'][0]),
                        'nsslapd-listen-backlog-size': parseInt(attrs['nsslapd-listen-backlog-size'][0]),
                        'nsslapd-max-filter-nest-level': parseInt(attrs['nsslapd-max-filter-nest-level'][0]),
                        'nsslapd-maxcontrolsperop': parseInt(attrs['nsslapd-maxcontrolsperop'][0]),
                        'nsslapd-tcp-fin-timeout': parseInt(attrs['nsslapd-tcp-fin-timeout'][0]),
                        'nsslapd-tcp-keepalive-time': parseInt(attrs['nsslapd-tcp-keepalive-time'][0]),
                        'nsslapd-maxsimplepaged-per-conn': parseInt(attrs['nsslapd-maxsimplepaged-per-conn'][0]),
                        // Record original values
                        '_nsslapd-ndn-cache-enabled': ndnEnabled,
                        '_nsslapd-ignore-virtual-attrs': ignoreVirtAttrs,
                        '_nsslapd-connection-nocanon': connNoCannon,
                        '_nsslapd-enable-turbo-mode': turboMode,
                        '_nsslapd-threadnumber': parseInt(attrs['nsslapd-threadnumber'][0]),
                        '_nsslapd-maxthreadsperconn': parseInt(attrs['nsslapd-maxthreadsperconn'][0]),
                        '_nsslapd-maxdescriptors': parseInt(attrs['nsslapd-maxdescriptors'][0]),
                        '_nsslapd-timelimit': parseInt(attrs['nsslapd-timelimit'][0]),
                        '_nsslapd-sizelimit': parseInt(attrs['nsslapd-sizelimit'][0]),
                        '_nsslapd-pagedsizelimit': parseInt(attrs['nsslapd-pagedsizelimit'][0]),
                        '_nsslapd-idletimeout': parseInt(attrs['nsslapd-idletimeout'][0]),
                        '_nsslapd-ioblocktimeout': parseInt(attrs['nsslapd-ioblocktimeout'][0]),
                        '_nsslapd-outbound-ldap-io-timeout': parseInt(attrs['nsslapd-outbound-ldap-io-timeout'][0]),
                        '_nsslapd-maxbersize': parseInt(attrs['nsslapd-maxbersize'][0]),
                        '_nsslapd-maxsasliosize': parseInt(attrs['nsslapd-maxsasliosize'][0]),
                        '_nsslapd-listen-backlog-size': parseInt(attrs['nsslapd-listen-backlog-size'][0]),
                        '_nsslapd-max-filter-nest-level': parseInt(attrs['nsslapd-max-filter-nest-level'][0]),
                        '_nsslapd-maxcontrolsperop': parseInt(attrs['nsslapd-maxcontrolsperop'][0]),
                        '_nsslapd-tcp-fin-timeout': parseInt(attrs['nsslapd-tcp-fin-timeout'][0]),
                        '_nsslapd-tcp-keepalive-time': parseInt(attrs['nsslapd-tcp-keepalive-time'][0]),
                        '_nsslapd-maxsimplepaged-per-conn': parseInt(attrs['nsslapd-maxsimplepaged-per-conn'][0]),
                    }, this.props.enableTree());
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.setState({
                        loaded: true
                    });
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading server configuration - $0"), errMsg)
                    );
                });
    }

    handleSaveConfig() {
        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'config', 'replace'
        ];

        this.setState({
            loading: true
        });

        for (const attr of tuning_attrs) {
            if (this.state['_' + attr] !== this.state[attr]) {
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

        log_cmd("handleSaveConfig", "Saving Tuning configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.loadConfig();
                    this.props.addNotification(
                        "success",
                        _("Successfully updated tuning configuration")
                    );
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.loadConfig();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error updating tuning configuration - $0"), errMsg)
                    );
                });
    }

    render () {
        let body = "";
        let saveBtnName = _("Save Settings");
        const extraPrimaryProps = {};
        if (this.state.loading) {
            saveBtnName = _("Saving settings ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        if (!this.state.loaded) {
            body = (
                <div className="ds-loading-spinner ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>{_("Loading Tuning Configuration ...")}</Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="lg" />
                </div>
            );
        } else {
            body = (
                <div className={this.state.loading ? "ds-disabled ds-margin-bottom-md" : "ds-margin-bottom-md"}>
                    <Grid>
                        <GridItem span={12}>
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    {_("Tuning & Limits")}
                                    <Button
                                        variant="plain"
                                        aria-label={_("Refresh settings")}
                                        onClick={() => {
                                            this.loadConfig(1);
                                        }}
                                    >
                                        <SyncAltIcon size="lg" />
                                    </Button>
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>
                    <Form className="ds-margin-left" isHorizontal>
                        <Grid className="ds-margin-top-xlg">
                            <GridItem className="ds-label" span={2} title={_("The number of worker threads that handle database operations.  Set to '-1' for enable auto tuning. (nsslapd-threadnumber).")}>
                                {_("Number Of Worker Threads")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-threadnumber']}
                                    min={-1}
                                    max={512}
                                    onMinus={() => { this.onMinusConfig("nsslapd-threadnumber") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-threadnumber", -1, 512) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-threadnumber") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem
                                className="ds-label"
                                offset={5}
                                span={2}
                                title={_("The maximum number of threads that can handle requests for a single connection (nsslapd-maxthreadsperconn).")}
                            >
                                {_("Maximum Threads Per Connection")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-maxthreadsperconn']}
                                    min={1}
                                    max={65535}
                                    onMinus={() => { this.onMinusConfig("nsslapd-maxthreadsperconn") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-maxthreadsperconn", 0, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-maxthreadsperconn") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The maximum number of seconds allocated for a search request.  Set to '-1' to disable the time limit (nsslapd-timelimit).")}
                        >
                            <GridItem className="ds-label" span={2}>
                                {_("Search Time Limit")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-timelimit']}
                                    min={-1}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-timelimit") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-timelimit", -1, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-timelimit") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem className="ds-label" offset={5} span={2} title={_("The maximum number of entries to return from a search operation.  Set to '-1' to disable the size limit (nsslapd-sizelimit).")}>
                                {_("Search Size Limit")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-sizelimit']}
                                    min={-1}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-sizelimit") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-sizelimit", -1, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-sizelimit") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={2} title={_("The maximum number of entries to return from a paged search operation. Set to '-1' to disable the size limit (nsslapd-pagedsizelimit).")}>
                                {_("Paged Search Size Limit")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-pagedsizelimit']}
                                    min={-1}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-pagedsizelimit") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-pagedsizelimit", -1, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-pagedsizelimit") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem className="ds-label" offset={5} span={2} title={_("Sets the amount of time in seconds after which an idle LDAP client connection is closed by the server (nsslapd-idletimeout).")}>
                                {_("Idle Connection Timeout")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-idletimeout']}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-idletimeout") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-idletimeout", 0, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-idletimeout") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={2} title={_("Sets the amount of time in milliseconds after which the connection to a stalled LDAP client is closed (nsslapd-ioblocktimeout).")}>
                                {_("I/O Block Timeout")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-ioblocktimeout']}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-ioblocktimeout") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-ioblocktimeout", 0, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-ioblocktimeout") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem className="ds-label" offset={5} span={2} title={_("Sets the I/O wait time for all outbound LDAP connections (nsslapd-outbound-ldap-io-timeout).")}>
                                {_("Outbound IO Timeout")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-outbound-ldap-io-timeout']}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-outbound-ldap-io-timeout") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-outbound-ldap-io-timeout", 0, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-outbound-ldap-io-timeout") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={2} title={_("The maximum size in bytes allowed for an incoming message (nsslapd-maxbersize).")}>
                                {_("Maximum BER Size")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-maxbersize']}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-maxbersize") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-maxbersize", 0, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-maxbersize") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem className="ds-label" offset={5} span={2} title={_("The maximum allowed SASL IO packet size that the server will accept (nsslapd-maxsasliosize).")}>
                                {_("Maximum SASL IO Size")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-maxsasliosize']}
                                    min={-1}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-maxsasliosize") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-maxsasliosize", -1, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-maxsasliosize") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={2} title={_("The maximum length for how long the connection queue for the socket can grow before refusing connections (nsslapd-listen-backlog-size).")}>
                                {_("Listen Backlog Size")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-listen-backlog-size']}
                                    min={1}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-listen-backlog-size") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-listen-backlog-size", 1, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-listen-backlog-size") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem className="ds-label" offset={5} span={2} title={_("Sets how deep a nested search filter is analysed (nsslapd-max-filter-nest-level).")}>
                                {_("Maximum Nested Filter Level")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-max-filter-nest-level']}
                                    min={-1}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-max-filter-nest-level") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-max-filter-nest-level", -1, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-max-filter-nest-level") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={2} title={_("Sets the time in seconds after which a TCP connection is closed if there is no more data to send (nsslapd-tcp-fin-timeout).")}>
                                {_("TCP FIN Timeout")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-tcp-fin-timeout']}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-tcp-fin-timeout") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-tcp-fin-timeout", 0, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-tcp-fin-timeout") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem className="ds-label" offset={5} span={2} title={_("Sets the time in milliseconds after which a TCP connection is closed if there is no more data to send (nsslapd-tcp-keepalive-time).")}>
                                {_("TCP Keepalive Time")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-tcp-keepalive-time']}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-tcp-keepalive-time") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-tcp-keepalive-time", 0, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-tcp-keepalive-time") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={2}
                                title={_("Maximum number of paged results searches allowed on a connection (nsslapd-maxsimplepaged-per-conn).")}
                            >
                                {_("Maximum Paged Result Searches Per Connection")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-maxsimplepaged-per-conn']}
                                    min={-1}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinusConfig("nsslapd-maxsimplepaged-per-conn") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-maxsimplepaged-per-conn", -1, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-maxsimplepaged-per-conn") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem className="ds-label" offset={5} span={2} title={_("The maximum number of LDAP controls allowed per operation (nsslapd-maxcontrolsperop).")}>
                                {_("Maximum Controls Per Operation")}
                            </GridItem>
                            <GridItem span={1}>
                                <NumberInput
                                    value={this.state['nsslapd-maxcontrolsperop']}
                                    min={1}
                                    max={1000}
                                    onMinus={() => { this.onMinusConfig("nsslapd-maxcontrolsperop") }}
                                    onChange={(e) => { this.onConfigChange(e, "nsslapd-maxcontrolsperop", 1, 0) }}
                                    onPlus={() => { this.onPlusConfig("nsslapd-maxcontrolsperop") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={4} title={_("Disable DNS reverse entries for outgoing connections (nsslapd-connection-nocanon).")}>
                                <Checkbox
                                    id="nsslapd-connection-nocanon"
                                    isChecked={this.state['nsslapd-connection-nocanon']}
                                    onChange={(e, checked) => {
                                        this.handleChange(e);
                                    }}
                                    label={_("Disable Reverse DNS Lookups")}
                                />
                            </GridItem>
                            <GridItem className="ds-label" offset={5} span={4} title={_("Sets the worker threads to continuously read a connection without passing it back to the polling mechanism. (nsslapd-enable-turbo-mode).")}>
                                <Checkbox
                                    id="nsslapd-enable-turbo-mode"
                                    isChecked={this.state['nsslapd-enable-turbo-mode']}
                                    onChange={(e, checked) => {
                                        this.handleChange(e);
                                    }}
                                    label={_("Enable Connection Turbo Mode")}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={4} title={_("Disable the virtual attribute lookup in a search entry (nsslapd-ignore-virtual-attrs).")}>
                                <Checkbox
                                    id="nsslapd-ignore-virtual-attrs"
                                    isChecked={this.state['nsslapd-ignore-virtual-attrs']}
                                    onChange={(e, checked) => {
                                        this.handleChange(e);
                                    }}
                                    label={_("Disable Virtual Attribute Lookups")}
                                />
                            </GridItem>
                            <GridItem className="ds-label" offset={5} span={4} title={_("Enable the normalized DN cache.  Each thread has its own cache (nsslapd-ndn-cache-enabled).")}>
                                <Checkbox
                                    isChecked={this.state['nsslapd-ndn-cache-enabled']}
                                    id="nsslapd-ndn-cache-enabled"
                                    onChange={(e, checked) => {
                                        this.handleChange(e);
                                    }}
                                    label={_("Enable Normalized DN Cache")}
                                />
                            </GridItem>

                        </Grid>
                    </Form>

                    <Button
                        isDisabled={this.state.saveDisabled || this.state.loading}
                        variant="primary"
                        className="ds-margin-top-xlg ds-left-margin"
                        onClick={this.handleSaveConfig}
                        isLoading={this.state.loading}
                        spinnerAriaValueText={this.state.loading ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>
                </div>
            );
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
    serverId: "",
    attrs: {},
};
