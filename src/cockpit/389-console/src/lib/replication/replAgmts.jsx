import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup, DoubleConfirmModal } from "../notifications.jsx";
import { ReplAgmtTable } from "./replTables.jsx";
import { ReplAgmtModal } from "./replModals.jsx";
import { log_cmd, valid_dn, valid_port } from "../tools.jsx";
import PropTypes from "prop-types";
import {
    noop
} from "patternfly-react";
import {
    Button
} from "@patternfly/react-core";
import {
    SortByDirection,
} from '@patternfly/react-table';

export class ReplAgmts extends React.Component {
    _mounted = false;
    constructor(props) {
        super(props);
        this.state = {
            showCreateAgmtModal: false,
            showEditAgmtModal: false,
            showConfirmDeleteAgmt: false,
            showConfirmInitAgmt: false,
            showConfirmEnableAgmt: false,
            showConfirmDisableAgmt: false,
            errObj: {},
            modalMsg: "",
            modalScheduleMsg: "",
            savingAgmt: false,
            mounted: false,
            rows: [],
            page: 1,
            value: "",
            sortBy: {},
            // Create agreement
            agmtName: "",
            agmtHost: "",
            agmtPort: "",
            agmtProtocol: "LDAP",
            agmtBindMethod: "SIMPLE",
            agmtBindDN: "",
            agmtBindPW: "",
            agmtBindPWConfirm: "",
            agmtBootstrap: false,
            agmtBootstrapProtocol: "LDAP",
            agmtBootstrapBindMethod: "SIMPLE",
            agmtBootstrapBindDN: "",
            agmtBootstrapBindPW: "",
            agmtBootstrapBindPWConfirm: "",
            agmtStripAttrs: [],
            agmtFracAttrs: [],
            agmtFracInitAttrs: [],
            agmtSync: true,
            agmtSyncMon: true,
            agmtSyncTue: true,
            agmtSyncWed: true,
            agmtSyncThu: true,
            agmtSyncFri: true,
            agmtSyncSat: true,
            agmtSyncSun: true,
            agmtStartTime: "0",
            agmtEndTime: "0",
            agmtInit: "noinit",
            agmtSaveOK: false,
            modalChecked: false,
            modalSpinning: false,
            // Init agmt
            agmtInitCounter: 0,
            agmtInitIntervals: [],

            isExcludeAttrsCreateOpen: false,
            isExcludeInitAttrsCreateOpen: false,
            isStripAttrsCreateOpen: false,
            isExcludeAttrsEditOpen: false,
            isExcludeInitAttrsEditOpen: false,
            isStripAttrsEditOpen: false,
        };

        // Create - Exclude Attributes
        this.onExcludeAttrsCreateToggle = isExcludeAttrsCreateOpen => {
            this.setState({
                isExcludeAttrsCreateOpen
            });
        };
        this.onExcludeAttrsCreateClear = () => {
            this.setState({
                agmtFracAttrs: [],
                isExcludeAttrsCreateOpen: false
            });
        };

        // Create - Exclude Init Attributes
        this.onExcludeAttrsInitCreateToggle = isExcludeInitAttrsCreateOpen => {
            this.setState({
                isExcludeInitAttrsCreateOpen
            });
        };
        this.onExcludeAttrsInitCreateClear = () => {
            this.setState({
                agmtFracInitAttrs: [],
                isExcludeInitAttrsCreateOpen: false
            });
        };

        // Create - Skip Attributes
        this.onStripAttrsCreateToggle = isStripAttrsCreateOpen => {
            this.setState({
                isStripAttrsCreateOpen
            });
        };
        this.onStripAttrsCreateClear = () => {
            this.setState({
                agmtStripAttrs: [],
                isStripAttrsCreateOpen: false
            });
        };

        // Edit - Exclude Attributes
        this.onExcludeAttrsEditToggle = isExcludeAttrsEditOpen => {
            this.setState({
                isExcludeAttrsEditOpen
            });
        };
        this.onExcludeAttrsEditClear = () => {
            this.setState({
                agmtFracAttrs: [],
                isExcludeAttrsEditOpen: false
            });
        };

        // Edit - Exclude Init Attributes
        this.onExcludeAttrsInitEditToggle = isExcludeInitAttrsEditOpen => {
            this.setState({
                isExcludeInitAttrsEditOpen
            });
        };
        this.onExcludeAttrsInitEditClear = () => {
            this.setState({
                agmtFracInitAttrs: [],
                isExcludeInitAttrsEditOpen: false
            });
        };

        // Edit - Skip Attributes
        this.onStripAttrsEditToggle = isStripAttrsEditOpen => {
            this.setState({
                isStripAttrsEditOpen
            });
        };
        this.onStripAttrsEditClear = () => {
            this.setState({
                agmtStripAttrs: [],
                isStripAttrsEditOpen: false
            });
        };

        this.showCreateAgmtModal = this.showCreateAgmtModal.bind(this);
        this.closeCreateAgmtModal = this.closeCreateAgmtModal.bind(this);
        this.closeEditAgmtModal = this.closeEditAgmtModal.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.handleTAFracInitAttrChange = this.handleTAFracInitAttrChange.bind(this);
        this.handleTAFracAttrChange = this.handleTAFracAttrChange.bind(this);
        this.handleTAStripAttrChange = this.handleTAStripAttrChange.bind(this);
        this.handleTAFracInitAttrChangeEdit = this.handleTAFracInitAttrChangeEdit.bind(this);
        this.handleTAFracAttrChangeEdit = this.handleTAFracAttrChangeEdit.bind(this);
        this.handleTAStripAttrChangeEdit = this.handleTAStripAttrChangeEdit.bind(this);
        this.createAgmt = this.createAgmt.bind(this);
        this.showEditAgmt = this.showEditAgmt.bind(this);
        this.saveAgmt = this.saveAgmt.bind(this);
        this.pokeAgmt = this.pokeAgmt.bind(this);
        this.initAgmt = this.initAgmt.bind(this);
        this.enableAgmt = this.enableAgmt.bind(this);
        this.disableAgmt = this.disableAgmt.bind(this);
        this.deleteAgmt = this.deleteAgmt.bind(this);
        this.showConfirmDeleteAgmt = this.showConfirmDeleteAgmt.bind(this);
        this.closeConfirmDeleteAgmt = this.closeConfirmDeleteAgmt.bind(this);
        this.showConfirmInitAgmt = this.showConfirmInitAgmt.bind(this);
        this.closeConfirmInitAgmt = this.closeConfirmInitAgmt.bind(this);
        this.confirmToggle = this.confirmToggle.bind(this);
        this.closeConfirmEnableAgmt = this.closeConfirmEnableAgmt.bind(this);
        this.closeConfirmDisableAgmt = this.closeConfirmDisableAgmt.bind(this);
        this.watchAgmtInit = this.watchAgmtInit.bind(this);
        // Table sort and search
        this.onSort = this.onSort.bind(this);
        this.onSearchChange = this.onSearchChange.bind(this);
    }

    componentDidMount () {
        this._mounted = true;
        let rows = JSON.parse(JSON.stringify(this.props.rows));
        this.setState({rows: rows});
    }

    componentWillUnmount () {
        this._mounted = false;
    }

    listEqual(old_values, new_values) {
        if (old_values.length != new_values.length) {
            return false;
        }
        for (let i = old_values.length; i--;) {
            if (old_values[i] != new_values[i]) {
                return false;
            }
        }
        return true;
    }

    handleModalChange (e) {
        this.setState({
            [e.target.id]: e.target.checked,
        });
    }

    handleChange (e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let time_val = "";
        let valueErr = false;
        let errObj = this.state.errObj;
        let all_good = true;
        let modal_msg = "";
        let modal_schedule_msg = "";
        let edit = false;
        if (value == "") {
            valueErr = true;
        }
        errObj[attr] = valueErr;
        if (e.target.name == "agmt-modal-edit") {
            let orig_attr = "_" + e.target.id;
            all_good = false;
            if ((attr != 'agmtHost' && this.state.agmtHost != this.state._agmtHost) ||
                (attr != 'agmtPort' && this.state.agmtPort != this.state._agmtPort) ||
                (attr != 'agmtBindDN' && this.state.agmtBindDN != this.state._agmtBindDN) ||
                (attr != 'agmtBindMethod' && this.state.agmtBindMethod != this.state._agmtBindMethod) ||
                (attr != 'agmtProtocol' && this.state.agmtProtocol != this.state._agmtProtocol) ||
                (attr != 'agmtSync' && this.state.agmtSync != this.state._agmtSync) ||
                (attr != 'agmtBootstrap' && this.state.agmtBootstrap != this.state._agmtBootstrap) ||
                (attr != 'agmtStripAttrs' && !this.listEqual(this.state.agmtStripAttrs, this.state._agmtStripAttrs)) ||
                (attr != 'agmtFracAttrs' && !this.listEqual(this.state.agmtFracAttrs, this.state._agmtFracAttrs)) ||
                (attr != 'agmtFracInitAttrs' && !this.listEqual(this.state.agmtFracInitAttrs, this.state._agmtFracInitAttrs))) {
                all_good = true;
            }
            if (!this.state._agmtSync) {
                if ((attr != 'agmtSyncMon' && this.state.agmtSyncMon != this.state._agmtSyncMon) ||
                    (attr != 'agmtSyncTue' && this.state.agmtSyncTue != this.state._agmtSyncTue) ||
                    (attr != 'agmtSyncWed' && this.state.agmtSyncWed != this.state._agmtSyncWed) ||
                    (attr != 'agmtSyncThu' && this.state.agmtSyncThu != this.state._agmtSyncThu) ||
                    (attr != 'agmtSyncFri' && this.state.agmtSyncFri != this.state._agmtSyncFri) ||
                    (attr != 'agmtSyncSat' && this.state.agmtSyncSat != this.state._agmtSyncSat) ||
                    (attr != 'agmtSyncSun' && this.state.agmtSyncSun != this.state._agmtSyncSun)) {
                    all_good = true;
                }
            }
            if (this.state._agmtBootstrap) {
                if ((attr != 'agmtBootstrapBindDN' && this.state.agmtBootstrapBindDN != this.state._agmtBootstrapBindDN) ||
                    (attr != 'agmtBootstrapBindPW' && this.state.agmtBootstrapBindPW != this.state._agmtBootstrapBindPW) ||
                    (attr != 'agmtBootstrapBindPWConfirm' && this.state.agmtBootstrapBindPWConfirm != this.state._agmtBootstrapBindPWConfirm) ||
                    (attr != 'agmtBootstrapBindMethod' && this.state.agmtBootstrapBindMethod != this.state._agmtBootstrapBindMethod) ||
                    (attr != 'agmtBootstrapProtocol' && this.state.agmtBootstrapProtocol != this.state._agmtBootstrapProtocol)) {
                    all_good = true;
                } else {
                    all_good = false;
                }
            }
            if (attr != 'agmtStripAttrs' &&
                attr != 'agmtFracAttrs' &&
                attr != 'agmtFracInitAttrs' &&
                value != this.state[orig_attr]) {
                all_good = true;
            } else if ((attr == 'agmtStripAttrs' && !this.listEqual(value, this.state._agmtStripAttrs)) ||
                       (attr == 'agmtFracAttrs' && !this.listEqual(value, this.state._agmtFracAttrs)) ||
                       (attr == 'agmtFracInitAttrs' && !this.listEqual(value, this.state._agmtFracInitAttrs))) {
                all_good = true;
            }
        }

        if (e.target.type == "time") {
            // Strip out the colon from the time
            time_val = value.replace(':', '');
        }

        if (e.target.name.startsWith("agmt-modal")) {
            // Validate modal settings "live"
            if (attr == 'agmtName') {
                if (value == "") {
                    all_good = false;
                }
            } else if (this.state.agmtName == "") {
                all_good = false;
            }
            if (attr == 'agmtHost') {
                if (value == "") {
                    all_good = false;
                }
            } else if (this.state.agmtHost == "") {
                all_good = false;
            } else if (edit && value == this.state._agmtHost) {
                all_good = false;
            }
            if (attr == 'agmtPort') {
                if (value == "") {
                    all_good = false;
                } else if (!valid_port(value)) {
                    all_good = false;
                    errObj['agmtPort'] = true;
                    modal_msg = "Invalid Consumer Port number";
                }
            } else if (this.state.agmtPort == "") {
                all_good = false;
            }
            if (attr == 'agmtBindDN') {
                if (value == "") {
                    all_good = false;
                }
                if (!valid_dn(value)) {
                    errObj['agmtBindDN'] = true;
                    all_good = false;
                    modal_msg = "Invalid DN for Bind DN";
                }
            } else if (this.state.agmtBindDN == "") {
                all_good = false;
            } else if (!valid_dn(this.state.agmtBindDN)) {
                modal_msg = "Invalid DN for Bind DN";
                errObj['agmtBindDN'] = true;
                all_good = false;
            }
            if (attr == 'agmtBindPW') {
                if (value == "") {
                    all_good = false;
                } else if (value != this.state.agmtBindPWConfirm) {
                    modal_msg = "Passwords Do Not Match";
                    errObj['agmtBindPW'] = true;
                    errObj['agmtBindPWConfirm'] = true;
                    all_good = false;
                } else {
                    errObj['agmtBindPW'] = false;
                    errObj['agmtBindPWConfirm'] = false;
                }
            } else if (this.state.agmtBindPW == "") {
                all_good = false;
            }
            if (attr == 'agmtBindPWConfirm') {
                if (value == "") {
                    all_good = false;
                } else if (value != this.state.agmtBindPW) {
                    modal_msg = "Passwords Do Not Match";
                    errObj['agmtBindPW'] = true;
                    errObj['agmtBindPWConfirm'] = true;
                    all_good = false;
                } else {
                    errObj['agmtBindPW'] = false;
                    errObj['agmtBindPWConfirm'] = false;
                }
            } else if (this.state.agmtBindPWConfirm == "") {
                all_good = false;
            }

            // Check for conflicting bind methods verses connection protocol
            if (attr == 'agmtBindMethod') {
                if (value == "SSLCLIENTAUTH" && this.state.agmtProtocol == "LDAP") {
                    modal_msg = "You must use the connection protocol LDAPS if you choose the bind method SSLCLIENTAUTH";
                    errObj['agmtBindMethod'] = true;
                    all_good = false;
                } else if (value == "SASL/GSSAPI" && this.state.agmtProtocol == "LDAPS") {
                    // GSSAPI must be over LDAP, not LDAPS
                    modal_msg = "You must use the connection protocol LDAP if you choose the bind method SASL/GSSAPI";
                    errObj['agmtBindMethod'] = true;
                    all_good = false;
                } else {
                    errObj['agmtBindMethod'] = false;
                    errObj['agmtProtocol'] = false;
                }
            } else if (attr == 'agmtProtocol') {
                if (value == "LDAP" && this.state.agmtBindMethod == "SSLCLIENTAUTH") {
                    modal_msg = "You must use the connection protocol LDAPS if you choose the bind method SSLCLIENTAUTH";
                    errObj['agmtBindMethod'] = true;
                    all_good = false;
                } else if (value == "LDAPS" && this.state.agmtBindMethod == "SASL/GSSAPI") {
                    // GSSAPI must be over LDAP, not LDAPS
                    modal_msg = "You must use the connection protocol LDAP if you choose the bind method SASL/GSSAPI";
                    errObj['agmtBindMethod'] = true;
                    all_good = false;
                } else {
                    errObj['agmtBindMethod'] = false;
                    errObj['agmtProtocol'] = false;
                }
            } else {
                if (this.state.agmtBindMethod == "SSLCLIENTAUTH" && this.state.agmtProtocol == "LDAP") {
                    modal_msg = "You must use the connection protocol LDAPS if you choose the bind method SSLCLIENTAUTH";
                    errObj['agmtBindMethod'] = true;
                    errObj['agmtProtocol'] = true;
                    all_good = false;
                } else {
                    errObj['agmtBindMethod'] = false;
                    errObj['agmtProtocol'] = false;
                }
            }
            // Handle the bootstrap settings.  There is a lot going on here.  If
            // the Bind Method is SIMPLE we need a user password, if it's
            // SSLCLIENTAUTH we do not need a password.  We also have to enforce
            // LDAPS is used for SSLCLIENTAUTH.  This is similar to how we
            // handle the agmt schedule settings.  We always need to check all
            // the bootstrap settings if one of the bootstrap settings is
            // changed - so there is a lot of overlap of checks, and setting and
            // unsetting the errObj, etc
            if (attr == 'agmtBootstrap') {
                if (value) {
                    if (this.state.agmtBootstrapBindMethod == "SIMPLE") {
                        if (this.state.agmtBootstrapBindPW == "" || this.state.agmtBootstrapBindPWConfirm == "") {
                            // Can't be empty
                            errObj['agmtBootstrapBindPW'] = true;
                            errObj['agmtBootstrapBindPWConfirm'] = true;
                            all_good = false;
                        } else if (this.state.agmtBootstrapBindPW != this.state.agmtBootstrapBindPWConfirm) {
                            // Must match
                            modal_msg = "Bootstrap Passwords Do Not Match";
                            errObj['agmtBootstrapBindPW'] = true;
                            errObj['agmtBootstrapBindPWConfirm'] = true;
                            all_good = false;
                        } else {
                            errObj['agmtBootstrapProtocol'] = false;
                            errObj['agmtBootstrapBindMethod'] = false;
                        }
                    } else if (this.state.agmtBootstrapProtocol != "LDAPS") {
                        modal_msg = "You must use the connection protocol LDAPS if you choose the bind method SSLCLIENTAUTH";
                        errObj['agmtBootstrapProtocol'] = true;
                        all_good = false;
                    } else {
                        // All good, reset the errObj
                        errObj['agmtBootstrapProtocol'] = false;
                        errObj['agmtBootstrapBindMethod'] = false;
                    }
                    if (this.state.agmtBootstrapBindDN == "" || !valid_dn(this.state.agmtBootstrapBindDN)) {
                        errObj['agmtBootstrapBindDN'] = true;
                        all_good = false;
                    }
                }
            } else if (this.state.agmtBootstrap) {
                // Check all the bootstrap settings
                if (attr == "agmtBootstrapBindDN") {
                    if (value == "" || !valid_dn(value)) {
                        errObj['agmtBootstrapBindDN'] = true;
                        all_good = false;
                    } else {
                        errObj['agmtBootstrapBindDN'] = false;
                    }
                } else if (this.state.agmtBootstrapBindDN == "" || !valid_dn(this.state.agmtBootstrapBindDN)) {
                    errObj['agmtBootstrapBindDN'] = true;
                    all_good = false;
                } else {
                    // No problems here, make sure the errObj is reset
                    errObj['agmtBootstrapBindDN'] = false;
                }

                if (attr == 'agmtBootstrapBindMethod') {
                    // Adjusting the Bind Method, if SIMPLE then verify the
                    // passwords are set and correct
                    if (value == "SIMPLE") {
                        if (this.state.agmtBootstrapBindPW == "" || this.state.agmtBootstrapBindPWConfirm == "") {
                            // Can't be empty
                            errObj['agmtBootstrapBindPW'] = true;
                            errObj['agmtBootstrapBindPWConfirm'] = true;
                            all_good = false;
                        } else if (this.state.agmtBootstrapBindPW != this.state.agmtBootstrapBindPWConfirm) {
                            // Must match
                            modal_msg = "Bootstrap Passwords Do Not Match";
                            errObj['agmtBootstrapBindPW'] = true;
                            errObj['agmtBootstrapBindPWConfirm'] = true;
                            all_good = false;
                        }
                    } else {
                        // Not SIMPLE, ignore the passwords and reset errObj
                        errObj['agmtBootstrapBindPW'] = false;
                        errObj['agmtBootstrapBindPWConfirm'] = false;
                        if (this.state.agmtBootstrapProtocol != "LDAPS") {
                            modal_msg = "You must use the connection protocol LDAPS if you choose the bind method SSLCLIENTAUTH";
                            errObj['agmtBootstrapBindMethod'] = true;
                            all_good = false;
                        } else {
                            // All good, reset the errObj
                            errObj['agmtBootstrapProtocol'] = false;
                            errObj['agmtBootstrapBindMethod'] = false;
                        }
                    }
                } else if (this.state.agmtBootstrapBindMethod == "SIMPLE") {
                    // Current bind method is SIMPLE, check old password values,
                    // and new ones.
                    if (attr == 'agmtBootstrapBindPW') {
                        // Modifying password
                        if (value == "") {
                            all_good = false;
                            errObj['agmtBootstrapBindPW'] = true;
                        } else if (value != this.state.agmtBootstrapBindPWConfirm) {
                            modal_msg = "Bootstrap Passwords Do Not Match";
                            errObj['agmtBootstrapBindPW'] = true;
                            errObj['agmtBootstrapBindPWConfirm'] = true;
                            all_good = false;
                        } else {
                            errObj['agmtBootstrapBindPW'] = false;
                            errObj['agmtBootstrapBindPWConfirm'] = false;
                        }
                    } else if (this.state.agmtBootstrapBindPW == "") {
                        // Current value is no good
                        all_good = false;
                    }
                    if (attr == 'agmtBootstrapBindPWConfirm') {
                        // Modifying password confirmation
                        if (value == "") {
                            all_good = false;
                            errObj['agmtBootstrapBindPWConfirm'] = true;
                        } else if (value != this.state.agmtBootstrapBindPW) {
                            modal_msg = "Bootstrap Passwords Do Not Match";
                            errObj['agmtBootstrapBindPW'] = true;
                            errObj['agmtBootstrapBindPWConfirm'] = true;
                            all_good = false;
                        } else {
                            errObj['agmtBootstrapBindPW'] = false;
                            errObj['agmtBootstrapBindPWConfirm'] = false;
                        }
                    } else if (this.state.agmtBootstrapBindPWConfirm == "") {
                        // Current value is no good
                        all_good = false;
                    }
                } else {
                    // Bind method is SSLCLIENTAUTH, make sure the connection protocol is LDAPS
                    if (this.state.agmtBootstrapProtocol != "LDAPS") {
                        modal_msg = "You must use the connection protocol LDAPS if you choose the bind method SSLCLIENTAUTH";
                        errObj['agmtBootstrapProtocol'] = true;
                        all_good = false;
                    } else {
                        // All good, reset the errObj
                        errObj['agmtBootstrapProtocol'] = false;
                        errObj['agmtBootstrapBindMethod'] = false;
                    }
                }
            }

            if (attr == 'agmtSync') {
                if (!value) {
                    if (this.state.agmtStartTime >= this.state.agmtEndTime) {
                        modal_schedule_msg = "Schedule start time is greater than or equal to the end time";
                        errObj['agmtStartTime'] = true;
                        all_good = false;
                    }
                }
            } else if (!this.state.agmtSync) {
                // Check the days first
                let have_days = false;
                let days = ["agmtSyncSun", "agmtSyncMon", "agmtSyncTue", "agmtSyncWed",
                    "agmtSyncThu", "agmtSyncFri", "agmtSyncSat"];
                for (let day of days) {
                    if ((attr != day && this.state[day]) || (attr == day && value)) {
                        have_days = true;
                        break;
                    }
                }
                if (!have_days) {
                    modal_schedule_msg = "You must select at least one day for replication";
                    all_good = false;
                } else if (attr == 'agmtStartTime') {
                    if (time_val == "") {
                        all_good = false;
                        errObj['agmtStartTime'] = true;
                        modal_schedule_msg = "Schedule start time must be set";
                    } else if (time_val >= this.state.agmtEndTime.replace(":", "")) {
                        errObj['agmtStartTime'] = true;
                        all_good = false;
                        modal_schedule_msg = "Schedule start time is greater than or equal to the end time";
                    } else {
                        // All good, reset form
                        modal_schedule_msg = "";
                        errObj['agmtStartTime'] = false;
                        errObj['agmtEndTime'] = false;
                    }
                } else if (attr == 'agmtEndTime') {
                    if (time_val == "") {
                        errObj['agmtEndTime'] = true;
                        all_good = false;
                    } else if (this.state.agmtStartTime.replace(":", "") >= time_val) {
                        modal_schedule_msg = "Schedule start time is greater than or equal to the end time";
                        errObj['agmtStartTime'] = true;
                        all_good = false;
                    } else {
                        // All good, reset form
                        modal_schedule_msg = "";
                        errObj['agmtStartTime'] = false;
                        errObj['agmtEndTime'] = false;
                    }
                } else if (this.state.agmtStartTime >= this.state.agmtEndTime) {
                    modal_schedule_msg = "Schedule start time is greater than or equal to the end time";
                    errObj['agmtStartTime'] = true;
                    all_good = false;
                }
            }
            // End of agmt modal live validation
        }
        // We handle strings and arrays here, need to find a better way to differentiate.
        if (e.target.id.endsWith('Attrs')) {
            if (this.state[attr].includes(e.target.value)) {
                this.setState(
                    (prevState) => ({
                        [attr]: prevState[attr].filter((item) => item !== e.target.value),
                        errObj: errObj,
                        agmtSaveOK: all_good,
                        modalMsg: modal_msg,
                        modalScheduleMsg: modal_schedule_msg,
                        [e.target.toggle]: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        [attr]: [...prevState[attr], e.target.value],
                        errObj: errObj,
                        agmtSaveOK: all_good,
                        modalMsg: modal_msg,
                        modalScheduleMsg: modal_schedule_msg,
                        [e.target.toggle]: false
                    }),
                );
            }
        } else {
            this.setState({
                [attr]: value,
                errObj: errObj,
                agmtSaveOK: all_good,
                modalMsg: modal_msg,
                modalScheduleMsg: modal_schedule_msg,
                [e.target.toggle]: false
            });
        }
    }

    handleTAStripAttrChangeEdit (values) {
        // TypeAhead handling
        let e = {
            target: {
                name: 'agmt-modal-edit',
                id: 'agmtStripAttrs',
                value: values,
                type: 'input',
                toggle: 'isStripAttrsEditOpen',
            }
        };
        this.handleChange(e);
    }

    handleTAFracAttrChangeEdit (values) {
        // TypeAhead handling
        let e = {
            target: {
                name: 'agmt-modal-edit',
                id: 'agmtFracAttrs',
                value: values,
                type: 'input',
                toggle: 'isExcludeAttrsEditOpen',
            }
        };
        this.handleChange(e);
    }

    handleTAFracInitAttrChangeEdit (values) {
        // TypeAhead handling
        let e = {
            target: {
                name: 'agmt-modal-edit',
                id: 'agmtFracInitAttrs',
                value: values,
                type: 'input',
                toggle: 'isExcludeInitAttrsEditOpen',
            }
        };
        this.handleChange(e);
    }

    handleTAStripAttrChange (values) {
        // TypeAhead handling
        let e = {
            target: {
                name: 'agmt-modal',
                id: 'agmtStripAttrs',
                value: values,
                type: 'input',
                toggle: 'isStripAttrsCreateOpen',
            }
        };
        this.handleChange(e);
    }

    handleTAFracAttrChange (values) {
        // TypeAhead handling
        let e = {
            target: {
                name: 'agmt-modal',
                id: 'agmtFracAttrs',
                value: values,
                type: 'input',
                toggle: 'isExcludeAttrsCreateOpen',
            }
        };
        this.handleChange(e);
    }

    handleTAFracInitAttrChange (values) {
        // TypeAhead handling
        let e = {
            target: {
                name: 'agmt-modal',
                id: 'agmtFracInitAttrs',
                value: values,
                type: 'input',
                toggle: 'isExcludeInitAttrsCreateOpen',
            }
        };
        this.handleChange(e);
    }

    onSelectToggle = (isExpanded, toggleId) => {
        this.setState({
            [toggleId]: isExpanded
        });
    }

    onSelectClear = (toggleId, collection) => {
        this.setState({
            [toggleId]: false,
            [collection]: []
        });
    }

    showConfirmDeleteAgmt (agmtName) {
        this.setState({
            agmtName: agmtName,
            showConfirmDeleteAgmt: true,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmDeleteAgmt () {
        this.setState({
            showConfirmDeleteAgmt: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    showConfirmInitAgmt (agmtName) {
        this.setState({
            agmtName: agmtName,
            showConfirmInitAgmt: true,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmInitAgmt () {
        this.setState({
            showConfirmInitAgmt: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    showCreateAgmtModal () {
        this.setState({
            showCreateAgmtModal: true,
            agmtName: "",
            agmtHost: "",
            agmtPort: "",
            agmtProtocol: "LDAP",
            agmtBindMethod: "SIMPLE",
            agmtBindDN: "",
            agmtBindPW: "",
            agmtBindPWConfirm: "",
            agmtBootstrap: false,
            agmtBootstrapProtocol: "LDAP",
            agmtBootstrapBindMethod: "SIMPLE",
            agmtBootstrapBindDN: "",
            agmtBootstrapBindPW: "",
            agmtBootstrapBindPWConfirm: "",
            agmtStripAttrs: [],
            agmtFracAttrs: [],
            agmtFracInitAttrs: [],
            agmtSync: true,
            agmtSyncMon: true,
            agmtSyncTue: true,
            agmtSyncWed: true,
            agmtSyncThu: true,
            agmtSyncFri: true,
            agmtSyncSat: true,
            agmtSyncSun: true,
            agmtStartTime: "00:00",
            agmtEndTime: "23:59",
            agmtInit: "noinit",
            agmtSaveOK: false,
            modalScheduleMsg: "",
            errObj: {
                // Marks all the fields as required
                agmtName: true,
                agmtHost: true,
                agmtPort: true,
                agmtBindDN: true,
                agmtBindPW: true,
                agmtBindPWConfirm: true,
                agmtStartTime: false,
                agmtEndTime: false,
            }
        });
    }

    closeCreateAgmtModal () {
        this.setState({
            showCreateAgmtModal: false
        });
    }

    closeEditAgmtModal () {
        this.setState({
            showEditAgmtModal: false
        });
    }

    showEditAgmt (agmtName) {
        // Search for the agmt to get all the details
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'get', agmtName, '--suffix=' + this.props.suffix,
        ];

        log_cmd('showEditAgmt', 'Edit replication agreement', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let agmtName = "";
                    let agmtHost = "";
                    let agmtPort = "";
                    let agmtProtocol = "";
                    let agmtBindMethod = "";
                    let agmtBindDN = "";
                    let agmtBindPW = "";
                    let agmtBindPWConfirm = "";
                    let agmtBootstrap = false;
                    let agmtBootstrapProtocol = "";
                    let agmtBootstrapBindMethod = "";
                    let agmtBootstrapBindDN = "";
                    let agmtBootstrapBindPW = "";
                    let agmtBootstrapBindPWConfirm = "";
                    let agmtStripAttrs = [];
                    let agmtFracAttrs = [];
                    let agmtFracInitAttrs = [];
                    let agmtSync = true;
                    let agmtSyncMon = false;
                    let agmtSyncTue = false;
                    let agmtSyncWed = false;
                    let agmtSyncThu = false;
                    let agmtSyncFri = false;
                    let agmtSyncSat = false;
                    let agmtSyncSun = false;
                    let agmtStartTime = "";
                    let agmtEndTime = "";
                    for (let attr in config['attrs']) {
                        let val = config['attrs'][attr][0];
                        if (attr == "cn") {
                            agmtName = val;
                        }
                        if (attr == "nsds5replicahost") {
                            agmtHost = val;
                        }
                        if (attr == "nsds5replicaport") {
                            agmtPort = val;
                        }
                        if (attr == "nsds5replicatransportinfo") {
                            agmtProtocol = val;
                        }
                        if (attr == "nsds5replicabindmethod") {
                            agmtBindMethod = val.toUpperCase();
                        }
                        if (attr == "nsds5replicabinddn") {
                            agmtBindDN = val;
                        }
                        if (attr == "nsds5replicacredentials") {
                            agmtBindPW = val;
                            agmtBindPWConfirm = val;
                        }
                        if (attr == "nsds5replicabootstraptransportinfo") {
                            agmtBootstrapProtocol = val;
                        }
                        if (attr == "nsds5replicabootstrapbindmethod") {
                            agmtBootstrapBindMethod = val.toUpperCase();
                        }
                        if (attr == "nsds5replicabootstrapbinddn") {
                            agmtBootstrapBindDN = val;
                            agmtBootstrap = true;
                        }
                        if (attr == "nsds5replicabootstrapcredentials") {
                            agmtBootstrapBindPW = val;
                            agmtBootstrapBindPWConfirm = val;
                        }
                        if (attr == "nsds5replicatedattributelist") {
                            let attrs = val.replace("(objectclass=*) $ EXCLUDE", "").trim();
                            agmtFracAttrs = attrs.split(' ');
                        }
                        if (attr == "nsds5replicatedattributelisttotal") {
                            let attrs = val.replace("(objectclass=*) $ EXCLUDE", "").trim();
                            agmtFracInitAttrs = attrs.split(' ');
                        }
                        if (attr == "nsds5replicastripattrs") {
                            agmtStripAttrs = val.split(' ');
                        }
                        if (attr == "nsds5replicaupdateschedule") {
                            agmtSync = false;
                            // Parse schedule
                            let parts = val.split(' ');
                            let times = parts[0].split('-');
                            let days = parts[1];

                            // Do the times
                            agmtStartTime = times[0].substring(0, 2) + ":" + times[0].substring(2, 4);
                            agmtEndTime = times[1].substring(0, 2) + ":" + times[1].substring(2, 4);

                            // Do the days
                            if (days.includes("0")) {
                                agmtSyncSun = true;
                            }
                            if (days.includes("1")) {
                                agmtSyncMon = true;
                            }
                            if (days.includes("2")) {
                                agmtSyncTue = true;
                            }
                            if (days.includes("3")) {
                                agmtSyncWed = true;
                            }
                            if (days.includes("4")) {
                                agmtSyncThu = true;
                            }
                            if (days.includes("5")) {
                                agmtSyncFri = true;
                            }
                            if (days.includes("6")) {
                                agmtSyncSat = true;
                            }
                        }
                    }
                    if (this._mounted) {
                        this.setState({
                            showEditAgmtModal: true,
                            agmtName: agmtName,
                            agmtHost: agmtHost,
                            agmtPort: agmtPort,
                            agmtProtocol: agmtProtocol,
                            agmtBindMethod: agmtBindMethod,
                            agmtBindDN: agmtBindDN,
                            agmtBindPW: agmtBindPW,
                            agmtBindPWConfirm: agmtBindPWConfirm,
                            agmtBootstrap: agmtBootstrap,
                            agmtBootstrapProtocol: agmtBootstrapProtocol,
                            agmtBootstrapBindMethod: agmtBootstrapBindMethod,
                            agmtBootstrapBindDN: agmtBootstrapBindDN,
                            agmtBootstrapBindPW: agmtBootstrapBindPW,
                            agmtBootstrapBindPWConfirm: agmtBootstrapBindPWConfirm,
                            agmtStripAttrs: agmtStripAttrs,
                            agmtFracAttrs: agmtFracAttrs,
                            agmtFracInitAttrs: agmtFracInitAttrs,
                            agmtSync: agmtSync,
                            agmtSyncMon: agmtSyncMon,
                            agmtSyncTue: agmtSyncTue,
                            agmtSyncWed: agmtSyncWed,
                            agmtSyncThu: agmtSyncThu,
                            agmtSyncFri: agmtSyncFri,
                            agmtSyncSat: agmtSyncSat,
                            agmtSyncSun: agmtSyncSun,
                            agmtStartTime: agmtStartTime,
                            agmtEndTime: agmtEndTime,
                            agmtSaveOK: false,
                            modalMsg: "",
                            errObj: {},
                            // Record original values before editing
                            _agmtName: agmtName,
                            _agmtHost: agmtHost,
                            _agmtPort: agmtPort,
                            _agmtProtocol: agmtProtocol,
                            _agmtBindMethod: agmtBindMethod,
                            _agmtBindDN: agmtBindDN,
                            _agmtBindPW: agmtBindPW,
                            _agmtBindPWConfirm: agmtBindPWConfirm,
                            _agmtBootstrap: agmtBootstrap,
                            _agmtBootstrapProtocol: agmtBootstrapProtocol,
                            _agmtBootstrapBindMethod: agmtBootstrapBindMethod,
                            _agmtBootstrapBindDN: agmtBootstrapBindDN,
                            _agmtBootstrapBindPW: agmtBootstrapBindPW,
                            _agmtBootstrapBindPWConfirm: agmtBootstrapBindPWConfirm,
                            _agmtStripAttrs: agmtStripAttrs,
                            _agmtFracAttrs: agmtFracAttrs,
                            _agmtFracInitAttrs: agmtFracInitAttrs,
                            _agmtSync: agmtSync,
                            _agmtSyncMon: agmtSyncMon,
                            _agmtSyncTue: agmtSyncTue,
                            _agmtSyncWed: agmtSyncWed,
                            _agmtSyncThu: agmtSyncThu,
                            _agmtSyncFri: agmtSyncFri,
                            _agmtSyncSat: agmtSyncSat,
                            _agmtSyncSun: agmtSyncSun,
                            _agmtStartTime: agmtStartTime,
                            _agmtEndTime: agmtEndTime,
                        });
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to get agreement information for: "${agmtName}" - ${errMsg.desc}`
                    );
                });
    }

    saveAgmt () {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'set', this.state.agmtName, '--suffix=' + this.props.suffix,
        ];

        // Handle Schedule
        if (!this.state.agmtSync) {
            let agmt_days = "";
            if (this.state.agmtSyncSun) {
                agmt_days += "0";
            }
            if (this.state.agmtSyncMon) {
                agmt_days += "1";
            }
            if (this.state.agmtSyncTue) {
                agmt_days += "2";
            }
            if (this.state.agmtSyncWed) {
                agmt_days += "3";
            }
            if (this.state.agmtSyncThu) {
                agmt_days += "4";
            }
            if (this.state.agmtSyncFri) {
                agmt_days += "5";
            }
            if (this.state.agmtSyncSat) {
                agmt_days += "6";
            }
            cmd.push('--schedule=' + this.state.agmtStartTime.replace(':', '') + "-" + this.state.agmtEndTime.replace(':', '') + " " + agmt_days);
        } else if (this.state.agmtSync != this.state._agmtSync && this.state.agmtSync) {
            // We disabled custom scheduleRow
            cmd.push('--schedule=');
        }
        if (this.state.agmtBindMethod != this.state._agmtBindMethod) {
            cmd.push('--bind-method=' + this.state.agmtBindMethod);
        }
        if (this.state.agmtProtocol != this.state._agmtProtocol) {
            cmd.push('--conn-protocol=' + this.state.agmtProtocol);
        }
        if (this.state.agmtBindPW != this.state._agmtBindPW) {
            cmd.push('--bind-passwd=' + this.state.agmtBindPW);
        }
        if (this.state.agmtBindDN != this.state._agmtBindDN) {
            cmd.push('--bind-dn=' + this.state.agmtBindDN);
        }
        if (this.state.agmtFracAttrs != this.state._agmtFracAttrs) {
            cmd.push('--frac-list=' + this.state.agmtFracAttrs.join(' '));
        }
        if (this.state.agmtFracInitAttrs != this.state._agmtFracInitAttrs) {
            cmd.push('--frac-list-total=' + this.state.agmtFracInitAttrs.join(' '));
        }
        if (this.state.agmtStripAttrs != this.state._agmtStripAttrs) {
            cmd.push('--strip-list=' + this.state.agmtStripAttrs.join(' '));
        }
        if (this.state.agmtHost != this.state._agmtHost) {
            cmd.push('--host=' + this.state.agmtHost);
        }
        if (this.state.agmtPort != this.state._agmtPort) {
            cmd.push('--port=' + this.state.agmtPort);
        }
        if (this.state.agmtBootstrap) {
            if (this.state.agmtBootstrapBindMethod != this.state._agmtBootstrapBindMethod) {
                cmd.push('--bootstrap-bind-method=' + this.state.agmtBootstrapBindMethod);
            }
            if (this.state.agmtBootstrapProtocol != this.state._agmtBootstrapProtocol) {
                cmd.push('--bootstrap-conn-protocol=' + this.state.agmtBootstrapProtocol);
            }
            if (this.state.agmtBootstrapBindPW != this.state._agmtBootstrapBindPW) {
                cmd.push('--bootstrap-bind-passwd=' + this.state.agmtBootstrapBindPW);
            }
            if (this.state.agmtBootstrapBindDN != this.state._agmtBootstrapBindDN) {
                cmd.push('--bootstrap-bind-dn=' + this.state.agmtBootstrapBindDN);
            }
        }

        this.setState({
            savingAgmt: true,
        });
        log_cmd('saveAgmt', 'update replication agreement', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    if (this._mounted) {
                        this.setState({
                            savingAgmt: false,
                            showEditAgmtModal: false,
                        });
                    }
                    this.props.addNotification(
                        'success',
                        'Successfully updated replication agreement'
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to update replication agreement - ${errMsg.desc}`
                    );
                    this.setState({
                        savingAgmt: false
                    });
                });
    }

    pokeAgmt (agmtName) {
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'poke', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('pokeAgmt', 'send updates now', cmd);
        cockpit
                .spawn(cmd, { superuser: true, "err": "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully poked replication agreement'
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        'error',
                        `Failed to poke replication agreement - ${errMsg.desc}`
                    );
                });
    }

    initAgmt (agmtName) {
        this.setState({
            modalSpinning: true
        });
        let init_cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'init', '--suffix=' + this.props.suffix, this.state.agmtName];
        log_cmd('initAgmt', 'Initialize agreement', init_cmd);
        cockpit
                .spawn(init_cmd, { superuser: true, "err": "message" })
                .done(content => {
                    var agmtIntervalCount = this.state.agmtInitCounter + 1;
                    var intervals = this.state.agmtInitIntervals;
                    this.props.reload(this.props.suffix);
                    intervals[agmtIntervalCount] = setInterval(this.watchAgmtInit, 2000, this.state.agmtName, agmtIntervalCount);
                    if (this._mounted) {
                        this.setState({
                            agmtInitCounter: agmtIntervalCount,
                            agmtInitIntervals: intervals,
                            showConfirmInitAgmt: false
                        });
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        'error',
                        `Failed to initialize replication agreement - ${errMsg.desc}`
                    );
                    this.setState({
                        showConfirmInitAgmt: false
                    });
                });
    }

    confirmToggle (agmtName, state) {
        if (state == 'Enabled') {
            this.setState({
                agmtName: agmtName,
                showConfirmDisableAgmt: true
            });
        } else {
            this.setState({
                agmtName: agmtName,
                showConfirmEnableAgmt: true
            });
        }
    }

    closeConfirmEnableAgmt () {
        this.setState({
            showConfirmEnableAgmt: false
        });
    }

    closeConfirmDisableAgmt () {
        this.setState({
            showConfirmDisableAgmt: false
        });
    }

    enableAgmt (agmtName) {
        // Enable/disable agmt
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'enable', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('enableAgmt', 'enable agmt', cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully enabled replication agreement');
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to enabled replication agreement - ${errMsg.desc}`
                    );
                });
    }

    disableAgmt (agmtName) {
        // Enable/disable agmt
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'disable', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('disableAgmt', 'Disable agmt', cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully disabled replication agreement');
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to disable replication agreement - ${errMsg.desc}`
                    );
                });
    }

    deleteAgmt () {
        this.setState({
            modalSpinning: true
        });
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'delete', '--suffix=' + this.props.suffix, this.state.agmtName];
        log_cmd('deleteAgmt', 'Delete agmt', cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully deleted replication agreement');
                    this.setState({
                        showDeleteConfirm: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete replication agreement - ${errMsg.desc}`
                    );
                    this.setState({
                        showDeleteConfirm: false,
                    });
                });
    }

    createAgmt () {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'create', this.state.agmtName, '--suffix=' + this.props.suffix,
            '--host=' + this.state.agmtHost, '--port=' + this.state.agmtPort,
            '--bind-method=' + this.state.agmtBindMethod, '--conn-protocol=' + this.state.agmtProtocol,
            '--bind-dn=' + this.state.agmtBindDN, '--bind-passwd=' + this.state.agmtBindPW
        ];

        // Handle Schedule
        if (!this.state.agmtSync) {
            let agmt_days = "";
            if (this.state.agmtSyncSun) {
                agmt_days += "0";
            }
            if (this.state.agmtSyncMon) {
                agmt_days += "1";
            }
            if (this.state.agmtSyncTue) {
                agmt_days += "2";
            }
            if (this.state.agmtSyncWed) {
                agmt_days += "3";
            }
            if (this.state.agmtSyncThu) {
                agmt_days += "4";
            }
            if (this.state.agmtSyncFri) {
                agmt_days += "5";
            }
            if (this.state.agmtSyncSat) {
                agmt_days += "6";
            }
            cmd.push('--schedule=' + this.state.agmtStartTime.replace(':', '') + "-" + this.state.agmtEndTime.replace(':', '') + " " + agmt_days);
        }

        // Handle fractional and stripped attributes
        if (this.state.agmtFracAttrs.length > 0) {
            cmd.push('--frac-list=' + this.state.agmtFracAttrs.join(' '));
        }
        if (this.state.agmtFracInitAttrs.length > 0) {
            cmd.push('--frac-list-total=' + this.state.agmtFracAttrs.join(' '));
        }
        if (this.state.agmtStripAttrs.length > 0) {
            cmd.push('--strip-list=' + this.state.agmtStripAttrs.join(' '));
        }

        if (this.state.agmtBootstrap) {
            if (this.state.agmtBootstrapBindDN != "") {
                cmd.push('--bootstrap-bind-dn=' + this.state.agmtBootstrapBindDN);
            }
            if (this.state.agmtBootstrapBindDNPW != "") {
                cmd.push('--bootstrap-bind-passwd=' + this.state.agmtBootstrapBindDNPW);
            }
            if (this.state.agmtBootstrapBindMethod != "") {
                cmd.push('--bootstrap-bind-method=' + this.state.agmtBootstrapBindMethod);
            }
            if (this.state.agmtBootstrapProtocol != "") {
                cmd.push('--bootstrap-conn-protocol=' + this.state.agmtBootstrapProtocol);
            }
        }

        this.setState({
            savingAgmt: true
        });
        log_cmd('createAgmt', 'Create replication agreement', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    if (this._mounted) {
                        this.setState({
                            savingAgmt: false,
                            showCreateAgmtModal: false,
                        });
                    }
                    this.props.addNotification(
                        'success',
                        'Successfully created replication agreement'
                    );
                    if (this.state.agmtInit == 'online-init') {
                        this.initAgmt(this.state.agmtName);
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to create replication agreement - ${errMsg.desc}`
                    );
                    this.setState({
                        savingAgmt: false
                    });
                });
    }

    watchAgmtInit(agmtName, idx) {
        // Watch the init, then clear the interval index
        let status_cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'init-status', '--suffix=' + this.props.suffix, agmtName];
        log_cmd('watchAgmtInit', 'Get initialization status for agmt', status_cmd);
        cockpit
                .spawn(status_cmd, {superuser: true, "err": "message"})
                .done(data => {
                    let init_status = JSON.parse(data);
                    if (init_status.startsWith('Agreement successfully initialized') ||
                        init_status.startsWith('Agreement initialization failed')) {
                        // Either way we're done, stop watching the status
                        clearInterval(this.state.agmtInitIntervals[idx]);
                    }
                    this.props.reload(this.props.suffix);
                });
    }

    onSort(_event, index, direction) {
        const sortedRows = this.state.rows.sort((a, b) => (a[index] < b[index] ? -1 : a[index] > b[index] ? 1 : 0));
        this.setState({
            sortBy: {
                index,
                direction
            },
            rows: direction === SortByDirection.asc ? sortedRows : sortedRows.reverse()
        });
    }

    onSearchChange(value, event) {
        let rows = [];
        let val = value.toLowerCase();
        for (let row of this.state.rows) {
            if (val != "" &&
                row[0].indexOf(val) == -1 &&
                row[1].indexOf(val) == -1 &&
                row[2].indexOf(val) == -1) {
                // Not a match, skip it
                continue;
            }
            rows.push([row[0], row[1], row[2], row[3], row[4], row[5]]);
        }
        if (val == "") {
            // reset rows
            rows = JSON.parse(JSON.stringify(this.props.rows));
        }
        this.setState({
            rows: rows,
            value: value,
            page: 1,
        });
    }

    render() {
        return (
            <div className="ds-margin-right">
                <ReplAgmtTable
                    key={this.state.rows}
                    rows={this.state.rows}
                    edit={this.showEditAgmt}
                    poke={this.pokeAgmt}
                    init={this.showConfirmInitAgmt}
                    enable={this.confirmToggle}
                    delete={this.showConfirmDeleteAgmt}
                    page={this.state.page}
                    sort={this.onSort}
                    sortBy={this.state.sortBy}
                    search={this.onSearchChange}
                    value={this.state.value}
                />
                <div className="ds-margin-top ds-container ds-inline">
                    <Button
                        variant="primary"
                        onClick={this.showCreateAgmtModal}
                    >
                        Create Agreement
                    </Button>
                    <Button
                        className="ds-left-margin"
                        variant="default"
                        onClick={() => {
                            this.props.reload(this.props.suffix);
                        }}
                    >
                        Refresh Agreements
                    </Button>
                </div>
                <ReplAgmtModal
                    showModal={this.state.showCreateAgmtModal}
                    closeHandler={this.closeCreateAgmtModal}
                    handleChange={this.handleChange}
                    handleStripChange={this.handleTAStripAttrChange}
                    handleFracChange={this.handleTAFracAttrChange}
                    handleFracInitChange={this.handleTAFracInitAttrChange}
                    onExcludeAttrsToggle={this.onExcludeAttrsCreateToggle}
                    onExcludeAttrsClear={this.onExcludeAttrsCreateClear}
                    onExcludeAttrsInitToggle={this.onExcludeAttrsInitCreateToggle}
                    onExcludeAttrsInitClear={this.onExcludeAttrsInitCreateClear}
                    onStripAttrsToggle={this.onStripAttrsCreateToggle}
                    onStripAttrsClear={this.onStripAttrsCreateClear}
                    isExcludeAttrsOpen={this.state.isExcludeAttrsCreateOpen}
                    isExcludeInitAttrsOpen={this.state.isExcludeInitAttrsCreateOpen}
                    isStripAttrsOpen={this.state.isStripAttrsCreateOpen}
                    saveHandler={this.createAgmt}
                    spinning={this.state.savingAgmt}
                    agmtName={this.state.agmtName}
                    agmtHost={this.state.agmtHost}
                    agmtPort={this.state.agmtPort}
                    agmtBindDN={this.state.agmtBindDN}
                    agmtBindPW={this.state.agmtBindPW}
                    agmtBindPWConfirm={this.state.agmtBindPWConfirm}
                    agmtProtocol={this.state.agmtProtocol}
                    agmtBindMethod={this.state.agmtBindMethod}
                    agmtBootstrap={this.state.agmtBootstrap}
                    agmtBootstrapBindDN={this.state.agmtBootstrapBindDN}
                    agmtBootstrapBindPW={this.state.agmtBootstrapBindPW}
                    agmtBootstrapBindPWConfirm={this.state.agmtBootstrapBindPWConfirm}
                    agmtBootstrapProtocol={this.state.agmtBootstrapProtocol}
                    agmtBootstrapBindMethod={this.state.agmtBootstrapBindMethod}
                    agmtStripAttrs={this.state.agmtStripAttrs}
                    agmtFracAttrs={this.state.agmtFracAttrs}
                    agmtFracInitAttrs={this.state.agmtFracInitAttrs}
                    agmtSync={this.state.agmtSync}
                    agmtSyncMon={this.state.agmtSyncMon}
                    agmtSyncTue={this.state.agmtSyncTue}
                    agmtSyncWed={this.state.agmtSyncWed}
                    agmtSyncThu={this.state.agmtSyncThu}
                    agmtSyncFri={this.state.agmtSyncFri}
                    agmtSyncSat={this.state.agmtSyncSat}
                    agmtSyncSun={this.state.agmtSyncSun}
                    agmtStartTime={this.state.agmtStartTime}
                    agmtEndTime={this.state.agmtEndTime}
                    availAttrs={this.props.attrs}
                    error={this.state.errObj}
                    errorMsg={this.state.modalMsg}
                    errorScheduleMsg={this.state.modalScheduleMsg}
                    saveOK={this.state.agmtSaveOK}
                />
                <ReplAgmtModal
                    showModal={this.state.showEditAgmtModal}
                    closeHandler={this.closeEditAgmtModal}
                    handleChange={this.handleChange}
                    handleStripChange={this.handleTAStripAttrChangeEdit}
                    handleFracChange={this.handleTAFracAttrChangeEdit}
                    handleFracInitChange={this.handleTAFracInitAttrChangeEdit}
                    onExcludeAttrsToggle={this.onExcludeAttrsEditToggle}
                    onExcludeAttrsClear={this.onExcludeAttrsEditClear}
                    onExcludeAttrsInitToggle={this.onExcludeAttrsInitEditToggle}
                    onExcludeAttrsInitClear={this.onExcludeAttrsInitEditClear}
                    onStripAttrsToggle={this.onStripAttrsEditToggle}
                    onStripAttrsClear={this.onStripAttrsEditClear}
                    isExcludeAttrsOpen={this.state.isExcludeAttrsEditOpen}
                    isExcludeInitAttrsOpen={this.state.isExcludeInitAttrsEditOpen}
                    isStripAttrsOpen={this.state.isStripAttrsEditOpen}
                    saveHandler={this.saveAgmt}
                    spinning={this.state.savingAgmt}
                    agmtName={this.state.agmtName}
                    agmtHost={this.state.agmtHost}
                    agmtPort={this.state.agmtPort}
                    agmtBindDN={this.state.agmtBindDN}
                    agmtBindPW={this.state.agmtBindPW}
                    agmtBindPWConfirm={this.state.agmtBindPWConfirm}
                    agmtProtocol={this.state.agmtProtocol}
                    agmtBindMethod={this.state.agmtBindMethod}
                    agmtBootstrap={this.state.agmtBootstrap}
                    agmtBootstrapBindDN={this.state.agmtBootstrapBindDN}
                    agmtBootstrapBindPW={this.state.agmtBootstrapBindPW}
                    agmtBootstrapBindPWConfirm={this.state.agmtBootstrapBindPWConfirm}
                    agmtBootstrapProtocol={this.state.agmtBootstrapProtocol}
                    agmtBootstrapBindMethod={this.state.agmtBootstrapBindMethod}
                    agmtStripAttrs={this.state.agmtStripAttrs}
                    agmtFracAttrs={this.state.agmtFracAttrs}
                    agmtFracInitAttrs={this.state.agmtFracInitAttrs}
                    agmtSync={this.state.agmtSync}
                    agmtSyncMon={this.state.agmtSyncMon}
                    agmtSyncTue={this.state.agmtSyncTue}
                    agmtSyncWed={this.state.agmtSyncWed}
                    agmtSyncThu={this.state.agmtSyncThu}
                    agmtSyncFri={this.state.agmtSyncFri}
                    agmtSyncSat={this.state.agmtSyncSat}
                    agmtSyncSun={this.state.agmtSyncSun}
                    agmtStartTime={this.state.agmtStartTime}
                    agmtEndTime={this.state.agmtEndTime}
                    availAttrs={this.props.attrs}
                    error={this.state.errObj}
                    errorMsg={this.state.modalMsg}
                    errorScheduleMsg={this.state.modalScheduleMsg}
                    saveOK={this.state.agmtSaveOK}
                    edit
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteAgmt}
                    closeHandler={this.closeConfirmDeleteAgmt}
                    handleChange={this.handleModalChange}
                    actionHandler={this.deleteAgmt}
                    spinning={this.state.modalSpinning}
                    item={this.state.agmtName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Replication Agreement"
                    mMsg="Are you sure you want to delete this replication agreement?"
                    mSpinningMsg="Deleting Replication Agreement ..."
                    mBtnName="Delete Agreement"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmInitAgmt}
                    closeHandler={this.closeConfirmInitAgmt}
                    handleChange={this.handleModalChange}
                    actionHandler={this.initAgmt}
                    spinning={this.state.modalSpinning}
                    item={this.state.agmtName}
                    checked={this.state.modalChecked}
                    mTitle="Initialize Replication Agreement"
                    mMsg="Are you sure you want to initialize this replication agreement?"
                    mSpinningMsg="Initializing Replication Agreement ..."
                    mBtnName="Initialize Agreement"
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmEnableAgmt}
                    closeHandler={this.closeConfirmEnableAgmt}
                    actionFunc={this.enableAgmt}
                    actionParam={this.state.agmtName}
                    msg="Are you sure you want to enable this replication agreement?"
                    msgContent={this.state.agmtName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmDisableAgmt}
                    closeHandler={this.closeConfirmDisableAgmt}
                    actionFunc={this.disableAgmt}
                    actionParam={this.state.agmtName}
                    msg="Are you sure you want to disable this replication agreement?"
                    msgContent={this.state.agmtName}
                />
            </div>
        );
    }
}

ReplAgmts.propTypes = {
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    rows: PropTypes.array,
    addNotification: PropTypes.func,
    attrs: PropTypes.array,
};

ReplAgmts.defaultProps = {
    serverId: "",
    suffix: "",
    rows: [],
    addNotification: noop,
    attrs: [],
};
