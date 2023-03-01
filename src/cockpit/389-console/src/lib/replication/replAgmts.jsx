import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { ReplAgmtTable } from "./replTables.jsx";
import { ReplAgmtModal } from "./replModals.jsx";
import { log_cmd, valid_dn, valid_port, listsEqual } from "../tools.jsx";
import PropTypes from "prop-types";
import {
    Button,
} from "@patternfly/react-core";
import {
    SortByDirection,
} from '@patternfly/react-table';

const ldapOptions = ['SIMPLE', 'SASL/DIGEST-MD5', 'SASL/GSSAPI'];
const ldapsOptions = ['SIMPLE', 'SSLCLIENTAUTH'];
const ldapBootstrapOptions = ['SIMPLE'];
const ldapsBootstrapOptions = ['SIMPLE', 'SSLCLIENTAUTH'];

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
            savingAgmt: false,
            mounted: false,
            rows: [],
            page: 1,
            value: "",
            sortBy: {},
            // Create agreement
            agmtName: "",
            agmtHost: "",
            agmtPort: "636",
            agmtProtocol: "LDAPS",
            agmtBindMethod: "SIMPLE",
            agmtBindMethodOptions: ldapsOptions,
            agmtBindDN: "cn=replication manager,cn=config",
            agmtBindPW: "",
            agmtBindPWConfirm: "",
            agmtBootstrap: false,
            agmtBootstrapProtocol: "LDAPS",
            agmtBootstrapBindMethod: "SIMPLE",
            agmtBootstrapBindMethodOptions: ldapsBootstrapOptions,
            agmtBootstrapBindDN: "cn=replication manager,cn=config",
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
            agmtStartTime: "0000",
            agmtEndTime: "2359",
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
        this.handleTASelectChange = this.handleTASelectChange.bind(this);
        this.handleTimeChange = this.handleTimeChange.bind(this);
        this.handleCreateChange = this.handleCreateChange.bind(this);
        this.handleEditChange = this.handleEditChange.bind(this);
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
        this.validateConfig = this.validateConfig.bind(this);
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

    validateConfig(attr, value, modalData, errObj) {
        // Validate the current Settings
        let all_good = true;
        const configAttrs = [
            'agmtName', 'agmtHost'
        ];
        const dnAttrs = [
            'agmtBindDN',
        ];

        // If we disable
        if (attr == 'agmtBootstrap' && value == false) {
            errObj.agmtBootstrapBindDN = false;
            errObj.agmtBootstrapBindPW = false;
            errObj.agmtBootstrapBindPWConfirm = false;
        }
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
        } else if (attr == 'agmtProtocol') {
            if (value == "LDAP") {
                modalData.agmtBindMethodOptions = ldapOptions;
            } else {
                modalData.agmtBindMethodOptions = ldapsOptions;
            }
            if (modalData.agmtBindMethodOptions.indexOf(this.state.agmtBindMethod) == -1) {
                // Auto adjust the current state to account for the new method
                modalData.agmtBindMethod = modalData.agmtBindMethodOptions[0];
            }
        } else if (attr == "agmtBindMethod") {
            modalData.agmtBindMethod = value;
        } else if (attr == "agmtBootstrapBindMethod") {
            modalData.agmtBootstrapBindMethod = value;
        } else if (attr == 'agmtBootstrapProtocol') {
            if (value == "LDAP") {
                modalData.agmtBootstrapBindMethodOptions = ldapBootstrapOptions;
            } else {
                modalData.agmtBootstrapBindMethodOptions = ldapsBootstrapOptions;
            }
            if (modalData.agmtBootstrapBindMethodOptions.indexOf(this.state.agmtBootstrapBindMethod) == -1) {
                // Auto adjust the current state to account for the new method
                modalData.agmtBootstrapBindMethod = modalData.agmtBootstrapBindMethodOptions[0];
            }
        }

        // Check passwords match
        if (attr == 'agmtBindMethod' && (value != "SIMPLE" && value != "SASL/DIGEST-MD5")) {
            errObj.agmtBindPW = false;
            errObj.agmtBindPWConfirm = false;
        } else if ((this.state.agmtBindMethod == "SIMPLE" || this.state.agmtBindMethod == "SASL/DIGEST-MD5") ||
                   (attr == 'agmtBindMethod' && (value == "SIMPLE" || value == "SASL/DIGEST-MD5"))) {
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
        } else {
            errObj.agmtBindPW = false;
            errObj.agmtBindPWConfirm = false;
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
                // Bootstrapping is enabled, validate the settings
                if (this.state.agmtBootstrapBindMethod == "SIMPLE") {
                    if (this.state.agmtBootstrapBindPW == "" || this.state.agmtBootstrapBindPWConfirm == "") {
                        // Can't be empty
                        errObj.agmtBootstrapBindPW = true;
                        errObj.agmtBootstrapBindPWConfirm = true;
                        all_good = false;
                    } else if (this.state.agmtBootstrapBindPW != this.state.agmtBootstrapBindPWConfirm) {
                        // Must match
                        errObj.agmtBootstrapBindPW = true;
                        errObj.agmtBootstrapBindPWConfirm = true;
                        all_good = false;
                    } else {
                        errObj.agmtBootstrapProtocol = false;
                        errObj.agmtBootstrapBindMethod = false;
                    }
                } else {
                    // All good, reset the errObj
                    errObj.agmtBootstrapProtocol = false;
                    errObj.agmtBootstrapBindMethod = false;
                }
                if (this.state.agmtBootstrapBindDN == "" || !valid_dn(this.state.agmtBootstrapBindDN)) {
                    errObj.agmtBootstrapBindDN = true;
                    all_good = false;
                }
            }
        } else if (this.state.agmtBootstrap) {
            // Check all the bootstrap settings
            if (attr == "agmtBootstrapBindDN") {
                if (value == "" || !valid_dn(value)) {
                    errObj.agmtBootstrapBindDN = true;
                    all_good = false;
                } else {
                    errObj.agmtBootstrapBindDN = false;
                }
            } else if (this.state.agmtBootstrapBindDN == "" || !valid_dn(this.state.agmtBootstrapBindDN)) {
                errObj.agmtBootstrapBindDN = true;
                all_good = false;
            } else {
                // No problems here, make sure the errObj is reset
                errObj.agmtBootstrapBindDN = false;
            }

            if (attr == 'agmtBootstrapBindMethod') {
                // Adjusting the Bind Method, if SIMPLE then verify the
                // passwords are set and correct
                if (value == "SIMPLE") {
                    if (this.state.agmtBootstrapBindPW == "" || this.state.agmtBootstrapBindPWConfirm == "") {
                        // Can't be empty
                        errObj.agmtBootstrapBindPW = true;
                        errObj.agmtBootstrapBindPWConfirm = true;
                        all_good = false;
                    } else if (this.state.agmtBootstrapBindPW != this.state.agmtBootstrapBindPWConfirm) {
                        // Must match
                        errObj.agmtBootstrapBindPW = true;
                        errObj.agmtBootstrapBindPWConfirm = true;
                        all_good = false;
                    }
                } else {
                    // Not SIMPLE, ignore the passwords and reset errObj
                    errObj.agmtBootstrapBindPW = false;
                    errObj.agmtBootstrapBindPWConfirm = false;
                    if (this.state.agmtBootstrapProtocol == "LDAP") {
                        errObj.agmtBootstrapBindMethod = true;
                        all_good = false;
                    } else {
                        // All good, reset the errObj
                        errObj.agmtBootstrapProtocol = false;
                        errObj.agmtBootstrapBindMethod = false;
                    }
                }
            } else if (this.state.agmtBootstrapBindMethod == "SIMPLE") {
                // Current bind method is SIMPLE, check old password values,
                // and new ones.
                if (attr == 'agmtBootstrapBindPW') {
                    // Modifying password
                    if (value == "") {
                        all_good = false;
                        errObj.agmtBootstrapBindPW = true;
                    } else if (value != this.state.agmtBootstrapBindPWConfirm) {
                        errObj.agmtBootstrapBindPW = true;
                        errObj.agmtBootstrapBindPWConfirm = true;
                        all_good = false;
                    } else {
                        errObj.agmtBootstrapBindPW = false;
                        errObj.agmtBootstrapBindPWConfirm = false;
                    }
                } else if (this.state.agmtBootstrapBindPW == "") {
                    // Current value is no good
                    all_good = false;
                }
                if (attr == 'agmtBootstrapBindPWConfirm') {
                    // Modifying password confirmation
                    if (value == "") {
                        all_good = false;
                        errObj.agmtBootstrapBindPWConfirm = true;
                    } else if (value != this.state.agmtBootstrapBindPW) {
                        errObj.agmtBootstrapBindPW = true;
                        errObj.agmtBootstrapBindPWConfirm = true;
                        all_good = false;
                    } else {
                        errObj.agmtBootstrapBindPW = false;
                        errObj.agmtBootstrapBindPWConfirm = false;
                    }
                } else if (this.state.agmtBootstrapBindPWConfirm == "") {
                    // Current value is no good
                    all_good = false;
                }
            } else {
                // Bind method is SSLCLIENTAUTH, make sure the connection protocol is LDAPS
                if (this.state.agmtBootstrapProtocol == "LDAP") {
                    errObj.agmtBootstrapProtocol = true;
                    all_good = false;
                } else {
                    // All good, reset the errObj
                    errObj.agmtBootstrapProtocol = false;
                    errObj.agmtBootstrapBindMethod = false;
                }
            }
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
        const modalData = {
            agmtBindMethod: this.state.agmtBindMethod,
            agmtBindMethodOptions:this.state.agmtBindMethodOptions,
            agmtBootstrapBindMethod: this.state.agmtBootstrapBindMethod,
            agmtBootstrapBindMethodOptions: this.state.agmtBootstrapBindMethodOptions
        };
        if (e.target.type == "time") {
            // Strip out the colon from the time
            value = value.replace(':', '');
        }

        all_good = this.validateConfig(attr, value, modalData, errObj);

        this.setState({
            [attr]: value,
            errObj: errObj,
            agmtSaveOK: all_good,
            agmtBindMethod: modalData.agmtBindMethod,
            agmtBindMethodOptions: modalData.agmtBindMethodOptions,
            agmtBootstrapBindMethod: modalData.agmtBootstrapBindMethod,
            agmtBootstrapBindMethodOptions: modalData.agmtBootstrapBindMethodOptions,
            [e.target.toggle]: false
        });
    }

    handleEditChange (e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        const errObj = this.state.errObj;
        let all_good = false;
        const modalData = {
            agmtBindMethod: this.state.agmtBindMethod,
            agmtBindMethodOptions:this.state.agmtBindMethodOptions,
            agmtBootstrapBindMethod: this.state.agmtBootstrapBindMethod,
            agmtBootstrapBindMethodOptions: this.state.agmtBootstrapBindMethodOptions
        };
        errObj[attr] = false;

        if (e.target.type == "time") {
            // Strip out the colon from the time
            value = value.replace(':', '');
        }

        all_good = this.validateConfig(attr, value, modalData, errObj);

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
                (attr != 'agmtBootstrap' && this.state.agmtBootstrap != this.state._agmtBootstrap) ||
                (attr != 'agmtStripAttrs' && !listsEqual(this.state.agmtStripAttrs, this.state._agmtStripAttrs)) ||
                (attr != 'agmtFracAttrs' && !listsEqual(this.state.agmtFracAttrs, this.state._agmtFracAttrs)) ||
                (attr != 'agmtFracInitAttrs' && !listsEqual(this.state.agmtFracInitAttrs, this.state._agmtFracInitAttrs))) {
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
            if ((attr != "agmtBootstrap" && this.state.agmtBootstrap) || (attr == "agmtBootstrap" && value)) {
                if ((attr != 'agmtBootstrapBindDN' && this.state.agmtBootstrapBindDN != this.state._agmtBootstrapBindDN) ||
                    (attr != 'agmtBootstrapBindPW' && this.state.agmtBootstrapBindPW != this.state._agmtBootstrapBindPW) ||
                    (attr != 'agmtBootstrapBindPWConfirm' && this.state.agmtBootstrapBindPWConfirm != this.state._agmtBootstrapBindPWConfirm) ||
                    (attr != 'agmtBootstrapBindMethod' && this.state.agmtBootstrapBindMethod != this.state._agmtBootstrapBindMethod) ||
                    (attr != 'agmtBootstrapProtocol' && this.state.agmtBootstrapProtocol != this.state._agmtBootstrapProtocol)) {
                    all_good = true;
                }
            }
            if ((attr == 'agmtStripAttrs' && !listsEqual(value, this.state._agmtStripAttrs)) ||
                (attr == 'agmtFracAttrs' && !listsEqual(value, this.state._agmtFracAttrs)) ||
                (attr == 'agmtFracInitAttrs' && !listsEqual(value, this.state._agmtFracInitAttrs))) {
                all_good = true;
            } else if (attr != 'dummy' && value != this.state['_' + attr]) {
                all_good = true;
            }
        }

        this.setState({
            [attr]: value,
            errObj: errObj,
            agmtSaveOK: all_good,
            agmtBindMethod: modalData.agmtBindMethod,
            agmtBindMethodOptions: modalData.agmtBindMethodOptions,
            agmtBootstrapBindMethod: modalData.agmtBootstrapBindMethod,
            agmtBootstrapBindMethodOptions: modalData.agmtBootstrapBindMethodOptions,
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

    handleTAStripAttrChangeEdit (selection) {
        // TypeAhead handling
        const { agmtStripAttrs } = this.state;
        const e = { target: { id: 'dummy', value: "", type: 'input' } };
        if (agmtStripAttrs.includes(selection)) {
            const new_values = this.state.agmtStripAttrs.filter(item => item !== selection);
            this.setState({
                agmtStripAttrs: new_values,
                isStripAttrsEditOpen: false,
            }, () => { this.handleEditChange(e) });
        } else {
            const new_values = [...this.state.agmtStripAttrs, selection];
            this.setState({
                agmtStripAttrs: new_values,
                isStripAttrsEditOpen: false,
            }, () => { this.handleEditChange(e) });
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

    handleTAFracInitAttrChangeEdit (selection) {
        // TypeAhead handling
        const e = { target: { id: 'dummy', value: "", type: 'input' } };
        const { agmtFracInitAttrs } = this.state;
        if (agmtFracInitAttrs.includes(selection)) {
            const new_values = this.state.agmtFracInitAttrs.filter(item => item !== selection);
            this.setState({
                agmtFracInitAttrs: new_values,
                isExcludeInitAttrsEditOpen: false,
            }, () => { this.handleEditChange(e) });
        } else {
            const new_values = [...this.state.agmtFracInitAttrs, selection];
            this.setState({
                agmtFracInitAttrs: new_values,
                isExcludeInitAttrsEditOpen: false,
            }, () => { this.handleEditChange(e) });
        }
    }

    handleTAStripAttrChange (values) {
        // TypeAhead handling
        const e = {
            target: {
                name: 'agmt-modal',
                id: 'agmtStripAttrs',
                value: values,
                type: 'input',
                toggle: 'isStripAttrsCreateOpen',
            }
        };
        this.handleTASelectChange(e);
    }

    handleTAFracAttrChange (values) {
        // TypeAhead handling
        const e = {
            target: {
                name: 'agmt-modal',
                id: 'agmtFracAttrs',
                value: values,
                type: 'input',
                toggle: 'isExcludeAttrsCreateOpen',
            }
        };
        this.handleTASelectChange(e);
    }

    handleTAFracInitAttrChange (values) {
        // TypeAhead handling
        const e = {
            target: {
                name: 'agmt-modal',
                id: 'agmtFracInitAttrs',
                value: values,
                type: 'input',
                toggle: 'isExcludeInitAttrsCreateOpen',
            }
        };
        this.handleTASelectChange(e);
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
            agmtPort: "636",
            agmtProtocol: "LDAPS",
            agmtBindMethod: "SIMPLE",
            agmtBindDN: "cn=replication manager,cn=config",
            agmtBindPW: "",
            agmtBindPWConfirm: "",
            agmtBootstrap: false,
            agmtBootstrapProtocol: "LDAPS",
            agmtBootstrapBindMethod: "SIMPLE",
            agmtBootstrapBindDN: "cn=replication manager,cn=config",
            agmtBootstrapBindPW: "",
            agmtBootstrapBindPWConfirm: "",
            agmtStripAttrs: [],
            agmtFracAttrs: [],
            agmtFracInitAttrs: [],
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
            errObj: {},
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
                    let agmtBootstrapBindMethod = "SIMPLE";
                    let agmtBootstrapBindDN = "";
                    let agmtBootstrapBindPW = "";
                    let agmtBootstrapBindPWConfirm = "";
                    let agmtStripAttrs = [];
                    let agmtFracAttrs = [];
                    let agmtFracInitAttrs = [];
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
                    for (const attr in config.attrs) {
                        const val = config.attrs[attr][0];
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
                            const attrs = val.replace("(objectclass=*) $ EXCLUDE", "").trim();
                            agmtFracAttrs = attrs.split(' ');
                        }
                        if (attr == "nsds5replicatedattributelisttotal") {
                            const attrs = val.replace("(objectclass=*) $ EXCLUDE", "").trim();
                            agmtFracInitAttrs = attrs.split(' ');
                        }
                        if (attr == "nsds5replicastripattrs") {
                            agmtStripAttrs = val.split(' ');
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
            'repl-agmt', 'set', this.state.agmtName, '--suffix=' + this.props.suffix,
        ];
        let passwd = "";
        let bootstrap_passwd = "";

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
        if (this.state.agmtBindMethod != this.state._agmtBindMethod) {
            cmd.push('--bind-method=' + this.state.agmtBindMethod);
        }
        if (this.state.agmtProtocol != this.state._agmtProtocol) {
            cmd.push('--conn-protocol=' + this.state.agmtProtocol);
        }
        if (this.state.agmtBindPW != this.state._agmtBindPW) {
            passwd = this.state.agmtBindPW;
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
                bootstrap_passwd = this.state.agmtBootstrapBindPW
            }
            if (this.state.agmtBootstrapBindDN != this.state._agmtBootstrapBindDN) {
                cmd.push('--bootstrap-bind-dn=' + this.state.agmtBootstrapBindDN);
            }
        }

        this.setState({
            savingAgmt: true,
        });

        // Update args with password file
        if (passwd !== "") {
            // Add password file arg
            cmd.push("--bind-passwd-prompt");
        }
        if (bootstrap_passwd !== "") {
            // Add bootstrap password file arg
            cmd.push("--bootstrap-bind-passwd-prompt");
        }

        log_cmd('saveAgmt', 'edit agmt', cmd);

        let buffer = "";
        const proc = cockpit.spawn(cmd, { pty: true, environ: ["LC_ALL=C"], superuser: true, err: "message" });
        proc
                .done(data => {
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
                .fail(_ => {
                    this.props.addNotification(
                        "error",
                        `Failed to update replication agreement - ${buffer}`
                    );
                    this.setState({
                        savingAgmt: false
                    });
                })
                .stream(data => {
                    buffer += data;
                    const lines = buffer.split("\n");
                    const last_line = lines[lines.length - 1].toLowerCase();
                    if (last_line.includes("bootstrap")) {
                        proc.input(bootstrap_passwd + "\n", true);
                    } else {
                        proc.input(passwd + "\n", true);
                    }
                });
    }

    pokeAgmt (agmtName) {
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'poke', agmtName, '--suffix=' + this.props.suffix];
        log_cmd('pokeAgmt', 'send updates now', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully poked replication agreement'
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
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
        const init_cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'init', '--suffix=' + this.props.suffix, this.state.agmtName];
        log_cmd('initAgmt', 'Initialize agreement', init_cmd);
        cockpit
                .spawn(init_cmd, { superuser: true, err: "message" })
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
                    const errMsg = JSON.parse(err);
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
                showConfirmDisableAgmt: true,
                modalChecked: false,
                modalSpinning: false,
            });
        } else {
            this.setState({
                agmtName: agmtName,
                showConfirmEnableAgmt: true,
                modalChecked: false,
                modalSpinning: false,
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

    enableAgmt () {
        // Enable/disable agmt
        this.setState({
            modalSpinning: true
        });
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'enable', this.state.agmtName, '--suffix=' + this.props.suffix];
        log_cmd('enableAgmt', 'enable agmt', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully enabled replication agreement');
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to enabled replication agreement - ${errMsg.desc}`
                    );
                });
    }

    disableAgmt () {
        // Enable/disable agmt
        this.setState({
            modalSpinning: true
        });
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'disable', this.state.agmtName, '--suffix=' + this.props.suffix];
        log_cmd('disableAgmt', 'Disable agmt', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully disabled replication agreement');
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
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
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'delete', '--suffix=' + this.props.suffix, this.state.agmtName];
        log_cmd('deleteAgmt', 'Delete agmt', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully deleted replication agreement');
                    this.setState({
                        showConfirmDeleteAgmt: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete replication agreement - ${errMsg.desc}`
                    );
                    this.setState({
                        showConfirmDeleteAgmt: false,
                    });
                });
    }

    createAgmt () {
        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'create', this.state.agmtName, '--suffix=' + this.props.suffix,
            '--host=' + this.state.agmtHost, '--port=' + this.state.agmtPort,
            '--bind-method=' + this.state.agmtBindMethod, '--conn-protocol=' + this.state.agmtProtocol,
            '--bind-dn=' + this.state.agmtBindDN
        ];
        let passwd = "";
        let bootstrap_passwd = "";

        if (this.state.agmtBindPW != "") {
            passwd = this.state.agmtBindPW;
        }

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

        // Handle fractional and stripped attributes
        if (this.state.agmtFracAttrs.length > 0) {
            cmd.push('--frac-list=' + this.state.agmtFracAttrs.join(' '));
        }
        if (this.state.agmtFracInitAttrs.length > 0) {
            cmd.push('--frac-list-total=' + this.state.agmtFracInitAttrs.join(' '));
        }
        if (this.state.agmtStripAttrs.length > 0) {
            cmd.push('--strip-list=' + this.state.agmtStripAttrs.join(' '));
        }

        // Handle bootstrap settings
        if (this.state.agmtBootstrap) {
            if (this.state.agmtBootstrapBindDN != "") {
                cmd.push('--bootstrap-bind-dn=' + this.state.agmtBootstrapBindDN);
            }
            if (this.state.agmtBootstrapBindDNPW != "") {
                bootstrap_passwd = this.state.agmtBootstrapBindDNPW;
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

        // Update args with password prompt
        if (passwd !== "") {
            // Add password prompt arg
            cmd.push("--bind-passwd-prompt");
        }
        if (bootstrap_passwd !== "") {
            // Add bootstrap password prompt arg
            cmd.push("--bootstrap-bind-passwd-prompt");
        }

        log_cmd('createAgmt', 'Create agmt', cmd);

        let buffer = "";
        const proc = cockpit.spawn(cmd, { pty: true, environ: ["LC_ALL=C"], superuser: true, err: "message" });
        proc
                .done(data => {
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
                .fail(_ => {
                    this.props.addNotification(
                        "error",
                        `Failed to create replication agreement - ${buffer}`
                    );
                    this.setState({
                        savingAgmt: false
                    });
                })
                .stream(data => {
                    buffer += data;
                    const lines = buffer.split("\n");
                    const last_line = lines[lines.length - 1].toLowerCase();
                    if (last_line.includes("bootstrap")) {
                        proc.input(bootstrap_passwd + "\n", true);
                    } else {
                        proc.input(passwd + "\n", true);
                    }
                });
    }

    watchAgmtInit(agmtName, idx) {
        // Watch the init, then clear the interval index
        const status_cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-agmt', 'init-status', '--suffix=' + this.props.suffix, agmtName];
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
            <div className="ds-margin-right ds-margin-bottom-md">
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
                        variant="secondary"
                        onClick={() => {
                            this.props.reload(this.props.suffix);
                        }}
                    >
                        Refresh Agreements
                    </Button>
                </div>
                <ReplAgmtModal
                    key={this.state.showCreateAgmtModal ? "create1" : "create0"}
                    showModal={this.state.showCreateAgmtModal}
                    closeHandler={this.closeCreateAgmtModal}
                    handleChange={this.handleCreateChange}
                    handleTimeChange={this.handleTimeChange}
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
                    agmtBindMethodOptions={this.state.agmtBindMethodOptions}
                    agmtBootstrap={this.state.agmtBootstrap}
                    agmtBootstrapBindDN={this.state.agmtBootstrapBindDN}
                    agmtBootstrapBindPW={this.state.agmtBootstrapBindPW}
                    agmtBootstrapBindPWConfirm={this.state.agmtBootstrapBindPWConfirm}
                    agmtBootstrapProtocol={this.state.agmtBootstrapProtocol}
                    agmtBootstrapBindMethod={this.state.agmtBootstrapBindMethod}
                    agmtBootstrapBindMethodOptions={this.state.agmtBootstrapBindMethodOptions}
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
                    agmtInit={this.state.agmtInit}
                    availAttrs={this.props.attrs}
                    error={this.state.errObj}
                    saveOK={this.state.agmtSaveOK}
                />
                <ReplAgmtModal
                    key={this.state.showEditAgmtModal ? "edit1" : "edit0"}
                    showModal={this.state.showEditAgmtModal}
                    closeHandler={this.closeEditAgmtModal}
                    handleChange={this.handleEditChange}
                    handleTimeChange={this.handleTimeChange}
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
                    agmtBindMethodOptions={this.state.agmtBindMethodOptions}
                    agmtBootstrap={this.state.agmtBootstrap}
                    agmtBootstrapBindDN={this.state.agmtBootstrapBindDN}
                    agmtBootstrapBindPW={this.state.agmtBootstrapBindPW}
                    agmtBootstrapBindPWConfirm={this.state.agmtBootstrapBindPWConfirm}
                    agmtBootstrapProtocol={this.state.agmtBootstrapProtocol}
                    agmtBootstrapBindMethod={this.state.agmtBootstrapBindMethod}
                    agmtBootstrapBindMethodOptions={this.state.agmtBootstrapBindMethodOptions}
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
                <DoubleConfirmModal
                    showModal={this.state.showConfirmEnableAgmt}
                    closeHandler={this.closeConfirmEnableAgmt}
                    handleChange={this.handleModalChange}
                    actionHandler={this.enableAgmt}
                    spinning={this.state.modalSpinning}
                    item={this.state.agmtName}
                    checked={this.state.modalChecked}
                    mTitle="Enable Replication Agreement"
                    mMsg="Are you sure you want to enable this replication agreement?"
                    mSpinningMsg="Enabling ..."
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
                    mTitle="Disable Replication Agreement"
                    mMsg="Are you sure you want to disable this replication agreement?"
                    mSpinningMsg="Disabling ..."
                    mBtnName="Disable Agreement"
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
    attrs: [],
};
