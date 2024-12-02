import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { AttrEncryption } from "./attrEncryption.jsx";
import { SuffixConfig } from "./suffixConfig.jsx";
import { SuffixReferrals } from "./referrals.jsx";
import { SuffixIndexes } from "./indexes.jsx";
import { VLVIndexes } from "./vlvIndexes.jsx";
import { log_cmd, bad_file_name } from "../tools.jsx";
import {
    ImportModal,
    ExportModal,
    CreateSubSuffixModal,
    CreateLinkModal,
} from "./databaseModal.jsx";
import {
    Button,
	Grid,
	GridItem,
	Tab,
	Tabs,
	TabTitleText
} from '@patternfly/react-core';
import {
    FolderIcon,
    LeafIcon,
    SyncAltIcon
} from '@patternfly/react-icons';
import {
	Dropdown,
	DropdownToggle,
	DropdownItem,
	DropdownPosition,
	DropdownSeparator
} from '@patternfly/react-core/deprecated';

import PropTypes from "prop-types";

const _ = cockpit.gettext;

export class Suffix extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            loading: false,
            activeTabKey: 0,
            notifications: [],
            errObj: {},
            refRows: this.props.data.refRows,
            encAttrsRows: this.props.data.encAttrsRows,
            vlvItems: this.props.data.vlvItems,
            autoTuning: this.props.data.autoTuning,
            dropdownIsOpen: false,
            // Suffix configuration
            cachememsize: this.props.data.cachememsize,
            cachesize: this.props.data.cachesize,
            dncachememsize: this.props.data.dncachememsize,
            readOnly: this.props.data.readOnly,
            requireIndex: this.props.data.requireIndex,
            dbstate: this.props.data.dbstate,
            _cachememsize: this.props.data.cachememsize,
            _cachesize: this.props.data.cachesize,
            _dncachememsize: this.props.data.dncachememsize,
            _readOnly: this.props.data.readOnly,
            _requireIndex: this.props.data.requireIndex,
            _dbstate: this.props.data.dbstate,
            savingConfig: false,
            saveBtnDisabled: true,

            // Import/Export modals
            showImportModal: false,
            showExportModal: false,
            ldifLocation: "",
            attrEncryption: false,
            exportSpinner: false,
            showConfirmLDIFImport: false,
            importLDIFName: "",
            deleleLDIFName: "",
            modalChecked: false,
            modalSpinning: false,
            includeReplData: false,
            // Reindex all
            showReindexConfirm: false,
            // Create Sub Suffix
            showSubSuffixModal: false,
            subSuffixValue: "",
            subSuffixBeName: "",
            createSuffixEntry: false,
            noSuffixInit: true,
            createSampleEntries: false,
            saveSubSuffixBtnDisabled: true,
            subSuffixSaving: false,
            initOption: "noInit",

            // Create Link
            showLinkModal: false,
            createLinkSuffix: "",
            createLinkName: "",
            createNsfarmserverurl: "",
            createNsmultiplexorbinddn: "",
            createNsmultiplexorcredentials: "",
            createNsmultiplexorcredentialsConfirm: "",
            createUseStartTLS: false,
            createNsbindmechanism: "SIMPLE",
            linkSaving: false,
            linkSaveBtnDisabled: true,
            // Delete
            showDeleteConfirm: false,
        };

        // config.autoAddCss = false;

        // Dropdown tasks
        this.handleToggle = (_event, dropdownIsOpen) => {
            this.setState({
                dropdownIsOpen
            });
        };
        this.handleSelect = event => {
            this.setState({
                dropdownIsOpen: !this.state.dropdownIsOpen
            });
            this.onFocus();
        };
        this.onFocus = () => {
            const element = document.getElementById('suffix-dropdown');
            element.focus();
        };
        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        // General bindings
        // Import modal
        this.handleShowImportModal = this.handleShowImportModal.bind(this);
        this.closeImportModal = this.closeImportModal.bind(this);
        this.onChange = this.onChange.bind(this);
        this.doImport = this.doImport.bind(this);
        this.importLDIF = this.importLDIF.bind(this);
        this.showConfirmLDIFImport = this.showConfirmLDIFImport.bind(this);
        this.closeConfirmLDIFImport = this.closeConfirmLDIFImport.bind(this);
        // Export modal
        this.handleShowExportModal = this.handleShowExportModal.bind(this);
        this.closeExportModal = this.closeExportModal.bind(this);
        this.doExport = this.doExport.bind(this);
        this.onExportChange = this.onExportChange.bind(this);
        // Reindex Suffix Modal
        this.handleShowReindexConfirm = this.handleShowReindexConfirm.bind(this);
        this.closeReindexConfirm = this.closeReindexConfirm.bind(this);
        this.doReindex = this.doReindex.bind(this);
        // Create sub suffix modal
        this.handleShowSubSuffixModal = this.handleShowSubSuffixModal.bind(this);
        this.closeSubSuffixModal = this.closeSubSuffixModal.bind(this);
        this.createSubSuffix = this.createSubSuffix.bind(this);
        this.onSubSuffixOnSelect = this.onSubSuffixOnSelect.bind(this);
        this.onSubSuffixChange = this.onSubSuffixChange.bind(this);
        // Create link modal
        this.handleShowLinkModal = this.handleShowLinkModal.bind(this);
        this.closeLinkModal = this.closeLinkModal.bind(this);
        this.createLink = this.createLink.bind(this);
        this.onLinkChange = this.onLinkChange.bind(this);
        this.onLinkOnSelect = this.onLinkOnSelect.bind(this);
        // Suffix config
        this.saveSuffixConfig = this.saveSuffixConfig.bind(this);
        this.handleShowDeleteConfirm = this.handleShowDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
        this.onConfigChange = this.onConfigChange.bind(this);
        this.doDelete = this.doDelete.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    //
    // Import Modal
    //
    handleShowImportModal() {
        this.setState({
            ldifLocation: "",
            attrEncryption: false,
            showImportModal: true,
            errObj: {},
        });
    }

    closeImportModal() {
        this.setState({
            showImportModal: false
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

    onConfigChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let saveBtnDisabled = true;

        const configAttrs = [
            'cachememsize', 'cachesize', 'dncachememsize',
            'readOnly', 'requireIndex', 'dbstate'
        ];
        for (const check_attr of configAttrs) {
            if (attr !== check_attr) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    saveBtnDisabled = false;
                }
            } else if (value !== this.state['_' + check_attr]) {
                saveBtnDisabled = false;
            }
        }

        this.setState({
            [attr]: value,
            saveBtnDisabled
        });
    }

    showConfirmLDIFImport (name) {
        // call deleteLDIF
        this.setState({
            showConfirmLDIFImport: true,
            importLDIFName: name,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmLDIFImport () {
        // call importLDIF
        this.setState({
            showConfirmLDIFImport: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    importLDIF () {
        // Do import
        const import_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "import", this.props.suffix, this.state.importLDIFName, "--encrypted"
        ];

        this.setState({
            modalSpinning: true,
        });

        log_cmd("doImport", "Do online import", import_cmd);
        cockpit
                .spawn(import_cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        _("Import successfully initiated")
                    );
                    this.setState({
                        modalSpinning: false,
                        showConfirmLDIFImport: false,
                        showImportModal: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error importing LDIF file - $0"), errMsg.desc)
                    );
                    this.setState({
                        modalSpinning: false,
                        showConfirmLDIFImport: false
                    });
                });
    }

    doImport() {
        // Validate form before proceeding
        if (this.state.ldifLocation !== "") {
            this.setState({
                showConfirmLDIFImport: true,
                importLDIFName: this.state.ldifLocation,
            });
        }
    }

    //
    // Export modal
    //
    handleShowExportModal() {
        this.setState({
            ldifLocation: "",
            attrEncryption: false,
            showExportModal: true,
            exportSpinner: false,
            includeReplData: false,
            errObj: {},
        });
    }

    closeExportModal() {
        this.setState({
            showExportModal: false,
            exportSpinner: false
        });
    }

    doExport() {
        const missingArgs = { ldifLocation: false };
        if (this.state.ldifLocation === "") {
            this.props.addNotification(
                "warning",
                _("LDIF name is empty")
            );
            missingArgs.ldifLocation = true;
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        // Must not be a path
        if (bad_file_name(this.state.ldifLocation)) {
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

        // Do Export
        const export_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "export", this.props.suffix, "--ldif=" + this.state.ldifLocation
        ];

        if (this.state.attrEncryption) {
            export_cmd.push("--encrypted");
        }

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
                    this.props.reloadLDIFs();
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
                                    cockpit.format(_("Database export complete. You can find the LDIF file in $0 directory on the server machine."), attrs['nsslapd-ldifdir'][0])
                                );
                            })
                            .fail(err => {
                                const errMsg = JSON.parse(err);
                                this.props.addNotification(
                                    "success",
                                    _("Database export complete.")
                                );
                                this.props.addNotification(
                                    "error",
                                    cockpit.format(_("Error while trying to get the server's LDIF directory- $0"), errMsg.desc)
                                );
                            });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reloadLDIFs();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error exporting database - $0"), errMsg.desc)
                    );
                    this.setState({
                        showExportModal: false,
                    });
                });
    }

    //
    // Reindex entire database
    //
    handleShowReindexConfirm() {
        this.setState({
            showReindexConfirm: true,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeReindexConfirm() {
        this.setState({
            showReindexConfirm: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    doReindex() {
        // Show index status modal
        this.setState({
            modalSpinning: true
        });
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "reindex", "--wait", this.props.suffix];
        log_cmd("doReindex", "Reindex all attributes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        _("Database has successfully been reindexed")
                    );
                    this.setState({
                        modalSpinning: false,
                        showReindexConfirm: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to reindex database - $0"), errMsg.desc)
                    );
                    this.setState({
                        modalSpinning: false,
                        showReindexConfirm: false,
                    });
                });
    }

    //
    // Create sub suffix
    //
    handleShowSubSuffixModal() {
        this.setState({
            showSubSuffixModal: true,
            errObj: {},
        });
    }

    closeSubSuffixModal() {
        this.setState({
            showSubSuffixModal: false
        });
    }

    createSubSuffix() {
        // Create a new suffix
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "create", "--be-name", this.state.subSuffixBeName,
            "--suffix=" + this.state.subSuffixValue + "," + this.props.suffix,
            "--parent-suffix=" + this.props.suffix
        ];

        if (this.state.createSampleEntries) {
            cmd.push('--create-entries');
        }
        if (this.state.createSuffixEntry) {
            cmd.push('--create-suffix');
        }

        this.setState({
            subSuffixSaving: true
        });

        log_cmd("createSubSuffix", "Create a sub suffix", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.loadSuffixTree(false);
                    this.closeSubSuffixModal();
                    this.props.addNotification(
                        "success",
                        _("Successfully created new sub-suffix")
                    );
                    this.setState({
                        subSuffixSaving: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.loadSuffixTree(false);
                    this.closeSubSuffixModal();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error creating sub-suffix - $0"), errMsg.desc)
                    );
                    this.setState({
                        subSuffixSaving: false
                    });
                });
    }

    //
    // Create Chaining Link
    //
    handleShowLinkModal() {
        this.setState({
            showLinkModal: true,
            errObj: {},
        });
    }

    closeLinkModal() {
        this.setState({
            showLinkModal: false
        });
    }

    createLink() {
        // Add chaining link
        this.setState({
            linkSaving: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "link-create",
            "--suffix=" + this.state.createLinkSuffix + "," + this.props.suffix,
            "--server-url=" + this.state.createNsfarmserverurl,
            "--bind-mech=" + this.state.createNsbindmechanism,
            "--bind-dn=" + this.state.createNsmultiplexorbinddn,
            "--bind-pw=" + this.state.createNsmultiplexorcredentials,
            this.state.createLinkName
        ];
        if (this.state.createUseStartTLS) {
            cmd.push("--use-starttls=on");
        }
        log_cmd("createLink", "Create database link", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.loadSuffixTree(false);
                    this.closeLinkModal();
                    this.props.addNotification(
                        "success",
                        _("Successfully created database link")
                    );
                    this.setState({
                        linkSaving: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.loadSuffixTree(false);
                    this.closeLinkModal();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error creating database link - $0"), errMsg.desc)
                    );
                    this.setState({
                        linkSaving: false
                    });
                });
    }

    onLinkOnSelect(value, event) {
        this.setState({
            createNsbindmechanism: value,
        });
    }

    onLinkChange(e) {
        // Check for matching credentials
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let valueErr = false;
        let saveBtnDisabled = false;
        const errObj = this.state.errObj;

        const check_attrs = [
            "createLinkSuffix", "createLinkName", "createNsfarmserverurl",
            "createNsmultiplexorbinddn", "createNsmultiplexorcredentials",
            "createNsmultiplexorcredentialsConfirm", "createNsbindmechanism"
        ];
        for (const check_attr of check_attrs) {
            if (attr !== check_attr && this.state[check_attr] === "") {
                saveBtnDisabled = true;
            }
        }

        // Handle password validation
        if (attr !== "createNsmultiplexorcredentials" && attr !== "createNsmultiplexorcredentialsConfirm") {
            if (this.state.createNsmultiplexorcredentials !== this.state.createNsmultiplexorcredentialsConfirm) {
                saveBtnDisabled = true;
            }
        } else {
            if (attr === "createNsmultiplexorcredentials") {
                if (value !== this.state.createNsmultiplexorcredentialsConfirm) {
                    errObj.createNsmultiplexorcredentials = true;
                    errObj.createNsmultiplexorcredentialsConfirm = true;
                    saveBtnDisabled = true;
                } else {
                    errObj.createNsmultiplexorcredentials = false;
                    errObj.createNsmultiplexorcredentialsConfirm = false;
                }
            } else if (attr === "createNsmultiplexorcredentialsConfirm") {
                if (value !== this.state.createNsmultiplexorcredentials) {
                    errObj.createNsmultiplexorcredentials = true;
                    errObj.createNsmultiplexorcredentialsConfirm = true;
                    saveBtnDisabled = true;
                } else {
                    errObj.createNsmultiplexorcredentials = false;
                    errObj.createNsmultiplexorcredentialsConfirm = false;
                }
            }
        }

        if (value === "") {
            valueErr = true;
            saveBtnDisabled = true;
        }
        errObj[attr] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj,
            linkSaveBtnDisabled: saveBtnDisabled
        });
    }

    onSubSuffixOnSelect(value, event) {
        let noInit = false;
        let addSuffix = false;
        let addSample = false;

        if (value === "noInit") {
            noInit = true;
        } else if (value === "addSuffix") {
            addSuffix = true;
        } else { // addSample
            addSample = true;
        }
        this.setState({
            initOption: value,
            noSuffixInit: noInit,
            createSuffixEntry: addSuffix,
            createSampleEntries: addSample
        });
    }

    onSubSuffixChange(e) {
        const value = e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        let saveBtnDisabled = false;
        const check_attrs = ["subSuffixBeName", "subSuffixValue"];
        for (const check_attr of check_attrs) {
            if (this.state[check_attr] === "") {
                saveBtnDisabled = true;
            }
        }
        if (value === "") {
            valueErr = true;
            saveBtnDisabled = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj,
            saveSubSuffixBtnDisabled: saveBtnDisabled
        });
    }

    onExportChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        let saveBtnDisabled = false;

        if (e.target.id !== "ldifLocation" && this.state.ldifLocation === 0) {
            saveBtnDisabled = true;
        }
        if (value === "") {
            valueErr = true;
            saveBtnDisabled = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj,
            saveExportBtnDisabled: saveBtnDisabled
        });
    }

    //
    // Delete suffix
    //
    handleShowDeleteConfirm(item) {
        this.setState({
            showDeleteConfirm: true,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeDeleteConfirm() {
        this.setState({
            showDeleteConfirm: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    doDelete() {
        // Delete suffix
        this.setState({
            modalSpinning: true
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "delete", this.props.suffix, "--do-it"
        ];
        log_cmd("doDelete", "Delete database", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.loadSuffixTree(true);
                    this.closeDeleteConfirm();
                    this.props.addNotification(
                        "success",
                        _("Successfully deleted database")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.loadSuffixTree(true);
                    this.closeDeleteConfirm();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error deleting database - $0"), errMsg.desc)
                    );
                });
    }

    // Save config
    saveSuffixConfig() {
        console.log("Save suffix config: ", this.props.suffix);
        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'backend', 'suffix', 'set', this.props.suffix
        ];
        let requireRestart = false;
        if (this.state._readOnly !== this.state.readOnly) {
            if (this.state.readOnly) {
                cmd.push("--enable-readonly");
            } else {
                cmd.push("--disable-readonly");
            }
        }
        if (this.state._requireIndex !== this.state.requireIndex) {
            if (this.state.requireIndex) {
                cmd.push("--require-index");
            } else {
                cmd.push("--ignore-index");
            }
        }
        if (this.state._cachememsize !== this.state.cachememsize) {
            cmd.push("--cache-memsize=" + this.state.cachememsize);
            requireRestart = true;
        }
        if (this.state._cachesize !== this.state.cachesize) {
            cmd.push("--cache-size=" + this.state.cachesize);
            requireRestart = true;
        }
        if (this.state._dncachememsize !== this.state.dncachememsize) {
            cmd.push("--dncache-memsize=" + this.state.dncachememsize);
            requireRestart = true;
        }
        if (this.state._dbstate !== this.state.dbstate) {
            cmd.push("--state=" + this.state.dbstate);
            requireRestart = true;
        }
        if (cmd.length > 7) {
            this.setState({
                savingConfig: true
            });
            log_cmd("saveSuffixConfig", "Save suffix config", cmd);
            const msg = "Successfully updated suffix configuration";
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        // Continue with the next mod
                        this.props.reload(this.props.suffix);
                        if (requireRestart) {
                            this.props.addNotification(
                                "warning",
                                msg + _("You must restart the Directory Server for these changes to take effect.")
                            );
                        }
                        this.setState({
                            savingConfig: false
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.reload(this.props.suffix);
                        let msg = errMsg.desc;
                        if ('info' in errMsg) {
                            msg = errMsg.desc + " - " + errMsg.info;
                        }
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error updating suffix configuration - $0"), msg)
                        );
                        this.setState({
                            savingConfig: false
                        });
                    });
        }
    }

    //
    // Render the component
    //
    render () {
        let SuffixIcon = FolderIcon;
        if (this.props.dbtype === "subsuffix") {
            SuffixIcon = LeafIcon;
        }
        const { dropdownIsOpen, activeTabKey } = this.state;

        const dropdownItems = [
            <DropdownItem key="import" component="button" onClick={this.handleShowImportModal} title={_("Import an LDIF file to initialize the database")}>
                {_("Initialize Suffix")}
            </DropdownItem>,
            <DropdownItem key="export" component="button" onClick={this.handleShowExportModal} title={_("Export the database to an LDIF file")}>
                {_("Export Suffix")}
            </DropdownItem>,
            <DropdownItem key="reindex" component="button" onClick={this.handleShowReindexConfirm} title={_("Reindex the entire database")}>
                {_("Reindex Suffix")}
            </DropdownItem>,
            <DropdownItem key="subSuffix" component="button" onClick={this.handleShowSubSuffixModal} title={_("Create a sub-suffix under this suffix")}>
                {_("Create Sub-Suffix")}
            </DropdownItem>,
            <DropdownItem key="dbLink" component="button" onClick={this.handleShowLinkModal} title={_("Create a database chaining link subtree")}>
                {_("Create Database Link")}
            </DropdownItem>,
            <DropdownSeparator key="separator" />,
            <DropdownItem key="deleteSuffix" component="button" onClick={this.handleShowDeleteConfirm} title={_("This will permanently delete the database")}>
                {_("Delete Suffix")}
            </DropdownItem>,
        ];

        return (
            <div id="suffix-page">
                <Grid>
                    <GridItem className="ds-suffix-header" span={9}>
                        <SuffixIcon />
                        &nbsp;&nbsp;{this.props.suffix} (<i>{this.props.bename}</i>)
                        <Button 
                            variant="plain"
                            aria-label={_("Refresh suffix")}
                            onClick={() => this.props.reload(this.props.suffix)}
                        >
                            <SyncAltIcon />
                        </Button>
                    </GridItem>
                    <GridItem span={3}>
                        <Dropdown
                            className="ds-float-right"
                            position={DropdownPosition.right}
                            onSelect={this.handleSelect}
                            toggle={
                                <DropdownToggle id="suffix-dropdown" onToggle={(event, isOpen) => this.handleToggle(event, isOpen)}>
                                    {_("Suffix Tasks")}
                                </DropdownToggle>
                            }
                            isOpen={dropdownIsOpen}
                            dropdownItems={dropdownItems}
                        />
                    </GridItem>
                </Grid>

                <div className="ds-sub-header">
                    <Tabs isFilled activeKey={activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={0} title={<TabTitleText>{_("Settings")}</TabTitleText>}>
                            <SuffixConfig
                                cachememsize={this.state.cachememsize}
                                cachesize={this.state.cachesize}
                                dncachememsize={this.state.dncachememsize}
                                dbstate={this.state.dbstate}
                                readOnly={this.state.readOnly}
                                requireIndex={this.state.requireIndex}
                                autoTuning={this.state.autoTuning}
                                handleChange={this.onConfigChange}
                                handleSave={this.saveSuffixConfig}
                                saving={this.state.savingConfig}
                                saveBtnDisabled={this.state.saveBtnDisabled}
                            />
                        </Tab>
                        <Tab eventKey={1} title={<TabTitleText>{_("Referrals")}</TabTitleText>}>
                            <SuffixReferrals
                                rows={this.props.data.refRows}
                                suffix={this.props.suffix}
                                reload={this.props.reloadRefs}
                                addNotification={this.props.addNotification}
                                serverId={this.props.serverId}
                                key={this.state.refRows}
                            />
                        </Tab>
                        <Tab eventKey={2} title={<TabTitleText>{_("Indexes")}</TabTitleText>}>
                            <SuffixIndexes
                                systemIndexRows={this.props.data.systemIndexRows}
                                indexRows={this.props.data.indexRows}
                                suffix={this.props.suffix}
                                serverId={this.props.serverId}
                                addNotification={this.props.addNotification}
                                reload={this.props.reloadIndexes}
                            />
                        </Tab>
                        <Tab eventKey={3} title={<TabTitleText>{_("VLV Indexes")}</TabTitleText>}>
                            <VLVIndexes
                                suffix={this.props.suffix}
                                serverId={this.props.serverId}
                                vlvItems={this.props.data.vlvItems}
                                addNotification={this.props.addNotification}
                                attrs={this.props.attrs}
                                reload={this.props.reloadVLV}
                                key={this.props.vlvTableKey}
                            />
                        </Tab>
                        <Tab eventKey={4} title={<TabTitleText>{_("Encrypted Attributes")}</TabTitleText>}>
                            <AttrEncryption
                                rows={this.props.data.encAttrsRows}
                                suffix={this.props.suffix}
                                serverId={this.props.serverId}
                                addNotification={this.props.addNotification}
                                attrs={this.props.attrs}
                                reload={this.props.reloadAttrEnc}
                            />
                        </Tab>
                    </Tabs>
                </div>
                <DoubleConfirmModal
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    handleChange={this.onChange}
                    actionHandler={this.doDelete}
                    spinning={this.state.modalSpinning}
                    item={this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete Suffix")}
                    mMsg={_("Are you really sure you want to delete this suffix?")}
                    mSpinningMsg={_("Deleting suffix ...")}
                    mBtnName={_("Delete Suffix")}
                />
                <CreateLinkModal
                    showModal={this.state.showLinkModal}
                    closeHandler={this.closeLinkModal}
                    handleChange={this.onLinkChange}
                    handleSelectChange={this.onLinkOnSelect}
                    saveHandler={this.createLink}
                    suffix={this.props.suffix}
                    starttls_checked={this.state.createUseStartTLS}
                    error={this.state.errObj}
                    saving={this.state.linkSaving}
                    saveBtnDisabled={this.state.linkSaveBtnDisabled}
                    bindMech={this.state.createNsbindmechanism}
                />
                <CreateSubSuffixModal
                    showModal={this.state.showSubSuffixModal}
                    closeHandler={this.closeSubSuffixModal}
                    handleChange={this.onSubSuffixChange}
                    handleSelectChange={this.onSubSuffixOnSelect}
                    saveHandler={this.createSubSuffix}
                    suffix={this.props.suffix}
                    error={this.state.errObj}
                    saving={this.state.subSuffixSaving}
                    saveBtnDisabled={this.state.saveSubSuffixBtnDisabled}
                    initOption={this.state.initOption}
                />
                <ImportModal
                    showModal={this.state.showImportModal}
                    closeHandler={this.closeImportModal}
                    handleChange={this.onChange}
                    saveHandler={this.doImport}
                    showConfirmImport={this.showConfirmLDIFImport}
                    rows={this.props.LDIFRows}
                    suffix={this.props.suffix}
                    saveBtnDisabled={this.state.ldifLocation === ""}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmLDIFImport}
                    closeHandler={this.closeConfirmLDIFImport}
                    handleChange={this.onChange}
                    actionHandler={this.importLDIF}
                    spinning={this.state.modalSpinning}
                    item={this.state.importLDIFName}
                    checked={this.state.modalChecked}
                    mTitle={_("Initialize Database From LDIF")}
                    mMsg={_("Are you sure you want to initialize the database (it will permanently overwrite the current database)?")}
                    mSpinningMsg={_("Initializing Database ...")}
                    mBtnName={_("Initialize Database")}
                />
                <ExportModal
                    showModal={this.state.showExportModal}
                    closeHandler={this.closeExportModal}
                    handleChange={this.onExportChange}
                    saveHandler={this.doExport}
                    spinning={this.state.exportSpinner}
                    error={this.state.errObj}
                    includeReplData={this.state.includeReplData}
                    saveBtnDisabled={this.state.ldifLocation === ""}
                />
                <DoubleConfirmModal
                    showModal={this.state.showReindexConfirm}
                    closeHandler={this.closeReindexConfirm}
                    handleChange={this.onChange}
                    actionHandler={this.doReindex}
                    spinning={this.state.modalSpinning}
                    item={this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle={_("Reindex All Attributes")}
                    mMsg={_("Are you sure you want to reindex all the attribute indexes?")}
                    mSpinningMsg={_("Reindexing Database ...")}
                    mBtnName={_("Reindex")}
                />
            </div>
        );
    }
}

// Property types and defaults

Suffix.propTypes = {
    serverId: PropTypes.string,
    suffix: PropTypes.string,
    bename: PropTypes.string,
    loadSuffixTree: PropTypes.func,
    reload: PropTypes.func,
    reloadRefs: PropTypes.func,
    reloadIndexes: PropTypes.func,
    reloadVLV: PropTypes.func,
    reloadAttrEnc: PropTypes.func,
    reloadLDIFs: PropTypes.func,
    addNotification: PropTypes.func,
    dbtype: PropTypes.string,
    data: PropTypes.object,
    attrs: PropTypes.array,
    LDIFRows: PropTypes.array,
    enableTree: PropTypes.func,
};

Suffix.defaultProps = {
    serverId: "",
    suffix: "",
    bename: "",
    dbtype: "",
    data: {},
    attrs: [],
    LDIFRows: [],
};
