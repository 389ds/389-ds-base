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
    ControlLabel,
    FormControl,
    Form,
    Row,
} from "patternfly-react";
import {
    Button,
    Checkbox,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    Spinner,
    // TextInput,
    noop
} from "@patternfly/react-core";
import { log_cmd, bad_file_name } from "../tools.jsx";
import PropTypes from "prop-types";

export class Backups extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeKey: 1,
            showConfirmBackupDelete: false,
            showConfirmBackup: false,
            showConfirmRestoreReplace: false,
            showConfirmLDIFReplace: false,
            showRestoreSpinningModal: false,
            showDelBackupSpinningModal: false,
            showBackupModal: false,
            backupSpinning: false,
            backupName: "",
            deleteBackup: "",
            // LDIF
            showConfirmLDIFDelete: false,
            showConfirmLDIFImport: false,
            showConfirmRestore: false,
            showLDIFSpinningModal: false,
            showLDIFDeleteSpinningModal: false,
            showExportModal: false,
            exportSpinner: false,
            includeReplData: false,
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
        this.validateBackup = this.validateBackup.bind(this);
        this.closeConfirmRestoreReplace = this.closeConfirmRestoreReplace.bind(this);
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
        this.validateLDIF = this.validateLDIF.bind(this);
        this.closeConfirmLDIFReplace = this.closeConfirmLDIFReplace.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    showExportModal () {
        this.setState({
            showExportModal: true,
            exportSpinner: false,
            ldifName: "",
            ldifSuffix: this.props.suffixes[0],
            includeReplData: false,
        });
    }

    closeExportModal () {
        this.setState({
            showExportModal: false
        });
    }

    closeConfirmLDIFReplace () {
        this.setState({
            showConfirmLDIFReplace: false
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
            backupSpinning: false,
            backupName: ""
        });
    }

    closeBackupModal () {
        this.setState({
            showBackupModal: false,
        });
    }

    showConfirmLDIFImport (name, suffix) {
        this.setState({
            showConfirmLDIFImport: true,
            ldifName: name,
            ldifSuffix: suffix
        });
    }

    closeConfirmLDIFImport () {
        // call importLDIF
        this.setState({
            showConfirmLDIFImport: false,
        });
    }

    showConfirmLDIFDelete (name) {
        // call deleteLDIF
        this.setState({
            showConfirmLDIFDelete: true,
            ldifName: name
        });
    }

    closeConfirmLDIFDelete () {
        // call importLDIF
        this.setState({
            showConfirmLDIFDelete: false,
        });
    }

    showConfirmBackup (name) {
        // call deleteLDIF
        this.setState({
            showConfirmBackup: true,
            backupName: name,
        });
    }

    closeConfirmBackup () {
        // call importLDIF
        this.setState({
            showConfirmBackup: false,
        });
    }

    showConfirmRestore (name) {
        this.setState({
            showConfirmRestore: true,
            backupName: name,
        });
    }

    closeConfirmRestore () {
        // call importLDIF
        this.setState({
            showConfirmRestore: false,
        });
    }

    showConfirmBackupDelete (name) {
        // calls deleteBackup
        this.setState({
            showConfirmBackupDelete: true,
            backupName: name,
        });
    }

    closeConfirmBackupDelete () {
        // call importLDIF
        this.setState({
            showConfirmBackupDelete: false,
        });
    }

    closeConfirmRestoreReplace () {
        this.setState({
            showConfirmRestoreReplace: false,
        });
    }

    importLDIF() {
        this.showLDIFSpinningModal();

        let cmd = [
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
                    let errMsg = JSON.parse(err);
                    this.closeLDIFSpinningModal();
                    this.props.addNotification(
                        "error",
                        `Failure importing LDIF - ${errMsg.desc}`
                    );
                });
    }

    deleteLDIF (e) {
        this.showLDIFDeleteSpinningModal();

        let cmd = [
            "dsctl", this.props.serverId,
            "ldifs", "--delete", this.state.ldifName
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
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.closeLDIFDeleteSpinningModal();
                    this.props.addNotification(
                        "error",
                        `Failure deleting LDIF file - ${errMsg.desc}`
                    );
                });
    }

    validateBackup() {
        for (let i = 0; i < this.props.backups.length; i++) {
            if (this.state.backupName == this.props.backups[i]['name']) {
                this.setState({
                    showConfirmRestoreReplace: true
                });
                return;
            }
        }
        this.doBackup();
    }

    doBackup () {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backup", "create"
        ];
        if (this.state.backupName != "") {
            if (bad_file_name(this.state.backupName)) {
                this.props.addNotification(
                    "warning",
                    `Backup name should not be a path.  All backups are stored in the server's backup directory`
                );
                return;
            }
            cmd.push(this.state.backupName);
        }

        this.setState({
            backupSpinning: true
        });

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
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.closeBackupModal();
                    this.props.addNotification(
                        "error",
                        `Failure backing up server - ${errMsg.desc}`
                    );
                });
    }

    restoreBackup () {
        if (this.props.suffixes.length == 0) {
            this.props.addNotification(
                "error",
                `There are no databases defined to restore`
            );
            return;
        }

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
                    let errMsg = JSON.parse(err);
                    this.closeRestoreSpinningModal();
                    this.props.addNotification(
                        "error",
                        `Failure restoring up server - ${errMsg.desc}`
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
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.closeDelBackupSpinningModal();
                    this.props.addNotification(
                        "error",
                        `Failure deleting backup - ${errMsg.desc}`
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

    validateLDIF() {
        let ldifname = this.state.ldifName;
        if (!ldifname.endsWith(".ldif")) {
            // dsconf/dsctl adds ".ldif" if not set, so that's what we need to check
            ldifname = ldifname + ".ldif";
        }
        for (let i = 0; i < this.props.ldifs.length; i++) {
            if (ldifname == this.props.ldifs[i]['name']) {
                this.setState({
                    showConfirmLDIFReplace: true
                });
                return;
            }
        }
        this.doExport();
    }

    doExport() {
        let missingArgs = {ldifName: false};
        if (this.state.ldifName == "") {
            this.props.addNotification(
                "warning",
                `LDIF name is empty`
            );
            missingArgs.ldifName = true;
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        // Must not be a path
        if (bad_file_name(this.state.ldifName)) {
            this.props.addNotification(
                "warning",
                `LDIF name should not be a path.  All export files are stored in the server's LDIF directory`
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

        if (this.state.includeReplData) {
            export_cmd.push("--replication");
        }

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
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.closeExportModal();
                    this.props.addNotification(
                        "error",
                        `Error exporting database - ${errMsg.desc}`
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
                                        key={this.props.backups}
                                        rows={this.props.backups}
                                        confirmRestore={this.showConfirmRestore}
                                        confirmDelete={this.showConfirmBackupDelete}
                                    />
                                </div>
                                <div className="ds-inline">
                                    <Button
                                        variant="primary"
                                        onClick={this.showBackupModal}
                                        className="ds-margin-top"
                                    >
                                        Create Backup
                                    </Button>
                                    <Button
                                        variant="default"
                                        onClick={this.props.reload}
                                        className="ds-left-margin ds-margin-top"
                                    >
                                        Refresh Backups
                                    </Button>
                                </div>
                            </TabPane>

                            <TabPane eventKey={2}>
                                <div className="ds-margin-top-xlg">
                                    <LDIFManageTable
                                        key={this.props.ldifs}
                                        rows={this.props.ldifs}
                                        confirmImport={this.showConfirmLDIFImport}
                                        confirmDelete={this.showConfirmLDIFDelete}
                                    />
                                </div>
                                <div className="ds-inline">
                                    <Button
                                        variant="primary"
                                        onClick={this.showExportModal}
                                        className="ds-margin-top"
                                    >
                                        Create LDIF Export
                                    </Button>
                                    <Button
                                        variant="default"
                                        onClick={this.props.reload}
                                        className="ds-left-margin ds-margin-top"
                                    >
                                        Refresh LDIFs
                                    </Button>
                                </div>
                            </TabPane>
                        </TabContent>
                    </div>
                </TabContainer>

                <ExportModal
                    showModal={this.state.showExportModal}
                    closeHandler={this.closeExportModal}
                    handleChange={this.handleChange}
                    saveHandler={this.validateLDIF}
                    spinning={this.state.exportSpinner}
                    error={this.state.errObj}
                    suffixes={this.props.suffixes}
                    includeReplData={this.state.includeReplData}
                />
                <BackupModal
                    showModal={this.state.showBackupModal}
                    closeHandler={this.closeBackupModal}
                    handleChange={this.handleChange}
                    saveHandler={this.validateBackup}
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
                <ConfirmPopup
                    showModal={this.state.showConfirmRestoreReplace}
                    closeHandler={this.closeConfirmRestoreReplace}
                    actionFunc={this.doBackup}
                    msg="Replace Existing Backup"
                    msgContent="A backup already eixsts with the same name, do you want to replace it?"
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmLDIFReplace}
                    closeHandler={this.closeConfirmLDIFReplace}
                    actionFunc={this.doExport}
                    msg="Replace Existing LDIF File"
                    msgContent="A LDIF file already eixsts with the same name, do you want to replace it?"
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
            error,
        } = this.props;
        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="md" /> Exporting database... <font size="2">(You can safely close this window)</font>
                    </div>
                </Row>;
        }
        let suffixList = suffixes.map((suffix) =>
            <option key={suffix} value={suffix}>{suffix}</option>
        );
        return (
            <Modal
                variant={ModalVariant.small}
                title="Export Database To LDIF File"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={saveHandler}>
                        Create LDIF
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <Row>
                        <Col sm={4}>
                            <ControlLabel>Select Suffix</ControlLabel>
                        </Col>
                        <Col sm={8}>
                            <select id="ldifSuffix" onChange={handleChange}>
                                {suffixList}
                            </select>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Name of exported LDIF file, if left blank the data and time will be used as the file name">
                        <Col sm={4}>
                            <ControlLabel>LDIF File Name</ControlLabel>
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="ldifName"
                                className={error.ldifName ? "ds-input-bad" : ""}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top-xlg">
                        <Col sm={12} className="ds-margin-left">
                            <Checkbox
                                id="includeReplData"
                                isChecked={this.props.includeReplData}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Include the replication metadata needed to restore or initialize another replica."
                                label="Include Replication Data"
                            />
                        </Col>
                    </Row>
                    {spinner}
                </Form>
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
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="md" /> Backing up databases... <font size="2">(You can safely close this window)</font>
                    </div>
                </Row>;
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Backup The Server"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={saveHandler}>
                        Create Backup
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <Row title="Backup name, if left blank the date and time will be used as the name">
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
                    {spinner}
                </Form>
            </Modal>
        );
    }
}

export class RestoreModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            msg
        } = this.props;

        return (
            <Modal
                variant={ModalVariant.small}
                title="Restoring Server From Backup"
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Close
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <div className="ds-modal-spinner">
                        <Spinner size="md" /> Restoring backup <b>{msg}</b> ...
                        <p className="ds-margin-top"><font size="2"> (You can safely close this window)</font></p>
                    </div>
                </Form>
            </Modal>
        );
    }
}

export class DeleteBackupModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            msg
        } = this.props;

        return (
            <Modal
                variant={ModalVariant.small}
                title="Delete Backup"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Close
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <div className="ds-modal-spinner">
                        <Spinner size="md" /> Deleting backup <b>{msg}</b> ...
                        <p className="ds-margin-top"><font size="2"> (You can safely close this window)</font></p>
                    </div>
                </Form>
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
            <Modal
                variant={ModalVariant.small}
                title="Import LDIF File"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Close
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <div className="ds-modal-spinner">
                        <Spinner size="md" /> Importing LDIF <b>{msg}</b> ...
                        <p className="ds-margin-top"><font size="2"> (You can safely close this window)</font></p>
                    </div>
                </Form>
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
            <Modal
                variant={ModalVariant.small}
                title="Delete LDIF File"
                isOpen={showModal}
                onClose={closeHandler}
                aria-labelledby="ds-modal"
                actions={[
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Close
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <div className="ds-modal-spinner">
                        <Spinner size="md" /> Deleting LDIF file <b>{msg}</b> ...
                        <p className="ds-margin-top"><font size="2"> (You can safely close this window)</font></p>
                    </div>
                </Form>
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
    reload: PropTypes.func,
    enableTree: PropTypes.func,
};

Backups.defaultProps = {
    backups: [],
    ldifs: [],
    reload: noop,
    enableTree: noop,
};
