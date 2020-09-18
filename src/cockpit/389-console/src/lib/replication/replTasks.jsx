import cockpit from "cockpit";
import React from "react";
import { log_cmd, bad_file_name } from "../tools.jsx";
import { RUVTable } from "./replTables.jsx";
import { ExportModal, ExportCLModal } from "./replModals.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import PropTypes from "prop-types";
import {
    Button,
    Col,
    ControlLabel,
    Form,
    Icon,
    noop,
    Row,
} from "patternfly-react";

export class ReplRUV extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            errObj: {},
            rid: "",
            localRID: "",
            ldifLocatio: "",
            saveOK: false,
            showConfirmCleanRUV: false,
            modalChecked: false,
            modalSpinning: false,
            showConfirmCLImport: false,
            showCLExport: false,
            defaultCL: true,
            debugCL: false,
            decodeCL: false,
            exportCSN: false,
            ldifFile: "/tmp/changelog.ldif",
        };
        this.showConfirmCleanRUV = this.showConfirmCleanRUV.bind(this);
        this.closeConfirmCleanRUV = this.closeConfirmCleanRUV.bind(this);
        this.showConfirmCLImport = this.showConfirmCLImport.bind(this);
        this.closeConfirmCLImport = this.closeConfirmCLImport.bind(this);
        this.showCLExport = this.showCLExport.bind(this);
        this.closeCLExport = this.closeCLExport.bind(this);
        this.showConfirmExport = this.showConfirmExport.bind(this);
        this.closeConfirmExport = this.closeConfirmExport.bind(this);
        this.handleLDIFChange = this.handleLDIFChange.bind(this);
        this.handleCLLDIFChange = this.handleCLLDIFChange.bind(this);
        this.handleRadioChange = this.handleRadioChange.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.cleanRUV = this.cleanRUV.bind(this);
        this.doExport = this.doExport.bind(this);
        this.exportChangelog = this.exportChangelog.bind(this);
        this.importChangelog = this.importChangelog.bind(this);
    }

    showConfirmCleanRUV (rid) {
        this.setState({
            rid: rid,
            showConfirmCleanRUV: true,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmCleanRUV () {
        this.setState({
            showConfirmCleanRUV: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    showConfirmCLImport () {
        this.setState({
            showConfirmCLImport: true,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmCLImport () {
        this.setState({
            showConfirmCLImport: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    showCLExport () {
        this.setState({
            saveOK: true,
            showCLExport: true,
            decodeCL: false,
            defaultCL: true,
            debugCL: false,
            exportChangelog: false,
            ldifFile: "/tmp/changelog.ldif"
        });
    }

    closeCLExport () {
        this.setState({
            showCLExport: false,
        });
    }

    showConfirmExport () {
        this.setState({
            saveOK: false,
            showConfirmExport: true,
        });
    }

    closeConfirmExport () {
        this.setState({
            showConfirmExport: false,
        });
    }

    handleRadioChange(e) {
        // Handle the changelog export options
        let defaultCL = false;
        let debugCL = false;
        if (e.target.id == "defaultCL") {
            defaultCL = true;
        } else if (e.target.id == "debugCL") {
            debugCL = true;
        }
        this.setState({
            defaultCL: defaultCL,
            debugCL: debugCL,
        });
    }

    cleanRUV () {
        // Enable/disable agmt
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'repl-tasks', 'cleanallruv', '--replica-id=' + this.state.rid,
            '--force-cleaning', '--suffix=' + this.props.suffix];

        log_cmd('cleanRUV', 'Clean the rid', cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        'success',
                        'Successfully started CleanAllRUV task');
                    this.closeConfirmCleanRUV();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to start CleanAllRUV task - ${errMsg.desc}`
                    );
                    this.closeConfirmCleanRUV();
                });
    }

    handleLDIFChange (e) {
        let value = e.target.value;
        let saveOK = true;
        if (value == "" || bad_file_name(value)) {
            saveOK = false;
        }
        this.setState({
            [e.target.id]: value,
            saveOK: saveOK
        });
    }

    handleCLLDIFChange (e) {
        let value = e.target.value;
        let saveOK = true;
        if (value == "" || value.indexOf(' ') >= 0) {
            saveOK = false;
        }
        this.setState({
            [e.target.id]: value,
            saveOK: saveOK
        });
    }

    handleChange (e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value,
        });
    }

    doExport () {
        // Do Export
        let export_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "export", this.props.suffix, "--ldif=" + this.state.ldifLocation,
            '--replication', "--encrypted"
        ];

        this.setState({
            exportSpinner: true,
        });

        log_cmd("doExport", "replication do online export", export_cmd);
        cockpit
                .spawn(export_cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Database export complete`
                    );
                    this.setState({
                        showConfirmExport: false,
                        exportSpinner: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error exporting database - ${errMsg.desc}`
                    );
                    this.setState({
                        showConfirmExport: false,
                        exportSpinner: false,
                    });
                });
    }

    importChangelog () {
        // Do changelog import
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "import-changelog", "default", "--replica-root", this.props.suffix
        ];

        this.setState({
            modalSpinning: true,
        });

        log_cmd("importChangelog", "Import relication changelog via LDIF", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Changelog was successfully initialized`
                    );
                    this.setState({
                        showConfirmCLImport: false,
                        modalSpinning: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error importing changelog LDIF - ${errMsg.desc}`
                    );
                    this.setState({
                        showConfirmCLImport: false,
                        modalSpinning: false,
                    });
                });
    }

    exportChangelog () {
        // Do changelog export
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "export-changelog"
        ];

        if (this.state.defaultCL) {
            cmd.push("default");
        } else {
            cmd.push("to-ldif");
            if (this.state.exportCSN) {
                cmd.push("--csn-only");
            }
            if (this.state.decodeCL) {
                cmd.push("--decode");
            }
            if (this.state.ldifFile) {
                cmd.push("--output-file=" + this.state.ldifFile);
            }
        }
        cmd.push("--replica-root=" + this.props.suffix);

        this.setState({
            exportSpinner: true,
        });

        log_cmd("exportChangelog", "Import relication changelog via LDIF", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Changelog was successfully exported`
                    );
                    this.setState({
                        showCLExport: false,
                        exportSpinner: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error importing changelog LDIF - ${errMsg.desc}`
                    );
                    this.setState({
                        showCLExport: false,
                        exportSpinner: false,
                    });
                });
    }

    render() {
        // Strip out the local RUV and display it different then only allow
        // cleaning of remote rids
        let remote_rows = [];
        let localRID = "";
        let localURL = "";
        let localCSN = "";
        let localRawCSN = "";
        let localMinCSN = "";
        let localRawMinCSN = "";
        for (let row of this.props.rows) {
            if (row.rid == this.props.localRID) {
                localRID = row.rid;
                localURL = row.url;
                localCSN = row.maxcsn;
                localRawCSN = row.raw_maxcsn;
                localMinCSN = row.csn;
                localRawMinCSN = row.raw_csn;
            } else {
                remote_rows.push(row);
            }
        }
        let localRUV =
            <div className="ds-left-indent-md">
                <Row className="ds-margin-top-med">
                    <Col sm={2}>
                        <ControlLabel>
                            Replica ID
                        </ControlLabel>
                    </Col>
                    <Col sm={10}>
                        <b>{localRID}</b>
                    </Col>
                </Row>
                <Row>
                    <Col sm={2}>
                        <ControlLabel>
                            LDAP URL
                        </ControlLabel>
                    </Col>
                    <Col sm={10}>
                        <b>{localURL}</b>
                    </Col>
                </Row>
                <Row>
                    <Col sm={2}>
                        <ControlLabel>
                            Min CSN
                        </ControlLabel>
                    </Col>
                    <Col sm={10}>
                        <b>{localMinCSN}</b> ({localRawMinCSN})
                    </Col>
                </Row>
                <Row>
                    <Col sm={2}>
                        <ControlLabel>
                            Max CSN
                        </ControlLabel>
                    </Col>
                    <Col sm={10}>
                        <b>{localCSN}</b> ({localRawCSN})
                    </Col>
                </Row>
            </div>;

        if (localRID == "") {
            localRUV =
                <div className="ds-indent">
                    <i>
                        There is no local RUV, the database might not have been initialized yet.
                    </i>
                </div>;
        }

        return (
            <div className="ds-margin-top-xlg ds-indent">
                <ControlLabel className="ds-h4">
                    Local RUV
                    <Icon className="ds-left-margin ds-refresh"
                        type="fa" name="refresh" title="Refresh the RUV for this suffix"
                        onClick={() => {
                            this.props.reload(this.props.suffix);
                        }}
                    />
                </ControlLabel>
                {localRUV}
                <hr />
                <Row className="ds-margin-top">
                    <Col sm={12} className="ds-word-wrap">
                        <ControlLabel className="ds-h4">
                            Remote RUV's
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh the RUV for this suffix"
                                onClick={() => {
                                    this.props.reload(this.props.suffix);
                                }}
                            />
                        </ControlLabel>
                    </Col>
                </Row>
                <div className="ds-left-indent-md ds-margin-top-lg">
                    <RUVTable
                        rows={remote_rows}
                        confirmDelete={this.showConfirmCleanRUV}
                    />
                </div>
                <hr />
                <h4 className="ds-margin-top-xlg">Create Replica Initialization LDIF File</h4>
                <Form className="ds-margin-top-lg ds-left-indent-md" horizontal>
                    <Row className="ds-margin-top-lg">
                        <Col sm={3} componentClass={ControlLabel}>
                            <Button
                                bsStyle="primary"
                                onClick={this.showConfirmExport}
                                title="See Database Tab -> Backups & LDIFs to manage the new LDIF"
                            >
                                Export Replica
                            </Button>
                        </Col>
                        <Col sm={8}>
                            <p className="ds-margin-top">
                                Export this suffix with the replication metadata to an LDIF file for initializing another replica.
                            </p>
                        </Col>
                    </Row>
                </Form>
                <hr />
                <h4 className="ds-margin-top-xlg">Replication Change Log Tasks</h4>
                <Form className="ds-margin-top-lg ds-left-indent-md" horizontal>
                    <Row className="ds-margin-top-lg">
                        <Col
                            sm={3} componentClass={ControlLabel}
                            title="Export the changelog to an LDIF file.  Typically used for changelog encryption purposes, or debugging."
                        >
                            <Button
                                bsStyle="primary"
                                onClick={this.showCLExport}
                            >
                                Export Changelog
                            </Button>
                        </Col>
                        <Col sm={8}>
                            <p className="ds-margin-top">
                                Export the replication changelog to a LDIF file.  Used for preparing to encrypt the changelog, or simply for debugging.
                            </p>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top-lg">
                        <Col
                            sm={3} componentClass={ControlLabel}
                            title="Initialize the changelog with an LDIF file for changelog encryption purposes."
                        >
                            <Button
                                bsStyle="primary"
                                onClick={this.showConfirmCLImport}
                            >
                                Import Changelog
                            </Button>
                        </Col>
                        <Col sm={8}>
                            <p className="ds-margin-top">
                                Initialize the replication changelog from an LDIF file.  Used to initialize the change log after encryption has been enabled.
                            </p>
                        </Col>
                    </Row>
                </Form>
                <hr />

                <DoubleConfirmModal
                    showModal={this.state.showConfirmCleanRUV}
                    closeHandler={this.closeConfirmCleanRUV}
                    handleChange={this.handleChange}
                    actionHandler={this.cleanRUV}
                    spinning={this.state.modalSpinning}
                    item={"Replica ID " + this.state.rid}
                    checked={this.state.modalChecked}
                    mTitle="Remove RUV Element (CleanAllRUV)"
                    mMsg="Are you sure you want to attempt to clean this Replica ID from the suffix?"
                    mSpinningMsg="Starting cleaning task (CleanAllRUV) ..."
                    mBtnName="Remove RUV Element"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmCLImport}
                    closeHandler={this.closeConfirmCLImport}
                    handleChange={this.handleChange}
                    actionHandler={this.importChangelog}
                    spinning={this.state.modalSpinning}
                    item={"Replicated Suffix " + this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle="Initialize Replication Changelog From LDIF"
                    mMsg="Are you sure you want to attempt to initialize the changelog from LDIF?  This will reject all operations during during the initialization."
                    mSpinningMsg="Initialzing Replication Change Log ..."
                    mBtnName="Import Changelog LDIF"
                />
                <ExportModal
                    showModal={this.state.showConfirmExport}
                    closeHandler={this.closeConfirmExport}
                    handleChange={this.handleLDIFChange}
                    saveHandler={this.doExport}
                    spinning={this.state.exportSpinner}
                    saveOK={this.state.saveOK}
                />
                <ExportCLModal
                    showModal={this.state.showCLExport}
                    closeHandler={this.closeCLExport}
                    handleChange={this.handleChange}
                    handleLDIFChange={this.handleCLLDIFChange}
                    handleRadioChange={this.handleRadioChange}
                    saveHandler={this.exportChangelog}
                    defaultCL={this.state.defaultCL}
                    debugCL={this.state.debugCL}
                    decodeCL={this.state.decodeCL}
                    exportCSN={this.state.exportCSN}
                    ldifFile={this.state.ldifFile}
                    spinning={this.state.exportSpinner}
                    saveOK={this.state.saveOK}
                />

            </div>
        );
    }
}

ReplRUV.propTypes = {
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    rows: PropTypes.array,
    addNotification: PropTypes.func,
    localRID: PropTypes.string,
    reload: PropTypes.func,
};

ReplRUV.defaultProps = {
    serverId: "",
    suffix: "",
    rows: [],
    addNotification: noop,
    localRID: "",
    reload: noop,
};
