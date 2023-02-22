import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { LDIFManageTable, BackupTable } from "./databaseTables.jsx";
import {
    Button,
    Checkbox,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions,
} from "@patternfly/react-core";
import { log_cmd, bad_file_name } from "../tools.jsx";
import PropTypes from "prop-types";

export class Backups extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeTabKey: 0,
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
            errObj: {},
            modalSpinning: false,
            modalChecked: false,
        };

        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.handleChange = this.handleChange.bind(this);
        this.handleConfirmChange = this.handleConfirmChange.bind(this);

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
        this.validateBackup = this.validateBackup.bind(this);
        this.closeConfirmRestoreReplace = this.closeConfirmRestoreReplace.bind(this);
        // LDIFS
        this.importLDIF = this.importLDIF.bind(this);
        this.deleteLDIF = this.deleteLDIF.bind(this);
        this.showConfirmLDIFImport = this.showConfirmLDIFImport.bind(this);
        this.closeConfirmLDIFImport = this.closeConfirmLDIFImport.bind(this);
        this.showConfirmLDIFDelete = this.showConfirmLDIFDelete.bind(this);
        this.closeConfirmLDIFDelete = this.closeConfirmLDIFDelete.bind(this);
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
            ldifSuffix: suffix,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeConfirmLDIFImport () {
        // call importLDIF
        this.setState({
            showConfirmLDIFImport: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    showConfirmLDIFDelete (name) {
        // call deleteLDIF
        this.setState({
            showConfirmLDIFDelete: true,
            ldifName: name,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeConfirmLDIFDelete () {
        // call importLDIF
        this.setState({
            showConfirmLDIFDelete: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    showConfirmBackup (name) {
        // call deleteLDIF
        this.setState({
            showConfirmBackup: true,
            backupName: name,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeConfirmBackup () {
        // call importLDIF
        this.setState({
            showConfirmBackup: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    showConfirmRestore (name) {
        this.setState({
            showConfirmRestore: true,
            backupName: name,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeConfirmRestore () {
        // call importLDIF
        this.setState({
            showConfirmRestore: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    showConfirmBackupDelete (name) {
        // calls deleteBackup
        this.setState({
            showConfirmBackupDelete: true,
            backupName: name,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeConfirmBackupDelete () {
        // call importLDIF
        this.setState({
            showConfirmBackupDelete: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeConfirmRestoreReplace () {
        this.setState({
            showConfirmRestoreReplace: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    handleConfirmChange(e) {
        this.setState({
            modalChecked: e.target.checked
        });
    }

    importLDIF() {
        this.setState({
            modalSpinning: true
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "import", this.state.ldifSuffix, this.state.ldifName, "--encrypted"
        ];
        log_cmd("importLDIF", "Importing LDIF", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeConfirmLDIFImport();
                    this.props.addNotification(
                        "success",
                        `LDIF was successfully imported`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.closeConfirmLDIFImport();
                    this.props.addNotification(
                        "error",
                        `Failure importing LDIF - ${errMsg.desc}`
                    );
                });
    }

    deleteLDIF (e) {
        this.setState({
            modalSpinning: true
        });
        const cmd = [
            "dsctl", this.props.serverId,
            "ldifs", "--delete", this.state.ldifName
        ];
        log_cmd("deleteLDIF", "Deleting LDIF", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.closeConfirmLDIFDelete();
                    this.props.addNotification(
                        "success",
                        `LDIF file was successfully deleted`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload();
                    this.closeConfirmLDIFDelete();
                    this.props.addNotification(
                        "error",
                        `Failure deleting LDIF file - ${errMsg.desc}`
                    );
                });
    }

    validateBackup() {
        for (let i = 0; i < this.props.backups.length; i++) {
            if (this.state.backupName == this.props.backups[i].name) {
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
                    const cmd = [
                        "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                        "config", "get", "nsslapd-bakdir"
                    ];
                    log_cmd("doBackup", "Get the backup directory", cmd);
                    cockpit
                            .spawn(cmd, { superuser: true, err: "message" })
                            .done(content => {
                                const config = JSON.parse(content);
                                const attrs = config.attrs;
                                this.props.addNotification(
                                    "success",
                                    `Server has been backed up. You can find the backup in ${attrs['nsslapd-bakdir'][0]} directory on the server machine.`
                                );
                            })
                            .fail(err => {
                                const errMsg = JSON.parse(err);
                                this.props.addNotification(
                                    "success",
                                    `Server has been backed up.`
                                );
                                this.props.addNotification(
                                    "error",
                                    `Error while trying to get the server's backup directory- ${errMsg.desc}`
                                );
                        });
                }
                )
                .fail(err => {
                    const errMsg = JSON.parse(err);
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

        this.setState({
            modalSpinning: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backup", "restore", this.state.backupName
        ];
        log_cmd("restoreBackup", "Restoring server", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeConfirmRestore();
                    this.props.addNotification(
                        "success",
                        `Server has been restored`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.closeRestoreSpinningModal();
                    this.props.addNotification(
                        "error",
                        `Failure restoring up server - ${errMsg.desc}`
                    );
                });
    }

    deleteBackup (e) {
        // Show confirmation
        this.setState({ modalSpinning: true });

        const cmd = [
            "dsctl", this.props.serverId, "backups", "--delete", this.state.backupName
        ];
        log_cmd("deleteBackup", "Deleting backup", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.closeConfirmBackupDelete();
                    this.props.addNotification(
                        "success",
                        `Backup was successfully deleted`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload();
                    this.closeConfirmBackupDelete();
                    this.props.addNotification(
                        "error",
                        `Failure deleting backup - ${errMsg.desc}`
                    );
                });
    }

    handleChange(e) {
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

    validateLDIF() {
        let ldifname = this.state.ldifName;
        if (!ldifname.endsWith(".ldif")) {
            // dsconf/dsctl adds ".ldif" if not set, so that's what we need to check
            ldifname = ldifname + ".ldif";
        }
        for (let i = 0; i < this.props.ldifs.length; i++) {
            if (ldifname == this.props.ldifs[i].name) {
                this.setState({
                    showConfirmLDIFReplace: true
                });
                return;
            }
        }
        this.doExport();
    }

    doExport() {
        const missingArgs = { ldifName: false };
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
        const export_cmd = [
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
                    this.setState({
                        showExportModal: false,
                    });
                    const cmd = [
                        "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                        "config", "get", "nsslapd-ldifdir"
                    ];
                    log_cmd("doExport", "Get the backup directory", cmd);
                    cockpit
                            .spawn(cmd, { superuser: true, err: "message" })
                            .done(content => {
                                const config = JSON.parse(content);
                                const attrs = config.attrs;
                                this.props.addNotification(
                                    "success",
                                    `Database export complete. You can find the LDIF file in ${attrs['nsslapd-ldifdir'][0]} directory on the server machine.`
                                );
                            })
                            .fail(err => {
                                const errMsg = JSON.parse(err);
                                this.props.addNotification(
                                    "success",
                                    `Database export complete.`
                                );
                                this.props.addNotification(
                                    "error",
                                    `Error while trying to get the server's LDIF directory- ${errMsg.desc}`
                                );
                            });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
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
        let refreshBtnName = "Refresh";
        const extraPrimaryProps = {};
        if (this.props.refreshing) {
            refreshBtnName = "Refreshing ...";
            extraPrimaryProps.spinnerAriaValueText = "Refreshing";
        }

        return (
            <div>
                <TextContent>
                    <Text className="ds-config-header" component={TextVariants.h2}>
                        Database Backups & LDIFs
                    </Text>
                </TextContent>
                <Tabs className="ds-margin-top-lg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Backups <font size="2">({this.props.backups.length})</font></TabTitleText>}>
                        <div className="ds-indent">
                            <div className="ds-margin-top-lg">
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
                                    variant="secondary"
                                    onClick={() => {
                                        this.props.reload(1);
                                    }}
                                    className="ds-left-margin ds-margin-top"
                                    isLoading={this.props.refreshing}
                                    spinnerAriaValueText={this.props.refreshing ? "Refreshing" : undefined}
                                    {...extraPrimaryProps}
                                >
                                    {refreshBtnName}
                                </Button>
                            </div>
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>LDIFs <font size="2">({this.props.ldifs.length})</font></TabTitleText>}>
                        <div className="ds-indent">
                            <div className="ds-margin-top-lg">
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
                                    Create LDIF
                                </Button>
                                <Button
                                    variant="secondary"
                                    onClick={this.props.reload}
                                    className="ds-left-margin ds-margin-top"
                                    isLoading={this.props.refreshing}
                                    spinnerAriaValueText={this.props.refreshing ? "Refreshing" : undefined}
                                    {...extraPrimaryProps}
                                >
                                    {refreshBtnName}
                                </Button>
                            </div>
                        </div>
                    </Tab>
                </Tabs>

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
                <ImportingModal
                    showModal={this.state.showLDIFSpinningModal}
                    closeHandler={this.closeLDIFSpinningModal}
                    msg={this.state.ldifName}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmLDIFDelete}
                    closeHandler={this.closeConfirmLDIFDelete}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.deleteLDIF}
                    spinning={this.state.modalSpinning}
                    item={this.state.ldifName}
                    checked={this.state.modalChecked}
                    mTitle="Delete LDIF File"
                    mMsg="Are you sure you want to delete this LDIF?"
                    mSpinningMsg="Deleting LDIF ..."
                    mBtnName="Delete LDIF"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmLDIFImport}
                    closeHandler={this.closeConfirmLDIFImport}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.importLDIF}
                    spinning={this.state.modalSpinning}
                    item={this.state.ldifName}
                    checked={this.state.modalChecked}
                    mTitle="Import LDIF File"
                    mMsg="Are you sure you want to import this LDIF?"
                    mSpinningMsg="Importing LDIF ..."
                    mBtnName="Import LDIF"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmRestore}
                    closeHandler={this.closeConfirmRestore}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.restoreBackup}
                    spinning={this.state.modalSpinning}
                    item={this.state.backupName}
                    checked={this.state.modalChecked}
                    mTitle="Restore Database"
                    mMsg="Are you sure you want to restore from this backup?"
                    mSpinningMsg="Restoring ..."
                    mBtnName="Restore"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmBackupDelete}
                    closeHandler={this.closeConfirmBackupDelete}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.deleteBackup}
                    spinning={this.state.modalSpinning}
                    item={this.state.backupName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Backup"
                    mMsg="Are you sure you want to delete this backup?"
                    mSpinningMsg="Deleting backup ..."
                    mBtnName="Delete Backup"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmRestoreReplace}
                    closeHandler={this.closeConfirmRestoreReplace}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.doBackup}
                    spinning={this.state.modalSpinning}
                    item={this.state.backupName}
                    checked={this.state.modalChecked}
                    mTitle="Replace Existing Backup"
                    mMsg="A backup already eixsts with the same name, do you want to replace it?"
                    mSpinningMsg="Replacing backup ..."
                    mBtnName="Replace Backup"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmLDIFReplace}
                    closeHandler={this.closeConfirmLDIFReplace}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.doExport}
                    spinning={this.state.modalSpinning}
                    item={this.state.ldifName}
                    checked={this.state.modalChecked}
                    mTitle="Replace Existing LDIF File"
                    mMsg="A LDIF file already exists with the same name, do you want to replace it?"
                    mSpinningMsg="Replacing LDIF ..."
                    mBtnName="Replace LDIF"
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
        let createBtnName = "Create LDIF";
        const extraPrimaryProps = {};
        let exportMsg = "";
        if (spinning) {
            createBtnName = "Creating ...";
            extraPrimaryProps.spinnerAriaValueText = "Creating";
        }
        if (spinning) {
            exportMsg =
                <div className="ds-margin-top">
                    <font size="2">(You can safely close this window)</font>
                </div>;
        }

        const suffixList = suffixes.map((suffix) => (
            <FormSelectOption key={suffix} value={suffix} label={suffix} />
        ));

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Export Database To LDIF File"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Creating ..." : undefined}
                        isDisabled={spinning}
                        {...extraPrimaryProps}
                    >
                        {createBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            Select Suffix
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect id="ldifSuffix" onChange={(value, event) => { handleChange(event) }} aria-label="FormSelect Input">
                                {suffixList}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Name of exported LDIF file, if left blank the data and time will be used as the file name"
                    >
                        <GridItem className="ds-label" span={3}>
                            LDIF File Name
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="ldifName"
                                aria-describedby="horizontal-form-name-helper"
                                name="ldifName"
                                onChange={(value, event) => { handleChange(event) }}
                                validated={error.ldifName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Name of exported LDIF file, if left blank the data and time will be used as the file name"
                    >
                        <GridItem span={12}>
                            <Checkbox
                                id="includeReplData"
                                className="ds-indent ds-margin-top"
                                isChecked={this.props.includeReplData}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Include the replication metadata needed to restore or initialize another replica."
                                label="Include Replication Data"
                            />
                        </GridItem>
                    </Grid>
                    {exportMsg}
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
        let createBtnName = "Create Backup";
        const extraPrimaryProps = {};
        let exportMsg = "";
        if (spinning) {
            createBtnName = "Backing up ...";
            extraPrimaryProps.spinnerAriaValueText = "Backing up";
        }
        if (spinning) {
            exportMsg =
                <div className="ds-margin-top">
                    <font size="2">(You can safely close this window)</font>
                </div>;
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Backup The Server"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        isDisabled={spinning}
                        spinnerAriaValueText={spinning ? "Creating ..." : undefined}
                        {...extraPrimaryProps}
                    >
                        {createBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid
                        title="Backup name, if left blank the date and time will be used as the name"
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            Backup Name
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="backupName"
                                aria-describedby="horizontal-form-name-helper"
                                name="ldifName"
                                onChange={(value, event) => { handleChange(event) }}
                                validated={error.backupName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    {exportMsg}
                </Form>
            </Modal>
        );
    }
}

export class ImportingModal extends React.Component {
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
};
