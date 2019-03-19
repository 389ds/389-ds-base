import cockpit from "cockpit";
import React from "react";
import {
    noop,
    FormGroup,
    FormControl,
    Row,
    Col,
    Form,
    ControlLabel
} from "patternfly-react";
import { Typeahead } from "react-bootstrap-typeahead";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd } from "../tools.jsx";
import "../../css/ds.css";

class ReferentialIntegrity extends React.Component {
    componentWillMount(prevProps) {
        this.getAttributes();
        this.updateFields();
    }

    componentDidUpdate(prevProps) {
        if (this.props.rows !== prevProps.rows) {
            this.updateFields();
        }
    }

    constructor(props) {
        super(props);

        this.state = {
            updateDelay: "",
            membershipAttr: [],
            entryScope: "",
            excludeEntryScope: "",
            containerScope: "",
            attributes: []
        };

        this.updateFields = this.updateFields.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.getAttributes = this.getAttributes.bind(this);
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    updateFields() {
        let membershipAttrList = [];

        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(
                row => row.cn[0] === "referential integrity postoperation"
            );

            this.setState({
                updateDelay:
                    pluginRow["referint-update-delay"] === undefined
                        ? ""
                        : pluginRow["referint-update-delay"][0],
                entryScope:
                    pluginRow["nsslapd-pluginEntryScope"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginEntryScope"][0],
                excludeEntryScope:
                    pluginRow["nsslapd-pluginExcludeEntryScope"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginExcludeEntryScope"][0],
                containerScope:
                    pluginRow["nsslapd-pluginContainerScope"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginContainerScope"][0]
            });

            if (pluginRow["referint-membership-attr"] === undefined) {
                this.setState({ membershipAttr: [] });
            } else {
                for (let value of pluginRow["referint-membership-attr"]) {
                    membershipAttrList = [
                        ...membershipAttrList,
                        { id: value, label: value }
                    ];
                }
                this.setState({ membershipAttr: membershipAttrList });
            }
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
                        attrs.push({
                            id: content.name,
                            label: content.name
                        });
                    }
                    this.setState({
                        attributes: attrs
                    });
                })
                .fail(err => {
                    this.props.addNotification(
                        "error",
                        `Failed to get attributes - ${err}`
                    );
                });
    }

    render() {
        const {
            updateDelay,
            membershipAttr,
            entryScope,
            excludeEntryScope,
            containerScope,
            attributes
        } = this.state;

        let specificPluginCMD = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "referential-integrity",
            "set",
            "--update-delay",
            updateDelay || "delete",
            "--entry-scope",
            entryScope || "delete",
            "--exclude-entry-scope",
            excludeEntryScope || "delete",
            "--container-scope",
            containerScope || "delete"
        ];

        // Delete attributes if the user set an empty value to the field
        specificPluginCMD = [...specificPluginCMD, "--membership-attr"];
        if (membershipAttr.length != 0) {
            for (let value of membershipAttr) {
                specificPluginCMD = [...specificPluginCMD, value.label];
            }
        } else {
            specificPluginCMD = [...specificPluginCMD, "delete"];
        }

        return (
            <div>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="referential integrity postoperation"
                    pluginName="Referential Integrity"
                    cmdName="referential-integrity"
                    specificPluginCMD={specificPluginCMD}
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup
                                    key="updateDelay"
                                    controlId="updateDelay"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Sets the update interval. Special values: 0 - The check is performed immediately, -1 - No check is performed (referint-update-delay)"
                                    >
                                        Update Delay
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={updateDelay}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="membershipAttr"
                                    controlId="membershipAttr"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Specifies attributes to check for and update (referint-membership-attr)"
                                    >
                                        Membership Attribute
                                    </Col>
                                    <Col sm={6}>
                                        <Typeahead
                                            allowNew
                                            multiple
                                            onChange={value => {
                                                this.setState({
                                                    membershipAttr: value
                                                });
                                            }}
                                            selected={membershipAttr}
                                            options={attributes}
                                            newSelectionPrefix="Add a membership attribute: "
                                            placeholder="Type an attribute..."
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="entryScope"
                                    controlId="entryScope"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Defines the subtree in which the plug-in looks for the delete or rename operations of a user entry (nsslapd-pluginEntryScope)"
                                    >
                                        Entry Scope
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={entryScope}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="excludeEntryScope"
                                    controlId="excludeEntryScope"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Defines the subtree in which the plug-in ignores any operations for deleting or renaming a user (nsslapd-pluginExcludeEntryScope)"
                                    >
                                        Exclude Entry Scope
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={excludeEntryScope}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="containerScope"
                                    controlId="containerScope"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={2}
                                        title="Specifies which branch the plug-in searches for the groups to which the user belongs. It only updates groups that are under the specified container branch, and leaves all other groups not updated (nsslapd-pluginContainerScope)"
                                    >
                                        Container Scope
                                    </Col>
                                    <Col sm={6}>
                                        <FormControl
                                            type="text"
                                            value={containerScope}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
                </PluginBasicConfig>
            </div>
        );
    }
}

ReferentialIntegrity.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

ReferentialIntegrity.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default ReferentialIntegrity;
