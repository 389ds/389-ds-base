import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup } from "../notifications.jsx";
import { AttrEncryption } from "./attrEncryption.jsx";
import { SuffixConfig } from "./suffixConfig.jsx";
import { SuffixReferrals } from "./referrals.jsx";
import { SuffixIndexes } from "./indexes.jsx";
import { VLVIndexes } from "./vlvIndexes.jsx";
import { log_cmd } from "../tools.jsx";
import {
    ImportModal,
    ExportModal,
    ReindexModal,
    CreateSubSuffixModal,
    CreateLinkModal,
} from "./databaseModal.jsx";
import {
    DropdownButton,
    MenuItem,
    Nav,
    NavItem,
    Row,
    Col,
    ControlLabel,
    Icon,
    TabContent,
    TabPane,
    TabContainer,
    noop
} from "patternfly-react";
import PropTypes from "prop-types";
import "../../css/ds.css";

export class Suffix extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            loading: false,
            activeKey: 1,
            notifications: [],
            errObj: {},
            refRows: this.props.data.refRows,
            encAttrsRows: this.props.data.encAttrsRows,
            vlvItems: this.props.data.vlvItems,
            autoTuning: this.props.data.autoTuning,
            // Suffix configuration
            cachememsize: this.props.data.cachememsize,
            cachesize: this.props.data.cachesize,
            dncachememsize: this.props.data.dncachememsize,
            readOnly: this.props.data.readOnly,
            requireIndex: this.props.data.requireIndex,
            _cachememsize: this.props.data.cachememsize,
            _cachesize: this.props.data.cachesize,
            _dncachememsize: this.props.data.dncachememsize,
            _readOnly: this.props.data.readOnly,
            _requireIndex: this.props.data.requireIndex,

            // Import/Export modals
            showImportModal: false,
            showExportModal: false,
            ldifLocation: "",
            attrEncryption: false,
            exportSpinner: false,
            importSpinner: false,
            showConfirmLDIFImport: false,
            deleleLDIFName: "",
            // Reindex all
            showReindexConfirm: false,
            // Create Sub Suffix
            showSubSuffixModal: false,
            subSuffixValue: "",
            subSuffixBeName: "",
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
            linkPwdMatch: false,
            // Delete
            showDeleteConfirm: false,
        };

        // General bindings
        this.handleNavSelect = this.handleNavSelect.bind(this);
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
        // Reindex Suffix Modal
        this.showReindexConfirm = this.showReindexConfirm.bind(this);
        this.closeReindexConfirm = this.closeReindexConfirm.bind(this);
        this.doReindex = this.doReindex.bind(this);
        // Create sub suffix modal
        this.showSubSuffixModal = this.showSubSuffixModal.bind(this);
        this.closeSubSuffixModal = this.closeSubSuffixModal.bind(this);
        this.createSubSuffix = this.createSubSuffix.bind(this);
        // Create link modal
        this.showLinkModal = this.showLinkModal.bind(this);
        this.closeLinkModal = this.closeLinkModal.bind(this);
        this.createLink = this.createLink.bind(this);
        this.handleLinkChange = this.handleLinkChange.bind(this);
        // Suffix config
        this.saveSuffixConfig = this.saveSuffixConfig.bind(this);
        // Attr Encrypt Modal
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
        this.doDelete = this.doDelete.bind(this);
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    //
    // Import Modal
    //
    showImportModal() {
        this.setState({
            ldifLocation: "",
            attrEncryption: false,
            showImportModal: true,
            importSpinner: false,
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

    showConfirmLDIFImport (item) {
        // call deleteLDIF
        this.setState({
            showConfirmLDIFImport: true,
            importLDIFName: item.name,
        });
    }

    closeConfirmLDIFImport () {
        // call importLDIF
        this.setState({
            showConfirmLDIFImport: false,
        });
    }

    importLDIF (ldif) {
        // Do import
        let import_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "import", this.props.suffix, ldif, "--encrypted"
        ];

        this.setState({
            importSpinner: true,
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
                        showImportModal: false
                    });
                })
                .fail(err => {
                    this.props.addNotification(
                        "error",
                        `Error importing LDIF file - ${err}`
                    );
                    this.setState({
                        showImportModal: false
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
            "backend", "export", this.props.suffix, "--ldif=" + this.state.ldifLocation, "--encrypted"
        ];

        if (this.state.attrEncrpytion) {
            export_cmd.push("--encrypted");
        }

        this.setState({
            exportSpinner: true,
        });

        log_cmd("doExport", "Do online export", export_cmd);
        cockpit
                .spawn(export_cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadLDIFs();
                    this.props.addNotification(
                        "success",
                        `Database export complete`
                    );
                    this.setState({
                        showExportModal: false,
                    });
                })
                .fail(err => {
                    this.loadLDIFs();
                    this.props.addNotification(
                        "error",
                        `Error exporting database - ${err}`
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
            showReindexConfirm: true
        });
    }

    closeReindexConfirm() {
        this.setState({
            showReindexConfirm: false
        });
    }

    closeReindexModal() {
        this.setState({
            showReindexModal: false
        });
    }

    doReindex() {
        // Show index status modal
        this.setState({
            showReindexModal: true
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
                        showReindexModal: false,
                    });
                })
                .fail(err => {
                    this.props.addNotification(
                        "error",
                        `Failed to reindex database - ${err}`
                    );
                    this.setState({
                        showReindexModal: false,
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
        let missingArgs = {
            createSuffix: false,
            createBeName: false
        };
        let errors = false;
        if (this.state.subSuffixValue == "") {
            this.props.addNotification(
                "warning",
                `Missing Suffix`
            );
            missingArgs.subSuffixValue = true;
            errors = true;
        }
        if (this.state.subSuffixBeName == "") {
            this.props.addNotification(
                "warning",
                `Missing backend name`
            );
            missingArgs.subSuffixBeName = true;
            errors = true;
        }
        if (errors) {
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        // Create a new suffix
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "create", "--be-name", this.state.subSuffixBeName,
            "--suffix=" + this.state.subSuffixValue + "," + this.props.suffix,
            "--parent-suffix=" + this.props.suffix
        ];

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
                })
                .fail(err => {
                    this.props.loadSuffixTree(false);
                    this.closeSubSuffixModal();
                    this.props.addNotification(
                        "error",
                        `Error creating sub-suffix - ${err}`
                    );
                });
    }

    //
    // Create Chaining Link
    //
    showLinkModal() {
        this.setState({
            showLinkModal: true,
            linkPwdMatch: true,
            errObj: {},
        });
    }

    closeLinkModal() {
        this.setState({
            showLinkModal: false
        });
    }

    createLink() {
        // Check for required paramters
        let formError = false;
        let missingArgs = {
            createLinkSuffix: false,
            createNsfarmserverurl: false,
            createLinkName: false,
            createNsmultiplexorbinddn: false,
            createNsmultiplexorcredentials: false,
            createNsmultiplexorcredentialsConfirm: false
        };
        if (this.state.createLinkSuffix == "") {
            this.props.addNotification(
                "warning",
                `Missing subsuffix!`
            );
            missingArgs.createLinkSuffix = true;
            formError = true;
        }
        if (this.state.createNsfarmserverurl == "") {
            this.props.addNotification(
                "warning",
                `Missing Server URL!`
            );
            missingArgs.createNsfarmserverurl = true;
            formError = true;
        }
        if (this.state.createLinkName == "") {
            this.props.addNotification(
                "warning",
                `Missing Link Name`
            );
            missingArgs.createLinkName = true;
            formError = true;
        }
        if (this.state.createNsmultiplexorbinddn == "") {
            this.props.addNotification(
                "warning",
                `Missing Bind DN`
            );
            missingArgs.createNsmultiplexorbinddn = true;
            formError = true;
        }
        // Check passwords match
        if (this.state.createNsmultiplexorcredentials == "" &&
            this.state.createNsmultiplexorcredentialsConfirm == "") {
            this.props.addNotification(
                "warning",
                `Missing Bind Password`
            );
            missingArgs.createNsmultiplexorcredentialsConfirm = true;
            missingArgs.createNsmultiplexorcredentials = true;
            formError = true;
        }
        if (this.state.createNsmultiplexorcredentials != this.state.createNsmultiplexorcredentialsConfirm) {
            this.props.addNotification(
                "warning",
                `Passwords do not match`
            );
            missingArgs.createNsmultiplexorcredentialsConfirm = true;
            missingArgs.createNsmultiplexorcredentials = false;
            formError = true;
        }
        if (formError) {
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        // Add chaining link
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
            cmd.push("--use-starttls");
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
                })
                .fail(err => {
                    this.props.loadSuffixTree(false);
                    this.closeLinkModal();
                    this.props.addNotification(
                        "error",
                        `Error creating database link - ${err}`
                    );
                });
    }

    checkPasswords() {
        let pwdMatch = false;
        if (this.state.createNsmultiplexorcredentials == this.state.createNsmultiplexorcredentialsConfirm) {
            pwdMatch = true;
        }
        this.setState({
            linkPwdMatch: pwdMatch
        });
    }

    handleLinkChange(e) {
        // Check for matching credentials
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
        }, this.checkPasswords);
    }

    //
    // Delete suffix
    //
    showDeleteConfirm(item) {
        this.setState({
            showDeleteConfirm: true
        });
    }

    closeDeleteConfirm() {
        this.setState({
            showDeleteConfirm: false
        });
    }

    doDelete() {
        // Delete suffix
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "delete", this.props.suffix
        ];
        log_cmd("doDelete", "Delete database", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.loadSuffixTree(true);
                    this.closeLinkModal();
                    this.props.addNotification(
                        "success",
                        `Successfully deleted database`
                    );
                })
                .fail(err => {
                    this.props.loadSuffixTree(true);
                    this.closeLinkModal();
                    this.props.addNotification(
                        "error",
                        `Error deleting database - ${err}`
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
        if (cmd.length > 7) {
            log_cmd("saveSuffixConfig", "Save suffix config", cmd);
            let msg = "Successfully updated suffix configuration";
            if (requireRestart) {
                msg = ("Successfully updated suffix configuration.  These " +
                    "changes require the server to be restarted to take effect");
            }
            cockpit
                    .spawn(cmd, {superuser: true, "err": "message"})
                    .done(content => {
                        // Continue with the next mod
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "success",
                            msg
                        );
                    })
                    .fail(err => {
                        this.props.reload(this.props.suffix);
                        this.props.addNotification(
                            "error",
                            `Error updating suffix configuration - ${err}`
                        );
                    });
        }
    }

    //
    // Render the component
    //
    render () {
        let suffixIcon = "tree";
        if (this.props.dbtype == "subsuffix") {
            suffixIcon = "leaf";
        }

        const confirm_msg =
            <span>
                Are you sure you want to import: <b>{this.state.importLDIFName}</b> ?
            </span>;

        return (
            <div className="container-fluid" id="suffix-page">
                <Row>
                    <Col sm={10} className="ds-word-wrap">
                        <ControlLabel className="ds-suffix-header"><Icon type="fa" name={suffixIcon} /> {this.props.suffix} (<i>{this.props.bename}</i>)</ControlLabel>
                    </Col>
                    <Col sm={2}>
                        <div>
                            <DropdownButton bsStyle="primary" title="Suffix Tasks" id="mydropdown">
                                <MenuItem eventKey="1" onClick={this.showImportModal} title="Import an LDIF file to initialize the database">
                                    Initialize Suffix
                                </MenuItem>
                                <MenuItem eventKey="2" onClick={this.showExportModal} title="Export the database to an LDIF file">
                                    Export Suffix
                                </MenuItem>
                                <MenuItem eventKey="3" onClick={this.showReindexConfirm} title="Reindex the entire database">
                                    Reindex Suffix
                                </MenuItem>
                                <MenuItem eventKey="4" onClick={this.showSubSuffixModal} title="Create a sub-suffix under this suffix">
                                    Create Sub-Suffix
                                </MenuItem>
                                <MenuItem eventKey="5" onClick={this.showLinkModal} title="Create a database chaining link subtree">
                                    Create Database Link
                                </MenuItem>
                                <MenuItem divider />
                                <MenuItem eventKey="6" onClick={this.showDeleteConfirm} title="This will permanently delete the database">
                                    Delete Suffix
                                </MenuItem>
                            </DropdownButton>
                        </div>
                    </Col>
                </Row>
                <p />

                <TabContainer id="basic-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                    <div>
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: 'Settings'}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: 'Referrals'}} />
                            </NavItem>
                            <NavItem eventKey={3}>
                                <div dangerouslySetInnerHTML={{__html: 'Indexes'}} />
                            </NavItem>
                            <NavItem eventKey={5}>
                                <div dangerouslySetInnerHTML={{__html: 'VLV Indexes'}} />
                            </NavItem>
                            <NavItem eventKey={4}>
                                <div dangerouslySetInnerHTML={{__html: 'Encrypted Attributes'}} />
                            </NavItem>
                        </Nav>
                        <TabContent>

                            <TabPane eventKey={1}>
                                <SuffixConfig
                                    cachememsize={this.state.cachememsize}
                                    cachesize={this.state.cachesize}
                                    dncachememsize={this.state.dncachememsize}
                                    readOnly={this.state.readOnly}
                                    requireIndex={this.state.requireIndex}
                                    autoTuning={this.state.autoTuning}
                                    handleChange={this.handleChange}
                                    saveHandler={this.saveSuffixConfig}
                                />
                            </TabPane>

                            <TabPane eventKey={2}>
                                <SuffixReferrals
                                    rows={this.props.data.refRows}
                                    suffix={this.props.suffix}
                                    reload={this.props.reloadRefs}
                                    addNotification={this.props.addNotification}
                                    serverId={this.props.serverId}
                                    key={this.state.refRows}
                                />
                            </TabPane>

                            <TabPane eventKey={3}>
                                <div className="ds-indent ds-tab-table">
                                    <TabContainer id="index-tabs" defaultActiveKey={1}>
                                        <SuffixIndexes
                                            systemIndexRows={this.props.data.systemIndexRows}
                                            indexRows={this.props.data.indexRows}
                                            suffix={this.props.suffix}
                                            serverId={this.props.serverId}
                                            addNotification={this.props.addNotification}
                                            reload={this.props.reloadIndexes}
                                        />
                                    </TabContainer>
                                </div>
                            </TabPane>
                            <TabPane eventKey={4}>
                                <div className="ds-sub-header">
                                    <AttrEncryption
                                        rows={this.props.data.encAttrsRows}
                                        suffix={this.props.suffix}
                                        serverId={this.props.serverId}
                                        addNotification={this.props.addNotification}
                                        attrs={this.props.attrs}
                                        reload={this.props.reloadAttrEnc}
                                    />
                                </div>
                            </TabPane>
                            <TabPane eventKey={5}>
                                <VLVIndexes
                                    suffix={this.props.suffix}
                                    serverId={this.props.serverId}
                                    vlvItems={this.props.data.vlvItems}
                                    addNotification={this.props.addNotification}
                                    attrs={this.props.attrs}
                                    reload={this.props.reloadVLV}
                                />
                            </TabPane>
                        </TabContent>
                    </div>
                </TabContainer>

                <ConfirmPopup
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    actionFunc={this.doDelete}
                    actionParam={this.props.suffix}
                    msg="Are you really sure you want to delete the database?"
                    msgContent={this.props.suffix}
                />
                <CreateLinkModal
                    showModal={this.state.showLinkModal}
                    closeHandler={this.closeLinkModal}
                    handleChange={this.handleLinkChange}
                    saveHandler={this.createLink}
                    suffix={this.props.suffix}
                    pwdMatch={this.state.linkPwdMatch}
                    error={this.state.errObj}
                />
                <CreateSubSuffixModal
                    showModal={this.state.showSubSuffixModal}
                    closeHandler={this.closeSubSuffixModal}
                    handleChange={this.handleChange}
                    saveHandler={this.createSubSuffix}
                    suffix={this.props.suffix}
                    error={this.state.errObj}
                />
                <ImportModal
                    showModal={this.state.showImportModal}
                    closeHandler={this.closeImportModal}
                    handleChange={this.handleChange}
                    saveHandler={this.doImport}
                    showConfirmImport={this.showConfirmLDIFImport}
                    spinning={this.state.importSpinner}
                    rows={this.props.LDIFRows}
                    suffix={this.props.suffix}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmLDIFImport}
                    closeHandler={this.closeConfirmLDIFImport}
                    actionFunc={this.importLDIF}
                    actionParam={this.state.importLDIFName}
                    msg={confirm_msg}
                    msgContent="WARNING: This action will permanently overwrite the current database!"
                />

                <ExportModal
                    showModal={this.state.showExportModal}
                    closeHandler={this.closeExportModal}
                    handleChange={this.handleChange}
                    saveHandler={this.doExport}
                    spinning={this.state.exportSpinner}
                    error={this.state.errObj}
                />
                <ConfirmPopup
                    showModal={this.state.showReindexConfirm}
                    closeHandler={this.closeReindexConfirm}
                    actionFunc={this.doReindex}
                    actionParam={this.props.suffix}
                    msg="Are you sure you want to reindex all the attribute indexes?"
                    msgContent=""
                />
                <ReindexModal
                    showModal={this.state.showReindexModal}
                    closeHandler={this.closeReindexModal}
                    msg="Reindexing All Attribute Indexes"
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
};

Suffix.defaultProps = {
    serverId: "",
    suffix: "",
    bename: "",
    loadSuffixTree: noop,
    reload: noop,
    reloadRefs: noop,
    reloadIndexes: noop,
    reloadVLV: noop,
    reloadAttrEnc: noop,
    reloadLDIFs: noop,
    addNotification: noop,
    dbtype: "",
    data: {},
    attrs: [],
    LDIFRows: []
};
