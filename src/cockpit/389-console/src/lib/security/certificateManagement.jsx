import cockpit from "cockpit";
import React from "react";
import {
    Nav,
    NavItem,
    TabContainer,
    TabContent,
    TabPane,
    Button,
    Spinner,
    noop
} from "patternfly-react";
import { ConfirmPopup } from "../../lib/notifications.jsx";
import {
    CertTable
} from "./securityTables.jsx";
import {
    EditCertModal,
    SecurityAddCertModal,
    SecurityAddCACertModal,
} from "./securityModals.jsx";
import PropTypes from "prop-types";
import "../../css/ds.css";
import { log_cmd } from "../../lib/tools.jsx";

export class CertificateManagement extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            activeKey: 1,
            ServerCerts: this.props.ServerCerts,
            CACerts: this.props.CACerts,
            showEditModal: false,
            showAddModal: false,
            modalSpinner: false,
            showConfirmDelete: false,
            certName: "",
            certFile: "",
            flags: "",
            errObj: {},
            isCACert: false,
            showConfirmCAChange: false,
            loading: false,
        };

        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.addCACert = this.addCACert.bind(this);
        this.handleAddChange = this.handleAddChange.bind(this);
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

    handleNavSelect(key) {
        this.setState({
            activeKey: key
        });
    }

    showAddModal () {
        this.setState({
            showAddModal: true,
            errObj: {certName: true, certFile: true}
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
            errObj: {certName: true, certFile: true}
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
            modalSpinner: true,
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
                        modalSpinner: false
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully added certificate`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        modalSpinner: false,
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
            modalSpinner: true,
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
                        modalSpinner: false,
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully added certificate`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        modalSpinner: false,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error adding certificate - ${msg}`
                    );
                });
    }

    showDeleteConfirm(dataRow) {
        this.setState({
            showConfirmDelete: true,
            certName: dataRow.nickname[0],
        });
    }

    delCert () {
        this.setState({
            modalSpinner: true,
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
                        modalSpinner: false,
                        showConfirmDelete: false,
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully deleted certificate`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        certName: '',
                        modalSpinner: false,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error deleting certificate - ${msg}`
                    );
                });
    }

    showEditModal (rowData) {
        this.setState({
            showEditModal: true,
            certName: rowData.nickname[0],
            flags: rowData.flags[0],
            isCACert: false,
        });
    }

    closeEditModal () {
        this.setState({
            showEditModal: false,
            flags: ''
        });
    }

    showEditCAModal (rowData) {
        this.setState({
            showEditModal: true,
            certName: rowData.nickname[0],
            flags: rowData.flags[0],
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
                    showConfirmCAChange: true
                });
                return;
            }
        }
        this.doEditCert();
    }

    closeConfirmCAChange () {
        this.setState({
            showConfirmCAChange: false
        });
    }

    doEditCert () {
        this.setState({
            modalSpinner: true,
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
                        modalSpinner: false,
                    });
                    this.props.addNotification(
                        "success",
                        `Successfully changed certificate's trust flags`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.setState({
                        showEditModal: false,
                        flags: '',
                        certName: '',
                        modalSpinner: false,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error setting trust flags - ${msg}`
                    );
                });
    }

    handleAddChange (e) {
        const value = e.target.value;
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

    handleFlagChange (e) {
        const checked = e.target.checked;
        const id = e.target.id;
        let flags = this.state.flags;
        let SSLFlags = '';
        let EmailFlags = '';
        let OSFlags = '';
        [SSLFlags, EmailFlags, OSFlags] = flags.split(',');

        if (id.endsWith('SSL')) {
            for (let trustFlag of ['C', 'T', 'c', 'P', 'p']) {
                if (id.startsWith(trustFlag)) {
                    if (checked) {
                        SSLFlags += trustFlag;
                    } else {
                        SSLFlags = SSLFlags.replace(trustFlag, '');
                    }
                }
            }
        } else if (id.endsWith('Email')) {
            for (let trustFlag of ['C', 'T', 'c', 'P', 'p']) {
                if (id.startsWith(trustFlag)) {
                    if (checked) {
                        EmailFlags += trustFlag;
                    } else {
                        EmailFlags = EmailFlags.replace(trustFlag, '');
                    }
                }
            }
        } else {
            // Object Signing (OS)
            for (let trustFlag of ['C', 'T', 'c', 'P', 'p']) {
                if (id.startsWith(trustFlag)) {
                    if (checked) {
                        OSFlags += trustFlag;
                    } else {
                        OSFlags = OSFlags.replace(trustFlag, '');
                    }
                }
            }
        }
        this.setState({
            flags: SSLFlags + "," + EmailFlags + "," + OSFlags
        });
    }

    closeConfirmDelete () {
        this.setState({
            showConfirmDelete: false,
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
                    let certNames = [];
                    for (let cert of certs) {
                        certNames.push(cert.attrs['nickname']);
                    }
                    this.setState({
                        ServerCerts: certs,
                        loading: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
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
                    let certs = JSON.parse(content);
                    this.setState({
                        CACerts: certs,
                        loading: false
                    }, this.reloadCerts);
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
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
        let CATitle = 'Trusted Certificate Authorites <font size="1">(' + this.state.CACerts.length + ')</font>';
        let ServerTitle = 'TLS Certificates <font size="1">(' + this.state.ServerCerts.length + ')</font>';

        let certificatePage = '';

        if (this.state.loading) {
            certificatePage =
                <div className="ds-loading-spinner ds-center">
                    <p />
                    <h4>Loading certificates ...</h4>
                    <Spinner loading size="md" />
                </div>;
        } else {
            certificatePage =
                <div className="container-fluid">
                    <div className="ds-tab-table">
                        <TabContainer id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                            <div>
                                <Nav bsClass="nav nav-tabs nav-tabs-pf">
                                    <NavItem eventKey={1}>
                                        <div dangerouslySetInnerHTML={{__html: CATitle}} />
                                    </NavItem>
                                    <NavItem eventKey={2}>
                                        <div dangerouslySetInnerHTML={{__html: ServerTitle}} />
                                    </NavItem>
                                </Nav>
                                <TabContent>
                                    <TabPane eventKey={1}>
                                        <div className="ds-margin-top-lg">
                                            <CertTable
                                                certs={this.state.CACerts}
                                                key={this.state.CACerts}
                                                editCert={this.showEditCAModal}
                                                delCert={this.showDeleteConfirm}
                                            />
                                            <Button
                                                bsStyle="primary"
                                                className="ds-margin-top-med"
                                                onClick={() => {
                                                    this.showAddCAModal();
                                                }}
                                            >
                                                Add CA Certificate
                                            </Button>
                                        </div>
                                    </TabPane>
                                    <TabPane eventKey={2}>
                                        <div className="ds-margin-top-lg">
                                            <CertTable
                                                certs={this.state.ServerCerts}
                                                key={this.state.ServerCerts}
                                                editCert={this.showEditModal}
                                                delCert={this.showDeleteConfirm}
                                            />
                                            <Button
                                                bsStyle="primary"
                                                className="ds-margin-top-med"
                                                onClick={() => {
                                                    this.showAddModal();
                                                }}
                                            >
                                                Add Server Certificate
                                            </Button>
                                        </div>
                                    </TabPane>
                                </TabContent>
                            </div>
                        </TabContainer>
                    </div>
                </div>;
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
                    spinning={this.state.modalSpinner}
                />
                <SecurityAddCertModal
                    showModal={this.state.showAddModal}
                    closeHandler={this.closeAddModal}
                    handleChange={this.handleAddChange}
                    saveHandler={this.addCert}
                    spinning={this.state.modalSpinner}
                    error={this.state.errObj}
                />
                <SecurityAddCACertModal
                    showModal={this.state.showAddCAModal}
                    closeHandler={this.closeAddCAModal}
                    handleChange={this.handleAddChange}
                    saveHandler={this.addCACert}
                    spinning={this.state.modalSpinner}
                    error={this.state.errObj}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    actionFunc={this.delCert}
                    msg="Are you sure you want to delete this certificate?"
                    msgContent={this.state.certName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmCAChange}
                    closeHandler={this.closeConfirmCAChange}
                    actionFunc={this.doEditCert}
                    msg="Removing the 'C' or 'T' flags from the SSL trust catagory could break all TLS connectivity to and from the server, are you sure you want to proceed?"
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
    addNotification: noop,
};

export default CertificateManagement;
