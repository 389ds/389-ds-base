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
    CertTable
} from "./securityTables.jsx";
import {
    EditCertModal,
    SecurityAddCertModal,
    SecurityAddCACertModal,
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
            tableKey: 0,
            showEditModal: false,
            showAddModal: false,
            modalSpinning: false,
            showConfirmDelete: false,
            certName: "",
            certFile: "",
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
        this.addCert = this.addCert.bind(this);
        this.showAddModal = this.showAddModal.bind(this);
        this.closeAddModal = this.closeAddModal.bind(this);
        this.showAddCAModal = this.showAddCAModal.bind(this);
        this.closeAddCAModal = this.closeAddCAModal.bind(this);
        this.showEditModal = this.showEditModal.bind(this);
        this.closeEditModal = this.closeEditModal.bind(this);
        this.showEditCAModal = this.showEditCAModal.bind(this);
        this.handleFlagChange = this.handleFlagChange.bind(this);
        this.editCert = this.editCert.bind(this);
        this.doEditCert = this.doEditCert.bind(this);
        this.closeConfirmCAChange = this.closeConfirmCAChange.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.delCert = this.delCert.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
        this.reloadCerts = this.reloadCerts.bind(this);
        this.reloadCACerts = this.reloadCACerts.bind(this);
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
        console.log("MARK flags: ", newFlags, this.state._flags);
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
    addNotification: PropTypes.func,
};

CertificateManagement.defaultProps = {
    serverId: "",
    CACerts: [],
    ServerCerts: [],
};

export default CertificateManagement;
