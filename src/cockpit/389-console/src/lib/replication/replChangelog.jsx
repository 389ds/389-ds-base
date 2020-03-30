import cockpit from "cockpit";
import React from "react";
import "../../css/ds.css";
import PropTypes from "prop-types";
import { ConfirmPopup } from "../notifications.jsx";
import { log_cmd } from "../tools.jsx";
import {
    noop,
    Row,
    Button,
    Col,
    ControlLabel,
    Checkbox,
    Form,
    Icon,
    Spinner,
} from "patternfly-react";

export class Changelog extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            loading: false,
            errObj: {},
            showConfirmDelete: false,
            // Changelog settings
            clDir: this.props.clDir,
            clMaxEntries: this.props.clMaxEntries,
            clMaxAge: this.props.clMaxAge,
            clCompactInt: this.props.clCompactInt,
            clTrimInt: this.props.clTrimInt,
            clEncrypt: this.props.clEncrypt,
            // Preserve original settings
            _clDir: this.props.clDir,
            _clMaxEntries: this.props.clMaxEntries,
            _clMaxAge: this.props.clMaxAge,
            _clCompactInt: this.props.clCompactInt,
            _clTrimInt: this.props.clTrimInt,
            _clEncrypt: this.props.clEncrypt,
        };

        this.handleChange = this.handleChange.bind(this);
        this.saveSettings = this.saveSettings.bind(this);
        this.createChangelog = this.createChangelog.bind(this);
        this.confirmChangelogDelete = this.confirmChangelogDelete.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
        this.deleteChangelog = this.deleteChangelog.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    createChangelog () {
        this.setState({
            saving: true
        });
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'create-changelog'
        ];
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload();
                    this.props.addNotification(
                        "success",
                        "Successfully created replication changelog"
                    );
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.setState({
                        saving: false
                    });
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        `Error creating changelog - ${msg}`
                    );
                });
    }

    confirmChangelogDelete () {
        this.setState({
            showConfirmDelete: true
        });
    }

    closeConfirmDelete () {
        this.setState({
            showConfirmDelete: false
        });
    }

    deleteChangelog () {
        this.setState({
            saving: true
        });
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'delete-changelog'
        ];
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload();
                    this.props.addNotification(
                        "success",
                        "Successfully deleted replication changelog"
                    );
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.setState({
                        saving: false
                    });
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        `Error deleting changelog - ${msg}`
                    );
                });
    }

    saveSettings () {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'set-changelog'
        ];
        if (this.state.clDir != this.state._clDir) {
            if (this.state.clDir == "") {
                // Changelog directory can not be empty
                let errObj = this.state.errObj;
                errObj["clDir"] = true;
                this.setState({
                    errObj: errObj
                });
                return;
            }
            cmd.push("--cl-dir=" + this.state.clDir);
        }
        if (this.state.clMaxEntries != this.state._clMaxEntries) {
            cmd.push("--max-entries=" + this.state.clMaxEntries);
        }
        if (this.state.clMaxAge != this.state._clMaxAge) {
            cmd.push("--max-age=" + this.state.clMaxAge);
        }
        if (this.state.clCompactInt != this.state._clCompactInt) {
            cmd.push("--compact-interval=" + this.state.clCompactInt);
        }
        if (this.state.clTrimInt != this.state._clTrimInt) {
            cmd.push("--trim-interval=" + this.state.clTrimInt);
        }
        if (this.state.clEncrypt != this.state._clEncrypt) {
            // TODO - Not implemented in dsconf yet
            // cmd.push("--encrypt=" + this.state.clEncrypt);
        }
        if (cmd.length > 5) {
            this.setState({
                // Start the spinner
                saving: true
            });
            log_cmd("saveSettings", "Applying replication changelog changes", cmd);
            let msg = "Successfully updated changelog configuration.";
            cockpit
                    .spawn(cmd, {superuser: true, "err": "message"})
                    .done(content => {
                        this.props.reload();
                        this.props.addNotification(
                            "success",
                            msg
                        );
                        this.setState({
                            saving: false
                        });
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.reload();
                        this.setState({
                            saving: false
                        });
                        let msg = errMsg.desc;
                        if ('info' in errMsg) {
                            msg = errMsg.desc + " - " + errMsg.info;
                        }
                        this.props.addNotification(
                            "error",
                            `Error updating changelog configuration - ${msg}`
                        );
                    });
        }
    }

    handleChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        let errObj = this.state.errObj;
        if (e.target.id == "clDir" && value == "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        });
    }

    render() {
        let clPage;
        if (this.state._clDir == "") {
            // No changelog, only show clDir and Create button
            clPage =
                <div>
                    <Row>
                        <Col sm={12} className="ds-word-wrap">
                            <ControlLabel className="ds-suffix-header">
                                Replication Changelog
                                <Icon className="ds-left-margin ds-refresh"
                                    type="fa" name="refresh" title="Refresh changelog settings"
                                    onClick={this.props.reload}
                                />
                            </ControlLabel>
                        </Col>
                    </Row>
                    <hr />
                    <div className="ds-margin-top-med ds-center">
                        <p>There is no Replication Changelog</p>
                        <Row className="ds-margin-top-lg" title="Create the replication changelog">
                            <Button
                                bsStyle="primary"
                                onClick={this.createChangelog}
                            >
                                Create Changelog
                            </Button>
                        </Row>
                    </div>
                </div>;
        } else if (this.state.saving) {
            clPage =
                <div className="ds-margin-top ds-loading-spinner ds-center">
                    <h4>Saving changelog configuration ...</h4>
                    <Spinner className="ds-margin-top-lg" loading size="md" />
                </div>;
        } else if (this.props.loading) {
            clPage =
                <div className="ds-loading-spinner ds-center">
                    <h4>Loading changelog configuration ...</h4>
                    <Spinner className="ds-margin-top-lg" loading size="md" />
                </div>;
        } else {
            clPage =
                <div>
                    <Row>
                        <Col sm={5} className="ds-word-wrap">
                            <h4>
                                Replication Changelog
                                <Icon className="ds-left-margin ds-refresh"
                                    type="fa" name="refresh" title="Refresh changelog settings"
                                    onClick={this.props.reload}
                                />
                            </h4>
                        </Col>
                        <Col sm={7}>
                            <Button
                                className="ds-float-right"
                                bsStyle="danger"
                                onClick={this.confirmChangelogDelete}
                            >
                                Delete Changelog
                            </Button>
                        </Col>
                    </Row>
                    <Form horizontal>
                        <hr />
                        <Row className="ds-margin-top" title="The filesystem location of the replication changelog database">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Directory
                            </Col>
                            <Col sm={8}>
                                <input value={this.state.clDir} id="clDir" onChange={this.handleChange} className={this.state.errObj.clDir ? "ds-input-auto-bad" : "ds-input-auto"} />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="Changelog trimming parameter.  Set the maximum number of changelog entries allowed in the database (nsslapd-changelogmaxentries).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Maximum Entries
                            </Col>
                            <Col sm={8}>
                                <input value={this.state.clMaxEntries} id="clMaxEntries" onChange={this.handleChange} className="ds-input-auto" />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="Changelog trimming parameter.  This set the maximum age of a changelog entry.  It is recommended to use the same value as the Replication Purge Delay.  (nsslapd-changelogmaxage).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Maximum Age
                            </Col>
                            <Col sm={8}>
                                <input value={this.state.clMaxAge} id="clMaxAge" onChange={this.handleChange} className="ds-input-auto" />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The changelog trimming interval.  Set how often the changelog checks if there are entries that can be purged from the changelog based on the trimming parameters (nsslapd-changelogtrim-interval).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Trimming Interval
                            </Col>
                            <Col sm={8}>
                                <input value={this.state.clTrimInt} id="clTrimInt" onChange={this.handleChange} className="ds-input-auto" />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The changelog compaction interval.  Set how often the changelog will compact itself, meaning remove empty/trimmed database slots.  The default is 30 days. (nsslapd-changelogcompactdb-interval).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Compaction Interval
                            </Col>
                            <Col sm={8}>
                                <input value={this.state.clCompactInt} id="clCompactInt" onChange={this.handleChange} className="ds-input-auto" />
                            </Col>
                        </Row>
                        <Row hidden className="ds-margin-top" title="TLS must first be enabled in the server for change encryption to work.  Please consult Administration Guide for more details.">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Encryption
                            </Col>
                            <Col sm={2}>
                                <Checkbox
                                    id="clEncrypt"
                                    checked={this.state.clEncrypt}
                                    onChange={this.handleChange}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top-lg">
                            <Col sm={2}>
                                <Button
                                    bsStyle="primary"
                                    onClick={this.saveSettings}
                                >
                                    Save
                                </Button>
                            </Col>
                        </Row>
                    </Form>
                </div>;
        }

        return (
            <div>
                {clPage}
                <ConfirmPopup
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    actionFunc={this.deleteChangelog}
                    msg="Are you sure you want to delete the changelog?"
                    msgContent="This will invalidate all replication agreements, and they will need to be reinitialized."
                />
            </div>
        );
    }
}

Changelog.propTypes = {
    serverId: PropTypes.string,
    clDir: PropTypes.string,
    clMaxEntries: PropTypes.string,
    clMaxAge: PropTypes.string,
    clCompactInt: PropTypes.string,
    clTrimInt: PropTypes.string,
    clEncrypt: PropTypes.bool,
    addNotification: PropTypes.func,
    reload: PropTypes.func,
    enableTree: PropTypes.func,
};

Changelog.defaultProps = {
    serverId: "",
    clDir: "",
    clMaxEntries: "",
    clMaxAge: "",
    clCompactInt: "",
    clTrimInt: "",
    clEncrypt: false,
    addNotification: noop,
    reload: noop,
    enableTree: noop,
};
