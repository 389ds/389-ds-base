import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { ReplAgmtTable } from "./replTables.jsx";
import { WinsyncAgmtModal } from "./replModals.jsx";
import {
    log_cmd, valid_dn, valid_port,
    listsEqual, callCmdStreamPassword
} from "../tools.jsx";
import PropTypes from "prop-types";
import {
    Button,
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
            agmtStartTime: "0000",
            agmtEndTime: "2359",
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

            isExcludeAttrCreateOpen: false,
            isExcludeAttrEditOpen: false,
        };

        // Create - Exclude Attributes
        this.onExcludeAttrCreateToggle = isExcludeAttrCreateOpen => {
            this.setState({
                isExcludeAttrCreateOpen
            });
        };
        this.onExcludeAttrCreateClear = () => {
            this.setState({
                agmtFracAttrs: [],
                isExcludeAttrCreateOpen: false
            });
        };

        // Edit - Exclude Attributes
        this.onExcludeAttrEditToggle = isExcludeAttrEditOpen => {
            this.setState({
                isExcludeAttrEditOpen
            });
        };
        this.onExcludeAttrEditClear = () => {
            this.setState({
                agmtFracAttrs: [],
                isExcludeAttrEditOpen: false
            });
        };

        this.showCreateAgmtModal = this.showCreateAgmtModal.bind(this);
        this.closeCreateAgmtModal = this.closeCreateAgmtModal.bind(this);
        this.closeEditAgmtModal = this.closeEditAgmtModal.bind(this);
        this.handleTASelectChange = this.handleTASelectChange.bind(this);
        this.handleTimeChange = this.handleTimeChange.bind(this);
        this.handleCreateChange = this.handleCreateChange.bind(this);
        this.handleEditChange = this.handleEditChange.bind(this);
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
        const rows = JSON.parse(JSON.stringify(this.props.rows));
        this.setState({ rows: rows });
    }

    componentWillUnmount () {
        this._mounted = false;
    }

    handleModalChange (e) {
        this.setState({
            [e.target.id]: e.target.checked,
        });
    }

    validateConfig(attr, value, errObj) {
        // Validate the current Settings
        let all_good = true;
        const configAttrs = [
            'agmtName', 'agmtHost', "agmtWinDomain"
        ];
        const dnAttrs = [
            'agmtBindDN', 'agmtWinSubtree', 'agmtWinSubtree', 'agmtDSSubtree'
        ];

        // If we disable
        if (attr == 'agmtSync' && value == true) {
            errObj.agmtSyncMon = false;
            errObj.agmtSyncTue = false;
            errObj.agmtSyncWed = false;
            errObj.agmtSyncThu = false;
            errObj.agmtSyncFri = false;
            errObj.agmtSyncSat = false;
            errObj.agmtSyncSun = false;
            errObj.agmtStartTime = false;
            errObj.agmtEndTime = false;
        }

        for (const configAttr of configAttrs) {
            if (attr == configAttr) {
                if (value == "") {
                    errObj[attr] = true;
                    all_good = false;
                } else {
                    errObj[attr] = false;
                }
            } else if (this.state[configAttr] == "") {
                errObj[configAttr] = true;
                all_good = false;
            }
        }

        for (const dnAttr of dnAttrs) {
            if (attr == dnAttr) {
                if (!valid_dn(value)) {
                    errObj[dnAttr] = true;
                    all_good = false;
                } else {
                    errObj[attr] = false;
                }
            } else if (!valid_dn(this.state[dnAttr])) {
                errObj[dnAttr] = true;
                all_good = false;
            }
        }

        if (attr == 'agmtPort') {
            if (!valid_port(value)) {
                errObj.agmtPort = true;
                all_good = false;
            } else {
                errObj.agmtPort = false;
            }
        }

        // Check passwords match
        if (attr == 'agmtBindPW') {
            if (value != this.state.agmtBindPWConfirm || value == "") {
                errObj.agmtBindPW = true;
                errObj.agmtBindPWConfirm = true;
                all_good = false;
            } else {
                errObj.agmtBindPW = false;
                errObj.agmtBindPWConfirm = false;
            }
        } else if (attr == 'agmtBindPWConfirm') {
            if (value != this.state.agmtBindPW || value == "") {
                errObj.agmtBindPW = true;
                errObj.agmtBindPWConfirm = true;
                all_good = false;
            } else {
                errObj.agmtBindPW = false;
                errObj.agmtBindPWConfirm = false;
            }
        } else if (this.state.agmtBindPW != this.state.agmtBindPWConfirm || this.state.agmtBindPW == "" || this.state.agmtBindPWConfirm == "") {
            // Not a pasword change, but the values are no good
            errObj.agmtBindPW = true;
            errObj.agmtBindPWConfirm = true;
            all_good = false;
        }

        if (attr == 'agmtSync') {
            if (value) {
                // Just set all the days and let the user remove days as needed
                errObj.agmtStartTime = false;
                errObj.agmtEndTime = false;
                this.setState({
                    agmtSyncMon: true,
                    agmtSyncTue: true,
                    agmtSyncWed: true,
                    agmtSyncThu: true,
                    agmtSyncFri: true,
                    agmtSyncSat: true,
                    agmtSyncSun: true,
                    agmtStartTime: "0000",
                    agmtEndTime: "2359",
                });
            }
        } else if (this.state.agmtSync) {
            // Check the days first
            let have_days = false;
            const days = [
                "agmtSyncSun", "agmtSyncMon", "agmtSyncTue", "agmtSyncWed",
                "agmtSyncThu", "agmtSyncFri", "agmtSyncSat"
            ];
            for (const day of days) {
                if ((attr != day && this.state[day]) || (attr == day && value)) {
                    have_days = true;
                    break;
                }
            }
            errObj.agmtSyncSun = false;
            errObj.agmtSyncMon = false;
            errObj.agmtSyncTue = false;
            errObj.agmtSyncWed = false;
            errObj.agmtSyncThu = false;
            errObj.agmtSyncFri = false;
            errObj.agmtSyncSat = false;
            if (!have_days) {
                errObj.agmtSyncSun = true;
                errObj.agmtSyncMon = true;
                errObj.agmtSyncTue = true;
                errObj.agmtSyncWed = true;
                errObj.agmtSyncThu = true;
                errObj.agmtSyncFri = true;
                errObj.agmtSyncSat = true;
                all_good = false;
            } else if (attr == 'agmtStartTime') {
                if (value == "") {
                    all_good = false;
                    errObj.agmtStartTime = true;
                } else if (value >= this.state.agmtEndTime) {
                    errObj.agmtStartTime = true;
                    all_good = false;
                } else {
                    // All good, reset form
                    errObj.agmtStartTime = false;
                    errObj.agmtEndTime = false;
                }
            } else if (attr == 'agmtEndTime') {
                if (value == "") {
                    errObj.agmtEndTime = true;
                    all_good = false;
                } else if (this.state.agmtStartTime >= value) {
                    errObj.agmtStartTime = true;
                    all_good = false;
                } else {
                    // All good, reset form
                    errObj.agmtStartTime = false;
                    errObj.agmtEndTime = false;
                }
            } else if (this.state.agmtStartTime >= this.state.agmtEndTime) {
                errObj.agmtStartTime = true;
                all_good = false;
            }
        }

        return all_good;
    }

    handleTimeChange(action, attr, val) {
        let value = val.replace(":", "");
        const errObj = this.state.errObj;
        const e = { target: { id: 'dummy', value: "", type: 'input' } };

        if (value == "") {
            value = "0000";
        }
        if (attr == "agmtStartTime") {
            if (value > this.state.agmtEndTime) {
                errObj.agmtStartTime = true;
            } else {
                errObj.agmtStartTime = false;
                errObj.agmtEndTime = false;
            }
        } else if (attr == "agmtEndTime") {
            if (this.state.agmtStartTime > value) {
                errObj.agmtEndTime = true;
            } else {
                errObj.agmtEndTime = false;
                errObj.agmtStartTime = false;
            }
        }

        this.setState({
            [attr]: value,
            errObj: errObj,
        }, () => { action == "edit" ? this.handleEditChange(e) : this.handleCreateChange(e) });
    }

    handleCreateChange (e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        const errObj = this.state.errObj;
        let all_good = true;

        if (e.target.type == "time") {
            // Strip out the colon from the time
            value = value.replace(':', '');
        }

        all_good = this.validateConfig(attr, value, errObj);

        this.setState({
            [attr]: value,
            errObj: errObj,
            agmtSaveOK: all_good,
            [e.target.toggle]: false
        });
    }

    handleEditChange (e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        const errObj = this.state.errObj;
        let all_good = true;
        errObj[attr] = false;

        if (e.target.type == "time") {
            // Strip out the colon from the time
            value = value.replace(':', '');
        }

        all_good = this.validateConfig(attr, value, errObj);

        if (all_good) {
            // All the values are valid, but did something change that warrants
            // the save button to be enabled?
            all_good = false;
            if ((attr != 'agmtHost' && this.state.agmtHost != this.state._agmtHost) ||
                (attr != 'agmtPort' && this.state.agmtPort != this.state._agmtPort) ||
                (attr != 'agmtBindDN' && this.state.agmtBindDN != this.state._agmtBindDN) ||
                (attr != 'agmtBindMethod' && this.state.agmtBindMethod != this.state._agmtBindMethod) ||
                (attr != 'agmtProtocol' && this.state.agmtProtocol != this.state._agmtProtocol) ||
                (attr != 'agmtSync' && this.state.agmtSync != this.state._agmtSync) ||
                (attr != 'agmtFracAttrs' && !listsEqual(this.state.agmtFracAttrs, this.state._agmtFracAttrs)) ||
                (attr != 'agmtSyncGroups' && this.state.agmtSyncGroups != this.state._agmtSyncGroups) ||
                (attr != 'agmtSyncUsers' && this.state.agmtSyncUsers != this.state._agmtSyncUsers) ||
                (attr != 'agmtWinDomain' && this.state.agmtWinDomain != this.state._agmtWinDomain) ||
                (attr != 'agmtWinSubtree' && this.state.agmtWinSubtree != this.state._agmtWinSubtree) ||
                (attr != 'agmtDSSubtree' && this.state.agmtDSSubtree != this.state._agmtDSSubtree) ||
                (attr != 'agmtOneWaySync' && this.state.agmtOneWaySync != this.state._agmtOneWaySync) ||
                (attr != 'agmtSyncInterval' && this.state.agmtSyncInterval != this.state._agmtSyncInterval)) {
                all_good = true;
            }
            if ((attr != "agmtSync" && this.state.agmtSync) || (attr == "agmtSync" && value)) {
                if ((attr != 'agmtSyncMon' && this.state.agmtSyncMon != this.state._agmtSyncMon) ||
                    (attr != 'agmtSyncTue' && this.state.agmtSyncTue != this.state._agmtSyncTue) ||
                    (attr != 'agmtSyncWed' && this.state.agmtSyncWed != this.state._agmtSyncWed) ||
                    (attr != 'agmtSyncThu' && this.state.agmtSyncThu != this.state._agmtSyncThu) ||
                    (attr != 'agmtSyncFri' && this.state.agmtSyncFri != this.state._agmtSyncFri) ||
                    (attr != 'agmtSyncSat' && this.state.agmtSyncSat != this.state._agmtSyncSat) ||
                    (attr != 'agmtSyncSun' && this.state.agmtSyncSun != this.state._agmtSyncSun) ||
                    (attr != 'agmtStartTime' && this.state.agmtStartTime != this.state._agmtStartTime) ||
                    (attr != 'agmtEndTime' && this.state.agmtEndTime != this.state._agmtEndTime)) {
                    all_good = true;
                }
            }
            if (attr == 'agmtFracAttrs' && !this.listEqual(value, this.state._agmtFracAttrs)) {
                all_good = true;
            } else if (attr != 'dummy' && value != this.state['_' + attr]) {
                all_good = true;
            }
        }

        this.setState({
            [attr]: value,
            errObj: errObj,
            agmtSaveOK: all_good,
            [e.target.toggle]: false
        });
    }

    handleTASelectChange (e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let valueErr = false;
        const errObj = this.state.errObj;
        if (value == "") {
            valueErr = true;
        }
        errObj[attr] = valueErr;

        // We handle strings and arrays here, need to find a better way to differentiate.
        if (attr.endsWith('Attrs')) {
            if (this.state[attr].includes(value)) {
                this.setState(
                    (prevState) => ({
                        [attr]: prevState[attr].filter((item) => item !== e.target.value),
                        errObj: errObj,
                        [e.target.toggle]: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        [attr]: [...prevState[attr], value],
                        errObj: errObj,
                        [e.target.toggle]: false
                    }),
                );
            }
        } else {
            this.setState({
                [attr]: value,
                errObj: errObj,
                [e.target.toggle]: false
            });
        }
    }

    handleTAFracAttrChangeEdit (selection) {
        // TypeAhead handling
        const { agmtFracAttrs } = this.state;
        const e = { target: { id: 'dummy', value: "", type: 'input' } };
        if (agmtFracAttrs.includes(selection)) {
            const new_values = this.state.agmtFracAttrs.filter(item => item !== selection);
            this.setState({
                agmtFracAttrs: new_values,
                isExcludeAttrsEditOpen: false,
            }, () => { this.handleEditChange(e) });
        } else {
            const new_values = [...this.state.agmtFracAttrs, selection];
            this.setState({
                agmtFracAttrs: new_values,
                isExcludeAttrsEditOpen: false,
            }, () => { this.handleEditChange(e) });
        }
    }

    handleTAFracAttrChange (values) {
        // TypeAhead handling
        const e = {
            target: {
                name: 'agmt-modal',
                id: 'agmtFracAttrs',
                value: values,
                type: 'input',
                toggle: 'isExcludeAttrCreateOpen',
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
            agmtPort: "636",
            agmtProtocol: "LDAP",
            agmtBindDN: "",
            agmtBindPW: "",
            agmtBindPWConfirm: "",
            agmtFracAttrs: [],
            agmtSync: false,
            agmtSyncMon: true,
            agmtSyncTue: true,
            agmtSyncWed: true,
            agmtSyncThu: true,
            agmtSyncFri: true,
            agmtSyncSat: true,
            agmtSyncSun: true,
            agmtStartTime: "0000",
            agmtEndTime: "2359",
            agmtInit: "noinit",
            agmtSaveOK: false,
            agmtSyncGroups: false,
            agmtSyncUsers: true,
            agmtWinDomain: "",
            agmtWinSubtree: "",
            agmtDSSubtree: "",
            agmtOneWaySync: "both", // "both", "toWindows", "fromWindows"
            agmtSyncInterval: "",
            errObj: {
                // Marks all these fields as required
                agmtName: true,
                agmtHost: true,
                agmtBindDN: true,
                agmtBindPW: true,
                agmtBindPWConfirm: true,
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
        const cmd = [
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
                    let agmtSync = false;
                    let agmtSyncMon = false;
                    let agmtSyncTue = false;
                    let agmtSyncWed = false;
                    let agmtSyncThu = false;
                    let agmtSyncFri = false;
                    let agmtSyncSat = false;
                    let agmtSyncSun = false;
                    let agmtStartTime = "0000";
                    let agmtEndTime = "2359";
                    let agmtSyncGroups = false;
                    let agmtSyncUsers = false;
                    let agmtWinDomain = "";
                    let agmtWinSubtree = "";
                    let agmtDSSubtree = "";
                    let agmtOneWaySync = "both";
                    let agmtSyncInterval = "";

                    for (const attr in config.attrs) {
                        const val = config.attrs[attr][0];
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
                            const attrs = val.replace("(objectclass=*) $ EXCLUDE", "").trim();
                            agmtFracAttrs = attrs.split(' ');
                        }
                        if (attr == "nsds5replicaupdateschedule") {
                            agmtSync = true;
                            // Parse schedule
                            const parts = val.split(' ');
                            const times = parts[0].split('-');
                            const days = parts[1];

                            // Do the times
                            agmtStartTime = times[0];
                            agmtEndTime = times[1];

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
                    const errMsg = JSON.parse(err);
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

        let passwd = "";

        // Handle Schedule
        if (this.state.agmtSync) {
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
        } else if (this.state.agmtSync != this.state._agmtSync && !this.state.agmtSync) {
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
            let value = this.state.agmtOneWaySync;
            if (value == "both") {
                value = "";
            }
            cmd.push('--one-way-sync=' + value);
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

        // Something changed, perform the update
        const config = {
            cmd: cmd,
            promptArg: "--bind-passwd-prompt",
            passwd: passwd,
            addNotification: this.props.addNotification,
            success_msg: "Successfully updated winsync agreement",
            error_msg: "Failed to update winsync agreement",
            state_callback: () => {
                this.setState({
                    savingAgmt: false,
                    showEditAgmtModal: false,
                })
            },
            reload_func: this.props.reload,
            reload_arg: this.props.suffix,
            funcName: "saveAgmt",
            funcDesc: "update winsync agreement"
        };
        callCmdStreamPassword(config);
    }

    pokeAgmt (agmtName) {
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'poke', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('pokeAgmt', 'send updates now', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully poked winsync agreement'
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        'error',
                        `Failed to poke winsync agreement - ${errMsg.desc}`
                    );
                });
    }

    initAgmt () {
        this.setState({
            modalSpinning: true
        });
        const init_cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'init', '--suffix=' + this.props.suffix, this.state.agmtName];
        log_cmd('initAgmt', 'Initialize winsync agreement', init_cmd);
        cockpit
                .spawn(init_cmd, { superuser: true, err: "message" })
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
                    const errMsg = JSON.parse(err);
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
                showConfirmDisableAgmt: true,
                modalSpinning: false,
                modalChecked: false,
            });
        } else {
            this.setState({
                agmtName: agmtName,
                showConfirmEnableAgmt: true,
                modalSpinning: false,
                modalChecked: false,
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
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'enable', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('enableAgmt', 'enable agmt', cmd);
        this.setState({
            modalSpinning: true
        });
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully enabled winsync agreement');
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to enabled winsync agreement - ${errMsg.desc}`
                    );
                });
    }

    disableAgmt (agmtName) {
        // Enable/disable agmt
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'disable', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('disableAgmt', 'Disable agmt', cmd);
        this.setState({
            modalSpinning: true
        });
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully disabled winsync agreement');
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
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
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'delete', '--suffix=' + this.props.suffix, this.state.agmtName];
        log_cmd('deleteAgmt', 'Delete agmt', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully deleted winsync agreement');
                    this.setState({
                        showConfirmDeleteAgmt: false,
                        deleteSpinning: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete winsync agreement - ${errMsg.desc}`
                    );
                    this.setState({
                        showConfirmDeleteAgmt: false,
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
            '--bind-dn=' + this.state.agmtBindDN,
            '--ds-subtree=' + this.state.agmtDSSubtree, '--win-subtree=' + this.state.agmtWinSubtree,
            '--win-domain=' + this.state.agmtWinDomain, '--one-way-sync=' + this.state.agmtOneWaySync
        ];

        let passwd = this.state.agmtBindPW;

        // Handle Schedule
        if (this.state.agmtSync) {
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

        // Something changed, perform the update
        let ext_func = ""
        if (this.state.agmtInit === 'online-init') {
            ext_func = this.initAgmt;
        }

        log_cmd('createAgmt', 'Create winsync agmt', cmd);
        const config = {
            cmd: cmd,
            promptArg: "--bind-passwd-prompt",
            passwd: passwd,
            addNotification: this.props.addNotification,
            success_msg: "Successfully created winsync agreement",
            error_msg: "Failed to create winsync agreement",
            state_callback: () => {
                this.setState({
                    savingAgmt: false,
                    showCreateAgmtModal: false,
                })
            },
            reload_func: this.props.reload,
            reload_arg: this.props.suffix,
            ext_func: ext_func,
            ext_arg: this.state.agmtName,
            funcName: "createAgmt",
            funcDesc: "Create winsync agreement"
        };
        callCmdStreamPassword(config);
    }

    watchAgmtInit(agmtName, idx) {
        // Watch the init, then clear the interval index
        const status_cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-winsync-agmt', 'init-status', '--suffix=' + this.props.suffix, agmtName];
        log_cmd('watchAgmtInit', 'Get initialization status for agmt', status_cmd);
        cockpit
                .spawn(status_cmd, { superuser: true, err: "message" })
                .done(data => {
                    const init_status = JSON.parse(data);
                    if (init_status.startsWith('Agreement successfully initialized') ||
                        init_status.startsWith('Agreement initialization failed')) {
                        // Either way we're done, stop watching the status
                        clearInterval(this.state.agmtInitIntervals[idx]);
                        this.props.reload(this.props.suffix);
                    }
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
        const val = value.toLowerCase();
        for (const row of this.state.rows) {
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
                <div className="ds-margin-top ds-container ds-inline ds-margin-bottom-md">
                    <Button
                        variant="primary"
                        onClick={this.showCreateAgmtModal}
                    >
                        Create Agreement
                    </Button>
                    <Button
                        className="ds-left-margin"
                        variant="secondary"
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
                    handleChange={this.handleCreateChange}
                    handleTimeChange={this.handleTimeChange}
                    handleFracChange={this.handleTAFracAttrChange}
                    onSelectToggle={this.onExcludeAttrCreateToggle}
                    onSelectClear={this.onExcludeAttrCreateClear}
                    isExcludeAttrOpen={this.state.isExcludeAttrCreateOpen}
                    saveHandler={this.createAgmt}
                    getToggleId={this.getToggleId}
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
                    agmtInit={this.state.agmtInit}
                    availAttrs={this.props.attrs}
                    error={this.state.errObj}
                    saveOK={this.state.agmtSaveOK}
                />
                <WinsyncAgmtModal
                    key={this.state.showEditAgmtModal ? "edit1" : "edit0"}
                    showModal={this.state.showEditAgmtModal}
                    closeHandler={this.closeEditAgmtModal}
                    handleChange={this.handleEditChange}
                    handleTimeChange={this.handleTimeChange}
                    handleFracChange={this.handleTAFracAttrChangeEdit}
                    onSelectToggle={this.onExcludeAttrEditToggle}
                    onSelectClear={this.onExcludeAttrEditClear}
                    isExcludeAttrOpen={this.state.isExcludeAttrEditOpen}
                    getToggleId={this.getToggleId}
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
                    agmtInit={this.state.agmtInit}
                    availAttrs={this.props.attrs}
                    error={this.state.errObj}
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
                    mMsg="Are you sure you want to initialize this winsync agreement?"
                    mSpinningMsg="Initializing Winsync Agreement ..."
                    mBtnName="Initialize Agreement"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmEnableAgmt}
                    closeHandler={this.closeConfirmEnableAgmt}
                    handleChange={this.handleModalChange}
                    actionHandler={this.enableAgmt}
                    spinning={this.state.modalSpinning}
                    item={this.state.agmtName}
                    checked={this.state.modalChecked}
                    mTitle="Enable Winsync Agreement"
                    mMsg="Are you sure you want to enable this winsync agreement?"
                    mSpinningMsg="Enabling Winsync Agreement ..."
                    mBtnName="Enable Agreement"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDisableAgmt}
                    closeHandler={this.closeConfirmDisableAgmt}
                    handleChange={this.handleModalChange}
                    actionHandler={this.disableAgmt}
                    spinning={this.state.modalSpinning}
                    item={this.state.agmtName}
                    checked={this.state.modalChecked}
                    mTitle="Disable Winsync Agreement"
                    mMsg="Are you sure you want to disable this winsync agreement?"
                    mSpinningMsg="Disabling Winsync Agreement ..."
                    mBtnName="Disable Agreement"
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
};
