import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup, DoubleConfirmModal } from "../notifications.jsx";
import { ReplAgmtTable } from "./replTables.jsx";
import { WinsyncAgmtModal } from "./replModals.jsx";
import { log_cmd, valid_dn, valid_port } from "../tools.jsx";
import PropTypes from "prop-types";
import {
    noop,
} from "patternfly-react";
import {
    Button
} from "@patternfly/react-core";
import {
    SortByDirection,
} from '@patternfly/react-table';

export class WinsyncAgmts extends React.Component {
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
            agmtProtocol: "LDAPS",
            agmtBindDN: "",
            agmtBindPW: "",
            agmtBindPWConfirm: "",
            agmtFracAttrs: [],
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
            // Winsync specific settings
            agmtSyncGroups: false,
            agmtSyncUsers: true,
            agmtWinDomain: "",
            agmtWinSubtree: "",
            agmtDSSubtree: "",
            agmtOneWaySync: "both", // "both", "toWindows", "fromWindows"
            agmtSyncInterval: "",
            // Init agmt
            agmtInitCounter: 0,
            agmtInitIntervals: [],
        };
        this.showCreateAgmtModal = this.showCreateAgmtModal.bind(this);
        this.closeCreateAgmtModal = this.closeCreateAgmtModal.bind(this);
        this.closeEditAgmtModal = this.closeEditAgmtModal.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.handleTAFracAttrChange = this.handleTAFracAttrChange.bind(this);
        this.handleTAFracAttrChangeEdit = this.handleTAFracAttrChangeEdit.bind(this);
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
        errObj[e.target.id] = valueErr;
        if (e.target.name == "agmt-modal-edit") {
            let orig_attr = "_" + e.target.id;
            let attr = e.target.id;
            all_good = false;
            if ((attr != 'agmtHost' && this.state.agmtHost != this.state._agmtHost) ||
                (attr != 'agmtPort' && this.state.agmtPort != this.state._agmtPort) ||
                (attr != 'agmtBindDN' && this.state.agmtBindDN != this.state._agmtBindDN) ||
                (attr != 'agmtProtocol' && this.state.agmtProtocol != this.state._agmtProtocol) ||
                (attr != 'agmtSync' && this.state.agmtSync != this.state._agmtSync) ||
                (attr != 'agmtSyncGroups' && this.state.agmtSyncGroups != this.state._agmtSyncGroups) ||
                (attr != 'agmtSyncUsers' && this.state.agmtSyncUsers != this.state._agmtSyncUsers) ||
                (attr != 'agmtWinDomain' && this.state.agmtWinDomain != this.state._agmtWinDomain) ||
                (attr != 'agmtWinSubtree' && this.state.agmtWinSubtree != this.state._agmtWinSubtree) ||
                (attr != 'agmtDSSubtree' && this.state.agmtDSSubtree != this.state._agmtDSSubtree) ||
                (attr != 'agmtOneWaySync' && this.state.agmtOneWaySync != this.state._agmtOneWaySync) ||
                (attr != 'agmtSyncInterval' && this.state.agmtSyncInterval != this.state._agmtSyncInterval) ||
                (attr != 'agmtFracAttrs' && !this.listEqual(this.state.agmtFracAttrs, this.state._agmtFracAttrs))) {
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
            if (attr != 'agmtFracAttrs' &&
                value != this.state[orig_attr]) {
                all_good = true;
            } else if (attr == 'agmtFracAttrs' && !this.listEqual(value, this.state._agmtFracAttrs)) {
                all_good = true;
            }
        }

        if (e.target.type == "time") {
            // Strip out the colon from the time
            time_val = value.replace(':', '');
        }

        if (e.target.name.startsWith("agmt-modal")) {
            // Validate modal settings "live"
            if (e.target.id == 'agmtName') {
                if (value == "") {
                    all_good = false;
                }
            } else if (this.state.agmtName == "") {
                all_good = false;
            }
            if (e.target.id == 'agmtHost') {
                if (value == "") {
                    all_good = false;
                }
            } else if (this.state.agmtHost == "") {
                all_good = false;
            } else if (edit && value == this.state._agmtHost) {
                all_good = false;
            }
            if (e.target.id == 'agmtPort') {
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
            if (e.target.id == 'agmtBindDN') {
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
            if (e.target.id == 'agmtBindPW') {
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
            if (e.target.id == 'agmtBindPWConfirm') {
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
            if (e.target.id == 'agmtSync') {
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
                    if ((e.target.id != day && this.state[day]) || (e.target.id == day && value)) {
                        have_days = true;
                        break;
                    }
                }
                if (!have_days) {
                    modal_schedule_msg = "You must select at least one day for replication";
                    all_good = false;
                } else if (e.target.id == 'agmtStartTime') {
                    if (time_val == "") {
                        all_good = false;
                        errObj['agmtStartTime'] = true;
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
                } else if (e.target.id == 'agmtEndTime') {
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
            if (e.target.id == 'agmtSyncGroups') {
                if (edit && value == this.state._agmtSyncGroups) {
                    all_good = false;
                }
            }
            if (e.target.id == 'agmtSyncUsers') {
                if (edit && value == this.state._agmtSyncUsers) {
                    all_good = false;
                }
            }
            if (e.target.id == 'agmtWinSubtree') {
                if (value == "") {
                    all_good = false;
                } else if (edit && value == this.state._agmtWinSubtree) {
                    all_good = false;
                }
                if (!valid_dn(value)) {
                    errObj['agmtWinSubtree'] = true;
                    all_good = false;
                    modal_msg = "Invalid DN for Windows Subtree";
                }
            }
            if (e.target.id == 'agmtDSSubtree') {
                if (value == "") {
                    all_good = false;
                } else if (edit && value == this.state._agmtDSSubtree) {
                    all_good = false;
                }
                if (!valid_dn(value)) {
                    errObj['agmtDSSubtree'] = true;
                    all_good = false;
                    modal_msg = "Invalid DN for Directory Server Subtree";
                }
            }
            if (e.target.id == 'agmtSyncInterval') {
                if (value != "" && isNaN(value)) {
                    errObj['agmtSyncInterval'] = true;
                    all_good = false;
                    modal_msg = "Invalid value, value must be a number";
                } else if (edit && value == this.state._agmtSyncInterval) {
                    all_good = false;
                }
            }
            if (e.target.id == 'agmtOneWaySync') {
                if (edit && value == this.state._agmtOneWaySync) {
                    all_good = false;
                }
            }
            // End of agmt modal live validation
        }
        this.setState({
            [e.target.id]: value,
            errObj: errObj,
            agmtSaveOK: all_good,
            modalMsg: modal_msg,
            modalScheduleMsg: modal_schedule_msg,
        });
    }

    handleTAFracAttrChangeEdit (values) {
        // TypeAhead handling
        let e = {
            target: {
                name: 'agmt-modal-edit',
                id: 'agmtFracAttrs',
                value: values,
                type: 'input',
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
            }
        };
        this.handleChange(e);
    }

    showConfirmDeleteAgmt (agmtName) {
        this.setState({
            agmtName: agmtName,
            showConfirmDeleteAgmt: true,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    closeConfirmDeleteAgmt () {
        this.setState({
            showConfirmDeleteAgmt: false,
            modalChecked: false,
        });
    }

    showConfirmInitAgmt (agmtName) {
        this.setState({
            agmtName: agmtName,
            showConfirmInitAgmt: true,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    closeConfirmInitAgmt () {
        this.setState({
            showConfirmInitAgmt: false,
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
            agmtBindDN: "",
            agmtBindPW: "",
            agmtBindPWConfirm: "",
            agmtFracAttrs: [],
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
            agmtSyncGroups: false,
            agmtSyncUsers: true,
            agmtWinDomain: "",
            agmtWinSubtree: "",
            agmtDSSubtree: "",
            agmtOneWaySync: "both", // "both", "toWindows", "fromWindows"
            agmtSyncInterval: "",
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
                agmtWinDomain: true,
                agmtWinSubtree: true,
                agmtDSSubtree: true,
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
            'repl-winsync-agmt', 'get', agmtName, '--suffix=' + this.props.suffix,
        ];

        log_cmd('showEditAgmt', 'Edit winsync agreement', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let agmtName = "";
                    let agmtHost = "";
                    let agmtPort = "";
                    let agmtProtocol = "";
                    let agmtBindDN = "";
                    let agmtBindPW = "";
                    let agmtBindPWConfirm = "";
                    let agmtFracAttrs = [];
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
                    let agmtSyncGroups = false;
                    let agmtSyncUsers = false;
                    let agmtWinDomain = "";
                    let agmtWinSubtree = "";
                    let agmtDSSubtree = "";
                    let agmtOneWaySync = "both";
                    let agmtSyncInterval = "";

                    for (let attr in config['attrs']) {
                        let val = config['attrs'][attr][0];
                        if (attr == "winsyncinterval") {
                            agmtSyncInterval = val;
                        }
                        if (attr == "onewaysync") {
                            agmtOneWaySync = val;
                        }
                        if (attr == "nsds7directoryreplicasubtree") {
                            agmtDSSubtree = val;
                        }
                        if (attr == "nsds7windowsreplicasubtree") {
                            agmtWinSubtree = val;
                        }
                        if (attr == "nsds7windowsdomain") {
                            agmtWinDomain = val;
                        }
                        if (attr == "nsds7newwinusersyncenabled") {
                            if (val.toLowerCase() == "on") {
                                agmtSyncUsers = true;
                            }
                        }
                        if (attr == "nsds7newwingroupsyncenabled") {
                            if (val.toLowerCase() == "on") {
                                agmtSyncGroups = true;
                            }
                        }
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
                        if (attr == "nsds5replicabinddn") {
                            agmtBindDN = val;
                        }
                        if (attr == "nsds5replicacredentials") {
                            agmtBindPW = val;
                            agmtBindPWConfirm = val;
                        }
                        if (attr == "nsds5replicatedattributelist") {
                            let attrs = val.replace("(objectclass=*) $ EXCLUDE", "").trim();
                            agmtFracAttrs = attrs.split(' ');
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
                            errObj: {},
                            agmtName: agmtName,
                            agmtHost: agmtHost,
                            agmtPort: agmtPort,
                            agmtProtocol: agmtProtocol,
                            agmtBindDN: agmtBindDN,
                            agmtBindPW: agmtBindPW,
                            agmtBindPWConfirm: agmtBindPWConfirm,
                            agmtFracAttrs: agmtFracAttrs,
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
                            agmtSyncGroups: agmtSyncGroups,
                            agmtSyncUsers: agmtSyncUsers,
                            agmtWinDomain: agmtWinDomain,
                            agmtWinSubtree: agmtWinSubtree,
                            agmtDSSubtree: agmtDSSubtree,
                            agmtOneWaySync: agmtOneWaySync,
                            agmtSyncInterval: agmtSyncInterval,
                            agmtSaveOK: false,
                            // Record original values before editing
                            _agmtName: agmtName,
                            _agmtHost: agmtHost,
                            _agmtPort: agmtPort,
                            _agmtProtocol: agmtProtocol,
                            _agmtBindDN: agmtBindDN,
                            _agmtBindPW: agmtBindPW,
                            _agmtBindPWConfirm: agmtBindPWConfirm,
                            _agmtFracAttrs: agmtFracAttrs,
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
                            _agmtSyncGroups: agmtSyncGroups,
                            _agmtSyncUsers: agmtSyncUsers,
                            _agmtWinDomain: agmtWinDomain,
                            _agmtWinSubtree: agmtWinSubtree,
                            _agmtDSSubtree: agmtDSSubtree,
                            _agmtOneWaySync: agmtOneWaySync,
                            _agmtSyncInterval: agmtSyncInterval,
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
            'repl-winsync-agmt', 'set', this.state.agmtName, '--suffix=' + this.props.suffix,
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
        if (this.state.agmtSyncGroups != this.state._agmtSyncGroups) {
            let val = "off";
            if (this.state.agmtSyncGroups) {
                val = "on";
            }
            cmd.push('--sync-groups=' + val);
        }
        if (this.state.agmtSyncUsers != this.state._agmtSyncUsers) {
            let val = "off";
            if (this.state.agmtSyncUsers) {
                val = "on";
            }
            cmd.push('--sync-users=' + val);
        }
        if (this.state.agmtWinDomain != this.state._agmtWinDomain) {
            cmd.push('--win-domain=' + this.state.agmtWinDomain);
        }
        if (this.state.agmtWinSubtree != this.state._agmtWinSubtree) {
            cmd.push('--win-subtree=' + this.state.agmtWinSubtree);
        }
        if (this.state.agmtDSSubtree != this.state._agmtDSSubtree) {
            cmd.push('--ds-subtree=' + this.state.agmtDSSubtree);
        }
        if (this.state.agmtOneWaySync != this.state._agmtOneWaySync) {
            cmd.push('--one-way-sync=' + this.state.agmtOneWaySync);
        }
        if (this.state.agmtSyncInterval != this.state._agmtSyncInterval) {
            cmd.push('--sync-interval=' + this.state.agmtSyncInterval);
        }
        if (this.state.agmtProtocol != this.state._agmtProtocol) {
            cmd.push('--conn-protocol=' + this.state.agmtProtocol);
        }
        if (this.state.agmtBindPW != this.state._agmtBindPW) {
            cmd.push('--bind-passwd=' + this.state.agmtBindPW);
        }
        if (this.state.agmtBindDN != this.state._agmtBindDN) {
            cmd.push('--bind-passwd=' + this.state.agmtBindDN);
        }
        if (this.state.agmtFracAttrs != this.state._agmtFracAttrs) {
            cmd.push('--frac-list=' + this.state.agmtFracAttrs.join(' '));
        }
        if (this.state.agmtHost != this.state._agmtHost) {
            cmd.push('--host=' + this.state.agmtHost);
        }
        if (this.state.agmtPort != this.state._agmtPort) {
            cmd.push('--port=' + this.state.agmtPort);
        }

        this.setState({
            savingAgmt: true
        });
        log_cmd('saveAgmt', 'update winsync agreement', cmd);
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
                        'Successfully updated winsync agreement'
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to update winsync agreement - ${errMsg.desc}`
                    );
                    this.setState({
                        savingAgmt: false
                    });
                });
    }

    pokeAgmt (agmtName) {
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'poke', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('pokeAgmt', 'send updates now', cmd);
        cockpit
                .spawn(cmd, { superuser: true, "err": "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully poked winsync agreement'
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        'error',
                        `Failed to poke winsync agreement - ${errMsg.desc}`
                    );
                });
    }

    initAgmt () {
        let init_cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'init', '--suffix=' + this.props.suffix, this.state.agmtName];
        log_cmd('initAgmt', 'Initialize agreement', init_cmd);
        cockpit
                .spawn(init_cmd, { superuser: true, "err": "message" })
                .done(content => {
                    var agmtIntervalCount = this.state.agmtInitCounter + 1;
                    var intervals = this.state.agmtInitIntervals;
                    this.props.reload(this.props.suffix);
                    intervals[agmtIntervalCount] = setInterval(this.watchAgmtInit, 2000, this.state.agmtName, agmtIntervalCount);
                    // This triggers error and does not actually work
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
                        `Failed to initialize winsync agreement - ${errMsg.desc}`
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
            'repl-winsync-agmt', 'enable', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('enableAgmt', 'enable agmt', cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully enabled winsync agreement');
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to enabled winsync agreement - ${errMsg.desc}`
                    );
                });
    }

    disableAgmt (agmtName) {
        // Enable/disable agmt
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'disable', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('disableAgmt', 'Disable agmt', cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully disabled winsync agreement');
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to disable winsync agreement - ${errMsg.desc}`
                    );
                });
    }

    deleteAgmt () {
        this.setState({
            deleteSpinning: true
        });
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'delete', '--suffix=' + this.props.suffix, this.state.agmtName];
        log_cmd('deleteAgmt', 'Delete agmt', cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully deleted winsync agreement');
                    this.setState({
                        showDeleteConfirm: false,
                        deleteSpinning: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete winsync agreement - ${errMsg.desc}`
                    );
                    this.setState({
                        showDeleteConfirm: false,
                        deleteSpinning: false
                    });
                });
    }

    createAgmt () {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'create', this.state.agmtName, '--suffix=' + this.props.suffix,
            '--host=' + this.state.agmtHost, '--port=' + this.state.agmtPort,
            '--conn-protocol=' + this.state.agmtProtocol,
            '--bind-dn=' + this.state.agmtBindDN, '--bind-passwd=' + this.state.agmtBindPW,
            '--ds-subtree=' + this.state.agmtDSSubtree, '--win-subtree=' + this.state.agmtWinSubtree,
            '--win-domain=' + this.state.agmtWinDomain, '--one-way-sync=' + this.state.agmtOneWaySync
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

        if (this.state.agmtFracAttrs.length > 0) {
            cmd.push('--frac-list=' + this.state.agmtFracAttrs.join(' '));
        }
        if (this.state.agmtSyncGroups) {
            cmd.push('--sync-groups=on');
        }
        if (this.state.agmtSyncUsers) {
            cmd.push('--sync-users=on');
        }
        if (this.state.agmtSyncInterval != "") {
            cmd.push('--sync-interval=' + this.state.agmtSyncInterval);
        }

        this.setState({
            savingAgmt: true
        });
        log_cmd('createAgmt', 'Create winsync agreement', cmd);
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
                        'Successfully created winsync agreement'
                    );
                    if (this.state.agmtInit == 'online-init') {
                        this.initAgmt(this.state.agmtName);
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to create winsync agreement - ${errMsg.desc}`
                    );
                    this.setState({
                        savingAgmt: false
                    });
                });
    }

    watchAgmtInit(agmtName, idx) {
        // Watch the init, then clear the interval index
        let status_cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'init-status', '--suffix=' + this.props.suffix, agmtName];
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
                <WinsyncAgmtModal
                    showModal={this.state.showCreateAgmtModal}
                    closeHandler={this.closeCreateAgmtModal}
                    handleChange={this.handleChange}
                    handleFracChange={this.handleTAFracAttrChange}
                    saveHandler={this.createAgmt}
                    spinning={this.state.savingAgmt}
                    agmtName={this.state.agmtName}
                    agmtHost={this.state.agmtHost}
                    agmtPort={this.state.agmtPort}
                    agmtBindDN={this.state.agmtBindDN}
                    agmtBindPW={this.state.agmtBindPW}
                    agmtBindPWConfirm={this.state.agmtBindPWConfirm}
                    agmtProtocol={this.state.agmtProtocol}
                    agmtFracAttrs={this.state.agmtFracAttrs}
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
                    agmtSyncGroups={this.state.agmtSyncGroups}
                    agmtSyncUsers={this.state.agmtSyncUsers}
                    agmtWinDomain={this.state.agmtWinDomain}
                    agmtWinSubtree={this.state.agmtWinSubtree}
                    agmtDSSubtree={this.state.agmtDSSubtree}
                    agmtOneWaySync={this.state.agmtOneWaySync}
                    agmtSyncInterval={this.state.agmtSyncInterval}
                    availAttrs={this.props.attrs}
                    error={this.state.errObj}
                    errorMsg={this.state.modalMsg}
                    errorScheduleMsg={this.state.modalScheduleMsg}
                    saveOK={this.state.agmtSaveOK}
                />
                <WinsyncAgmtModal
                    showModal={this.state.showEditAgmtModal}
                    closeHandler={this.closeEditAgmtModal}
                    handleChange={this.handleChange}
                    handleFracChange={this.handleTAFracAttrChangeEdit}
                    saveHandler={this.saveAgmt}
                    spinning={this.state.savingAgmt}
                    agmtName={this.state.agmtName}
                    agmtHost={this.state.agmtHost}
                    agmtPort={this.state.agmtPort}
                    agmtBindDN={this.state.agmtBindDN}
                    agmtBindPW={this.state.agmtBindPW}
                    agmtBindPWConfirm={this.state.agmtBindPWConfirm}
                    agmtProtocol={this.state.agmtProtocol}
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
                    agmtSyncGroups={this.state.agmtSyncGroups}
                    agmtSyncUsers={this.state.agmtSyncUsers}
                    agmtWinDomain={this.state.agmtWinDomain}
                    agmtWinSubtree={this.state.agmtWinSubtree}
                    agmtDSSubtree={this.state.agmtDSSubtree}
                    agmtOneWaySync={this.state.agmtOneWaySync}
                    agmtSyncInterval={this.state.agmtSyncInterval}
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
                    mTitle="Delete Winsync Agreement"
                    mMsg="Are you sure you want to delete this winsync agreement"
                    mSpinningMsg="Deleting Winsync Agreement ..."
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
                    mTitle="Initialize Winsync Agreement"
                    mMsg="Are you sure you want to initialize this winsync agreement"
                    mSpinningMsg="Initializing Winsync Agreement ..."
                    mBtnName="Initialize Agreement"
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmEnableAgmt}
                    closeHandler={this.closeConfirmEnableAgmt}
                    actionFunc={this.enableAgmt}
                    actionParam={this.state.agmtName}
                    msg="Are you sure you want to enable this winsync agreement?"
                    msgContent={this.state.agmtName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmDisableAgmt}
                    closeHandler={this.closeConfirmDisableAgmt}
                    actionFunc={this.disableAgmt}
                    actionParam={this.state.agmtName}
                    msg="Are you sure you want to disable this winsync agreement?"
                    msgContent={this.state.agmtName}
                />
            </div>
        );
    }
}

WinsyncAgmts.propTypes = {
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    rows: PropTypes.array,
    addNotification: PropTypes.func,
};

WinsyncAgmts.defaultProps = {
    serverId: "",
    suffix: "",
    rows: [],
    addNotification: noop,
};
