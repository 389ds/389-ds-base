import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup, DoubleConfirmModal } from "../notifications.jsx";
import CustomCollapse from "../customCollapse.jsx";
import { log_cmd } from "../tools.jsx";
import {
    Modal,
    Icon,
    Button,
    Form,
    Row,
    Col,
    ControlLabel,
    FormControl,
    noop
} from "patternfly-react";
import PropTypes from "prop-types";
import "../../css/ds.css";

//
// This component is the global chaining & default configuration
//
export class ChainingDatabaseConfig extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            oids: this.props.data.oids,
            oidList: this.props.data.oidList,
            availableOids: this.props.data.availableOids,
            comps: this.props.data.comps,
            compList: this.props.data.compList,
            availableComps: this.props.data.availableComps,
            selectedOids: this.props.data.selectedOids,
            selectedComps: this.props.data.selectedComps,
            removeOids: this.props.data.removeOids,
            removeComps: this.props.data.removeComps,
            showConfirmCompDelete: false,
            showConfirmOidDelete: false,
            showOidModal: false,
            showCompsModal: false,
            // Chaining config settings
            defSearchCheck: this.props.data.defSearchCheck,
            defBindConnLimit: this.props.data.defBindConnLimit,
            defBindTimeout: this.props.data.defBindTimeout,
            defBindRetryLimit: this.props.data.defBindRetryLimit,
            defConcurLimit: this.props.data.defConcurLimit,
            defConcurOpLimit: this.props.data.defConcurOpLimit,
            defConnLife: this.props.data.defConnLife,
            defHopLimit: this.props.data.defHopLimit,
            defDelay: this.props.data.defDelay,
            defTestDelay: this.props.data.defTestDelay,
            defOpConnLimit: this.props.data.defOpConnLimit,
            defSizeLimit: this.props.data.defSizeLimit,
            defTimeLimit: this.props.data.defTimeLimit,
            defProxy: this.props.data.defProxy,
            defRefOnScoped: this.props.data.defRefOnScoped,
            defCheckAci: this.props.data.defCheckAci,
            defUseStartTLS: this.props.data.defUseStartTLS,
            // Original values used for saving config
            _defSearchCheck: this.props.data.defSearchCheck,
            _defBindConnLimit: this.props.data.defBindConnLimit,
            _defBindTimeout: this.props.data.defBindTimeout,
            _defBindRetryLimit: this.props.data.defBindRetryLimit,
            _defConcurLimit: this.props.data.defConcurLimit,
            _defConcurOpLimit: this.props.data.defConcurOpLimit,
            _defConnLife: this.props.data.defConnLife,
            _defHopLimit: this.props.data.defHopLimit,
            _defDelay: this.props.data.defDelay,
            _defTestDelay: this.props.data.defTestDelay,
            _defOpConnLimit: this.props.data.defOpConnLimit,
            _defSizeLimit: this.props.data.defSizeLimit,
            _defTimeLimit: this.props.data.defTimeLimit,
            _defProxy: this.props.data.defProxy,
            _defRefOnScoped: this.props.data.defRefOnScoped,
            _defCheckAci: this.props.data.defCheckAci,
            _defUseStartTLS: this.props.data.defUseStartTLS,
        };

        this.handleChange = this.handleChange.bind(this);
        this.save_chaining_config = this.save_chaining_config.bind(this);
        // Chaining Control OIDs
        this.showOidModal = this.showOidModal.bind(this);
        this.closeOidModal = this.closeOidModal.bind(this);
        this.handleOidChange = this.handleOidChange.bind(this);
        this.saveOids = this.saveOids.bind(this);
        this.deleteOids = this.deleteOids.bind(this);
        this.handleSelectOids = this.handleSelectOids.bind(this);
        // Chaining comps
        this.showCompsModal = this.showCompsModal.bind(this);
        this.closeCompsModal = this.closeCompsModal.bind(this);
        this.handleCompsChange = this.handleCompsChange.bind(this);
        this.saveComps = this.saveComps.bind(this);
        this.deleteComps = this.deleteComps.bind(this);
        this.handleSelectComps = this.handleSelectComps.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
        this.closeConfirmOidDelete = this.closeConfirmOidDelete.bind(this);
        this.closeConfirmCompDelete = this.closeConfirmCompDelete.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    handleChange(e) {
        // Generic
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    save_chaining_config () {
        // Build up the command list
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'chaining', 'config-set-def'
        ];
        let val = "";
        if (this.state._defUseStartTLS != this.state.defUseStartTLS) {
            val = "off";
            if (this.state.defUseStartTLS) {
                val = "on";
            }
            cmd.push("--use-starttls=" + val);
        }
        if (this.state._defCheckAci != this.state.defCheckAci) {
            val = "off";
            if (this.state.defCheckAci) {
                val = "on";
            }
            cmd.push("--check-aci=" + val);
        }
        if (this.state._defRefOnScoped != this.state.defRefOnScoped) {
            val = "off";
            if (this.state.defRefOnScoped) {
                val = "on";
            }
            cmd.push("--return-ref=" + val);
        }
        if (this.state._defProxy != this.state.defProxy) {
            val = "off";
            if (this.state.defProxy) {
                val = "on";
            }
            cmd.push("--proxied-auth=" + val);
        }
        if (this.state._defTestDelay != this.state.defTestDelay) {
            cmd.push("--test-response-delay=" + this.state.defTestDelay);
        }
        if (this.state._defOpConnLimit != this.state.defOpConnLimit) {
            cmd.push("--conn-op-limit=" + this.state.defOpConnLimit);
        }
        if (this.state._defSizeLimit != this.state.defSizeLimit) {
            cmd.push("--size-limit=" + this.state.defSizeLimit);
        }
        if (this.state._defTimeLimit != this.state.defTimeLimit) {
            cmd.push("--time-limit=" + this.state.defTimeLimit);
        }
        if (this.state._defSearchCheck != this.state.defSearchCheck) {
            cmd.push("--abandon-check-interval=" + this.state.defSearchCheck);
        }
        if (this.state._defBindTimeout != this.state.defBindTimeout) {
            cmd.push("--bind-timeout=" + this.state.defBindTimeout);
        }
        if (this.state._defBindRetryLimit != this.state.defBindRetryLimit) {
            cmd.push("--bind-attempts=" + this.state.defBindRetryLimit);
        }
        if (this.state._defConcurLimit != this.state.defConcurLimit) {
            cmd.push("--bind-limit=" + this.state.defConcurLimit);
        }
        if (this.state._defConcurOpLimit != this.state.defConcurOpLimit) {
            cmd.push("--op-limit=" + this.state.defConcurOpLimit);
        }
        if (this.state._defConnLife != this.state.defConnLife) {
            cmd.push("--conn-lifetime=" + this.state.defConnLife);
        }
        if (this.state._defHopLimit != this.state.defHopLimit) {
            cmd.push("--hop-limit=" + this.state.defHopLimit);
        }
        if (this.state._defDelay != this.state.defDelay) {
            cmd.push("--response-delay=" + this.state.defDelay);
        }
        if (this.state._defBindConnLimit != this.state.defBindConnLimit) {
            cmd.push("--conn-bind-limit=" + this.state.defBindConnLimit);
        }

        // If we have chaining mods, then apply them...
        if (cmd.length > 5) {
            log_cmd("save_chaining_config", "Applying default chaining config change", cmd);
            cockpit
                    .spawn(cmd, {superuser: true, "err": "message"})
                    .done(content => {
                        // Continue with the next mod
                        this.props.reload();
                        this.props.addNotification(
                            "success",
                            `Successfully updated chaining configuration`
                        );
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.reload();
                        this.props.addNotification(
                            "error",
                            `Error updating chaining configuration - ${errMsg.desc}`
                        );
                    });
        }
    }

    //
    // Chaining OID modal functions
    //
    showOidModal () {
        this.setState({
            showOidModal: true
        });
    }

    closeOidModal() {
        this.setState({
            showOidModal: false
        });
    }

    handleOidChange(e) {
        const options = e.target.options;
        let values = [];
        for (let option of options) {
            if (option.selected) {
                values.push(option.value);
            }
        }
        this.setState({
            selectedOids: values
        });
    }

    saveOids () {
        // Save chaining control oids
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-set"
        ];
        for (let oid of this.state.selectedOids) {
            if (!this.state.oidList.includes(oid)) {
                cmd.push('--add-control=' + oid);
            }
        }
        this.closeOidModal();
        log_cmd("saveOids", "Save new chaining OID controls", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.props.addNotification(
                        "success",
                        `Successfully updated chaining controls`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        `Error updating chaining controls - ${errMsg.desc}`
                    );
                });
    }

    handleSelectOids (e) {
        const options = e.target.options;
        let values = [];
        for (let option of options) {
            if (option.selected) {
                values.push(option.value);
            }
        }
        this.setState({
            removeOids: values
        });
    }

    deleteOids(props) {
        // Remove chaining controls
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-set"
        ];
        for (let oid of this.state.removeOids) {
            cmd.push('--del-control=' + oid);
        }
        this.state.removeOids = [];

        log_cmd("deleteOids", "Delete chaining controls", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.props.addNotification(
                        "success",
                        `Successfully removed chaining controls`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        `Error removing chaining controls - ${errMsg.desc}`
                    );
                });
    }

    //
    // Chaining Component modal functions
    //
    showCompsModal () {
        this.setState({
            showCompsModal: true
        });
    }

    closeCompsModal() {
        this.setState({
            showCompsModal: false
        });
    }

    handleCompsChange(e) {
        const options = e.target.options;
        let values = [];
        for (let option of options) {
            if (option.selected) {
                values.push(option.value);
            }
        }
        this.setState({
            selectedComps: values
        });
    }

    handleSelectComps (e) {
        const options = e.target.options;
        let values = [];
        for (let option of options) {
            if (option.selected) {
                values.push(option.value);
            }
        }
        this.setState({
            removeComps: values
        });
    }

    saveComps () {
        // Save chaining control oids
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-set"
        ];
        for (let comp of this.state.selectedComps) {
            if (!this.state.compList.includes(comp)) {
                cmd.push('--add-comp=' + comp);
            }
        }
        this.closeCompsModal();
        log_cmd("saveComps", "Save new chaining components", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.props.addNotification(
                        "success",
                        `Successfully updated chaining components`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        `Error updating chaining components - ${errMsg.desc}`
                    );
                });
    }

    deleteComps(props) {
        // Remove chaining comps
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-set"
        ];
        for (let comp of this.state.removeComps) {
            cmd.push('--del-comp=' + comp);
        }
        this.state.removeComps = [];

        log_cmd("deleteComps", "Delete chaining components", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.props.addNotification(
                        "success",
                        `Successfully removed chaining components`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        `Error removing chaining components - ${errMsg.desc}`
                    );
                });
    }

    //
    // Confirm deletion functions
    //
    showConfirmDelete(item) {
        if (item == "oid") {
            if (this.state.removeOids.length) {
                this.setState({
                    showConfirmOidDelete: true
                });
            }
        } else if (item == "comp") {
            if (this.state.removeComps.length) {
                this.setState({
                    showConfirmCompDelete: true
                });
            }
        }
    }

    closeConfirmOidDelete() {
        this.setState({
            showConfirmOidDelete: false
        });
    }

    closeConfirmCompDelete() {
        this.setState({
            showConfirmCompDelete: false
        });
    }

    render() {
        // Get OIDs and comps
        this.state.oids = this.state.oidList.map((oid) =>
            <option key={oid} value={oid}>{oid}</option>
        );
        this.state.comps = this.state.compList.map((comp) =>
            <option key={comp} value={comp}>{comp}</option>
        );

        return (
            <div id="chaining-page">
                <h3 className="ds-config-header">Database Chaining Settings</h3>
                <hr />
                <div className="ds-container">
                    <div className="ds-chaining-split">
                        <form>
                            <label className="ds-config-label" htmlFor="chaining-oid-list" title="A list of LDAP control OIDs to be forwarded through chaining"><b>Forwarded LDAP Controls</b></label>
                            <select id="chaining-oid-list" onChange={this.handleSelectOids} className="ds-chaining-list" name="nstransmittedcontrols" size="10" multiple>
                                {this.state.oids}
                            </select>
                        </form>
                        <div className="clearfix ds-container">
                            <div className="ds-panel-left">
                                <button type="button" onClick={this.showOidModal} className="ds-button-left">Add</button>
                            </div>
                            <div className="ds-panel-right">
                                <button type="button" onClick={e => this.showConfirmDelete("oid")} className="ds-button-right">Delete</button>
                            </div>
                        </div>
                    </div>
                    <div className="ds-chaining-divider" />
                    <div className="ds-chaining-split">
                        <form>
                            <label className="ds-config-label" htmlFor="chaining-comp-list" title="A list of components to go through chaining"><b>Components to Chain</b></label>
                            <select id="chaining-comp-list" onChange={this.handleSelectComps} className="ds-chaining-list" name="nsactivechainingcomponents" size="10" multiple>
                                {this.state.comps}}
                            </select>
                        </form>
                        <div className="clearfix ds-container">
                            <div className="ds-panel-left">
                                <button type="button" onClick={this.showCompsModal} className="ds-button-left">Add</button>
                            </div>
                            <div className="ds-panel-right">
                                <button type="button" onClick={e => this.showConfirmDelete("comp")} className="ds-button-right">Delete</button>
                            </div>
                        </div>
                    </div>
                </div>
                <h4 className="ds-margin-top ds-sub-header ds-center">Default Database Link Creation Settings</h4>
                <hr />
                <Form horizontal>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The size limit of entries returned over a database link (nsslapd-sizelimit).">
                            Size Limit
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defSizeLimit" onChange={this.handleChange} defaultValue={this.state.defSizeLimit} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The maximum number of operations per connections. (nsconcurrentoperationslimit).">
                            Max Operations Per Conn
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defConcurOpLimit" onChange={this.handleChange} defaultValue={this.state.defConcurOpLimit} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The time limit of an operation over a database link (nsslapd-timelimit).">
                            Time Limit
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defTimeLimit" onChange={this.handleChange} defaultValue={this.state.defTimeLimit} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The maximum number of operations per connections. (nsconcurrentoperationslimit).">
                            Connection Lifetime
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defConnLife" onChange={this.handleChange} defaultValue={this.state.defConnLife} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The maximum number of TCP connections the database link establishes with the remote server.  (nsbindconnectionslimit).">
                            Max TCP Connections
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defBindConnLimit" onChange={this.handleChange} defaultValue={this.state.defBindConnLimit} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The number of seconds that pass before the server checks for abandoned operations.  (nsabandonedsearchcheckinterval).">
                            Abandoned Op Check Interval
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defSearchCheck" onChange={this.handleChange} defaultValue={this.state.defSearchCheck} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit).">
                            Max LDAP Connections
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defOpConnLimit" onChange={this.handleChange} defaultValue={this.state.defOpConnLimit} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The number of seconds that pass before the server checks for abandoned operations.  (nsabandonedsearchcheckinterval).">
                            Abandoned Op Check Interval
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defSearchCheck" onChange={this.handleChange} defaultValue={this.state.defSearchCheck} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit).">
                            Max Binds Per Connection
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defOpConnLimit" onChange={this.handleChange} defaultValue={this.state.defOpConnLimit} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The maximum number of times a request can be forwarded from one database link to another.  (nshoplimit).">
                            Database Link Hop Limit
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defHopLimit" onChange={this.handleChange} defaultValue={this.state.defHopLimit} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The amount of time before the bind attempt times out. (nsbindtimeout).">
                            Bind Timeout
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defBindTimeout" onChange={this.handleChange} defaultValue={this.state.defBindTimeout} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="The number of times the database link tries to bind with the remote server after a connection failure. (nsbindretrylimit).">
                            Bind Retry Limit
                        </Col>
                        <Col sm={8}>
                            <input className="ds-input-auto" type="text" id="defBindRetryLimit" onChange={this.handleChange} defaultValue={this.state.defBindRetryLimit} />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci).">
                            Check Local ACIs
                        </Col>
                        <Col sm={8}>
                            <input type="checkbox" onChange={this.handleChange} defaultChecked={this.state.defCheckAci} className="ds-config-checkbox" id="nsusdefCheckAciestarttls" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch).">
                            Send Referral On Scoped Search
                        </Col>
                        <Col sm={8}>
                            <input type="checkbox" onChange={this.handleChange} defaultChecked={this.state.defRefOnScoped} className="ds-config-checkbox" id="defRefOnScoped" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci).">
                            Allow Proxied Authentication
                        </Col>
                        <Col sm={8}>
                            <input type="checkbox" onChange={this.handleChange} defaultChecked={this.state.defProxy} className="ds-config-checkbox" id="defProxy" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4} title="Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch).">
                            Use StartTLS
                        </Col>
                        <Col sm={8}>
                            <input type="checkbox" onChange={this.handleChange} defaultChecked={this.state.defUseStartTLS} className="ds-config-checkbox" id="defUseStartTLS" />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top-lg">
                        <Col sm={5}>
                            <button className="btn btn-primary save-button" onClick={this.save_chaining_config}>Save Default Settings</button>
                        </Col>
                    </Row>
                </Form>

                <ChainControlsModal
                    showModal={this.state.showOidModal}
                    closeHandler={this.closeOidModal}
                    handleChange={this.handleOidChange}
                    saveHandler={this.saveOids}
                    oidList={this.state.availableOids}
                />
                <ChainCompsModal
                    showModal={this.state.showCompsModal}
                    closeHandler={this.closeCompsModal}
                    handleChange={this.handleCompsChange}
                    saveHandler={this.saveComps}
                    compList={this.state.availableComps}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmOidDelete}
                    closeHandler={this.closeConfirmOidDelete}
                    actionFunc={this.deleteOids}
                    msg="Are you sure you want to delete these OID's?"
                    msgContent={this.state.removeOids}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmCompDelete}
                    closeHandler={this.closeConfirmCompDelete}
                    actionFunc={this.deleteComps}
                    msg="Are you sure you want to delete these components?"
                    msgContent={this.state.removeComps}
                />
            </div>
        );
    }
}

//
// This is the component for the actual database link under a suffix
//
export class ChainingConfig extends React.Component {
    constructor(props) {
        super(props);

        if (this.props.data !== undefined) {
            this.state = {
                errObj: {},
                showDeleteConfirm: false,
                linkPwdMatch: true,
                modalSpinning: false,
                modalChecked: false,
                // Settings
                nsfarmserverurl: this.props.data.nsfarmserverurl,
                nsmultiplexorbinddn: this.props.data.nsmultiplexorbinddn,
                nsmultiplexorcredentials: this.props.data.nsmultiplexorcredentials,
                nsmultiplexorcredentials_confirm: this.props.data.nsmultiplexorcredentials,
                sizelimit: this.props.data.sizelimit,
                timelimit: this.props.data.timelimit,
                bindconnlimit: this.props.data.bindconnlimit,
                opconnlimit: this.props.data.opconnlimit,
                concurrbindlimit: this.props.data.concurrbindlimit,
                bindtimeout: this.props.data.bindtimeout,
                bindretrylimit: this.props.data.bindretrylimit,
                concurroplimit: this.props.data.concurroplimit,
                connlifetime: this.props.data.connlifetime,
                searchcheckinterval: this.props.data.searchcheckinterval,
                hoplimit: this.props.data.hoplimit,
                nsbindmechanism: this.props.data.nsbindmechanism,
                nsusestarttls: this.props.data.nsusestarttls,
                nsreferralonscopedsearch: this.props.data.nsreferralonscopedsearch,
                nsproxiedauthorization: this.props.data.nsproxiedauthorization,
                nschecklocalaci: this.props.data.nschecklocalaci,
                // Original settings
                _nsfarmserverurl: this.props.data.nsfarmserverurl,
                _nsmultiplexorbinddn: this.props.data.nsmultiplexorbinddn,
                _nsmultiplexorcredentials: this.props.data.nsmultiplexorcredentials,
                _nsmultiplexorcredentials_confirm: this.props.data.nsmultiplexorcredentials,
                _sizelimit: this.props.data.sizelimit,
                _timelimit: this.props.data.timelimit,
                _bindconnlimit: this.props.data.bindconnlimit,
                _opconnlimit: this.props.data.opconnlimit,
                _concurrbindlimit: this.props.data.concurrbindlimit,
                _bindtimeout: this.props.data.bindtimeout,
                _bindretrylimit: this.props.data.bindretrylimit,
                _concurroplimit: this.props.data.concurroplimit,
                _connlifetime: this.props.data.connlifetime,
                _searchcheckinterval: this.props.data.searchcheckinterval,
                _hoplimit: this.props.data.hoplimit,
                _nsbindmechanism: this.props.data.nsbindmechanism,
                _nsusestarttls: this.props.data.nsusestarttls,
                _nsreferralonscopedsearch: this.props.data.nsreferralonscopedsearch,
                _nsproxiedauthorization: this.props.data.nsproxiedauthorization,
                _nschecklocalaci: this.props.data.nschecklocalaci,
            };
        } else {
            this.state = {
                errObj: {},
                showDeleteConfirm: false,
                linkPwdMatch: true,
            };
        }
        this.handleChange = this.handleChange.bind(this);
        this.saveLink = this.saveLink.bind(this);
        this.deleteLink = this.deleteLink.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    showDeleteConfirm () {
        this.setState({
            showDeleteConfirm: true,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    closeDeleteConfirm () {
        this.setState({
            showDeleteConfirm: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    checkPasswords() {
        let pwdMatch = false;
        if (this.state.nsmultiplexorcredentials == this.state.nsmultiplexorcredentials_confirm) {
            pwdMatch = true;
        }
        this.setState({
            linkPwdMatch: pwdMatch
        });
    }

    handleChange (e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        let errObj = this.state.errObj;
        if (value == "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        }, this.checkPasswords);
    }

    saveLink() {
        let missingArgs = {};
        let errors = false;

        if (this.state.nsfarmserverurl == "") {
            this.props.addNotification(
                "warning",
                `Missing Remote Server LDAP URL`
            );
            missingArgs.nsfarmserverurl = true;
            errors = true;
        }
        if (this.state.nsmultiplexorbinddn == "") {
            this.props.addNotification(
                "warning",
                `Missing Remote Bind DN`
            );
            missingArgs.nsmultiplexorbinddn = true;
            errors = true;
        }
        if (this.state.nsmultiplexorcredentials == "") {
            this.props.addNotification(
                "warning",
                `Missing Remote Bind DN Password`
            );
            missingArgs.nsmultiplexorcredentials = true;
            errors = true;
        }
        if (this.state.nsmultiplexorcredentials_confirm == "") {
            this.props.addNotification(
                "warning",
                `Missing Remote Bind DN Password Confirmation`
            );
            missingArgs.nsmultiplexorcredentials_confirm = true;
            errors = true;
        }
        if (this.state.nsmultiplexorcredentials != this.state.nsmultiplexorcredentials_confirm) {
            this.props.addNotification(
                "warning",
                `Remote Bind DN Password Do Not Match`
            );
            missingArgs.nsmultiplexorcredentials = true;
            missingArgs.nsmultiplexorcredentials_confirm = true;
            errors = true;
        }
        if (errors) {
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        // Buld up the command of all the hcnge we have to do
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "link-set", this.props.suffix
        ];

        if (this.state.nsfarmserverurl != this.state._nsfarmserverurl) {
            cmd.push('--server-url=' + this.state.nsfarmserverurl);
        }
        if (this.state.nsmultiplexorbinddn != this.state._nsmultiplexorbinddn) {
            cmd.push('--bind-dn=' + this.state.nsmultiplexorbinddn);
        }
        if (this.state.nsmultiplexorcredentials != this.state._nsmultiplexorcredentials) {
            cmd.push('--bind-pw=' + this.state.nsmultiplexorcredentials);
        }
        if (this.state.timelimit != this.state._timelimit) {
            cmd.push('--time-limit=' + this.state.timelimit);
        }
        if (this.state.sizelimit != this.state._sizelimit) {
            cmd.push('--size-limit=' + this.state.sizelimit);
        }
        if (this.state.bindconnlimit != this.state._bindconnlimit) {
            cmd.push('--conn-bind-limit=' + this.state.bindconnlimit);
        }
        if (this.state.opconnlimit != this.state._opconnlimit) {
            cmd.push('--conn-op-limit=' + this.state.opconnlimit);
        }
        if (this.state.concurrbindlimit != this.state._concurrbindlimit) {
            cmd.push('--bind-limit=' + this.state.concurrbindlimit);
        }
        if (this.state.bindtimeout != this.state._bindtimeout) {
            cmd.push('--bind-timeout=' + this.state.bindtimeout);
        }
        if (this.state.bindretrylimit != this.state._bindretrylimit) {
            cmd.push('--bind-attempts=' + this.state.bindretrylimit);
        }
        if (this.state.concurroplimit != this.state._concurroplimit) {
            cmd.push('--op-limit=' + this.state.concurroplimit);
        }
        if (this.state.connlifetime != this.state._connlifetime) {
            cmd.push('--conn-lifetime=' + this.state.connlifetime);
        }
        if (this.state.searchcheckinterval != this.state._searchcheckinterval) {
            cmd.push('--abandon-check-interval=' + this.state.searchcheckinterval);
        }
        if (this.state.hoplimit != this.state._hoplimit) {
            cmd.push('--hop-limit=' + this.state.hoplimit);
        }
        if (this.state.nsbindmechanism != this.state._nsbindmechanism) {
            cmd.push('--bind-mech=' + this.state.nsbindmechanism);
        }

        if (this.state.nsusestarttls != this.state._nsusestarttls) {
            if (this.state.nsusestarttls) {
                cmd.push('--use-starttls=on');
            } else {
                cmd.push('--use-starttls=off');
            }
        }
        if (this.state.nsreferralonscopedsearch != this.state._nsreferralonscopedsearch) {
            if (this.state.nsreferralonscopedsearch) {
                cmd.push('--return-ref=on');
            } else {
                cmd.push('--return-ref=off');
            }
        }
        if (this.state.nsproxiedauthorization != this.state._nsproxiedauthorization) {
            if (this.state.nsproxiedauthorization) {
                cmd.push('--proxied-auth=on');
            } else {
                cmd.push('--proxied-auth=off');
            }
        }
        if (this.state.nschecklocalaci != this.state._nschecklocalaci) {
            if (this.state.nschecklocalaci) {
                cmd.push('--check-aci=on');
            } else {
                cmd.push('--check-aci=off');
            }
        }

        if (cmd.length > 6) {
            // Something changed, perform the update
            log_cmd("saveLink", "Save chaining link config", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "success",
                            `Successfully Updated Link Configuration`
                        );
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "error",
                            `Failed to update link configuration - ${errMsg.desc}`
                        );
                    });
        }
    }

    deleteLink() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "link-delete", this.props.suffix
        ];
        log_cmd("deleteLink", "Delete database chaining link", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.loadSuffixTree(true);
                    this.props.addNotification(
                        "success",
                        `Successfully Deleted Database Link`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.loadSuffixTree(true);
                    this.props.addNotification(
                        "error",
                        `Failed to delete database link - ${errMsg.desc}`
                    );
                });
    }

    render () {
        const error = this.state.errObj;

        return (
            <div>
                <Row>
                    <Col sm={10} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header">
                            <Icon type="fa" name="link" /> <b>{this.props.suffix}</b> (<i>{this.props.bename}</i>)
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh database link"
                                onClick={() => this.props.reload(this.props.suffix)}
                            />
                        </ControlLabel>
                    </Col>
                    <Col sm={2}>
                        <Button
                            bsStyle="danger"
                            onClick={this.showDeleteConfirm}
                        >
                            Delete Link
                        </Button>
                    </Col>
                </Row>
                <Form horizontal autoComplete="off" className="ds-margin-top-xlg">
                    <Row title="The LDAP URL for the remote server.  Add additional failure server URLs by separating them with a space. (nsfarmserverurl)">
                        <Col componentClass={ControlLabel} sm={4}>
                            Remote Server LDAP URL
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="nsfarmserverurl"
                                className="ds-input-auto"
                                onChange={this.handleChange}
                                defaultValue={this.state.nsfarmserverurl}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The distinguished name (DN) of the entry to authenticate to the remote server. (nsmultiplexorbinddn)">
                        <Col componentClass={ControlLabel} sm={4}>
                            Remote Server Bind DN
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="nsmultiplexorbinddn"
                                className="ds-input-auto"
                                onChange={this.handleChange}
                                defaultValue={this.state.nsmultiplexorbinddn}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The password for the authenticating entry. (nsmultiplexorcredentials)">
                        <Col componentClass={ControlLabel} sm={4}>
                            Bind DN Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="password"
                                id="nsmultiplexorcredentials"
                                className={(error.nsmultiplexorcredentials || !this.state.linkPwdMatch) ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={this.handleChange}
                                defaultValue={this.state.nsmultiplexorcredentials}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Confirm the password for the authenticating entry. (nsmultiplexorcredentials)">
                        <Col componentClass={ControlLabel} sm={4}>
                            Confirm Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="password"
                                id="nsmultiplexorcredentials_confirm"
                                className={(error.nsmultiplexorcredentials_confirm || !this.state.linkPwdMatch) ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={this.handleChange}
                                defaultValue={this.state.nsmultiplexorcredentials_confirm}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The authentication mechanism.  Simple (user name and password), SASL/DIGEST-MD5, or SASL>GSSAPI. (nsbindmechanism)">
                        <Col componentClass={ControlLabel} sm={4}>
                            Bind Mechanism
                        </Col>
                        <Col sm={8}>
                            <select value={this.state.nsbindmechanism}
                                className="btn btn-default dropdown ds-dblink-dropdown"
                                onChange={this.handleChange}
                                id="nsbindmechanism"
                            >
                                <option>Simple</option>
                                <option>SASL/DIGEST-MD5</option>
                                <option>SASL/GSSAPI</option>
                            </select>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Use StartTLS for connections to the remote server. (nsusestarttls)">
                        <Col componentClass={ControlLabel} sm={4}>
                            Use StartTLS
                        </Col>
                        <Col sm={8}>
                            <input type="checkbox" onChange={this.props.handleChange} defaultChecked={this.state.nsusestarttls} className="ds-config-checkbox" id="nsusestarttls" />
                        </Col>
                    </Row>
                </Form>

                <CustomCollapse className="ds-margin-top">
                    <Form horizontal className="ds-margin-top ds-margin-left">
                        <Row className="ds-margin-top" title="The size limit of entries returned over a database link (nsslapd-sizelimit).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Size Limit
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="sizelimit"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.sizelimit}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The time limit of an operation over a database link (nsslapd-timelimit).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Time Limit
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="timelimit"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.timelimit}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The maximum number of TCP connections the database link establishes with the remote server.  (nsbindconnectionslimit).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Max TCP Connections
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="bindconnlimit"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.bindconnlimit}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Max LDAP Connections
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="opconnlimit"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.opconnlimit}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The maximum number of concurrent bind operations per TCP connection. (nsconcurrentbindlimit).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Max Binds Per Connection
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="concurrbindlimit"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.concurrbindlimit}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The amount of time before the bind attempt times out. (nsbindtimeout).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Bind Timeout
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="bindtimeout"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.bindtimeout}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The number of times the database link tries to bind with the remote server after a connection failure. (nsbindretrylimit).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Bind Retry Limit
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="bindretrylimit"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.bindretrylimit}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The maximum number of operations per connections. (nsconcurrentoperationslimit).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Max Operations Per Connection
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="concurroplimit"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.concurroplimit}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The life of a database link connection to the remote server in seconds.  0 is unlimited  (nsconnectionlife).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Connection Lifetime
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="connlifetime"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.connlifetime}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The number of seconds that pass before the server checks for abandoned operations.  (nsabandonedsearchcheckinterval).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Abandoned Op Check Interval
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="searchcheckinterval"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.searchcheckinterval}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The maximum number of times a request can be forwarded from one database link to another.  (nshoplimit).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Hop Limit
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    id="hoplimit"
                                    className="ds-input-auto"
                                    onChange={this.handleChange}
                                    defaultValue={this.state.hoplimit}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="Allow proxied authentication to the remote server. (nsproxiedauthorization).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Allow Proxied Authentication
                            </Col>
                            <Col sm={8}>
                                <input type="checkbox" onChange={this.props.handleChange} defaultChecked={this.state.nsproxiedauthorization} className="ds-config-checkbox" id="nsproxiedauthorization" />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Check Local ACIs
                            </Col>
                            <Col sm={8}>
                                <input type="checkbox" onChange={this.props.handleChange} defaultChecked={this.state.nschecklocalaci} className="ds-config-checkbox" id="nschecklocalaci" />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Send Referral On Scoped Search
                            </Col>
                            <Col sm={8}>
                                <input type="checkbox" onChange={this.props.handleChange} defaultChecked={this.state.nsreferralonscopedsearch} className="ds-config-checkbox" id="nsreferralonscopedsearch" />
                            </Col>
                        </Row>
                    </Form>
                    <hr />
                </CustomCollapse>
                <div className="ds-margin-top-lg">
                    <button onClick={this.saveLink} className="btn btn-primary">Save Configuration</button>
                </div>
                <DoubleConfirmModal
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    handleChange={this.handleChange}
                    actionHandler={this.deleteLink}
                    spinning={this.state.modalSpinning}
                    item={this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle="Delete Database Link"
                    mMsg="Are you really sure you want to delete this database link?"
                    mSpinningMsg="Deleting Database Link..."
                    mBtnName="Delete Database Link"
                />
            </div>
        );
    }
}

//
// Chaining modals
//

export class ChainControlsModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            oidList
        } = this.props;

        const oids = oidList.map((oid) =>
            <option key={oid} value={oid}>{oid}</option>
        );

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Chaining LDAP Controls
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <label className="ds-config-label" htmlFor="avail-chaining-oid-list" title="A list of LDAP control OIDs to be forwarded through chaining">Available LDAP Controls</label>
                            <div>
                                <select id="avail-chaining-oid-list" onChange={handleChange} className="ds-width-auto" size="10" multiple>
                                    {oids}
                                </select>
                            </div>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Add & Save New Controls
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

export class ChainCompsModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            compList
        } = this.props;

        const comps = compList.map((comp) =>
            <option key={comp} value={comp}>{comp}</option>
        );

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Chaining Components
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <label className="ds-config-label" htmlFor="avail-chaining-comp-list" title="A list of LDAP control OIDs to be forwarded through chaining">Available Components</label>
                            <div>
                                <select id="avail-chaining-comp-list" onChange={handleChange} className="ds-width-auto" size="10" multiple>
                                    {comps}
                                </select>
                            </div>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Add & Save New Components
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

// Property types and defaults

ChainCompsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    compList: PropTypes.array,
};

ChainCompsModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    compList: [],
};

ChainControlsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    oidList: PropTypes.array,
};

ChainControlsModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    oidList: [],
};

ChainingDatabaseConfig.propTypes = {
    serverId: PropTypes.string,
    addNotification: PropTypes.func,
    reload: PropTypes.func,
    data: PropTypes.object,
    enableTree: PropTypes.func,
};

ChainingDatabaseConfig.defaultProps = {
    serverId: "",
    addNotification: noop,
    reload: noop,
    data: {},
    enableTree: PropTypes.noop,
};

ChainingConfig.propTypes = {
    serverId: PropTypes.string,
    suffix: PropTypes.string,
    bename: PropTypes.string,
    loadSuffixTree: PropTypes.func,
    addNotification: PropTypes.func,
    data: PropTypes.object,
    reload: PropTypes.func,
    enableTree: PropTypes.func,
};

ChainingConfig.defaultProps = {
    serverId: "",
    suffix: "",
    bename: "",
    loadSuffixTree: noop,
    addNotification: noop,
    data: {},
    reload: noop,
    enableTree: noop,
};
