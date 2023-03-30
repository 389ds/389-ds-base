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
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faClone,
    faLeaf,
    faTree,
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';
import PropTypes from "prop-types";
import { log_cmd, valid_dn, callCmdStreamPassword } from "../tools.jsx";

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

        this.onMinus = () => {
            this.setState({
                enableRID: Number(this.state.enableRID) - 1
            });
        };
        this.onNumberChange = (event) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                enableRID: newValue > 65534 ? 65534 : newValue < 1 ? 1 : newValue
            });
        };

        this.onPlus = () => {
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
        this.handleEnableChange = this.handleEnableChange.bind(this);
        this.validateEnable = this.validateEnable.bind(this);
        this.handleChange = this.handleChange.bind(this);
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

    handleChange (e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        if (value == "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        });
    }

    validateEnable() {
        const errObj = {};
        let all_good = true;

        const dnAttrs = ['enableBindDN', 'enableBindGroupDN'];
        for (const attr of dnAttrs) {
            if (this.state[attr] != "" && (!valid_dn(this.state[attr]) || !this.state[attr].includes(','))) {
                all_good = false;
                errObj[attr] = true;
            }
        }

        if (this.state.enableBindDN) {
            if (this.state.enableBindPW == "" || this.state.enableBindPW != this.state.enableBindPWConfirm) {
                errObj.enableBindPW = true;
                errObj.enableBindPWConfirm = true;
                all_good = false;
            }
        }

        this.setState({
            errObj: errObj,
            disabled: !all_good
        });
    }

    handleEnableChange (e) {
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
        if (this.state.enableBindDN != "" && !valid_dn(this.state.enableBindDN)) {
            this.props.addNotification(
                "error",
                `The Bind DN is not a valid DN (Distinguished Name) ${this.state.enableBindDN}`
            );
            return;
        }
        if (this.state.enableBindGroupDN != "" && !valid_dn(this.state.enableBindGroupDN)) {
            this.props.addNotification(
                "error",
                `The Group DN is not a valid DN (Distinguished Name)`
            );
            return;
        }
        if (this.state.enableBindPW != this.state.enableBindPWConfirm) {
            this.props.addNotification(
                "error",
                `The Bind DN passwords do not match`
            );
            return;
        }
        if (this.state.enableRID != "" && (this.state.enableRID < 1 || this.state.enableRID > 65534)) {
            this.props.addNotification(
                "error",
                `The Replica ID is not in the valid range of 1 - 65534`
            );
            return;
        }

        // Now enable replication
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'enable', '--suffix=' + this.props.suffix,
            '--role=' + this.state.enableRole
        ];
        let passwd = "";
        if (this.state.enableBindDN != "") {
            cmd.push('--bind-dn=' + this.state.enableBindDN);
        }
        if (this.state.enableBindPW != "") {
            passwd = this.state.enableBindPW;
        }
        if (this.state.enableBindGroupDN != "") {
            cmd.push('--bind-group-dn=' + this.state.enableBindGroupDN);
        }
        if (this.state.enableRole == "Supplier") {
            cmd.push('--replica-id=' + this.state.enableRID);
        }

        this.setState({
            enableSpinning: true
        });

        this.props.disableTree();


        // Something changed, perform the update
        const config = {
            cmd: cmd,
            promptArg: "--bind-passwd-prompt",
            passwd: passwd,
            addNotification: this.props.addNotification,
            success_msg: `Successfully enabled replication for "${this.props.suffix}"`,
            error_msg: `Failed to enable replication for "${this.props.suffix}"`,
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
                        `Successfully disabled replication for "${this.props.suffix}"`
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
                        `Failed to disable replication for "${this.props.suffix}" - ${errMsg.desc}`
                    );
                });
    }

    //
    // Render the component
    //
    render () {
        let spinning = "";
        let spintext = "";
        let suffixIcon = faTree;
        if (this.props.replicated) {
            suffixIcon = faClone;
        } else {
            if (this.props.repl == "subsuffix") {
                suffixIcon = faLeaf;
            }
        }
        if (this.props.spinning) {
            spinning =
                <Spinner className="ds-margin-top ds-margin-left ds-inline-spinner" size="sm" />;
            spintext =
                <font size="2"><i>Refreshing</i></font>;
        }
        let suffixClass = "ds-margin-top-lg";
        if (this.props.disabled) {
            suffixClass = "ds-margin-top-lg ds-disabled";
        }

        let enabledContent =
            <div className={suffixClass}>
                <Tabs isFilled activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Configuration</TabTitleText>}>
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
                    <Tab eventKey={1} title={<TabTitleText>Agreements <font size="2">({this.props.agmtRows.length})</font></TabTitleText>}>
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
                    <Tab eventKey={2} title={<TabTitleText>Winsync Agreements <font size="2">({this.props.winsyncRows.length})</font></TabTitleText>}>
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
                    <Tab eventKey={3} title={<TabTitleText>Change Log</TabTitleText>}>
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
                    <Tab eventKey={4} title={<TabTitleText>RUV's & Tasks</TabTitleText>}>
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
            </div>;

        let replActionButton = "";
        if (this.props.replicated) {
            replActionButton =
                <Button
                    className="ds-float-right"
                    variant="danger"
                    onClick={this.handleReplChange}
                    title="Disable replication, and remove all replication agreements."
                >
                    Disable
                </Button>;
        } else {
            enabledContent =
                <div className="ds-center ds-margin-top-xlg">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Replication is not enabled for this suffix
                        </Text>
                    </TextContent>
                    <Button
                        variant="primary"
                        onClick={this.handleReplChange}
                        className="ds-margin-top-lg"
                    >
                        Enable Replication
                    </Button>
                </div>;
        }

        return (
            <div id="suffix-page">
                <Grid>
                    <GridItem className="ds-suffix-header" span={8}>
                        <FontAwesomeIcon size="sm" icon={suffixIcon} />&nbsp;&nbsp;{this.props.suffix}
                        <FontAwesomeIcon
                            className="ds-left-margin ds-refresh"
                            icon={faSyncAlt}
                            title="Refresh replication settings for this suffix"
                            onClick={() => this.props.reload(false)}
                        />
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
                    handleChange={this.handleEnableChange}
                    saveHandler={this.enableReplication}
                    spinning={this.state.enableSpinning}
                    enableRole={this.state.enableRole}
                    enableRID={this.state.enableRID}
                    enableBindDN={this.state.enableBindDN}
                    disabled={this.state.disabled}
                    onMinus={this.onMinus}
                    onNumberChange={this.onNumberChange}
                    onPlus={this.onPlus}
                    error={this.state.errObj}
                />
                <DoubleConfirmModal
                    showModal={this.state.showDisableReplModal}
                    closeHandler={this.closeDisableReplModal}
                    handleChange={this.handleChange}
                    actionHandler={this.disableReplication}
                    spinning={this.state.modalSpinning}
                    item={this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle="Disable Replication"
                    mMsg="Are you sure you want to disable replication for this suffix?"
                    mSpinningMsg="Disabling Replication ..."
                    mBtnName="Disable Replication"
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
