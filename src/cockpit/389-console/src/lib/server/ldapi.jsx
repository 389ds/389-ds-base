import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Form,
    Grid,
    GridItem,
    Select,
    SelectOption,
    SelectVariant,
    Spinner,
    TextInput,
    noop
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';
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
            attributes: [],
            isUIDOpen: false,
            isGIDOpen: false,
        };

        this.onUIDToggle = isUIDOpen => {
            this.setState({
                isUIDOpen
            });
        };

        this.onUIDSelect = (event, selection, isPlaceholder) => {
            this.setState({
                'nsslapd-ldapiuidnumbertype': selection,
                isUIDOpen: false
            });
        };

        this.onGIDToggle = isGIDOpen => {
            this.setState({
                isGIDOpen
            });
        };

        this.onGIDSelect = (event, selection, isPlaceholder) => {
            this.setState({
                'nsslapd-ldapigidnumbertype': selection,
                isGIDOpen: false
            });
        };

        this.handleChange = this.handleChange.bind(this);
        this.loadConfig = this.loadConfig.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
        this.getAttributes = this.getAttributes.bind(this);
        this.reloadConfig = this.reloadConfig.bind(this);
    }

    componentDidMount() {
        // Loading config
        if (!this.state.loaded) {
            this.loadConfig();
        } else {
            this.props.enableTree();
        }
    }

    getAttributes() {
        const attr_cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema",
            "attributetypes",
            "list"
        ];
        log_cmd("getAttributes", "Get attrs", attr_cmd);
        cockpit
                .spawn(attr_cmd, { superuser: true, err: "message" })
                .done(content => {
                    const attrContent = JSON.parse(content);
                    let attrs = [];
                    for (let content of attrContent["items"]) {
                        attrs.push(content.name[0]);
                    }

                    this.setState({
                        attributes: attrs,
                    }, this.props.enableTree);
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification("error", `Failed to get attributes - ${errMsg.desc}`);
                });
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
        }, this.getAttributes);
    }

    reloadConfig() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("loadConfig", "Load server configuration", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    this.setState({
                        attrs: attrs
                    }, this.loadConfig);
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error loading server configuration - ${errMsg.desc}`
                    );
                });
    }

    saveConfig() {
        this.setState({
            loading: true
        });

        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'config', 'replace'
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
                    this.setState({
                        loading: false
                    }, this.reloadConfig);
                    this.props.addNotification(
                        "success",
                        "Successfully updated LDAPI configuration"
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.setState({
                        loading: false
                    }, this.reloadConfig);
                    this.props.addNotification(
                        "error",
                        `Error updating LDAPI configuration - ${errMsg.desc}`
                    );
                });
    }

    render() {
        let mapUserAttrs = "";
        let extraPrimaryProps = {};
        let saveBtnName = "Save Settings";
        if (this.state.loading) {
            saveBtnName = "Saving settings ...";
            extraPrimaryProps.spinnerAriaValueText = "Loading";
        }

        if (this.state['nsslapd-ldapimaptoentries']) {
            let attributes = this.state.attributes.map((option, index) => (
                <SelectOption key={index} value={option} />
            ));
            mapUserAttrs =
                <div className="ds-margin-left">
                    <Grid
                        className="ds-margin-left ds-margin-top"
                        title="The Directory Server attribute to map system UIDs to user entries (nsslapd-ldapiuidnumbertype)."
                    >
                        <GridItem className="ds-label" span={3}>
                            LDAPI UID Number Attribute
                        </GridItem>
                        <GridItem span={9}>
                            <Select
                                variant={SelectVariant.single}
                                aria-label="Select UID Input"
                                onToggle={this.onUIDToggle}
                                onSelect={this.onUIDSelect}
                                selections={this.state['nsslapd-ldapiuidnumbertype']}
                                isOpen={this.state.isUIDOpen}
                                aria-labelledby="UID"
                            >
                                {attributes}
                            </Select>
                        </GridItem>
                    </Grid>
                    <Grid
                        className="ds-margin-left ds-margin-top"
                        title="The Directory Server attribute to map system GUIDs to user entries (nsslapd-ldapigidnumbertype)."
                    >
                        <GridItem className="ds-label" span={3}>
                            LDAPI GID Number Attribute
                        </GridItem>
                        <GridItem span={9}>
                            <Select
                                variant={SelectVariant.single}
                                aria-label="Select GID Input"
                                onToggle={this.onGIDToggle}
                                onSelect={this.onGIDSelect}
                                selections={this.state['nsslapd-ldapigidnumbertype']}
                                isOpen={this.state.isGIDOpen}
                                aria-labelledby="GID"
                            >
                                {attributes}
                            </Select>
                        </GridItem>
                    </Grid>
                    <Grid
                        title="The subtree to search for user entries to use for autobind. (nsslapd-ldapientrysearchbase)."
                        className="ds-margin-left ds-margin-top"
                    >
                        <GridItem className="ds-label" span={3}>
                            LDAPI Entry Search Base
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.state['nsslapd-ldapientrysearchbase']}
                                type="text"
                                id="nsslapd-ldapientrysearchbase"
                                aria-describedby="horizontal-form-name-helper"
                                onChange={(str, e) => {
                                    this.handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                </div>;
        }

        let body =
            <div>
                <Form className="ds-margin-top-xlg" isHorizontal>
                    <Grid
                        title="The Unix socket file (nsslapd-ldapifilepath).  The UI requires this exact path so it is a read-only setting."
                        className="ds-margin-left"
                    >
                        <GridItem className="ds-label" span={3}>
                            LDAPI Socket File Path
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.state['nsslapd-ldapifilepath']}
                                type="text"
                                id="nsslapd-ldapifilepath"
                                aria-describedby="horizontal-form-name-helper"
                                isDisabled
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Map the Unix root entry to this Directory Manager DN (nsslapd-ldapimaprootdn).  The UI requires this to be set to the current root DN so it is a read-only setting."
                        className="ds-margin-left"
                    >
                        <GridItem className="ds-label" span={3}>
                            LDAPI Socket File Path
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.state['nsslapd-ldapimaprootdn']}
                                type="text"
                                id="nsslapd-ldapimaprootdn"
                                aria-describedby="horizontal-form-name-helper"
                                isDisabled
                            />
                        </GridItem>
                    </Grid>
                    <Grid
                        title="Map regular system users to Directory Server entries (nsslapd-ldapimaptoentries)."
                        className="ds-margin-left"
                    >
                        <GridItem span={5}>
                            <Checkbox
                                id="nsslapd-ldapimaptoentries"
                                isChecked={this.state['nsslapd-ldapimaptoentries']}
                                onChange={(checked, e) => {
                                    this.handleChange(e);
                                }}
                                aria-label="uncontrolled checkbox example"
                                label="Map System Users to Database Entries"
                            />
                        </GridItem>
                    </Grid>
                    {mapUserAttrs}
                </Form>
                <Button
                    isDisabled={this.state.saveDisabled}
                    variant="primary"
                    className="ds-margin-top-xlg"
                    onClick={this.saveConfig}
                    isLoading={this.state.loading}
                    spinnerAriaValueText={this.state.loading ? "Saving" : undefined}
                    {...extraPrimaryProps}
                >
                    {saveBtnName}
                </Button>
            </div>;

        if (!this.state.loaded) {
            body = <Spinner size="lg" />;
        }

        return (
            <div id="server-ldapi-page" className={this.state.loading ? "ds-disabled" : ""}>
                <Grid>
                    <GridItem span={5}>
                        <h4>LDAPI & AutoBind Settings <FontAwesomeIcon
                            size="lg"
                            className="ds-left-margin ds-refresh"
                            icon={faSyncAlt}
                            title="Refresh LDAPI settings"
                            onClick={this.loadConfig}
                        />
                        </h4>
                    </GridItem>
                </Grid>
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
