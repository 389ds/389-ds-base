import cockpit from "cockpit";
import React from "react";
import {
    noop,
    FormGroup,
    FormControl,
    Row,
    Col,
    Form,
    ControlLabel,
} from "patternfly-react";
import {
    Checkbox,
    Select,
    SelectVariant,
    SelectOption,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd } from "../tools.jsx";

class RetroChangelog extends React.Component {
    componentDidMount(prevProps) {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.getAttributes();
                this.updateFields();
            }
        }
    }

    componentDidUpdate(prevProps) {
        if (this.props.rows !== prevProps.rows) {
            this.updateFields();
        }
    }

    constructor(props) {
        super(props);

        this.state = {
            firstLoad: true,
            isReplicated: false,
            attribute: [],
            directory: "",
            maxAge: "",
            excludeSuffix: "",
            attributes: [],

            isAttributeOpen: false
        };

        // Attribute
        this.onAttributeSelect = (event, selection) => {
            if (this.state.attribute.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        attribute: prevState.attribute.filter((item) => item !== selection)
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({ attribute: [...prevState.attribute, selection] }),
                );
            }
        };
        this.onAttributeToggle = isAttributeOpen => {
            this.setState({
                isAttributeOpen
            });
        };
        this.onAttributeClear = () => {
            this.setState({
                attribute: [],
                isAttributeOpen: false
            });
        };

        this.updateFields = this.updateFields.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleCheckboxChange = this.handleCheckboxChange.bind(this);
        this.onSelectToggle = this.onSelectToggle.bind(this);
        this.onSelectClear = this.onSelectClear.bind(this);
    }

    handleCheckboxChange(checked, e) {
        this.setState({
            [e.target.id]: checked
        });
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    onSelectToggle = (isExpanded, toggleId) => {
        this.setState({
            [toggleId]: isExpanded
        });
    }

    onSelectClear = item => event => {
        this.setState({
            attribute: [],
            isRetroClAddAttrSelectOpen: false
        });
    }

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "Retro Changelog Plugin");

            this.setState({
                isReplicated: !(
                    pluginRow["isReplicated"] === undefined ||
                    pluginRow["isReplicated"][0] == "FALSE"
                ),
                attribute:
                    pluginRow["nsslapd-attribute"] === undefined
                        ? []
                        : [pluginRow["nsslapd-attribute"][0]],
                directory:
                    pluginRow["nsslapd-changelogdir"] === undefined
                        ? ""
                        : pluginRow["nsslapd-changelogdir"][0],
                maxAge:
                    pluginRow["nsslapd-changelogmaxage"] === undefined
                        ? ""
                        : pluginRow["nsslapd-changelogmaxage"][0],
                excludeSuffix:
                    pluginRow["nsslapd-exclude-suffix"] === undefined
                        ? ""
                        : pluginRow["nsslapd-exclude-suffix"][0]
            });
        }
    }

    getAttributes() {
        this.setState({ firstLoad: false });
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
                        attributes: attrs
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification("error", `Failed to get attributes - ${errMsg.desc}`);
                });
    }

    render() {
        const {
            isReplicated,
            attribute,
            directory,
            maxAge,
            excludeSuffix,
            attributes
        } = this.state;

        let specificPluginCMD = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "retro-changelog",
            "set",
            "--is-replicated",
            isReplicated ? "TRUE" : "FALSE",
            "--attribute",
            attribute.length != 0 ? attribute[0] : "delete",
            "--directory",
            directory || "delete",
            "--max-age",
            maxAge || "delete",
            "--exclude-suffix",
            excludeSuffix || "delete"
        ];

        return (
            <div>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Retro Changelog Plugin"
                    pluginName="Retro Changelog"
                    cmdName="retro-changelog"
                    specificPluginCMD={specificPluginCMD}
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup key="attribute" controlId="attribute">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="Specifies another Directory Server attribute which must be included in the retro changelog entries (nsslapd-attribute)"
                                    >
                                        Attribute
                                    </Col>
                                    <Col sm={7}>
                                        <Select
                                            variant={SelectVariant.typeahead}
                                            typeAheadAriaLabel="Type an attribute"
                                            onToggle={this.onAttributeToggle}
                                            onSelect={this.onAttributeSelect}
                                            onClear={this.onAttributeClear}
                                            selections={attribute}
                                            isOpen={this.state.isAttributeOpen}
                                            aria-labelledby="typeAhead-attribute"
                                            placeholderText="Type an attribute..."
                                            noResultsFoundText="There are no matching entries"
                                            >
                                            {attributes.map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                            ))}
                                        </Select>
                                    </Col>
                                </FormGroup>
                                <FormGroup key="directory" controlId="directory">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="Specifies the name of the directory in which the changelog database is created the first time the plug-in is run"
                                    >
                                        Directory
                                    </Col>
                                    <Col sm={7}>
                                        <FormControl
                                            type="text"
                                            value={directory}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="maxAge" controlId="maxAge">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="This attribute specifies the maximum age of any entry in the changelog (nsslapd-changelogmaxage)"
                                    >
                                        Max Age
                                    </Col>
                                    <Col sm={7}>
                                        <FormControl
                                            type="text"
                                            value={maxAge}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup key="excludeSuffix" controlId="excludeSuffix">
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="This attribute specifies the suffix which will be excluded from the scope of the plugin (nsslapd-exclude-suffix)"
                                    >
                                        Exclude Suffix
                                    </Col>
                                    <Col sm={5}>
                                        <FormControl
                                            type="text"
                                            value={excludeSuffix}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                    <Col sm={2}>
                                        <Checkbox
                                            id="isReplicated"
                                            isChecked={isReplicated}
                                            onChange={this.handleCheckboxChange}
                                            title="Sets a flag to indicate on a change in the changelog whether the change is newly made on that server or whether it was replicated over from another server (isReplicated)"
                                            label="Is Replicated"
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

RetroChangelog.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

RetroChangelog.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default RetroChangelog;
