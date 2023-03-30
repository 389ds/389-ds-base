import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { AttrEncryption } from "./attrEncryption.jsx";
import { SuffixConfig } from "./suffixConfig.jsx";
import { SuffixReferrals } from "./referrals.jsx";
import { SuffixIndexes } from "./indexes.jsx";
import { VLVIndexes } from "./vlvIndexes.jsx";
import { log_cmd, bad_file_name } from "../tools.jsx";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faLeaf,
    faTree,
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';
import {
    ImportModal,
    ExportModal,
    CreateSubSuffixModal,
    CreateLinkModal,
} from "./databaseModal.jsx";
import {
    Dropdown,
    DropdownToggle,
    DropdownItem,
    DropdownPosition,
    DropdownSeparator,
    Grid,
    GridItem,
    Tab,
    Tabs,
    TabTitleText,
} from "@patternfly/react-core";

import PropTypes from "prop-types";

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
        this.onToggle = dropdownIsOpen => {
            this.setState({
                dropdownIsOpen
            });
        };
        this.onSelect = event => {
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
        this.showImportModal = this.showImportModal.bind(this);
        this.closeImportModal = this.closeImportModal.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.doImport = this.doImport.bind(this);
        this.importLDIF = this.importLDIF.bind(this);
        this.showConfirmLDIFImport = this.showConfirmLDIFImport.bind(this);
        this.closeConfirmLDIFImport = this.closeConfirmLDIFImport.bind(this);
        // Export modal
        this.showExportModal = this.showExportModal.bind(this);
        this.closeExportModal = this.closeExportModal.bind(this);
        this.doExport = this.doExport.bind(this);
        this.handleExportChange = this.handleExportChange.bind(this);
        // Reindex Suffix Modal
        this.showReindexConfirm = this.showReindexConfirm.bind(this);
        this.closeReindexConfirm = this.closeReindexConfirm.bind(this);
        this.doReindex = this.doReindex.bind(this);
        // Create sub suffix modal
        this.showSubSuffixModal = this.showSubSuffixModal.bind(this);
        this.closeSubSuffixModal = this.closeSubSuffixModal.bind(this);
        this.createSubSuffix = this.createSubSuffix.bind(this);
        this.handleSubSuffixOnSelect = this.handleSubSuffixOnSelect.bind(this);
        this.handleSubSuffixChange = this.handleSubSuffixChange.bind(this);
        // Create link modal
        this.showLinkModal = this.showLinkModal.bind(this);
        this.closeLinkModal = this.closeLinkModal.bind(this);
        this.createLink = this.createLink.bind(this);
        this.handleLinkChange = this.handleLinkChange.bind(this);
        this.handleLinkOnSelect = this.handleLinkOnSelect.bind(this);
        // Suffix config
        this.saveSuffixConfig = this.saveSuffixConfig.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
        this.handleConfigChange = this.handleConfigChange.bind(this);
        this.doDelete = this.doDelete.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    //
    // Import Modal
    //
    showImportModal() {
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

    handleConfigChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let saveBtnDisabled = true;

        const configAttrs = [
            'cachememsize', 'cachesize', 'dncachememsize',
            'readOnly', 'requireIndex', 'dbstate'
        ];
        for (const check_attr of configAttrs) {
            if (attr != check_attr) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    saveBtnDisabled = false;
                }
            } else if (value != this.state['_' + check_attr]) {
                saveBtnDisabled = false;
            }
        }

        this.setState({
            [attr]: value,
            saveBtnDisabled: saveBtnDisabled
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
                        `Import successfully initiated`
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
                        `Error importing LDIF file - ${errMsg.desc}`
                    );
                    this.setState({
                        modalSpinning: false,
                        showConfirmLDIFImport: false
                    });
                });
    }

    doImport() {
        // Validate form before proceeding
        if (this.state.ldifLocation != "") {
            this.setState({
                showConfirmLDIFImport: true,
                importLDIFName: this.state.ldifLocation,
            });
        }
    }

    //
    // Export modal
    //
    showExportModal() {
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

        // Must not be a path
        if (bad_file_name(this.state.ldifLocation)) {
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
                    this.props.reloadLDIFs();
                    this.props.addNotification(
                        "error",
                        `Error exporting database - ${errMsg.desc}`
                    );
                    this.setState({
                        showExportModal: false,
                    });
                });
    }

    //
    // Reindex entire database
    //
    showReindexConfirm() {
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
                        `Database has successfully been reindexed`
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
                        `Failed to reindex database - ${errMsg.desc}`
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
    showSubSuffixModal() {
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
        let cmd = [
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
                        `Successfully created new sub-suffix`
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
                        `Error creating sub-suffix - ${errMsg.desc}`
                    );
                    this.setState({
                        subSuffixSaving: false
                    });
                });
    }

    //
    // Create Chaining Link
    //
    showLinkModal() {
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
        let cmd = [
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
                        `Successfully created database link`
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
                        `Error creating database link - ${errMsg.desc}`
                    );
                    this.setState({
                        linkSaving: false
                    });
                });
    }

    handleLinkOnSelect(value, event) {
        this.setState({
            createNsbindmechanism: value,
        });
    }

    handleLinkChange(e) {
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
            if (attr != check_attr && this.state[check_attr] == "") {
                saveBtnDisabled = true;
            }
        }

        // Handle password validation
        if (attr != "createNsmultiplexorcredentials" && attr != "createNsmultiplexorcredentialsConfirm") {
            if (this.state.createNsmultiplexorcredentials != this.state.createNsmultiplexorcredentialsConfirm) {
                saveBtnDisabled = true;
            }
        } else {
            if (attr == "createNsmultiplexorcredentials") {
                if (value != this.state.createNsmultiplexorcredentialsConfirm) {
                    errObj.createNsmultiplexorcredentials = true;
                    errObj.createNsmultiplexorcredentialsConfirm = true;
                    saveBtnDisabled = true;
                } else {
                    errObj.createNsmultiplexorcredentials = false;
                    errObj.createNsmultiplexorcredentialsConfirm = false;
                }
            } else if (attr == "createNsmultiplexorcredentialsConfirm") {
                if (value != this.state.createNsmultiplexorcredentials) {
                    errObj.createNsmultiplexorcredentials = true;
                    errObj.createNsmultiplexorcredentialsConfirm = true;
                    saveBtnDisabled = true;
                } else {
                    errObj.createNsmultiplexorcredentials = false;
                    errObj.createNsmultiplexorcredentialsConfirm = false;
                }
            }
        }

        if (value == "") {
            valueErr = true;
            saveBtnDisabled = true;
        }
        errObj[attr] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj,
            linkSaveBtnDisabled: saveBtnDisabled
        });
    }

    handleSubSuffixOnSelect(value, event) {
        let noInit = false;
        let addSuffix = false;
        let addSample = false;

        if (value == "noInit") {
            noInit = true;
        } else if (value == "addSuffix") {
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

    handleSubSuffixChange(e) {
        const value = e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        let saveBtnDisabled = false;
        const check_attrs = ["subSuffixBeName", "subSuffixValue"];
        for (const check_attr of check_attrs) {
            if (this.state[check_attr] == "") {
                saveBtnDisabled = true;
            }
        }
        if (value == "") {
            valueErr = true;
            saveBtnDisabled = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj,
            saveSubSuffixBtnDisabled: saveBtnDisabled
        });
    }

    handleExportChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        const errObj = this.state.errObj;
        let saveBtnDisabled = false;

        if (e.target.id != "ldifLocation" && this.state.ldifLocation == 0) {
            saveBtnDisabled = true;
        }
        if (value == "") {
            valueErr = true;
            saveBtnDisabled = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj,
            saveExportBtnDisabled: saveBtnDisabled
        });
    }

    //
    // Delete suffix
    //
    showDeleteConfirm(item) {
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
        })
        let cmd = [
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
                        `Successfully deleted database`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.loadSuffixTree(true);
                    this.closeDeleteConfirm();
                    this.props.addNotification(
                        "error",
                        `Error deleting database - ${errMsg.desc}`
                    );
                });
    }

    // Save config
    saveSuffixConfig() {
        console.log("Save suffix config: ", this.props.suffix);
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'backend', 'suffix', 'set', this.props.suffix
        ];
        let requireRestart = false;
        if (this.state._readOnly != this.state.readOnly) {
            if (this.state.readOnly) {
                cmd.push("--enable-readonly");
            } else {
                cmd.push("--disable-readonly");
            }
        }
        if (this.state._requireIndex != this.state.requireIndex) {
            if (this.state.requireIndex) {
                cmd.push("--require-index");
            } else {
                cmd.push("--ignore-index");
            }
        }
        if (this.state._cachememsize != this.state.cachememsize) {
            cmd.push("--cache-memsize=" + this.state.cachememsize);
            requireRestart = true;
        }
        if (this.state._cachesize != this.state.cachesize) {
            cmd.push("--cache-size=" + this.state.cachesize);
            requireRestart = true;
        }
        if (this.state._dncachememsize != this.state.dncachememsize) {
            cmd.push("--dncache-memsize=" + this.state.dncachememsize);
            requireRestart = true;
        }
        if (this.state._dbstate != this.state.dbstate) {
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
                                msg + "You must restart the Directory Server for these changes to take effect."
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
                            `Error updating suffix configuration - ${msg}`
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
        let suffixIcon = faTree;
        if (this.props.dbtype == "subsuffix") {
            suffixIcon = faLeaf;
        }
        const { dropdownIsOpen, activeTabKey } = this.state;

        const dropdownItems = [
            <DropdownItem key="import" component="button" onClick={this.showImportModal} title="Import an LDIF file to initialize the database">
                Initialize Suffix
            </DropdownItem>,
            <DropdownItem key="export" component="button" onClick={this.showExportModal} title="Export the database to an LDIF file">
                Export Suffix
            </DropdownItem>,
            <DropdownItem key="reindex" component="button" onClick={this.showReindexConfirm} title="Reindex the entire database">
                Reindex Suffix
            </DropdownItem>,
            <DropdownItem key="subSuffix" component="button" onClick={this.showSubSuffixModal} title="Create a sub-suffix under this suffix">
                Create Sub-Suffix
            </DropdownItem>,
            <DropdownItem key="dbLink" component="button" onClick={this.showLinkModal} title="Create a database chaining link subtree">
                Create Database Link
            </DropdownItem>,
            <DropdownSeparator key="separator" />,
            <DropdownItem key="deleteSuffix" component="button" onClick={this.showDeleteConfirm} title="This will permanently delete the database">
                Delete Suffix
            </DropdownItem>,
        ];

        return (
            <div id="suffix-page">
                <Grid>
                    <GridItem className="ds-suffix-header" span={9}>
                        <FontAwesomeIcon size="sm" icon={suffixIcon} />&nbsp;&nbsp;{this.props.suffix} (<i>{this.props.bename}</i>)
                        <FontAwesomeIcon
                            className="ds-left-margin ds-refresh"
                            icon={faSyncAlt}
                            title="Refresh suffix"
                            onClick={() => this.props.reload(this.props.suffix)}
                        />
                    </GridItem>
                    <GridItem span={3}>
                        <Dropdown
                            className="ds-float-right"
                            position={DropdownPosition.right}
                            onSelect={this.onSelect}
                            toggle={
                                <DropdownToggle id="suffix-dropdown" isPrimary onToggle={this.onToggle}>
                                    Suffix Tasks
                                </DropdownToggle>
                            }
                            isOpen={dropdownIsOpen}
                            dropdownItems={dropdownItems}
                        />
                    </GridItem>
                </Grid>

                <div className="ds-sub-header">
                    <Tabs isFilled activeKey={activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={0} title={<TabTitleText>Settings</TabTitleText>}>
                            <SuffixConfig
                                cachememsize={this.state.cachememsize}
                                cachesize={this.state.cachesize}
                                dncachememsize={this.state.dncachememsize}
                                dbstate={this.state.dbstate}
                                readOnly={this.state.readOnly}
                                requireIndex={this.state.requireIndex}
                                autoTuning={this.state.autoTuning}
                                handleChange={this.handleConfigChange}
                                saveHandler={this.saveSuffixConfig}
                                saving={this.state.savingConfig}
                                saveBtnDisabled={this.state.saveBtnDisabled}
                            />
                        </Tab>
                        <Tab eventKey={1} title={<TabTitleText>Referrals</TabTitleText>}>
                            <SuffixReferrals
                                rows={this.props.data.refRows}
                                suffix={this.props.suffix}
                                reload={this.props.reloadRefs}
                                addNotification={this.props.addNotification}
                                serverId={this.props.serverId}
                                key={this.state.refRows}
                            />
                        </Tab>
                        <Tab eventKey={2} title={<TabTitleText>Indexes</TabTitleText>}>
                            <SuffixIndexes
                                systemIndexRows={this.props.data.systemIndexRows}
                                indexRows={this.props.data.indexRows}
                                suffix={this.props.suffix}
                                serverId={this.props.serverId}
                                addNotification={this.props.addNotification}
                                reload={this.props.reloadIndexes}
                            />
                        </Tab>
                        <Tab eventKey={3} title={<TabTitleText>VLV Indexes</TabTitleText>}>
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
                        <Tab eventKey={4} title={<TabTitleText>Encrypted Attributes</TabTitleText>}>
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
                    handleChange={this.handleChange}
                    actionHandler={this.doDelete}
                    spinning={this.state.modalSpinning}
                    item={this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle="Delete Suffix"
                    mMsg="Are you really sure you want to delete this suffix?"
                    mSpinningMsg="Deleting suffix ..."
                    mBtnName="Delete Suffix"
                />
                <CreateLinkModal
                    showModal={this.state.showLinkModal}
                    closeHandler={this.closeLinkModal}
                    handleChange={this.handleLinkChange}
                    handleSelectChange={this.handleLinkOnSelect}
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
                    handleChange={this.handleSubSuffixChange}
                    handleSelectChange={this.handleSubSuffixOnSelect}
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
                    handleChange={this.handleChange}
                    saveHandler={this.doImport}
                    showConfirmImport={this.showConfirmLDIFImport}
                    rows={this.props.LDIFRows}
                    suffix={this.props.suffix}
                    saveBtnDisabled={this.state.ldifLocation == ""}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmLDIFImport}
                    closeHandler={this.closeConfirmLDIFImport}
                    handleChange={this.handleChange}
                    actionHandler={this.importLDIF}
                    spinning={this.state.modalSpinning}
                    item={this.state.importLDIFName}
                    checked={this.state.modalChecked}
                    mTitle="Initialize Database From LDIF"
                    mMsg="Are you sure you want to initialize the database (it will permanently overwrite the current database)?"
                    mSpinningMsg="Initializing Database ..."
                    mBtnName="Initialize Database"
                />
                <ExportModal
                    showModal={this.state.showExportModal}
                    closeHandler={this.closeExportModal}
                    handleChange={this.handleExportChange}
                    saveHandler={this.doExport}
                    spinning={this.state.exportSpinner}
                    error={this.state.errObj}
                    includeReplData={this.state.includeReplData}
                    saveBtnDisabled={this.state.ldifLocation == ""}
                />
                <DoubleConfirmModal
                    showModal={this.state.showReindexConfirm}
                    closeHandler={this.closeReindexConfirm}
                    handleChange={this.handleChange}
                    actionHandler={this.doReindex}
                    spinning={this.state.modalSpinning}
                    item={this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle="Reindex All Attributes"
                    mMsg="Are you sure you want to reindex all the attribute indexes?"
                    mSpinningMsg="Reindexing Database ..."
                    mBtnName="Reindex"
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
