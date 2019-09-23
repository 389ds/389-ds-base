import cockpit from "cockpit";
import React from "react";
import { log_cmd, bad_file_name } from "../tools.jsx";
import { RUVTable } from "./replTables.jsx";
import { ExportModal } from "./replModals.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import PropTypes from "prop-types";
import {
    Button,
    Col,
    ControlLabel,
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
        };
        this.showConfirmCleanRUV = this.showConfirmCleanRUV.bind(this);
        this.closeConfirmCleanRUV = this.closeConfirmCleanRUV.bind(this);
        this.showConfirmExport = this.showConfirmExport.bind(this);
        this.closeConfirmExport = this.closeConfirmExport.bind(this);
        this.handleLDIFChange = this.handleLDIFChange.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.cleanRUV = this.cleanRUV.bind(this);
        this.doExport = this.doExport.bind(this);
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

    handleChange (e) {
        this.setState({
            [e.target.id]: e.target.checked,
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

    render() {
        // Strip outthe loca RUV and display it diffent then onmly allow cleaning of remote rids
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
                <h4>Create Initialization LDIF File</h4>
                <div className="ds-left-indent-md ds-margin-top-lg">
                    <p><i>Export this suffix with the replication metadata needed to initialize another replica</i></p>
                    <Button
                        bsStyle="primary"
                        onClick={this.showConfirmExport}
                        className="ds-margin-top"
                        title="See Database Tab -> Backups & LDIFs to manage the new LDIF"
                    >
                        Export To LDIF
                    </Button>
                </div>
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
                <ExportModal
                    showModal={this.state.showConfirmExport}
                    closeHandler={this.closeConfirmExport}
                    handleChange={this.handleLDIFChange}
                    saveHandler={this.doExport}
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
