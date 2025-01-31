import React from "react";
import cockpit from "cockpit";
import { Changelog } from "./replChangelog.jsx";
import { ReplConfig } from "./replConfig.jsx";
import { WinsyncAgmts } from "./winsyncAgmts.jsx";
import { ReplAgmts } from "./replAgmts.jsx";
import { ReplRUV } from "./replTasks.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import { EnableReplModal } from "./replModals.jsx";
import {
    Button,
    Grid,
    GridItem,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { SyncAltIcon, TreeIcon, CloneIcon, LeafIcon } from "@patternfly/react-icons";
import PropTypes from "prop-types";
import { log_cmd, valid_dn, callCmdStreamPassword } from "../tools.jsx";

const _ = cockpit.gettext;

export class ReplSuffix extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            loading: false,
            activeTabKey: 0,
            showDisableConfirm: false,
            replicationEnabled: false,
            errObj: {},
            replEnabled: this.props.replicated,
            // Enable replication settings
            showEnableReplModal: false,
            enableRole: "Supplier",
            enableRID: 1,
            enableBindDN: "cn=replication manager,cn=config",
            enableBindPW: "",
            enableBindPWConfirm: "",
            enableBindGroupDN: "",
            enableSpinning: false,
            disabled: true, // Disable repl enable button
            // Disable replication
            showDisableReplModal: false,
            disableChecked: false,
            disableSpinning: false,
            modalChecked: false,
            modalSpinning: false,
        };

        this.handleMinusChange = () => {
            this.setState({
                enableRID: Number(this.state.enableRID) - 1
            });
        };
        this.handleNumberChange = (event) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                enableRID: newValue > 65534 ? 65534 : newValue < 1 ? 1 : newValue
            });
        };

        this.handlePlusChange = () => {
            this.setState({
                enableRID: Number(this.state.enableRID) + 1
            });
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            event.preventDefault();
            this.setState({
                activeTabKey: tabIndex
            });
        };

        // General bindings
        this.handleReplChange = this.handleReplChange.bind(this);
        this.onEnableChange = this.onEnableChange.bind(this);
        this.validateEnable = this.validateEnable.bind(this);
        this.onChange = this.onChange.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.disableReplication = this.disableReplication.bind(this);
        this.enableReplication = this.enableReplication.bind(this);
        this.closeEnableReplModal = this.closeEnableReplModal.bind(this);
        this.closeDisableReplModal = this.closeDisableReplModal.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    handleReplChange() {
        if (this.props.replicated) {
            // Disable Replication
            this.setState({
                showDisableReplModal: true,
                modalChecked: false,
                modalSpinning: false
            });
        } else {
            // Enable replication
            this.setState({
                showEnableReplModal: true,
                enableSpinning: false,
            });
        }
    }

    onChange (e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        if (value === "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj
        });
    }

    validateEnable() {
        const errObj = {};
        let all_good = true;

        const dnAttrs = ['enableBindDN', 'enableBindGroupDN'];
        for (const attr of dnAttrs) {
            if (this.state[attr] !== "" && (!valid_dn(this.state[attr]) || !this.state[attr].includes(','))) {
                all_good = false;
                errObj[attr] = true;
            }
        }

        if (this.state.enableBindDN) {
            if (this.state.enableBindPW === "" || this.state.enableBindPW !== this.state.enableBindPWConfirm) {
                errObj.enableBindPW = true;
                errObj.enableBindPWConfirm = true;
                all_good = false;
            }
        }

        this.setState({
            errObj,
            disabled: !all_good
        });
    }

    onEnableChange (e) {
        const value = e.target.value;
        const attr = e.target.id;

        this.setState({
            [attr]: value,
        }, () => { this.validateEnable() });
    }

    closeEnableReplModal () {
        this.setState({
            showEnableReplModal: false,
        });
    }

    enableReplication () {
        // First, Validate
        if (this.state.enableBindDN !== "" && !valid_dn(this.state.enableBindDN)) {
            this.props.addNotification(
                "error",
                cockpit.format(_("The Bind DN is not a valid DN (Distinguished Name) $0"), this.state.enableBindDN)
            );
            return;
        }
        if (this.state.enableBindGroupDN !== "" && !valid_dn(this.state.enableBindGroupDN)) {
            this.props.addNotification(
                "error",
                _("The Group DN is not a valid DN (Distinguished Name)")
            );
            return;
        }
        if (this.state.enableBindPW !== this.state.enableBindPWConfirm) {
            this.props.addNotification(
                "error",
                _("The Bind DN passwords do not match")
            );
            return;
        }
        if (this.state.enableRID !== "" && (this.state.enableRID < 1 || this.state.enableRID > 65534)) {
            this.props.addNotification(
                "error",
                _("The Replica ID is not in the valid range of 1 - 65534")
            );
            return;
        }

        // Now enable replication
        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'enable', '--suffix=' + this.props.suffix,
            '--role=' + this.state.enableRole
        ];
        let passwd = "";
        if (this.state.enableBindDN !== "") {
            cmd.push('--bind-dn=' + this.state.enableBindDN);
        }
        if (this.state.enableBindPW !== "") {
            passwd = this.state.enableBindPW;
        }
        if (this.state.enableBindGroupDN !== "") {
            cmd.push('--bind-group-dn=' + this.state.enableBindGroupDN);
        }
        if (this.state.enableRole === "Supplier") {
            cmd.push('--replica-id=' + this.state.enableRID);
        }

        this.setState({
            enableSpinning: true
        });

        this.props.disableTree();

        // Something changed, perform the update
        const config = {
            cmd,
            promptArg: "--bind-passwd-prompt",
            passwd,
            addNotification: this.props.addNotification,
            success_msg: cockpit.format(_("Successfully enabled replication for $0"), this.props.suffix),
            error_msg: cockpit.format(_("Failed to enable replication for $0"), this.props.suffix),
            state_callback: () => {},
            reload_func: this.props.reload,
            reload_arg: "1",
            funcName: "enableReplication",
            funcDesc: "Enable replication"
        };
        callCmdStreamPassword(config);
    }

    closeDisableReplModal () {
        this.setState({
            showDisableReplModal: false
        });
    }

    disableReplication () {
        this.props.disableTree();
        this.setState({
            modalSpinning: true
        });
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket', 'replication', 'disable', '--suffix=' + this.props.suffix];
        log_cmd('disableReplication', 'Disable replication', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(1);
                    this.setState({
                        modalSpinning: false
                    });
                    this.props.addNotification(
                        "success",
                        cockpit.format(_("Successfully disabled replication for $0"), this.props.suffix)
                    );
                })
                .fail(err => {
                    this.props.reload(1);
                    this.setState({
                        modalSpinning: false
                    });
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to disable replication for $0 - $1"), this.props.suffix, errMsg.desc)
                    );
                });
    }

    //
    // Render the component
    //
    render () {
        let spinning = "";
        let spintext = "";
        let SuffixIcon = TreeIcon;
        if (this.props.replicated) {
            SuffixIcon = CloneIcon;
        } else {
            if (this.props.repl === "subsuffix") {
                SuffixIcon = LeafIcon;
            }
        }
        if (this.props.spinning) {
            spinning =
                <Spinner className="ds-margin-top ds-margin-left ds-inline-spinner" size="sm" />;
            spintext =
                <font size="2"><i>{_("Refreshing")}</i></font>;
        }
        let suffixClass = "ds-margin-top-lg";
        if (this.props.disabled) {
            suffixClass = "ds-margin-top-lg ds-disabled";
        }

        let enabledContent = (
            <div className={suffixClass}>
                <Tabs isFilled activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>{_("Configuration")}</TabTitleText>}>
                        <ReplConfig
                            suffix={this.props.suffix}
                            role={this.props.role}
                            data={this.props.data}
                            serverId={this.props.serverId}
                            addNotification={this.props.addNotification}
                            reload={this.props.reload}
                            reloadConfig={this.props.reloadConfig}
                        />
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>{_("Agreements ")}<font size="2">({this.props.agmtRows.length})</font></TabTitleText>}>
                        <ReplAgmts
                            suffix={this.props.suffix}
                            serverId={this.props.serverId}
                            rows={this.props.agmtRows}
                            addNotification={this.props.addNotification}
                            reload={this.props.reloadAgmts}
                            attrs={this.props.attrs}
                            disableTable={this.props.disableAgmtTable}
                            key={this.props.agmtRows}
                        />
                    </Tab>
                    <Tab eventKey={2} title={<TabTitleText>{_("Winsync Agreements ")}<font size="2">({this.props.winsyncRows.length})</font></TabTitleText>}>
                        <WinsyncAgmts
                            suffix={this.props.suffix}
                            serverId={this.props.serverId}
                            rows={this.props.winsyncRows}
                            addNotification={this.props.addNotification}
                            reload={this.props.reloadWinsyncAgmts}
                            attrs={this.props.attrs}
                            disableTable={this.props.disableWSAgmtTable}
                            key={this.props.winsyncRows}
                        />
                    </Tab>
                    <Tab eventKey={3} title={<TabTitleText>{_("Change Log")}</TabTitleText>}>
                        <Changelog
                            suffix={this.props.suffix}
                            serverId={this.props.serverId}
                            addNotification={this.props.addNotification}
                            clMaxEntries={this.props.data.clMaxEntries}
                            clMaxAge={this.props.data.clMaxAge}
                            clTrimInt={this.props.data.clTrimInt}
                            clEncrypt={this.props.data.clEncrypt}
                            key={this.props.data}
                        />
                    </Tab>
                    <Tab eventKey={4} title={<TabTitleText>{_("RUV's & Tasks")}</TabTitleText>}>
                        <ReplRUV
                            suffix={this.props.suffix}
                            serverId={this.props.serverId}
                            rows={this.props.ruvRows}
                            addNotification={this.props.addNotification}
                            reload={this.props.reloadRUV}
                            localRID={this.props.data.nsds5replicaid}
                            ldifRows={this.props.ldifRows}
                            key={this.props.ruvRows}
                        />
                    </Tab>
                </Tabs>
            </div>
        );

        let replActionButton = "";
        if (this.props.replicated) {
            replActionButton = (
                <Button
                    className="ds-float-right"
                    variant="danger"
                    onClick={this.handleReplChange}
                    title={_("Disable replication, and remove all replication agreements.")}
                >
                    {_("Disable")}
                </Button>
            );
        } else {
            enabledContent = (
                <div className="ds-center ds-margin-top-xlg">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Replication is not enabled for this suffix")}
                        </Text>
                    </TextContent>
                    <Button
                        variant="primary"
                        onClick={this.handleReplChange}
                        className="ds-margin-top-lg"
                    >
                        {_("Enable Replication")}
                    </Button>
                </div>
            );
        }

        return (
            <div id="suffix-page">
                <Grid>
                    <GridItem className="ds-suffix-header" span={8}>
                        <SuffixIcon />
                        &nbsp;&nbsp;{this.props.suffix}
                        <Button 
                            variant="plain"
                            aria-label={_("Refresh replication settings for this suffix")}
                            onClick={() => this.props.reload(false)}
                        >
                            <SyncAltIcon />
                        </Button>
                        {spinning} {spintext}
                    </GridItem>
                    <GridItem span={4}>
                        {replActionButton}
                    </GridItem>
                </Grid>
                {enabledContent}
                <EnableReplModal
                    showModal={this.state.showEnableReplModal}
                    closeHandler={this.closeEnableReplModal}
                    handleChange={this.onEnableChange}
                    saveHandler={this.enableReplication}
                    spinning={this.state.enableSpinning}
                    enableRole={this.state.enableRole}
                    enableRID={this.state.enableRID}
                    enableBindDN={this.state.enableBindDN}
                    disabled={this.state.disabled}
                    onMinus={this.handleMinusChange}
                    onNumberChange={this.handleNumberChange}
                    onPlus={this.handlePlusChange}
                    error={this.state.errObj}
                />
                <DoubleConfirmModal
                    showModal={this.state.showDisableReplModal}
                    closeHandler={this.closeDisableReplModal}
                    handleChange={this.onChange}
                    actionHandler={this.disableReplication}
                    spinning={this.state.modalSpinning}
                    item={this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle={_("Disable Replication")}
                    mMsg={_("Are you sure you want to disable replication for this suffix?")}
                    mSpinningMsg={_("Disabling Replication ...")}
                    mBtnName={_("Disable Replication")}
                />
            </div>
        );
    }
}

ReplSuffix.propTypes = {
    serverId: PropTypes.string,
    suffix: PropTypes.string,
    role: PropTypes.string,
    addNotification: PropTypes.func,
    agmtRows: PropTypes.array,
    ldifRows: PropTypes.array,
    winsyncRows: PropTypes.array,
    ruvRows: PropTypes.array,
    reloadAgmts: PropTypes.func,
    reloadRUV: PropTypes.func,
    reloadConfig: PropTypes.func,
    reload: PropTypes.func,
    replicated: PropTypes.bool,
    attrs: PropTypes.array,
    enableTree: PropTypes.func,
    disableTree: PropTypes.func,
    spinning: PropTypes.bool,
    disabled: PropTypes.bool,
};

ReplSuffix.defaultProps = {
    serverId: "",
    suffix: "",
    role: "",
    agmtRows: [],
    winsyncRows: [],
    ruvRows: [],
    ldifRows: [],
    replicated: false,
    attrs: [],
    spinning: false,
    disabled: false,
};
