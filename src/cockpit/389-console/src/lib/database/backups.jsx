import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup } from "../notifications.jsx";
import { LDIFManageTable, BackupTable } from "./databaseTables.jsx";
import {
    Nav,
    NavItem,
    TabContent,
    TabPane,
    TabContainer,
    Col,
    Button,
    Spinner,
    Modal,
    Icon,
    ControlLabel,
    FormControl,
    Form,
    Row,
    noop
} from "patternfly-react";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import "../../css/ds.css";

export class Backups extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeKey: 1,
            showConfirmBackupDelete: false,
            showConfirmBackup: false,
            showConfirmRestore: false,
            showRestoreSpinningModal: false,
            showDelBackupSpinningModal: false,
            showBackupModal: false,
            backupSpinning: false,
            backupName: "",
            deleteBackup: "",
            // LDIF
            showConfirmLDIFDelete: false,
            showConfirmLDIFImport: false,
            showLDIFSpinningModal: false,
            showLDIFDeleteSpinningModal: false,
            showExportModal: false,
            exportSpinner: false,
            ldifName: "",
            ldifSuffix: "",
            errObj: {}
        };

        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleChange = this.handleChange.bind(this);

        // Backups
        this.doBackup = this.doBackup.bind(this);
        this.deleteBackup = this.deleteBackup.bind(this);
        this.restoreBackup = this.restoreBackup.bind(this);
        this.showConfirmRestore = this.showConfirmRestore.bind(this);
        this.closeConfirmRestore = this.closeConfirmRestore.bind(this);
        this.showConfirmBackup = this.showConfirmBackup.bind(this);
        this.closeConfirmBackup = this.closeConfirmBackup.bind(this);
        this.showConfirmBackupDelete = this.showConfirmBackupDelete.bind(this);
        this.closeConfirmBackupDelete = this.closeConfirmBackupDelete.bind(this);
        this.showBackupModal = this.showBackupModal.bind(this);
        this.closeBackupModal = this.closeBackupModal.bind(this);
        this.showRestoreSpinningModal = this.showRestoreSpinningModal.bind(this);
        this.closeRestoreSpinningModal = this.closeRestoreSpinningModal.bind(this);
        this.showDelBackupSpinningModal = this.showDelBackupSpinningModal.bind(this);
        this.closeDelBackupSpinningModal = this.closeDelBackupSpinningModal.bind(this);
        // LDIFS
        this.importLDIF = this.importLDIF.bind(this);
        this.deleteLDIF = this.deleteLDIF.bind(this);
        this.showConfirmLDIFImport = this.showConfirmLDIFImport.bind(this);
        this.closeConfirmLDIFImport = this.closeConfirmLDIFImport.bind(this);
        this.showConfirmLDIFDelete = this.showConfirmLDIFDelete.bind(this);
        this.closeConfirmLDIFDelete = this.closeConfirmLDIFDelete.bind(this);
        this.showLDIFSpinningModal = this.showLDIFSpinningModal.bind(this);
        this.showLDIFDeleteSpinningModal = this.showLDIFDeleteSpinningModal.bind(this);
        this.doExport = this.doExport.bind(this);
        this.showExportModal = this.showExportModal.bind(this);
        this.closeExportModal = this.closeExportModal.bind(this);
    }

    showExportModal () {
        this.setState({
            showExportModal: true,
            exportSpinner: false,
            ldifSuffix: this.props.suffixes[0]
        });
    }

    closeExportModal () {
        this.setState({
            showExportModal: false
        });
    }

    showLDIFSpinningModal () {
        this.setState({
            showLDIFSpinningModal: true
        });
    }

    closeLDIFSpinningModal () {
        this.setState({
            showLDIFSpinningModal: false
        });
    }

    showLDIFDeleteSpinningModal () {
        this.setState({
            showLDIFDeleteSpinningModal: true
        });
    }

    closeLDIFDeleteSpinningModal () {
        this.setState({
            showLDIFDeleteSpinningModal: false
        });
    }

    showDelBackupSpinningModal () {
        this.setState({
            showDelBackupSpinningModal: true
        });
    }

    closeDelBackupSpinningModal () {
        this.setState({
            showDelBackupSpinningModal: false
        });
    }

    showRestoreSpinningModal () {
        this.setState({
            showRestoreSpinningModal: true
        });
    }

    closeRestoreSpinningModal () {
        this.setState({
            showRestoreSpinningModal: false
        });
    }

    showBackupModal () {
        this.setState({
            showBackupModal: true,
            backupSpinning: false
        });
    }

    closeBackupModal () {
        this.setState({
            showBackupModal: false,
        });
    }

    showConfirmLDIFImport (item) {
        this.setState({
            showConfirmLDIFImport: true,
            ldifName: item.name,
            ldifSuffix: item.suffix[0]
        });
    }

    closeConfirmLDIFImport () {
        // call importLDIF
        this.setState({
            showConfirmLDIFImport: false,
        });
    }

    showConfirmLDIFDelete (item) {
        // call deleteLDIF
        this.setState({
            showConfirmLDIFDelete: true,
            ldifName: item.name
        });
    }

    closeConfirmLDIFDelete () {
        // call importLDIF
        this.setState({
            showConfirmLDIFDelete: false,
        });
    }

    showConfirmBackup (item) {
        // call deleteLDIF
        this.setState({
            showConfirmBackup: true,
            backupName: item.name,
        });
    }

    closeConfirmBackup () {
        // call importLDIF
        this.setState({
            showConfirmBackup: false,
        });
    }

    showConfirmRestore (item) {
        this.setState({
            showConfirmRestore: true,
            backupName: item.name,
        });
    }

    closeConfirmRestore () {
        // call importLDIF
        this.setState({
            showConfirmRestore: false,
        });
    }

    showConfirmBackupDelete (item) {
        // calls deleteBackup
        this.setState({
            showConfirmBackupDelete: true,
            backupName: item.name,
        });
    }

    closeConfirmBackupDelete () {
        // call importLDIF
        this.setState({
            showConfirmBackupDelete: false,
        });
    }

    importLDIF() {
        this.showLDIFSpinningModal();

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "import", this.state.ldifSuffix, this.state.ldifName, "--encrypted"
        ];
        log_cmd("importLDIF", "Importing LDIF", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeLDIFSpinningModal();
                    this.props.addNotification(
                        "success",
                        `LDIF was successfully imported`
                    );
                })
                .fail(err => {
                    this.closeLDIFSpinningModal();
                    this.props.addNotification(
                        "error",
                        `Failure importing LDIF - ${err}`
                    );
                });
    }

    deleteLDIF (e) {
        this.showLDIFDeleteSpinningModal();

        const cmd = [
            "dsctl", this.props.serverId, "ldifs", "--delete", this.state.ldifName
        ];
        log_cmd("deleteLDIF", "Deleting LDIF", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.closeLDIFDeleteSpinningModal();
                    this.props.addNotification(
                        "success",
                        `LDIF file was successfully deleted`
                    );
                })
                .fail(err => {
                    this.props.reload();
                    this.closeLDIFDeleteSpinningModal();
                    this.props.addNotification(
                        "error",
                        `Failure deleting LDIF file - ${err}`
                    );
                });
    }

    doBackup () {
        this.setState({
            backupSpinning: true
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backup", "create", this.state.backupName
        ];
        log_cmd("doBackup", "Add backup task", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.closeBackupModal();
                    this.props.addNotification(
                        "success",
                        `Server has been backed up`
                    );
                })
                .fail(err => {
                    this.props.reload();
                    this.closeBackupModal();
                    this.props.addNotification(
                        "error",
                        `Failure backing up server - ${err}`
                    );
                });
    }

    restoreBackup () {
        this.showRestoreSpinningModal();

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backup", "restore", this.state.backupName
        ];
        log_cmd("restoreBackup", "Restoring server", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeRestoreSpinningModal();
                    this.props.addNotification(
                        "success",
                        `Server has been restored`
                    );
                })
                .fail(err => {
                    this.closeRestoreSpinningModal();
                    this.props.addNotification(
                        "error",
                        `Failure restoring up server - ${err}`
                    );
                });
    }

    deleteBackup (e) {
        // Show confirmation
        this.showDelBackupSpinningModal();

        const cmd = [
            "dsctl", this.props.serverId, "backups", "--delete", this.state.backupName
        ];
        log_cmd("deleteBackup", "Deleting backup", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.closeDelBackupSpinningModal();
                    this.props.addNotification(
                        "success",
                        `Backup was successfully deleted`
                    );
                })
                .fail(err => {
                    this.props.reload();
                    this.closeDelBackupSpinningModal();
                    this.props.addNotification(
                        "error",
                        `Failure deleting backup - ${err}`
                    );
                });
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    handleChange(e) {
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
        });
    }

    doExport() {
        let missingArgs = {ldifLocation: false};
        if (this.state.ldifLocation == "") {
            this.props.addNotification(
                "warning",
                `LDIF name is empty`
            );
            missingArgs.ldifLocation = true;
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        // Do import
        let export_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "export", this.state.ldifSuffix, "--encrypted", "--ldif=" + this.state.ldifName
        ];

        this.setState({
            exportSpinner: true,
        });

        log_cmd("doExport", "Do online export", export_cmd);
        cockpit
                .spawn(export_cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.closeExportModal();
                    this.props.addNotification(
                        "success",
                        `Database export complete`
                    );
                    this.setState({
                        showExportModal: false,
                    });
                })
                .fail(err => {
                    this.props.reload();
                    this.closeExportModal();
                    this.props.addNotification(
                        "error",
                        `Error exporting database - ${err}`
                    );
                    this.setState({
                        showExportModal: false,
                    });
                });
    }

    render() {
        return (
            <div>
                <TabContainer id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div>
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: 'Backups'}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: 'LDIFs'}} />
                            </NavItem>
                        </Nav>
                        <TabContent>
                            <TabPane eventKey={1}>
                                <div className="ds-margin-top-xlg">
                                    <BackupTable
                                        rows={this.props.backups}
                                        confirmRestore={this.showConfirmRestore}
                                        confirmDelete={this.showConfirmBackupDelete}
                                    />
                                </div>
                                <p />
                                <Button
                                    bsStyle="primary"
                                    onClick={this.showBackupModal}
                                >
                                    Create Backup
                                </Button>
                            </TabPane>

                            <TabPane eventKey={2}>
                                <div className="ds-margin-top-xlg">
                                    <LDIFManageTable
                                        rows={this.props.ldifs}
                                        confirmImport={this.showConfirmLDIFImport}
                                        confirmDelete={this.showConfirmLDIFDelete}
                                    />
                                </div>
                                <p />
                                <Button
                                    bsStyle="primary"
                                    onClick={this.showExportModal}
                                >
                                    Create LDIF Export
                                </Button>
                            </TabPane>
                        </TabContent>
                    </div>
                </TabContainer>

                <ExportModal
                    showModal={this.state.showExportModal}
                    closeHandler={this.closeExportModal}
                    handleChange={this.handleChange}
                    saveHandler={this.doExport}
                    spinning={this.state.exportSpinner}
                    error={this.state.errObj}
                    suffixes={this.props.suffixes}
                />
                <BackupModal
                    showModal={this.state.showBackupModal}
                    closeHandler={this.closeBackupModal}
                    handleChange={this.handleChange}
                    saveHandler={this.doBackup}
                    spinning={this.state.backupSpinning}
                    error={this.state.errObj}
                />
                <RestoreModal
                    showModal={this.state.showRestoreSpinningModal}
                    closeHandler={this.closeRestoreSpinningModal}
                    msg={this.state.backupName}
                />
                <DeleteBackupModal
                    showModal={this.state.showDelBackupSpinningModal}
                    closeHandler={this.closeDelBackupSpinningModal}
                    msg={this.state.backupName}
                />
                <ImportingModal
                    showModal={this.state.showLDIFSpinningModal}
                    closeHandler={this.closeLDIFSpinningModal}
                    msg={this.state.ldifName}
                />
                <DeletingLDIFModal
                    showModal={this.state.showLDIFDeleteSpinningModal}
                    closeHandler={this.closeLDIFDeleteSpinningModal}
                    msg={this.state.ldifName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmLDIFDelete}
                    closeHandler={this.closeConfirmLDIFDelete}
                    actionFunc={this.deleteLDIF}
                    actionParam={this.state.ldifName}
                    msg="Are you sure you want to delete this LDIF?"
                    msgContent={this.state.ldifName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmLDIFImport}
                    closeHandler={this.closeConfirmLDIFImport}
                    actionFunc={this.importLDIF}
                    actionParam={this.state.ldifName}
                    msg="Are you sure you want to import this LDIF?"
                    msgContent={this.state.ldifName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmRestore}
                    closeHandler={this.closeConfirmRestore}
                    actionFunc={this.restoreBackup}
                    actionParam={this.state.backupName}
                    msg="Are you sure you want to restore this backup?"
                    msgContent={this.state.backupName}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmBackupDelete}
                    closeHandler={this.closeConfirmBackupDelete}
                    actionFunc={this.deleteBackup}
                    actionParam={this.state.backupName}
                    msg="Are you sure you want to delete this backup?"
                    msgContent={this.state.backupName}
                />

            </div>
        );
    }
}

class ExportModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            suffixes,
            spinning,
            error
        } = this.props;
        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-modal-spinner">
                        <Spinner loading inline size="lg" />Exporting database... <font size="1">(You can safely close this window)</font>
                    </div>
                </Row>;
        }
        let suffixList = suffixes.map((suffix) =>
            <option key={suffix} value={suffix}>{suffix}</option>
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
                            Export Database To LDIF File
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <Row>
                                <Col sm={3}>
                                    <ControlLabel>Select Suffix</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <select id="ldifSuffix" onChange={handleChange}>
                                        {suffixList}
                                    </select>
                                </Col>
                            </Row>
                            <p />
                            <Row title="Name of exported LDIF file">
                                <Col sm={3}>
                                    <ControlLabel>LDIF File Name</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <FormControl
                                        type="text"
                                        id="ldifName"
                                        className={error.ldifName ? "ds-input-bad" : ""}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </Row>
                            <p />
                            {spinner}
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
                            Create LDIF
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

export class BackupModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            spinning,
            error
        } = this.props;
        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-modal-spinner">
                        <Spinner loading inline size="lg" />Backing up databases... <font size="1">(You can safely close this window)</font>
                    </div>
                </Row>;
        }

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
                            Backup The Server
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <Row title="LDIF file to import">
                                <Col sm={3}>
                                    <ControlLabel>Backup Name</ControlLabel>
                                </Col>
                                <Col sm={9}>
                                    <FormControl
                                        type="text"
                                        id="backupName"
                                        className={error.backupName ? "ds-input-bad" : ""}
                                        onChange={handleChange}
                                    />
                                </Col>
                            </Row>
                            <p />
                            {spinner}
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
                            Do Backup
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class RestoreModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            msg
        } = this.props;

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
                            Restoring Server From Backup
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <div className="ds-modal-spinner">
                                <Spinner loading inline size="lg" /> Restoring backup <b>{msg}</b> ...
                                <p />
                                <p><font size="1"> (You can safely close this window)</font></p>
                            </div>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Close
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class DeleteBackupModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            msg
        } = this.props;

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
                            Delete Backup
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <div className="ds-modal-spinner">
                                <Spinner loading inline size="lg" /> Deleting backup <b>{msg}</b> ...
                                <p />
                                <p><font size="1"> (You can safely close this window)</font></p>
                            </div>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Close
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class ImportingModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            msg
        } = this.props;

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
                            Import LDIF File
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <div className="ds-modal-spinner">
                                <Spinner loading inline size="lg" /> Importing LDIF <b>{msg}</b> ...
                                <p />
                                <p><font size="1"> (You can safely close this window)</font></p>
                            </div>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Close
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class DeletingLDIFModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            msg
        } = this.props;

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
                            Delete LDIF File
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <div className="ds-modal-spinner">
                                <Spinner loading inline size="lg" /> Deleting LDIF file <b>{msg}</b> ...
                                <p />
                                <p><font size="1"> (You can safely close this window)</font></p>
                            </div>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Close
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

// Properties and defaults

ExportModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
    suffixes: PropTypes.array
};

BackupModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
};

DeletingLDIFModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    msg: PropTypes.string
};

RestoreModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    msg: PropTypes.string
};

DeleteBackupModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    msg: PropTypes.string
};

ImportingModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    msg: PropTypes.string
};

Backups.propTypes = {
    backups: PropTypes.array,
    ldifs: PropTypes.array,
    reload: PropTypes.func
};

Backups.defaultProps = {
    backups: [],
    ldifs: [],
    reload: noop
};
