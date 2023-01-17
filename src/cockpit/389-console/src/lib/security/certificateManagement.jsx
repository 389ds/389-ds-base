import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { DoubleConfirmModal } from "../../lib/notifications.jsx";
import {
    CertTable,
    CSRTable,
    KeyTable,
} from "./securityTables.jsx";
import {
    EditCertModal,
    SecurityAddCertModal,
    SecurityAddCACertModal,
    SecurityAddCSRModal,
    SecurityViewCSRModal,
} from "./securityModals.jsx";
import PropTypes from "prop-types";
import { log_cmd } from "../../lib/tools.jsx";

export class CertificateManagement extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            activeTabKey: 0,
            ServerCerts: this.props.ServerCerts,
            CACerts: this.props.CACerts,
            ServerCSRs: this.props.ServerCSRs,
            ServerKeys: this.props.ServerKeys,
            tableKey: 0,
            showEditModal: false,
            showAddModal: false,
            showAddCSRModal: false,
            showViewCSRModal: false,
            modalSpinning: false,
            showConfirmDelete: false,
            showCSRConfirmDelete: false,
            showKeyConfirmDelete: false,
            certName: "",
            certFile: "",
            csrContent: "",
            csrName: "",
            csrSubject: "",
            csrSubjectCommonName: "",
            csrSubjectOrg: "",
            csrSubjectOrgUnit: "",
            csrSubjectLocality: "",
            csrSubjectState: "",
            csrSubjectCountry: "",
            csrSubjectEmail: "",
            keyID: "",
            flags: "",
            _flags: "",
            errObj: {},
            isCACert: false,
            showConfirmCAChange: false,
            loading: false,
            modalChecked: false,
            disableSaveBtn: true,
        };

        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.sortFlags = (str) => {
            let flags = str.split('');
            let sorted_flags = flags.sort();
            return sorted_flags.join('');
        };

        this.addCACert = this.addCACert.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleCSRChange = this.handleCSRChange.bind(this);
        this.addCert = this.addCert.bind(this);
        this.showAddModal = this.showAddModal.bind(this);
        this.closeAddModal = this.closeAddModal.bind(this);
        this.showAddCAModal = this.showAddCAModal.bind(this);
        this.closeAddCAModal = this.closeAddCAModal.bind(this);
        this.showAddCSRModal = this.showAddCSRModal.bind(this);
        this.closeAddCSRModal = this.closeAddCSRModal.bind(this);
        this.showViewCSRModal = this.showViewCSRModal.bind(this);
        this.closeViewCSRModal = this.closeViewCSRModal.bind(this);
        this.showEditModal = this.showEditModal.bind(this);
        this.closeEditModal = this.closeEditModal.bind(this);
        this.showEditCAModal = this.showEditCAModal.bind(this);
        this.handleFlagChange = this.handleFlagChange.bind(this);
        this.editCert = this.editCert.bind(this);
        this.doEditCert = this.doEditCert.bind(this);
        this.closeConfirmCAChange = this.closeConfirmCAChange.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.showCSRDeleteConfirm = this.showCSRDeleteConfirm.bind(this);
        this.showKeyDeleteConfirm = this.showKeyDeleteConfirm.bind(this);
        this.delCert = this.delCert.bind(this);
        this.addCSR = this.addCSR.bind(this);
        this.delCSR = this.delCSR.bind(this);
        this.showCSR = this.showCSR.bind(this);
        this.delKey = this.delKey.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
        this.closeCSRConfirmDelete = this.closeCSRConfirmDelete.bind(this);
        this.closeKeyConfirmDelete = this.closeKeyConfirmDelete.bind(this);
        this.reloadCerts = this.reloadCerts.bind(this);
        this.reloadCACerts = this.reloadCACerts.bind(this);
        this.reloadCSRs = this.reloadCSRs.bind(this);
        this.reloadOrphanKeys = this.reloadOrphanKeys.bind(this);
        this.buildSubject = this.buildSubject.bind(this);
    }

    showAddModal () {
        this.setState({
            showAddModal: true,
            errObj: { certName: true, certFile: true }
        });
    }

    closeAddModal () {
        this.setState({
            showAddModal: false,
            certName: "",
            certFile: "",
        });
    }

    showAddCAModal () {
        this.setState({
            showAddCAModal: true,
            errObj: { certName: true, certFile: true }
        });
    }

    closeAddCAModal () {
        this.setState({
            showAddCAModal: false,
            certName: "",
            certFile: "",
        });
    }

    showAddCSRModal () {
        this.setState({
            showAddCSRModal: true,
            csrSubject: "",
            csrName: "",
            csrSubjectCommonName: "",
            csrSubjectOrg: "",
            csrSubjectOrgUnit: "",
            csrSubjectLocality: "",
            csrSubjectState: "",
            csrSubjectCountry: "",
            csrSubjectEmail: "",
            errObj: { csrName: true, csrSubjectCommonName: true},
        });
    }

    closeAddCSRModal () {
        this.setState({
            showAddCSRModal: false,
            csrSubject: "",
            csrName: "",
            csrSubjectCommonName: "",
            csrSubjectOrg: "",
            csrSubjectOrgUnit: "",
            csrSubjectLocality: "",
            csrSubjectState: "",
            csrSubjectCountry: "",
            csrSubjectEmail: "",
        });
    }

    showViewCSRModal (name) {
        this.showCSR(name)
        this.setState({
            showViewCSRModal: true,
            csrName: name,
            errObj: { csrName: true},
        });
    }

    closeViewCSRModal () {
        this.setState({
            showViewCSRModal: false,
        });
    }

    addCert () {
        if (this.state.certName == "") {
            this.props.addNotification(
                "warning",
                `Missing certificate nickname`
            );
            return;
        } else if (this.state.certFile == "") {
            this.props.addNotification(
                "warning",
                `Missing certificate file name`
            );
            return;
        }

        this.setState({
            modalSpinning: true,
            loading: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "certificate", "add", "--name=" + this.state.certName, "--file=" + this.state.certFile
        ];
        log_cmd("addCert", "Adding server cert", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.reloadCACerts();
                    this.setState({
                        showAddModal: false,
                        certFile: '',
                        certName: '',
                        modalSpinning: false
                    });
                    this.reloadOrphanKeys();
                    this.props.addNotification(
                        "success",
                        `Successfully added certificate`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        modalSpinning: false,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error adding certificate - ${msg}`
                    );
                });
    }

    addCSR () {
        if (this.state.csrName == "") {
            this.props.addNotification(
                "warning",
                `Missing CSR Name`
            );
            return;
        } else if (this.state.csrSubjectCommonName == "") {
            this.props.addNotification(
                "warning",
                `Missing CSR Subject Common Name`
            );
            return;
        }

        this.setState({
            modalSpinning: true,
            loading: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "csr", "req", "--name=" + this.state.csrName, "--subject=" + this.state.csrSubject
        ];
        log_cmd("addCSR", "Creating CSR", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.reloadCSRs();
                    this.setState({
                        showAddCSRModal: false,
                        csrSubject: '',
                        csrName: '',
                        modalSpinning: false,
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully created CSR`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (errMsg.desc.includes('certutil -s: improperly formatted name:')) {
                        this.props.addNotification(
                            "error",
                            `Error Improperly formatted subject`
                        );
                    } else {
                        this.props.addNotification(
                            "error",
                            `Error creating CSR - ${errMsg.desc}`
                        );
                    }
                    this.setState({
                        modalSpinning: false,
                        loading: false,
                    });

                });
    }

    showCSR (name) {
        if (name == "") {
            this.props.addNotification(
                "warning",
                `Missing CSR Name`
            );
            return;
        }

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "csr", "get", name
        ];

        log_cmd("showCSR", "Displaying CSR", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message"})
                .done(content => {
                    this.setState({
                        csrContent: content,
                        showViewCSRModal: true,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error displaying CSR - ${errMsg.desc}`
                    );
                });
    }

    addCACert () {
        if (this.state.certName == "") {
            this.props.addNotification(
                "warning",
                `Missing certificate nickname`
            );
            return;
        } else if (this.state.certFile == "") {
            this.props.addNotification(
                "warning",
                `Missing certificate file name`
            );
            return;
        }

        this.setState({
            modalSpinning: true,
            loading: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "ca-certificate", "add", "--name=" + this.state.certName, "--file=" + this.state.certFile
        ];
        log_cmd("addCACert", "Adding CA certificate", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.reloadCACerts();
                    this.setState({
                        showAddCAModal: false,
                        certFile: '',
                        certName: '',
                        modalSpinning: false,
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully added certificate`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        modalSpinning: false,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error adding certificate - ${msg}`
                    );
                });
    }

    showDeleteConfirm(nickname) {
        this.setState({
            showConfirmDelete: true,
            certName: nickname,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    showCSRDeleteConfirm(name) {
        this.setState({
            showCSRConfirmDelete: true,
            csrName: name,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    showKeyDeleteConfirm(key_id) {
        this.setState({
            showKeyConfirmDelete: true,
            keyID: key_id,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    delCert () {
        this.setState({
            modalSpinning: true,
            loading: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "certificate", "del", this.state.certName
        ];
        log_cmd("delCert", "Deleting certificate", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.reloadCACerts();
                    this.setState({
                        certName: '',
                        modalSpinning: false,
                        showConfirmDelete: false,
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully deleted certificate`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        certName: '',
                        modalSpinning: false,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error deleting certificate - ${msg}`
                    );
                });
    }

    delCSR (name) {
        this.setState({
            modalSpinning: true,
            loading: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "csr", "del", this.state.csrName
        ];
        log_cmd("delCSR", "Deleting CSR", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.reloadCSRs();
                    this.setState({
                        csrName: '',
                        csrSubject: '',
                        modalSpinning: false,
                        showCSRConfirmDelete: false,
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully deleted CSR`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        csrName: '',
                        csrSubject: '',
                        modalSpinning: false,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error deleting CSR - ${msg}`
                    );
                });
    }

    delKey (name) {
        this.setState({
            modalSpinning: true,
            loading: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "key", "del", this.state.keyID
        ];
        log_cmd("delKey", "Deleting key", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.reloadOrphanKeys();
                    this.setState({
                        keyID: '',
                        modalSpinning: false,
                        showKeyConfirmDelete: false,
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully deleted key`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        keyID: '',
                        modalSpinning: false,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error deleting key - ${msg}`
                    );
                });
    }

    showEditModal (name, flags) {
        this.setState({
            showEditModal: true,
            certName: name,
            flags: flags,
            isCACert: false,
        });
    }

    closeEditModal () {
        this.setState({
            showEditModal: false,
            flags: ''
        });
    }

    showEditCAModal (nickname, flags) {
        this.setState({
            showEditModal: true,
            certName: nickname,
            flags: flags,
            _flags: flags,
            isCACert: true,
        });
    }

    editCert () {
        // Check if CA cert flags were removed
        if (this.state.isCACert) {
            let SSLFlags = '';
            SSLFlags = this.state.flags.split(',', 1);
            if (!SSLFlags[0].includes('C') || !SSLFlags[0].includes('T')) {
                // This could remove the CA cert properties, better warn user
                this.setState({
                    showConfirmCAChange: true,
                    modalSpinning: false,
                    modalChecked: false
                });
                return;
            }
        }
        this.doEditCert();
    }

    closeConfirmCAChange () {
        this.setState({
            showConfirmCAChange: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    doEditCert () {
        this.setState({
            modalSpinning: true,
            loading: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "certificate", "set-trust-flags", this.state.certName, "--flags=" + this.state.flags
        ];
        log_cmd("doEditCert", "Editing trust flags", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.reloadCACerts();
                    this.setState({
                        showEditModal: false,
                        flags: '',
                        certName: '',
                        modalSpinning: false,
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully changed certificate's trust flags`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        showEditModal: false,
                        flags: '',
                        certName: '',
                        modalSpinning: false,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error setting trust flags - ${msg}`
                    );
                });
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

    handleCSRChange (e) {
        const value = e.target.value;
        const errObj = this.state.errObj;
        let valueErr = false;

        if (value == "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        }, this.buildSubject);
    }

    buildSubject () {
        let subject = ""
        const csrSubjectCN = this.state.csrSubjectCommonName;
        const csrSubjectO = this.state.csrSubjectOrg;
        const csrSubjectOU = this.state.csrSubjectOrgUnit;
        const csrSubjectL = this.state.csrSubjectLocality;
        const csrSubjectST = this.state.csrSubjectState;
        const csrSubjectC = this.state.csrSubjectCountry;
        const csrSubjectE = this.state.csrSubjectEmail;

        // Construct CSR subject string from state fields
        if (csrSubjectCN.length != 0) {
            subject = 'CN=' + csrSubjectCN;
        }
        if (csrSubjectO.length != 0) {
            subject += ',O=' + csrSubjectO;
        }
        if (csrSubjectOU.length != 0) {
            subject += ',OU=' + csrSubjectOU;
        }
        if (csrSubjectL.length != 0) {
            subject += ',L=' + csrSubjectL;
        }
        if (csrSubjectST.length != 0) {
            subject += ',ST=' + csrSubjectST;
        }
        // It would be nice to validate country code, certutil will complain if it isnt valid...
        if (csrSubjectC.length != 0) {
            subject += ',C=' + csrSubjectC;
        }
        if (csrSubjectE.length != 0) {
            subject += ',E=' + csrSubjectE;
        }

        // Update subject state
        this.setState({
            csrSubject: subject
        });
    }

    handleFlagChange (e) {
        const checked = e.target.checked;
        const id = e.target.id;
        const flags = this.state.flags;
        let SSLFlags = '';
        let EmailFlags = '';
        let OSFlags = '';
        let newFlags = "";
        let disableSaveBtn = true;
        [SSLFlags, EmailFlags, OSFlags] = flags.split(',');

        if (id.endsWith('SSL')) {
            for (const trustFlag of ['C', 'T', 'c', 'P', 'p']) {
                if (id.startsWith(trustFlag)) {
                    if (checked) {
                        SSLFlags += trustFlag;
                    } else {
                        SSLFlags = SSLFlags.replace(trustFlag, '');
                    }
                    SSLFlags = this.sortFlags(SSLFlags);
                }
            }
        } else if (id.endsWith('Email')) {
            for (const trustFlag of ['C', 'T', 'c', 'P', 'p']) {
                if (id.startsWith(trustFlag)) {
                    if (checked) {
                        EmailFlags += trustFlag;
                    } else {
                        EmailFlags = EmailFlags.replace(trustFlag, '');
                    }
                    EmailFlags = this.sortFlags(EmailFlags);
                }
            }
        } else {
            // Object Signing (OS)
            for (const trustFlag of ['C', 'T', 'c', 'P', 'p']) {
                if (id.startsWith(trustFlag)) {
                    if (checked) {
                        OSFlags += trustFlag;
                    } else {
                        OSFlags = OSFlags.replace(trustFlag, '');
                    }
                    OSFlags = this.sortFlags(OSFlags);
                }
            }
        }

        newFlags = SSLFlags + "," + EmailFlags + "," + OSFlags;

        if (newFlags != this.state._flags) {
            disableSaveBtn = false;
        }
        this.setState({
            flags: newFlags,
            disableSaveBtn: disableSaveBtn
        });
    }

    closeConfirmDelete () {
        this.setState({
            showConfirmDelete: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    closeCSRConfirmDelete () {
        this.setState({
            showCSRConfirmDelete: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    closeKeyConfirmDelete () {
        this.setState({
            showKeyConfirmDelete: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    reloadCerts () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "certificate", "list",
        ];
        log_cmd("reloadCerts", "Load certificates", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const certs = JSON.parse(content);
                    const key = this.state.tableKey + 1;
                    const certNames = [];
                    for (const cert of certs) {
                        certNames.push(cert.attrs.nickname);
                    }
                    this.setState({
                        ServerCerts: certs,
                        loading: false,
                        tableKey: key,
                        showConfirmCAChange: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        `Error loading server certificates - ${msg}`
                    );
                });
    }

    reloadCSRs () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "csr", "list",
        ];
        log_cmd("reloadCSRs", "Reload CSRs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const csrs = JSON.parse(content);
                    const key = this.state.tableKey + 1;
                    this.setState({
                        ServerCSRs: csrs,
                        loading: false,
                        tableKey: key,
                        showConfirmCSRChange: false
                    }), this.reloadOrphanKeys();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        `Error loading CSRs - ${msg}`
                    );
                });
    }

    reloadOrphanKeys () {
        // Set loaded: true
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "key", "list", "--orphan"
        ];
        log_cmd("reloadOrphanKeys", "Reload Orphan Keys", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const keys = JSON.parse(content);
                    const key = this.state.tableKey + 1;
                    this.setState(() => (
                        {
                            ServerKeys: keys,
                            loading: false,
                            tableKey: key,
                        })
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (!errMsg.desc.includes('certutil: no keys found')) {
                        this.props.addNotification(
                            "error",
                            `Error loading Orphan Keys - ${errMsg.desc}`
                        );
                    }
                    this.setState({
                        loading: false,
                        ServerKeys: []
                    });
                });
    }

    reloadCACerts () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "ca-certificate", "list",
        ];
        log_cmd("reloadCACerts", "Load certificates", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const certs = JSON.parse(content);
                    this.setState({
                        CACerts: certs,
                        loading: false
                    }, this.reloadCerts);
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        `Error loading CA certificates - ${msg}`
                    );
                });
    }

    render () {
        let certificatePage = '';
        if (this.state.loading) {
            certificatePage =
                <div className="ds-loading-spinner ds-center ds-margin-top-xlg">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Loading Certificates ...
                        </Text>
                    </TextContent>
                    <Spinner size="lg" />
                </div>;
        } else {
            certificatePage =
                <Tabs isBox isSecondary className="ds-margin-top-xlg ds-left-indent" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Trusted Certificate Authorites <font size="2">({this.state.CACerts.length})</font></TabTitleText>}>
                        <div className="ds-margin-top-lg ds-left-indent">
                            <CertTable
                                certs={this.state.CACerts}
                                key={this.state.tableKey}
                                editCert={this.showEditCAModal}
                                delCert={this.showDeleteConfirm}
                            />
                            <Button
                                variant="primary"
                                className="ds-margin-top-med"
                                onClick={() => {
                                    this.showAddCAModal();
                                }}
                            >
                                Add CA Certificate
                            </Button>
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>TLS Certificates <font size="2">({this.state.ServerCerts.length})</font></TabTitleText>}>
                        <div className="ds-margin-top-lg ds-left-indent">
                            <CertTable
                                certs={this.state.ServerCerts}
                                key={this.state.tableKey}
                                editCert={this.showEditModal}
                                delCert={this.showDeleteConfirm}
                            />
                            <Button
                                variant="primary"
                                className="ds-margin-top-med"
                                onClick={() => {
                                    this.showAddModal();
                                }}
                            >
                                Add Server Certificate
                            </Button>
                        </div>
                    </Tab>
                    <Tab eventKey={2} title={<TabTitleText>Certificate Sigining Requests <font size="2">({this.state.ServerCSRs.length})</font></TabTitleText>}>
                        <div className="ds-margin-top-lg ds-left-indent">
                            <CSRTable
                                ServerCSRs={this.state.ServerCSRs}
                                key={this.state.tableKey}
                                delCSR={this.showCSRDeleteConfirm}
                                viewCSR={this.showViewCSRModal}
                            />
                            <Button
                                variant="primary"
                                className="ds-margin-top-med"
                                onClick={() => {
                                    this.showAddCSRModal();
                                }}
                            >
                                Create Certificate Sigining Request
                            </Button>
                        </div>
                    </Tab>
                    <Tab eventKey={3} title={<TabTitleText> Orphan Keys <font size="2">({this.state.ServerKeys.length})</font></TabTitleText>}>
                        <div className="ds-margin-top-lg ds-left-indent">
                            <KeyTable
                                ServerKeys={this.state.ServerKeys}
                                key={this.state.tableKey}
                                delKey={this.showKeyDeleteConfirm}
                            />
                        </div>
                    </Tab>
                </Tabs>;
        }
        return (
            <div>
                {certificatePage}
                <EditCertModal
                    showModal={this.state.showEditModal}
                    closeHandler={this.closeEditModal}
                    handleChange={this.handleFlagChange}
                    saveHandler={this.editCert}
                    flags={this.state.flags}
                    disableSaveBtn={this.state.disableSaveBtn}
                    spinning={this.state.modalSpinning}
                />
                <SecurityAddCertModal
                    showModal={this.state.showAddModal}
                    closeHandler={this.closeAddModal}
                    handleChange={this.handleChange}
                    saveHandler={this.addCert}
                    spinning={this.state.modalSpinning}
                    error={this.state.errObj}
                />
                <SecurityAddCACertModal
                    showModal={this.state.showAddCAModal}
                    closeHandler={this.closeAddCAModal}
                    handleChange={this.handleChange}
                    saveHandler={this.addCACert}
                    spinning={this.state.modalSpinning}
                    error={this.state.errObj}
                />
                <SecurityAddCSRModal
                    showModal={this.state.showAddCSRModal}
                    closeHandler={this.closeAddCSRModal}
                    handleChange={this.handleCSRChange}
                    saveHandler={this.addCSR}
                    previewValue={this.state.csrSubject}
                    spinning={this.state.modalSpinning}
                    error={this.state.errObj}
                />
                <SecurityViewCSRModal
                    showModal={this.state.showViewCSRModal}
                    closeHandler={this.closeViewCSRModal}
                    name={this.state.csrName}
                    item={this.state.csrContent}
                    error={this.state.errObj}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.handleChange}
                    actionHandler={this.delCert}
                    spinning={this.state.modalSpinning}
                    item={this.state.certName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Certificate"
                    mMsg="Are you sure you want to delete this certificate?"
                    mSpinningMsg="Deleting Certificate ..."
                    mBtnName="Delete Certificate"
                />
                <DoubleConfirmModal
                    showModal={this.state.showCSRConfirmDelete}
                    closeHandler={this.closeCSRConfirmDelete}
                    handleChange={this.handleChange}
                    actionHandler={this.delCSR}
                    spinning={this.state.modalSpinning}
                    item={this.state.csrName}
                    checked={this.state.modalChecked}
                    mTitle="Delete CSR"
                    mMsg="Are you sure you want to delete this CSR?"
                    mSpinningMsg="Deleting CSR ..."
                    mBtnName="Delete CSR"
                />
                <DoubleConfirmModal
                    showModal={this.state.showKeyConfirmDelete}
                    closeHandler={this.closeKeyConfirmDelete}
                    handleChange={this.handleChange}
                    actionHandler={this.delKey}
                    spinning={this.state.modalSpinning}
                    item={this.state.keyID}
                    checked={this.state.modalChecked}
                    mTitle="Delete Key"
                    mMsg="Are you sure you want to delete this Key?"
                    mSpinningMsg="Deleting Key ..."
                    mBtnName="Delete Key"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmCAChange}
                    closeHandler={this.closeConfirmCAChange}
                    handleChange={this.handleChange}
                    actionHandler={this.doEditCert}
                    spinning={this.state.modalSpinning}
                    item={this.state.certName}
                    checked={this.state.modalChecked}
                    mTitle="Warning - Altering CA Certificate Properties"
                    mMsg="Removing the 'C' or 'T' flags from the SSL trust catagory could break all TLS connectivity to and from the server, are you sure you want to proceed?"
                    mSpinningMsg="Editing CA Certificate ..."
                    mBtnName="Change Trust Flags"
                />
            </div>
        );
    }
}

// Props and defaults

CertificateManagement.propTypes = {
    serverId: PropTypes.string,
    CACerts: PropTypes.array,
    ServerCerts: PropTypes.array,
    ServerCSRs: PropTypes.array,
    ServerKeys: PropTypes.array,
    addNotification: PropTypes.func,
};

CertificateManagement.defaultProps = {
    serverId: "",
    CACerts: [],
    ServerCerts: [],
    ServerCSRs: [],
    ServerKeys: [],
};

export default CertificateManagement;
