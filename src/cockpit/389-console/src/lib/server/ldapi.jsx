import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Col,
    ControlLabel,
    Form,
    FormControl,
    Icon,
    Row,
    noop,
    Spinner,
} from "patternfly-react";
import PropTypes from "prop-types";

const ldapi_attrs = [
    'nsslapd-ldapimaptoentries',
    'nsslapd-ldapifilepath',
    'nsslapd-ldapimaprootdn',
    'nsslapd-ldapientrysearchbase',
    'nsslapd-ldapigidnumbertype',
    'nsslapd-ldapiuidnumbertype',
];

export class ServerLDAPI extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            loading: false,
            loaded: false,
            saveDisabled: true,
            attrs: this.props.attrs,
            // settings

        };

        this.handleChange = this.handleChange.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
    }

    componentDidMount() {
        // Loading config
        if (!this.state.loaded) {
            this.loadConfig();
        } else {
            this.props.enableTree();
        }
    }

    handleChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (let ldapi_attr of ldapi_attrs) {
            if (attr == ldapi_attr && this.state['_' + ldapi_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let ldapi_attr of ldapi_attrs) {
            if (attr != ldapi_attr && this.state['_' + ldapi_attr] != this.state[ldapi_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [attr]: value,
            saveDisabled: disableSaveBtn,
        });
    }

    loadConfig() {
        let attrs = this.state.attrs;
        let mapToEntries = false;

        if ('nsslapd-ldapimaptoentries' in attrs) {
            if (attrs['nsslapd-ldapimaptoentries'][0] == "on") {
                mapToEntries = true;
            }
        }
        this.setState({
            loading: false,
            loaded: true,
            saveDisabled: true,
            'nsslapd-ldapimaptoentries': mapToEntries,
            'nsslapd-ldapifilepath': attrs['nsslapd-ldapifilepath'][0],
            'nsslapd-ldapimaprootdn': attrs['nsslapd-ldapimaprootdn'][0],
            'nsslapd-ldapientrysearchbase': attrs['nsslapd-ldapientrysearchbase'][0],
            'nsslapd-ldapigidnumbertype': attrs['nsslapd-ldapigidnumbertype'][0],
            'nsslapd-ldapiuidnumbertype': attrs['nsslapd-ldapiuidnumbertype'][0],
            // Record original values
            '_nsslapd-ldapimaptoentries': mapToEntries,
            '_nsslapd-ldapimaprootdn': attrs['nsslapd-ldapimaprootdn'][0],
            '_nsslapd-ldapifilepath': attrs['nsslapd-ldapifilepath'][0],
            '_nsslapd-ldapientrysearchbase': attrs['nsslapd-ldapientrysearchbase'][0],
            '_nsslapd-ldapigidnumbertype': attrs['nsslapd-ldapigidnumbertype'][0],
            '_nsslapd-ldapiuidnumbertype': attrs['nsslapd-ldapiuidnumbertype'][0],
        }, this.props.enableTree);
    }

    saveConfig() {
        this.setState({
            loading: true
        });

        let cmd = [
            'dsconf', '-j', this.props.serverId, 'config', 'replace'
        ];

        for (let attr of ldapi_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                let val = this.state[attr];
                if (typeof val === "boolean") {
                    if (val) {
                        val = "on";
                    } else {
                        val = "off";
                    }
                }
                cmd.push(attr + "=" + val);
            }
        }

        log_cmd("saveConfig", "Saving LDAPI settings", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadConfig();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully updated LDAPI configuration"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.loadConfig();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error updating LDAPI configuration - ${errMsg.desc}`
                    );
                });
    }

    render() {
        let mapUserAttrs = "";

        if (this.state['nsslapd-ldapimaptoentries']) {
            mapUserAttrs =
                <div>
                    <Row title="The Directory Server attribute to map system UIDs to user entries (nsslapd-ldapiuidnumbertype)." className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            LDAPI UID Number Attribute
                        </Col>
                        <Col sm={4}>
                            <FormControl
                                id="nsslapd-ldapiuidnumbertype"
                                type="text"
                                value={this.state['nsslapd-ldapiuidnumbertype']}
                                onChange={this.handleChange}
                                placeholder="e.g.  uidNumber"
                            />
                        </Col>
                    </Row>
                    <Row title="The Directory Server attribute to map system GUIDs to user entries (nsslapd-ldapigidnumbertype)." className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            LDAPI GID Number Attribute
                        </Col>
                        <Col sm={4}>
                            <FormControl
                                id="nsslapd-ldapigidnumbertype"
                                type="text"
                                value={this.state['nsslapd-ldapigidnumbertype']}
                                onChange={this.handleChange}
                                placeholder="e.g.  gidNumber"
                            />
                        </Col>
                    </Row>
                    <Row title="The subtree to search for user entries to use for autobind. (nsslapd-ldapientrysearchbase)." className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            LDAPI Entry Search Base
                        </Col>
                        <Col sm={4}>
                            <FormControl
                                id="nsslapd-ldapientrysearchbase"
                                type="text"
                                value={this.state['nsslapd-ldapientrysearchbase']}
                                onChange={this.handleChange}
                            />
                        </Col>
                    </Row>
                </div>;
        }

        let body =
            <div>
                <Form horizontal>
                    <Row title="The Unix socket file (nsslapd-ldapifilepath).  The UI requires this exact path so it is a read-only setting." className="ds-margin-top-lg">
                        <Col componentClass={ControlLabel} sm={3}>
                            LDAPI Socket File Path
                        </Col>
                        <Col sm={4}>
                            <FormControl
                                id="nsslapd-ldapifilepath"
                                type="text"
                                value={this.state['nsslapd-ldapifilepath']}
                                disabled
                            />
                        </Col>
                    </Row>
                    <Row title="Map the Unix root entry to this Directory Manager DN (nsslapd-ldapimaprootdn).  The UI requires this to be set to the current root DN so it is a read-only setting" className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            DN to map "root" To
                        </Col>
                        <Col sm={4}>
                            <FormControl
                                id="nsslapd-ldapimaprootdn"
                                type="text"
                                value={this.state['nsslapd-ldapimaprootdn']}
                                disabled
                            />
                        </Col>
                    </Row>
                    <Row
                        title="Map regular system users to Directory Server entries (nsslapd-ldapimaptoentries)."
                        className="ds-margin-top"
                    >
                        <Col componentClass={ControlLabel} sm={3}>
                            <Checkbox
                                checked={this.state['nsslapd-ldapimaptoentries']}
                                id="nsslapd-ldapimaptoentries"
                                onChange={this.handleChange} className="ds-margin-left-sm"
                            >
                                Map System Users to Database Entries
                            </Checkbox>
                        </Col>
                    </Row>
                    {mapUserAttrs}
                    <Button
                        disabled={this.state.saveDisabled}
                        bsStyle="primary"
                        className="ds-margin-top-med"
                        onClick={this.saveConfig}
                    >
                        Save Settings
                    </Button>
                </Form>
            </div>;

        if (this.state.lading || !this.state.loaded) {
            body = <Spinner loading size="md" />;
        }

        return (
            <div id="server-ldapi-page">
                <Row>
                    <Col sm={5}>
                        <ControlLabel className="ds-suffix-header ds-margin-top-lg">
                            LDAPI & AutoBind Settings
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh the LDAPI settings"
                                onClick={this.loadConfig}
                                disabled={this.state.loading}
                            />
                        </ControlLabel>
                    </Col>
                </Row>
                {body}
            </div>
        );
    }
}

// Property types and defaults

ServerLDAPI.propTypes = {
    addNotification: PropTypes.func,
    serverId: PropTypes.string,
    attrs: PropTypes.object,
};

ServerLDAPI.defaultProps = {
    addNotification: noop,
    serverId: "",
    attrs: {},
};
