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
    Tab,
    Tabs,
    TabTitleText,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions,
} from "@patternfly/react-core";
import { LogViewer } from '@patternfly/react-log-viewer';
import { log_cmd, bad_file_name, getApiErrorMessage } from "../tools.jsx";
import PropTypes from "prop-types";

const _ = cockpit.gettext;

export class Backups extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            activeTabKey: 0,
            showConfirmBackupDelete: false,
            showConfirmBackup: false,
            showConfirmRestoreReplace: false,
            showConfirmLDIFReplace: false,
            showBackupModal: false,
            backupSpinning: false,
            backupName: "",
            deleteBackup: "",
            backupBuffer: "",
            importBuffer: "",
            exportBuffer: "",
            restoreBuffer: "",
            backupCompleted: false,
            restoreCompleted: false,
            exportCompleted: false,
            importCompleted: false,
            // LDIF
            showConfirmLDIFDelete: false,
            showConfirmLDIFImport: false,
            showConfirmRestore: false,
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

        this.onChange = this.onChange.bind(this);
        this.onConfirmChange = this.onConfirmChange.bind(this);

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
        this.handleShowBackupModal = this.handleShowBackupModal.bind(this);
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
        this.handleShowExportModal = this.handleShowExportModal.bind(this);
        this.closeExportModal = this.closeExportModal.bind(this);
        this.validateLDIF = this.validateLDIF.bind(this);
        this.closeConfirmLDIFReplace = this.closeConfirmLDIFReplace.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    handleShowExportModal () {
        this.setState({
            showExportModal: true,
            exportSpinner: false,
            ldifName: "",
            ldifSuffix: this.props.suffixes[0],
            includeReplData: false,
            exportBuffer: "",
            exportCompleted: false,
        });
    }

    closeExportModal () {
        this.setState({
            showExportModal: false,
            exportBuffer: "",
            exportCompleted: false,
        });
    }

    closeConfirmLDIFReplace () {
        this.setState({
            showConfirmLDIFReplace: false,
            exportCompleted: false,
        });
    }

    handleShowBackupModal () {
        this.setState({
            showBackupModal: true,
            backupSpinning: false,
            backupName: "",
            backupCompleted: false,
        });
    }

    closeBackupModal () {
        this.setState({
            showBackupModal: false,
            backupBuffer: "",
            backupCompleted: false,
        });
    }

    showConfirmLDIFImport (name, suffix) {
        this.setState({
            showConfirmLDIFImport: true,
            ldifName: name,
            ldifSuffix: suffix,
            modalSpinning: false,
            modalChecked: false,
            importBuffer: "",
            importCompleted: false,
        });
    }

    closeConfirmLDIFImport () {
        // call importLDIF
        this.setState({
            showConfirmLDIFImport: false,
            modalSpinning: false,
            modalChecked: false,
            importBuffer: "",
            importCompleted: false,
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
            modalChecked: false,
            backupBuffer: "",
            backupCompleted: false,
        });
    }

    closeConfirmBackup () {
        // call importLDIF
        this.setState({
            showConfirmBackup: false,
            modalSpinning: false,
            modalChecked: false,
            backupBuffer: "",
            backupCompleted: false,
        });
    }

    showConfirmRestore (name) {
        this.setState({
            showConfirmRestore: true,
            backupName: name,
            modalSpinning: false,
            modalChecked: false,
            restoreBuffer: "",
            restoreCompleted: false,
        });
    }

    closeConfirmRestore () {
        // call importLDIF
        this.setState({
            showConfirmRestore: false,
            modalSpinning: false,
            modalChecked: false,
            restoreBuffer: "",
            restoreCompleted: false,
        });
    }

    showConfirmBackupDelete (name) {
        // calls deleteBackup
        this.setState({
            showConfirmBackupDelete: true,
            backupName: name,
            modalSpinning: false,
            modalChecked: false,
            backupCompleted: false,
        });
    }

    closeConfirmBackupDelete () {
        // call importLDIF
        this.setState({
            showConfirmBackupDelete: false,
            modalSpinning: false,
            modalChecked: false,
            backupCompleted: false,
        });
    }

    closeConfirmRestoreReplace () {
        this.setState({
            showConfirmRestoreReplace: false,
            modalSpinning: false,
            modalChecked: false,
            backupBuffer: "",
            restoreCompleted: false,
        });
    }

    onConfirmChange(e) {
        this.setState({
            modalChecked: e.target.checked
        });
    }

    importLDIF() {
        this.setState({
            modalSpinning: true,
            importBuffer: "",
        });

        let importBuffer = "";
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "import", this.state.ldifSuffix, this.state.ldifName, "--encrypted",
            "--watch"
        ];
        log_cmd("importLDIF", "Importing LDIF", cmd);
        cockpit
                .spawn(cmd, { pty: true, superuser: true, err: "message" })
                .done(content => {
                    this.setState({
                        modalSpinning: false,
                        importCompleted: true,
                    });
                    this.props.addNotification(
                        "success",
                        _("LDIF was successfully imported")
                    );
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.setState({
                        modalSpinning: false,
                        importCompleted: true,
                    });
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failure importing LDIF - $0"), errMsg)
                    );
                })
                .stream(data => {
                    importBuffer += data;
                    this.setState({
                        importBuffer: importBuffer
                    });
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
                    this.props.handleReload();
                    this.closeConfirmLDIFDelete();
                    this.props.addNotification(
                        "success",
                        _("LDIF file was successfully deleted")
                    );
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.props.handleReload();
                    this.closeConfirmLDIFDelete();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failure deleting LDIF file - $0"), errMsg)
                    );
                });
    }

    validateBackup() {
        for (let i = 0; i < this.props.backups.length; i++) {
            if (this.state.backupName === this.props.backups[i].name) {
                this.setState({
                    showConfirmRestoreReplace: true
                });
                return;
            }
        }
        this.doBackup();
    }

    doBackup () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backup", "create"
        ];
        if (this.state.backupName !== "") {
            if (bad_file_name(this.state.backupName)) {
                this.props.addNotification(
                    "warning",
                    _("Backup name should not be a path.  All backups are stored in the server's backup directory")
                );
                return;
            }
            cmd.push(this.state.backupName);
        }
        cmd.push("--watch");

        let backupBuffer = "";
        this.setState({
            modalSpinning: true,
            backupBuffer: "",
        });

        log_cmd("doBackup", "Add backup task", cmd);
        cockpit
                .spawn(cmd, { pty: true, superuser: true, err: "message" })
                .done(content => {
                    this.props.handleReload();
                    this.setState({
                        modalSpinning: false,
                        backupCompleted: true,
                    });
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
                                    cockpit.format(_("Server has been backed up. You can find the backup in $0 directory on the server machine."), attrs['nsslapd-bakdir'][0])
                                );
                            })
                            .fail(err => {
                                const errMsg = getApiErrorMessage(err);
                                this.props.addNotification(
                                    "success",
                                    _("Server has been backed up.")
                                );
                                this.props.addNotification(
                                    "error",
                                    cockpit.format(_("Error while trying to get the server's backup directory- $0"), errMsg)
                                );
                            });
                })
                .stream(data => {
                    backupBuffer += data;
                    this.setState({
                        backupBuffer: backupBuffer
                    });
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.props.handleReload();
                    this.setState({
                        modalSpinning: false,
                        backupCompleted: true,
                    });
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failure backing up server - $0"), errMsg)
                    );
                });
    }

    restoreBackup () {
        if (this.props.suffixes.length === 0) {
            this.props.addNotification(
                "error",
                _("There are no databases defined to restore")
            );
            return;
        }

        this.setState({
            modalSpinning: true,
            restoreBuffer: "",
        });
        let restoreBuffer = "";
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backup", "restore", this.state.backupName, "--watch"
        ];
        log_cmd("restoreBackup", "Restoring server", cmd);
        cockpit
                .spawn(cmd, { pty: true, superuser: true, err: "message" })
                .done(content => {
                    this.setState({
                        modalSpinning: false,
                        restoreCompleted: true,
                    });
                    this.props.addNotification(
                        "success",
                        _("Server has been restored")
                    );
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.setState({
                        modalSpinning: false,
                        restoreCompleted: true,
                    });
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failure restoring up server - $0"), errMsg)
                    );
                })
                .stream(data => {
                    restoreBuffer += data;
                    this.setState({
                        restoreBuffer: restoreBuffer
                    });
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
                    this.props.handleReload();
                    this.closeConfirmBackupDelete();
                    this.props.addNotification(
                        "success",
                        _("Backup was successfully deleted")
                    );
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.props.handleReload();
                    this.closeConfirmBackupDelete();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failure deleting backup - $0"), errMsg)
                    );
                });
    }

    onChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        if (value === "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj
        });
    }

    validateLDIF() {
        let ldifname = this.state.ldifName;
        if (!ldifname.endsWith(".ldif")) {
            // dsconf/dsctl adds ".ldif" if not set, so that's what we need to check
            ldifname = ldifname + ".ldif";
        }
        for (let i = 0; i < this.props.ldifs.length; i++) {
            if (ldifname === this.props.ldifs[i].name) {
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
        if (this.state.ldifName === "") {
            this.props.addNotification(
                "warning",
                _("LDIF name is empty")
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
                _("LDIF name should not be a path.  All export files are stored in the server's LDIF directory")
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
            "backend", "export", this.state.ldifSuffix, "--encrypted", "--ldif=" + this.state.ldifName,
            "--watch"
        ];

        if (this.state.includeReplData) {
            export_cmd.push("--replication");
        }

        this.setState({
            exportSpinner: true,
            exportBuffer: "",
        });

        let exportBuffer = "";
        log_cmd("doExport", "Do online export", export_cmd);
        cockpit
                .spawn(export_cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.handleReload();
                    this.setState({
                        exportSpinner: false,
                        exportCompleted: true,
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
                                    cockpit.format(_("Database export complete. You can find the LDIF file in $0 directory on the server machine."), attrs['nsslapd-ldifdir'][0])
                                );
                            })
                            .fail(err => {
                                const errMsg = getApiErrorMessage(err);
                                this.props.addNotification(
                                    "success",
                                    _("Database export complete.")
                                );
                                this.props.addNotification(
                                    "error",
                                    cockpit.format(_("Error while trying to get the server's LDIF directory- $0"), errMsg)
                                );
                            });
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.props.handleReload();
                    this.setState({
                        exportSpinner: false,
                        exportCompleted: true,
                    });
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error exporting database - $0"), errMsg)
                    );
                })
                .stream(data => {
                    exportBuffer += data;
                    this.setState({
                        exportBuffer: exportBuffer
                    });
                });
    }

    render() {
        let refreshBtnName = _("Refresh");
        const extraPrimaryProps = {};
        if (this.props.refreshing) {
            refreshBtnName = _("Refreshing ...");
            extraPrimaryProps.spinnerAriaValueText = _("Refreshing");
        }

        let restoreBufferItem = null;
        if (this.state.restoreBuffer !== "") {
            restoreBufferItem = <LogViewer
                data={this.state.restoreBuffer}
                isTextWrapped={false}
                hasLineNumbers={false}
                scrollToRow={this.state.restoreBuffer.length}
                height="200px"
            />;
        }

        let exportBufferItem = null;
        if (this.state.exportBuffer !== "") {
            exportBufferItem = <LogViewer
                data={this.state.exportBuffer}
                isTextWrapped={false}
                hasLineNumbers={false}
                scrollToRow={this.state.exportBuffer.length}
                height="200px"
            />;
        }
        let importBufferItem = null;
        if (this.state.importBuffer !== "") {
            importBufferItem = <LogViewer
                data={this.state.importBuffer}
                isTextWrapped={false}
                hasLineNumbers={false}
                scrollToRow={this.state.importBuffer.length}
                height="200px"
            />;
        }
        let backupBufferItem = null;
        if (this.state.backupBuffer !== "") {
            backupBufferItem = <LogViewer
                data={this.state.backupBuffer}
                isTextWrapped={false}
                hasLineNumbers={false}
                scrollToRow={this.state.backupBuffer.length}
                height="200px"
            />;
        }

        return (
            <div>
                <TextContent>
                    <Text className="ds-config-header" component={TextVariants.h2}>
                        {_("Database Backups & LDIFs")}
                    </Text>
                </TextContent>
                <Tabs className="ds-margin-top-lg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>{_("Backups ")}<font size="2">({this.props.backups.length})</font></TabTitleText>}>
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
                                    onClick={this.handleShowBackupModal}
                                    className="ds-margin-top"
                                >
                                    {_("Create Backup")}
                                </Button>
                                <Button
                                    variant="secondary"
                                    onClick={() => {
                                        this.props.handleReload(1);
                                    }}
                                    className="ds-left-margin ds-margin-top"
                                    isLoading={this.props.refreshing}
                                    spinnerAriaValueText={this.props.refreshing ? _("Refreshing") : undefined}
                                    {...extraPrimaryProps}
                                >
                                    {refreshBtnName}
                                </Button>
                            </div>
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>{_("LDIFs ")}<font size="2">({this.props.ldifs.length})</font></TabTitleText>}>
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
                                    onClick={this.handleShowExportModal}
                                    className="ds-margin-top"
                                >
                                    {_("Create LDIF")}
                                </Button>
                                <Button
                                    variant="secondary"
                                    onClick={this.props.handleReload}
                                    className="ds-left-margin ds-margin-top"
                                    isLoading={this.props.refreshing}
                                    spinnerAriaValueText={this.props.refreshing ? _("Refreshing") : undefined}
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
                    handleChange={this.onChange}
                    saveHandler={this.validateLDIF}
                    spinning={this.state.exportSpinner}
                    error={this.state.errObj}
                    suffixes={this.props.suffixes}
                    includeReplData={this.state.includeReplData}
                    ldifSuffix={this.state.ldifSuffix}
                    ldifName={this.state.ldifName}
                    watchBuffer={exportBufferItem}
                    exportCompleted={this.state.exportCompleted}
                />
                <BackupModal
                    showModal={this.state.showBackupModal}
                    closeHandler={this.closeBackupModal}
                    handleChange={this.onChange}
                    saveHandler={this.validateBackup}
                    spinning={this.state.backupSpinning}
                    watchBuffer={backupBufferItem}
                    error={this.state.errObj}
                    backupCompleted={this.state.backupCompleted}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmLDIFDelete}
                    closeHandler={this.closeConfirmLDIFDelete}
                    handleChange={this.onConfirmChange}
                    actionHandler={this.deleteLDIF}
                    spinning={this.state.modalSpinning}
                    item={this.state.ldifName}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete LDIF File")}
                    mMsg={_("Are you sure you want to delete this LDIF?")}
                    mSpinningMsg={_("Deleting LDIF ...")}
                    mBtnName={_("Delete LDIF")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmLDIFImport}
                    closeHandler={this.closeConfirmLDIFImport}
                    handleChange={this.onConfirmChange}
                    actionHandler={this.importLDIF}
                    spinning={this.state.modalSpinning}
                    item={importBufferItem ? importBufferItem : this.state.ldifName}
                    checked={this.state.modalChecked}
                    mTitle={_("Import LDIF File")}
                    mMsg={_("Are you sure you want to import this LDIF?")}
                    mSpinningMsg={_("Importing LDIF ...")}
                    mBtnName={this.state.importCompleted ? null : _("Import LDIF")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmRestore}
                    closeHandler={this.closeConfirmRestore}
                    handleChange={this.onConfirmChange}
                    actionHandler={this.restoreBackup}
                    spinning={this.state.modalSpinning}
                    item={restoreBufferItem ? restoreBufferItem : this.state.backupName}
                    checked={this.state.modalChecked}
                    mTitle={_("Restore Database")}
                    mMsg={_("Are you sure you want to restore from this backup?")}
                    mSpinningMsg={_("Restoring ...")}
                    mBtnName={this.state.restoreCompleted ? null : _("Restore")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmBackupDelete}
                    closeHandler={this.closeConfirmBackupDelete}
                    handleChange={this.onConfirmChange}
                    actionHandler={this.deleteBackup}
                    spinning={this.state.modalSpinning}
                    item={this.state.backupName}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete Backup")}
                    mMsg={_("Are you sure you want to delete this backup?")}
                    mSpinningMsg={_("Deleting backup ...")}
                    mBtnName={_("Delete Backup")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmRestoreReplace}
                    closeHandler={this.closeConfirmRestoreReplace}
                    handleChange={this.onConfirmChange}
                    actionHandler={this.doBackup}
                    spinning={this.state.modalSpinning}
                    item={this.state.backupName}
                    checked={this.state.modalChecked}
                    mTitle={_("Replace Existing Backup")}
                    mMsg={_("A backup already eixsts with the same name, do you want to replace it?")}
                    mSpinningMsg={_("Replacing backup ...")}
                    mBtnName={_("Replace Backup")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmLDIFReplace}
                    closeHandler={this.closeConfirmLDIFReplace}
                    handleChange={this.onConfirmChange}
                    actionHandler={this.doExport}
                    spinning={this.state.modalSpinning}
                    item={exportBufferItem ? exportBufferItem : this.state.ldifName}
                    checked={this.state.modalChecked}
                    mTitle={_("Replace Existing LDIF File")}
                    mMsg={_("A LDIF file already exists with the same name, do you want to replace it?")}
                    mSpinningMsg={_("Replacing LDIF ...")}
                    mBtnName={_("Replace LDIF")}
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
            ldifSuffix,
            ldifName,
            includeReplData,
            watchBuffer,
            exportCompleted,
        } = this.props;
        let createBtnName = _("Create LDIF");
        const extraPrimaryProps = {};
        let exportMsg = "";
        if (spinning) {
            createBtnName = _("Creating ...");
            extraPrimaryProps.spinnerAriaValueText = _("Creating");
        }
        if (spinning) {
            exportMsg = (
                <div className="ds-margin-top">
                    <font size="2">{_("(You can safely close this window)")}</font>
                </div>
            );
        }

        const suffixList = suffixes.map((suffix) => (
            <FormSelectOption key={suffix} value={suffix} label={suffix} />
        ));

        const actions = [];
        if (!exportCompleted) {
            actions.push(
                <Button
                    key="confirm"
                    variant="primary"
                    onClick={saveHandler}
                    isLoading={spinning}
                    isDisabled={spinning || ldifName === ""}
                    {...extraPrimaryProps}
                >
                    {createBtnName}
                </Button>
            );
        }
        actions.push(
            <Button key="close" variant="link" onClick={closeHandler}>
                {_("Close")}
            </Button>
        );
        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Export Database To LDIF File")}
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={actions}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            {_("Select Suffix")}
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect id="ldifSuffix" value={ldifSuffix} onChange={(event, value) => { handleChange(event) }} aria-label="FormSelect Input">
                                {suffixList}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid
                        title={_("Name of exported LDIF file, if left blank the data and time will be used as the file name")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("LDIF File Name")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="ldifName"
                                aria-describedby="horizontal-form-name-helper"
                                name="ldifName"
                                value={ldifName}
                                onChange={(event, value) => { handleChange(event) }}
                                validated={error.ldifName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title={_("Name of exported LDIF file, if left blank the data and time will be used as the file name")}
                    >
                        <GridItem span={12}>
                            <Checkbox
                                id="includeReplData"
                                className="ds-indent ds-margin-top"
                                isChecked={includeReplData}
                                onChange={(e, checked) => {
                                    handleChange(e);
                                }}
                                title={_("Include the replication metadata needed to restore or initialize another replica.")}
                                label={_("Include Replication Data")}
                            />
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-lg">
                            {watchBuffer}
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
            watchBuffer,
            error,
            backupCompleted,
        } = this.props;
        let createBtnName = _("Create Backup");
        const extraPrimaryProps = {};
        let exportMsg = "";
        if (spinning) {
            createBtnName = _("Backing up ...");
            extraPrimaryProps.spinnerAriaValueText = _("Backing up");
        }
        if (spinning) {
            exportMsg = (
                <div className="ds-margin-top">
                    <font size="2">{_("(You can safely close this window)")}</font>
                </div>
            );
        }

        const actions = [];
        if (!backupCompleted) {
            actions.push(
                <Button
                    key="confirm"
                    variant="primary"
                    onClick={saveHandler}
                    isLoading={spinning}
                    isDisabled={spinning}
                    {...extraPrimaryProps}
                >
                    {createBtnName}
                </Button>
            );
        }
        actions.push(
            <Button key="close" variant="link" onClick={closeHandler}>
                {_("Close")}
            </Button>
        );
        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Backup The Server")}
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={actions}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid
                        title={_("Backup name, if left blank the date and time will be used as the name")}
                        className="ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("Backup Name")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="backupName"
                                aria-describedby="horizontal-form-name-helper"
                                name="backupName"
                                onChange={(event, value) => { handleChange(event) }}
                                validated={error.backupName ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                        <GridItem span={12} className="ds-margin-top-lg">
                            {watchBuffer}
                        </GridItem>
                    </Grid>
                    {exportMsg}
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
    suffixes: PropTypes.array,
    includeReplData: PropTypes.bool,
    ldifSuffix: PropTypes.string,
    ldifName: PropTypes.string,
    watchBuffer: PropTypes.node,
    exportCompleted: PropTypes.bool,
};

BackupModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
    watchBuffer: PropTypes.node,
    backupCompleted: PropTypes.bool,
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
