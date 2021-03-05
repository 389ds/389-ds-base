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
    Col,
    ControlLabel,
    Icon,
    Nav,
    NavItem,
    noop,
    Row,
    Spinner,
    TabContainer,
    TabContent,
    TabPane,
} from "patternfly-react";
import PropTypes from "prop-types";
import { log_cmd, valid_dn } from "../tools.jsx";

export class ReplSuffix extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            loading: false,
            activeKey: 1,
            showDisableConfirm: false,
            replicationEnabled: false,
            errObj: {},
            replEnabled: this.props.replicated,
            // Enable replication settings
            showEnableReplModal: false,
            enableRole: "Supplier",
            enableRID: "1",
            enableBindDN: "cn=replication manager,cn=config",
            enableBindPW: "",
            enableBindPWConfirm: "",
            enableBindGroupDN: "",
            disabled: false, // Disable repl enable button
            // Disable replication
            showDisableReplModal: false,
            disableChecked: false,
            disableSpinning: false,
            modalChecked: false,
            modalSpinning: false,
        };

        // General bindings
        this.handleReplChange = this.handleReplChange.bind(this);
        this.handleEnableChange = this.handleEnableChange.bind(this);
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
                showEnableReplModal: true
            });
        }
    }

    handleChange (e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        let errObj = this.state.errObj;
        if (value == "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        });
    }

    handleEnableChange (e) {
        let value = e.target.value;
        let attr = e.target.id;
        let valueErr = false;
        let errObj = this.state.errObj;
        let disable = false;

        if (attr == "enableBindDN" && value != "" && (!valid_dn(value) || !value.includes(','))) {
            valueErr = true;
        }
        if (attr == "enableBindGroupDN" && value != "" && (!valid_dn(value) || !value.includes(','))) {
            valueErr = true;
        }
        if (attr == "enableBindPW") {
            if (value != this.state.enableBindPWConfirm) {
                valueErr = true;
            } else {
                errObj.enableBindPW = false;
                errObj.enableBindPWConfirm = false;
            }
        }
        if (attr == "enableBindPWConfirm") {
            if (value != this.state.enableBindPW) {
                valueErr = true;
            } else {
                errObj.enableBindPW = false;
                errObj.enableBindPWConfirm = false;
            }
        }

        // Validate form and disable enable button if something is wrong.
        if (valueErr) {
            disable = true;
        } else {
            if ((attr != "enableBindPW" && attr != "enableBindPWConfirm" && this.state.enableBindPW != this.state.enableBindPWConfirm) ||
                (this.state.enableBindDN != "" && attr != "enableBindDN" && (!valid_dn(this.state.enableBindDN) ||
                                                                             !this.state.enableBindDN.includes(','))) ||
                (this.state.enableBindGroupDN != "" && attr != "enableBindGroupDN" && (!valid_dn(this.state.enableBindGroupDN) ||
                                                                                    !this.state.enableBindGroupDN.includes(',')))) {
                disable = true;
            }
        }
        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            errObj: errObj,
            disabled: disable
        });
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
        if (this.state.enableBindDN != "") {
            cmd.push('--bind-dn=' + this.state.enableBindDN);
        }
        if (this.state.enableBindPW != "") {
            cmd.push('--bind-passwd=' + this.state.enableBindPW);
        }
        if (this.state.enableBindGroupDN != "") {
            cmd.push('--bind-group-dn=' + this.state.enableBindGroupDN);
        }
        if (this.state.enableRole == "Supplier") {
            cmd.push('--replica-id=' + this.state.enableRID);
        }

        this.props.disableTree();
        log_cmd('enableReplication', 'Enable replication', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(1);
                    this.props.addNotification(
                        "success",
                        `Successfully enabled replication for "${this.props.suffix}"`
                    );
                })
                .fail(err => {
                    this.props.reload(1);
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to enable replication for "${this.props.suffix}" - ${errMsg.desc}`
                    );
                });
    }

    closeDisableReplModal () {
        this.setState({
            showDisableReplModal: false
        });
    }

    disableReplication () {
        this.props.disableTree();
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket', 'replication', 'disable', '--suffix=' + this.props.suffix];
        log_cmd('disableReplication', 'Disable replication', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(1);
                    this.props.addNotification(
                        "success",
                        `Successfully disabled replication for "${this.props.suffix}"`
                    );
                })
                .fail(err => {
                    this.props.reload(1);
                    let errMsg = JSON.parse(err);
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
        let suffixIcon = "tree";
        if (this.props.replicated) {
            suffixIcon = "clone";
        } else {
            if (this.props.repl == "subsuffix") {
                suffixIcon = "leaf";
            }
        }
        if (this.props.spinning) {
            spinning =
                <Spinner className="ds-margin-top ds-margin-left ds-inline-spinner" loading inline size="sm" />;
            spintext =
                <font size="2"><i>Refreshing</i></font>;
        }
        let suffixClass = "ds-margin-top-xlg";
        if (this.props.disabled) {
            suffixClass = "ds-margin-top-xlg ds-disabled";
        }
        let replAgmtNavTitle = 'Agreements <font size="2">(' + this.props.agmtRows.length + ')</font>';
        let winsyncNavTitle = 'Winsync Agreements <font size="2">(' + this.props.winsyncRows.length + ')</font>';

        let enabledContent =
            <div className={suffixClass}>
                <TabContainer id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div>
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: 'Configuration'}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: replAgmtNavTitle}} />
                            </NavItem>
                            <NavItem eventKey={3}>
                                <div dangerouslySetInnerHTML={{__html: winsyncNavTitle}} />
                            </NavItem>
                            <NavItem eventKey={4}>
                                <div dangerouslySetInnerHTML={{__html: "Change Log"}} />
                            </NavItem>
                            <NavItem eventKey={5}>
                                <div dangerouslySetInnerHTML={{__html: "RUV's & Tasks"}} />
                            </NavItem>
                        </Nav>
                        <TabContent>
                            <TabPane eventKey={1}>
                                <ReplConfig
                                    suffix={this.props.suffix}
                                    role={this.props.role}
                                    data={this.props.data}
                                    serverId={this.props.serverId}
                                    addNotification={this.props.addNotification}
                                    reload={this.props.reload}
                                    reloadConfig={this.props.reloadConfig}
                                />
                            </TabPane>
                            <TabPane eventKey={2}>
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
                            </TabPane>
                            <TabPane eventKey={3}>
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
                            </TabPane>
                            <TabPane eventKey={4}>
                                <Changelog
                                    suffix={this.props.suffix}
                                    serverId={this.props.serverId}
                                    addNotification={this.props.addNotification}
                                    clMaxEntries={this.props.data['clMaxEntries']}
                                    clMaxAge={this.props.data['clMaxAge']}
                                    clTrimInt={this.props.data['clTrimInt']}
                                    clEncrypt={this.props.data['clEncrypt']}
                                    key={this.props.data}
                                />
                            </TabPane>
                            <TabPane eventKey={5}>
                                <ReplRUV
                                    suffix={this.props.suffix}
                                    serverId={this.props.serverId}
                                    rows={this.props.ruvRows}
                                    addNotification={this.props.addNotification}
                                    reload={this.props.reloadRUV}
                                    localRID={this.props.data.nsds5replicaid}
                                    key={this.props.ruvRows}
                                />
                            </TabPane>
                        </TabContent>
                    </div>
                </TabContainer>
            </div>;

        let replActionButton = "";
        if (this.props.replicated) {
            replActionButton =
                <Button
                    bsStyle="danger"
                    onClick={this.handleReplChange}
                    title="Disable replication, and remove all replication agreements."
                >
                    Disable
                </Button>;
        } else {
            enabledContent =
                <div className="ds-center ds-margin-top-xlg">
                    <h4>
                        Replication is not enabled for this suffix
                    </h4>
                    <Button
                        bsStyle="primary"
                        onClick={this.handleReplChange}
                        className="ds-margin-top-lg"
                    >
                        Enable Replication
                    </Button>
                </div>;
        }

        return (
            <div id="suffix-page">
                <Row>
                    <Col sm={8} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header"><Icon type="fa" name={suffixIcon} />
                            {" " + this.props.suffix}
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh replication settings for this suffix"
                                onClick={() => {
                                    this.props.reload(false);
                                }}
                            />
                            {spinning} {spintext}
                        </ControlLabel>
                    </Col>
                    <Col sm={4}>
                        <Row>
                            <Col className="ds-no-padding ds-container" componentClass={ControlLabel} sm={12}>
                                {replActionButton}
                            </Col>
                        </Row>
                    </Col>
                </Row>
                <p />
                {enabledContent}
                <EnableReplModal
                    showModal={this.state.showEnableReplModal}
                    closeHandler={this.closeEnableReplModal}
                    handleChange={this.handleEnableChange}
                    saveHandler={this.enableReplication}
                    spinning={this.state.addManagerSpinning}
                    role={this.state.enableRole}
                    disabled={this.state.disabled}
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
    addNotification: noop,
    agmtRows: [],
    winsyncRows: [],
    ruvRows: [],
    reloadAgmts: noop,
    reloadRUV: noop,
    reloadConfig: noop,
    reload: noop,
    replicated: false,
    attrs: [],
    enableTree: noop,
    disableTree: noop,
    spinning: false,
    disabled: false,
};
