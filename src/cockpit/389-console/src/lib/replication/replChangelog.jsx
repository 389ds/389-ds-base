import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import {
    Col,
    ControlLabel,
    Form,
    FormControl,
    noop,
    Row,
    Spinner,
} from "patternfly-react";
import {
    Button,
    Checkbox,
    Tooltip
} from '@patternfly/react-core';
import OutlinedQuestionCircleIcon from '@patternfly/react-icons/dist/js/icons/outlined-question-circle-icon';

export class Changelog extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            loading: false,
            errObj: {},
            showConfirmDelete: false,
            saveOK: false,
            // Changelog settings
            clMaxEntries: this.props.clMaxEntries,
            clMaxAge: this.props.clMaxAge.slice(0, -1),
            clMaxAgeUnit: this.props.clMaxAge.slice(-1).toLowerCase(),
            clTrimInt: this.props.clTrimInt,
            clEncrypt: this.props.clEncrypt,
            // Preserve original settings
            _clMaxEntries: this.props.clMaxEntries,
            _clMaxAge: this.props.clMaxAge.slice(0, -1),
            _clMaxAgeUnit: this.props.clMaxAge.slice(-1).toLowerCase(),
            _clTrimInt: this.props.clTrimInt,
            _clEncrypt: this.props.clEncrypt,
        };

        this.handleChange = this.handleChange.bind(this);
        this.saveSettings = this.saveSettings.bind(this);
    }

    saveSettings () {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'set-changelog', '--suffix', this.props.suffix
        ];
        let msg = "Successfully updated changelog configuration.";

        if (this.state.clMaxEntries != this.state._clMaxEntries) {
            cmd.push("--max-entries=" + this.state.clMaxEntries);
        }
        if (this.state.clMaxAge != this.state._clMaxAge || this.state.clMaxAgeUnit != this.state._clMaxAgeUnit) {
            cmd.push("--max-age=" + this.state.clMaxAge + this.state.clMaxAgeUnit);
        }
        if (this.state.clTrimInt != this.state._clTrimInt) {
            cmd.push("--trim-interval=" + this.state.clTrimInt);
        }
        if (this.state.clEncrypt != this.state._clEncrypt) {
            if (this.state.clEncrypt) {
                cmd.push("--encrypt");
                msg += "  This requires a server restart to take effect";
            } else {
                cmd.push("--disable-encrypt");
            }
        }
        if (cmd.length > 6) {
            this.setState({
                // Start the spinner
                saving: true
            });
            log_cmd("saveSettings", "Applying replication changelog changes", cmd);
            cockpit
                    .spawn(cmd, {superuser: true, "err": "message"})
                    .done(content => {
                        this.reloadChangelog();
                        this.props.addNotification(
                            "success",
                            msg
                        );
                        this.setState({
                            saving: false,
                            saveOK: false,
                        });
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.reloadChangelog();
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
        let all_good = false;
        let attr = e.target.id;

        errObj[e.target.id] = valueErr;
        if ((attr != 'clMaxEntries' && this.state.clMaxEntries != this.state._clMaxEntries) ||
            (attr != 'clMaxAge' && this.state.clMaxAge != this.state._clMaxAge) ||
            (attr != 'clMaxAgeUnit' && this.state.clMaxAgeUnit != this.state._clMaxAgeUnit) ||
            (attr != 'clTrimInt' && this.state.clTrimInt != this.state._clTrimInt) ||
            (attr != 'clEncrypt' && this.state.clEncrypt != this.state._clEncrypt)) {
            all_good = true;
        }
        if (attr == 'clMaxEntries' && value != this.state._clMaxEntries) {
            all_good = true;
        }
        if (attr == 'clMaxAge' && value != this.state._clMaxAge) {
            all_good = true;
        }
        if (attr == 'clMaxAgeUnit' && value != this.state._clMaxAgeUnit) {
            all_good = true;
        }
        if (attr == 'clTrimInt' && value != this.state._clTrimInt) {
            all_good = true;
        }
        if (attr == 'clEncrypt' && value != this.state._clEncrypt) {
            all_good = true;
        }

        this.setState({
            [e.target.id]: value,
            errObj: errObj,
            saveOK: all_good,
        });
    }

    clMapMaxAgeUnit(unit) {
        // Max teh unit tothe
    }

    reloadChangelog () {
        this.setState({
            loading: true,
        });
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'get-changelog', '--suffix', this.props.suffix];
        log_cmd("reloadChangelog", "Load the replication changelog info", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let clMaxEntries = "";
                    let clMaxAge = "";
                    let clMaxAgeUnit = "";
                    let clTrimInt = "";
                    let clEncrypt = false;
                    for (let attr in config['attrs']) {
                        let val = config['attrs'][attr][0];
                        if (attr == "nsslapd-changelogmaxentries") {
                            clMaxEntries = val;
                        } else if (attr == "nsslapd-changelogmaxage") {
                            clMaxAge = val.slice(0, -1);
                            clMaxAgeUnit = val.slice(-1).toLowerCase();
                        } else if (attr == "nsslapd-changelogtrim-interval") {
                            clTrimInt = val;
                        } else if (attr == "nsslapd-encryptionalgorithm") {
                            clEncrypt = true;
                        }
                    }
                    this.setState({
                        clMaxEntries: clMaxEntries,
                        clMaxAge: clMaxAge,
                        clMaxAgeUnit: clMaxAgeUnit,
                        clTrimInt: clTrimInt,
                        clEncrypt: clEncrypt,
                        _clMaxEntries: clMaxEntries,
                        _clMaxAge: clMaxAge,
                        _clTrimInt: clTrimInt,
                        _clEncrypt: clEncrypt,
                        saveOK: false,
                        loading: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to reload changelog for "${this.props.suffix}" - ${errMsg.desc}`
                    );
                    this.setState({
                        loading: false,
                    });
                });
    }

    render() {
        let clPage;
        if (this.state.saving) {
            clPage =
                <div className="ds-margin-top-xlg ds-center">
                    <h4>Saving changelog configuration ...</h4>
                    <Spinner className="ds-margin-top-lg" loading size="md" />
                </div>;
        } else if (this.loading) {
            clPage =
                <div className="ds-margin-top ds-center">
                    <h4>Loading changelog configuration ...</h4>
                    <Spinner className="ds-margin-top-lg" loading size="md" />
                </div>;
        } else {
            clPage =
                <div>
                    <Form horizontal>
                        <Row className="ds-margin-top-xlg" title="Changelog trimming parameter.  Set the maximum number of changelog entries allowed in the database (nsslapd-changelogmaxentries).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Maximum Entries
                            </Col>
                            <Col sm={4}>
                                <FormControl
                                    id="clMaxEntries"
                                    type="number"
                                    value={this.state.clMaxEntries}
                                    onChange={this.handleChange}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="Changelog trimming parameter.  This set the maximum age of a changelog entry (nsslapd-changelogmaxage).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Maximum Age
                            </Col>
                            <Col sm={2}>
                                <FormControl
                                    id="clMaxAge"
                                    type="number"
                                    value={this.state.clMaxAge}
                                    onChange={this.handleChange}
                                />
                            </Col>
                            <Col sm={2}>
                                <select
                                    className="btn btn-default dropdown"
                                    id="clMaxAgeUnit"
                                    onChange={this.handleChange}
                                    value={this.state.clMaxAgeUnit}
                                >
                                    <option value="s">Seconds</option>
                                    <option value="m">Minutes</option>
                                    <option value="h">Hours</option>
                                    <option value="d">Days</option>
                                    <option value="w">Weeks</option>
                                </select>
                            </Col>
                        </Row>
                        <Row className="ds-margin-top" title="The changelog trimming interval.  Set how often the changelog checks if there are entries that can be purged from the changelog based on the trimming parameters (nsslapd-changelogtrim-interval).">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Trimming Interval
                            </Col>
                            <Col sm={4}>
                                <FormControl
                                    id="clTrimInt"
                                    type="number"
                                    value={this.state.clTrimInt}
                                    onChange={this.handleChange}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top">
                            <Col componentClass={ControlLabel} sm={4}>
                                Changelog Encryption
                                <Tooltip
                                    id='CLtooltip'
                                    position="bottom"
                                    content={
                                        <div>
                                            Changelog encryption requires that the server must already be
                                            configured for security/TLS.  This setting also requires
                                            that you export and import the changelog which must be done
                                            while the database is in read-only mode.  So first put the
                                            database into read-only mode, then export the changelog, enable
                                            changelog encryption, restart the server, import the changelog,
                                            and finally unset the database read-only mode.
                                        </div>
                                    }
                                >
                                    <OutlinedQuestionCircleIcon
                                        className="ds-left-margin"
                                    />
                                </Tooltip>
                            </Col>
                            <Col sm={1}>
                                <Checkbox
                                    id="clEncrypt"
                                    isChecked={this.state.clEncrypt}
                                    onChange={(checked, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top-lg">
                            <Col sm={2}>
                                <Button
                                    variant="primary"
                                    onClick={this.saveSettings}
                                    isDisabled={!this.state.saveOK}
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
            </div>
        );
    }
}

Changelog.propTypes = {
    serverId: PropTypes.string,
    clMaxEntries: PropTypes.string,
    clMaxAge: PropTypes.string,
    clTrimInt: PropTypes.string,
    clEncrypt: PropTypes.bool,
    addNotification: PropTypes.func,
    suffix: PropTypes.string,
};

Changelog.defaultProps = {
    serverId: "",
    clMaxEntries: "",
    clMaxAge: "",
    clTrimInt: "",
    clEncrypt: false,
    addNotification: noop,
    suffix: "",
};
