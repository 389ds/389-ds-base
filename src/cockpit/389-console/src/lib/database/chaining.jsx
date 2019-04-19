import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup } from "../notifications.jsx";
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
    Checkbox,
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
            <div className="container-fluid" id="db-global-page">
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

                <h4 className="ds-sub-header"><br />Default Database Link Creation Settings</h4>
                <hr />
                <div className="ds-container">
                    <div className="ds-split">
                        <label htmlFor="defSizeLimit" className="ds-config-label" title="The size limit of entries returned over a database link (nsslapd-sizelimit).">
                            Size Limit</label><input className="ds-input" type="text" id="defSizeLimit" onChange={this.handleChange} value={this.state.defSizeLimit} size="15" />
                        <label htmlFor="defTimeLimit" className="ds-config-label" title="The time limit of an operation over a database link (nsslapd-timelimit).">
                            Time Limit</label><input className="ds-input" type="text" id="defTimeLimit" onChange={this.handleChange} value={this.state.defTimeLimit} size="15" />
                        <label htmlFor="defBindConnLimit" className="ds-config-label" title="The maximum number of TCP connections the database link establishes with the remote server.  (nsbindconnectionslimit).">
                            Max TCP Connections</label><input className="ds-input" id="defBindConnLimit" type="text" onChange={this.handleChange} value={this.state.defBindConnLimit} size="15" />
                        <label htmlFor="defOpConnLimit" className="ds-config-label" title="The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit).">
                            Max LDAP Connections</label><input className="ds-input" id="defOpConnLimit" type="text" onChange={this.handleChange} value={this.state.defOpConnLimit} size="15" />
                        <label htmlFor="defConcurLimit" className="ds-config-label" title="The maximum number of concurrent bind operations per TCP connection. (nsconcurrentbindlimit).">
                            Max Binds Per Connection</label><input className="ds-input" id="defConcurLimit" type="text" onChange={this.handleChange} value={this.state.defConcurLimit} size="15" />
                        <label htmlFor="defBindTimeout" className="ds-config-label" title="The amount of time before the bind attempt times out. (nsbindtimeout).">
                            Bind Timeout</label><input className="ds-input" id="defBindTimeout" type="text" onChange={this.handleChange} value={this.state.defBindTimeout} size="15" />
                        <label htmlFor="defBindRetryLimit" className="ds-config-label" title="The number of times the database link tries to bind with the remote server after a connection failure. (nsbindretrylimit).">
                            Bind Retry Limit</label><input className="ds-input" id="defBindRetryLimit" type="text" onChange={this.handleChange} value={this.state.defBindRetryLimit} size="15" />
                    </div>
                    <div className="ds-divider" />
                    <div className="ds-split ds-inline">
                        <div>
                            <label htmlFor="defConcurOpLimit" className="ds-config-label" title="The maximum number of operations per connections. (nsconcurrentoperationslimit).">
                                Max Operations Per Connection</label><input className="ds-input" id="defConcurOpLimit" type="text" onChange={this.handleChange} value={this.state.defConcurOpLimit} size="15" />
                        </div>
                        <div>
                            <label htmlFor="defConnLife" className="ds-config-label" title="The life of a database link connection to the remote server.  0 is unlimited  (nsconnectionlife).">
                                Connection Lifetime (in seconds)</label><input className="ds-input" id="defConnLife" type="text" onChange={this.handleChange} value={this.state.defConnLife} size="15" />
                        </div>
                        <div>
                            <label htmlFor="defSearchCheck" className="ds-config-label" title="The number of seconds that pass before the server checks for abandoned operations.  (nsabandonedsearchcheckinterval).">
                                Abandoned Op Check Interval</label><input className="ds-input" id="defSearchCheck" type="text" onChange={this.handleChange} value={this.state.defSearchCheck} size="15" />
                        </div>
                        <div>
                            <label htmlFor="defHopLimit" className="ds-config-label" title="The maximum number of times a request can be forwarded from one database link to another.  (nshoplimit).">
                                Database Link Hop Limit</label><input className="ds-input" type="text" onChange={this.handleChange} value={this.state.defHopLimit} id="defHopLimit" size="15" />
                        </div>
                        <div>
                            <p />
                            <input type="checkbox" onChange={this.handleChange} checked={this.state.defCheckAci} className="ds-config-checkbox" id="defCheckAci" /><label
                                htmlFor="defCheckAci" className="ds-label" title="Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci)."> Check Local ACIs</label>
                        </div>
                        <div>
                            <input type="checkbox" onChange={this.handleChange} checked={this.state.defRefOnScoped} className="ds-config-checkbox" id="defRefOnScoped" /><label
                                htmlFor="defRefOnScoped" className="ds-label" title="Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch)."> Send Referral On Scoped Search</label>
                        </div>
                        <div>
                            <input type="checkbox" onChange={this.handleChange} checked={this.state.defProxy} className="ds-config-checkbox" id="defProxy" /><label
                                htmlFor="defProxy" className="ds-label" title="Sets whether proxied authentication is allowed (nsproxiedauthorization)."> Allow Proxied Authentication</label>
                        </div>
                        <div>
                            <input type="checkbox" onChange={this.handleChange} checked={this.state.defUseStartTLS} className="ds-config-checkbox" id="defUseStartTLS" /><label
                                htmlFor="defUseStartTLS" className="ds-label" title="Sets whether to use Start TLS to initiate a secure, encrypted connection over an insecure port.  (nsusestarttls)."> Use StartTLS</label>
                        </div>
                    </div>
                </div>
                <div className="ds-save-btn">
                    <button className="btn btn-primary save-button" onClick={this.save_chaining_config}>Save Default Settings</button>
                </div>

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

    showDeleteConfirm () {
        this.setState({
            showDeleteConfirm: true
        });
    }

    closeDeleteConfirm () {
        this.setState({
            showDeleteConfirm: false
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
        let useStartTLSCheckBox;
        let checkLocalAci;
        let referralOnScope;
        let proxiedAuth;

        // Check local aci's checkbox
        if (this.state.nschecklocalaci) {
            checkLocalAci =
                <Checkbox id="nschecklocalaci" onChange={this.handleChange} key={this.state.nschecklocalaci} defaultChecked
                    title="Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci).">
                    Check Local ACIs
                </Checkbox>;
        } else {
            checkLocalAci =
                <Checkbox id="nschecklocalaci" onChange={this.handleChange} key={this.state.nschecklocalaci}
                    title="Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci).">
                    Check Local ACIs
                </Checkbox>;
        }
        // Referral on scoped search checkbox
        if (this.state.nsreferralonscopedsearch) {
            referralOnScope =
                <Checkbox id="nsreferralonscopedsearch" onChange={this.handleChange} defaultChecked
                    title="Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch).">
                    Send Referral On Scoped Search
                </Checkbox>;
        } else {
            referralOnScope =
                <Checkbox id="nsreferralonscopedsearch" onChange={this.handleChange}
                    title="Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch).">
                    Send Referral On Scoped Search
                </Checkbox>;
        }
        // Allow proxied auth checkbox
        if (this.state.nsproxiedauthorization) {
            proxiedAuth =
                <Checkbox id="nsproxiedauthorization" onChange={this.handleChange} defaultChecked
                    title="Allow proxied authentication to the remote server. (nsproxiedauthorization).">
                    Allow Proxied Authentication
                </Checkbox>;
        } else {
            proxiedAuth =
                <Checkbox id="nsproxiedauthorization" onChange={this.handleChange} defaultChecked
                    title="Allow proxied authentication to the remote server. (nsproxiedauthorization).">
                    Allow Proxied Authentication
                </Checkbox>;
        }
        // use startTLS checkbox
        if (this.state.nsusestarttls) {
            useStartTLSCheckBox =
                <Checkbox id="nsusestarttls" onChange={this.handleChange} title="Use StartTLS for connection to remote server. (nsusestarttls)"
                    defaultChecked
                >
                    Use StartTLS for remote connection
                </Checkbox>;
        } else {
            useStartTLSCheckBox =
                <Checkbox id="nsusestarttls" onChange={this.handleChange} title="Use StartTLS for connection to remote server. (nsusestarttls)">
                    Use StartTLS for remote connection
                </Checkbox>;
        }

        return (
            <div className="container-fluid">
                <Row>
                    <Col sm={8} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header"><Icon type="fa" name="link" /> {this.props.suffix} (<i>{this.props.bename}</i>)</ControlLabel>
                    </Col>
                    <Col sm={2}>
                        <Button
                            bsStyle="primary"
                            onClick={this.showDeleteConfirm}
                        >
                            Delete Link
                        </Button>
                    </Col>
                </Row>
                <h4>Database Link Configuration</h4>
                <hr />
                <Form horizontal autoComplete="off">
                    <Row title="The LDAP URL for the remote server.  Add additional failure server URLs by separating them with a space. (nsfarmserverurl)">
                        <Col sm={3}>
                            <ControlLabel>Remote Server LDAP URL(s)</ControlLabel>
                        </Col>
                        <Col sm={7}>
                            <FormControl
                                type="text"
                                id="nsfarmserverurl"
                                className="ds-input-auto"
                                onChange={this.handleChange}
                                defaultValue={this.state.nsfarmserverurl}
                            />
                        </Col>
                    </Row>
                    <p />
                    <Row title="The distinguished name (DN) of the entry to authenticate to the remote server. (nsmultiplexorbinddn)">
                        <Col sm={3}>
                            <ControlLabel>Remote Server Bind DN</ControlLabel>
                        </Col>
                        <Col sm={7}>
                            <FormControl
                                type="text"
                                id="nsmultiplexorbinddn"
                                className="ds-input-auto"
                                onChange={this.handleChange}
                                defaultValue={this.state.nsmultiplexorbinddn}
                            />
                        </Col>
                    </Row>
                    <p />
                    <Row title="The password for the authenticating entry. (nsmultiplexorcredentials)">
                        <Col sm={3}>
                            <ControlLabel>Bind DN Password</ControlLabel>
                        </Col>
                        <Col sm={7}>
                            <FormControl
                                type="password"
                                id="nsmultiplexorcredentials"
                                className={(error.nsmultiplexorcredentials || !this.state.linkPwdMatch) ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={this.handleChange}
                                defaultValue={this.state.nsmultiplexorcredentials}
                            />
                        </Col>
                    </Row>
                    <p />
                    <Row title="Confirm the password for the authenticating entry. (nsmultiplexorcredentials)">
                        <Col sm={3}>
                            <ControlLabel>Confirm Password</ControlLabel>
                        </Col>
                        <Col sm={7}>
                            <FormControl
                                type="password"
                                id="nsmultiplexorcredentials_confirm"
                                className={(error.nsmultiplexorcredentials_confirm || !this.state.linkPwdMatch) ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={this.handleChange}
                                defaultValue={this.state.nsmultiplexorcredentials_confirm}
                            />
                        </Col>
                    </Row>
                    <p />
                    <Row title="THe authentication mechanism.  Simple (user name and password), SASL/DIGEST-MD5, or SASL>GSSAPI. (nsbindmechanism)">
                        <Col sm={3}>
                            <ControlLabel>Bind Mechanism</ControlLabel>
                        </Col>
                        <Col sm={7}>
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
                    <p />
                    <Row>
                        <Col sm={9}>
                            {useStartTLSCheckBox}
                        </Col>
                    </Row>
                </Form>
                <p />

                <CustomCollapse>
                    <div className="ds-accordion-panel">
                        <div className="ds-margin-left">
                            <div className="ds-container">
                                <div className="ds-inline">
                                    <div>
                                        <label htmlFor="sizelimit" className="ds-config-label" title="The size limit of entries returned over a database link (nsslapd-sizelimit).">
                                            Size Limit</label><input onChange={this.handleChange} defaultValue={this.state.sizelimit} className="ds-input" type="text" id="sizelimit" size="15" />
                                    </div>
                                    <div>
                                        <label htmlFor="timelimit" className="ds-config-label" title="The time limit of an operation over a database link (nsslapd-timelimit).">
                                            Time Limit</label><input onChange={this.handleChange} defaultValue={this.state.timelimit} className="ds-input" type="text" id="timelimit" size="15" />
                                    </div>
                                    <div>
                                        <label htmlFor="bindconnlimit" className="ds-config-label" title="The maximum number of TCP connections the database link establishes with the remote server.  (nsbindconnectionslimit).">
                                            Max TCP Connections</label><input onChange={this.handleChange} defaultValue={this.state.bindconnlimit} className="ds-input" type="text" id="bindconnlimit" size="15" />
                                    </div>
                                    <div>
                                        <label htmlFor="opconnlimit" className="ds-config-label" title="The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit).">
                                            Max LDAP Connections</label><input onChange={this.handleChange} defaultValue={this.state.opconnlimit} className="ds-input" type="text" id="opconnlimit" size="15" />
                                    </div>
                                    <div>
                                        <label htmlFor="concurrbindlimit" className="ds-config-label" title="The maximum number of concurrent bind operations per TCP connection. (nsconcurrentbindlimit).">
                                            Max Binds Per Connection</label><input onChange={this.handleChange} defaultValue={this.state.concurrbindlimit} className="ds-input" type="text" id="concurrbindlimit" size="15" />
                                    </div>
                                    <div>
                                        <label htmlFor="bindtimeout" className="ds-config-label" title="The amount of time before the bind attempt times out. (nsbindtimeout).">
                                            Bind Timeout</label><input onChange={this.handleChange} defaultValue={this.state.bindtimeout} className="ds-input" type="text" id="bindtimeout" size="15" />
                                    </div>
                                </div>
                                <div className="ds-divider" />
                                <div className="ds-inline">
                                    <div>
                                        <label htmlFor="bindretrylimit" className="ds-config-label" title="The number of times the database link tries to bind with the remote server after a connection failure. (nsbindretrylimit).">
                                            Bind Retry Limit</label><input onChange={this.handleChange} defaultValue={this.state.bindretrylimit} className="ds-input" type="text" id="bindretrylimit" size="15" />
                                    </div>
                                    <div>
                                        <label htmlFor="concurroplimit" className="ds-config-label" title="The maximum number of operations per connections. (nsconcurrentoperationslimit).">
                                            Max Operations Per Connection</label><input onChange={this.handleChange} defaultValue={this.state.concurroplimit} className="ds-input" type="text" id="concurroplimit" size="15" />
                                    </div>
                                    <div>
                                        <label htmlFor="connlifetime" className="ds-config-label" title="The life of a database link connection to the remote server.  0 is unlimited  (nsconnectionlife).">
                                            Connection Lifetime (in seconds)</label><input onChange={this.handleChange} defaultValue={this.state.connlifetime} className="ds-input" type="text" id="connlifetime" size="15" />
                                    </div>
                                    <div>
                                        <label htmlFor="searchcheckinterval" className="ds-config-label" title="The number of seconds that pass before the server checks for abandoned operations.  (nsabandonedsearchcheckinterval).">
                                            Abandoned Op Check Interval</label><input onChange={this.handleChange} defaultValue={this.state.searchcheckinterval} className="ds-input" type="text" id="searchcheckinterval" size="15" />
                                    </div>
                                    <div>
                                        <label htmlFor="hoplimit" className="ds-config-label" title="The maximum number of times a request can be forwarded from one database link to another.  (nshoplimit).">
                                            Database Link Hop Limit</label><input onChange={this.handleChange} defaultValue={this.state.hoplimit} className="ds-input" type="text" id="hoplimit" size="15" />
                                    </div>
                                </div>
                            </div>
                            <p />
                            <div>
                                {proxiedAuth}
                            </div>
                            <div>
                                {checkLocalAci}
                            </div>
                            <div>
                                {referralOnScope}
                            </div>
                        </div>
                    </div>
                    <hr />
                </CustomCollapse>
                <div className="ds-save-btn">
                    <button onClick={this.saveLink} className="btn btn-primary">Save Configuration</button>
                </div>
                <ConfirmPopup
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    actionFunc={this.deleteLink}
                    actionParam={this.props.suffix}
                    msg="Are you really sure you want to delete this database link?"
                    msgContent={this.props.suffix}
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
                                <select id="avail-chaining-oid-list" onChange={handleChange} className="ds-chaining-form-list" size="10" multiple>
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
                                <select id="avail-chaining-comp-list" onChange={handleChange} className="ds-chaining-form-list" size="10" multiple>
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
};

ChainingDatabaseConfig.defaultProps = {
    serverId: "",
    addNotification: noop,
    reload: noop,
    data: {},
};

ChainingConfig.propTypes = {
    serverId: PropTypes.string,
    suffix: PropTypes.string,
    bename: PropTypes.string,
    loadSuffixTree: PropTypes.func,
    addNotification: PropTypes.func,
    data: PropTypes.object,
    reload: PropTypes.func,
};

ChainingConfig.defaultProps = {
    serverId: "",
    suffix: "",
    bename: "",
    loadSuffixTree: noop,
    addNotification: noop,
    data: {},
    reload: noop,
};
