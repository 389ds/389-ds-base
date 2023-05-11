import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Checkbox,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Select,
    SelectVariant,
    SelectOption,
    TextInput,
    Switch,
    ValidatedOptions,
} from "@patternfly/react-core";
import { AttrUniqConfigTable } from "./pluginTables.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";

const _ = cockpit.gettext;

class AttributeUniqueness extends React.Component {
    componentDidMount() {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.loadConfigs();
            }
        }
    }

    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            configRows: [],
            modalChecked: false,
            modalSpinning: false,
            tableKey: 0,
            saveBtnDisabled: true,
            error: {},

            configName: "",
            configEnabled: false,
            attrNames: [],
            subtrees: [],
            subtreesOptions: [],
            acrossAllSubtrees: false,
            topEntryOc: "",
            subtreeEnriesOc: "",
            _configName: "",
            _configEnabled: false,
            _attrNames: [],
            _subtrees: [],
            _subtreesOptions: [],
            _acrossAllSubtrees: false,
            _topEntryOc: "",
            _subtreeEnriesOc: "",

            newEntry: false,
            showConfigModal: false,
            showConfirmDelete: false,

            isAttributeNameOpen: false,
            isSubtreesOpen: false,
        };

        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.onChange = this.onChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleTypeaheadChange = this.handleTypeaheadChange.bind(this);
        this.loadConfigs = this.loadConfigs.bind(this);
        this.showEditConfigModal = this.showEditConfigModal.bind(this);
        this.handleShowAddConfigModal = this.handleShowAddConfigModal.bind(this);
        this.handleCloseModal = this.handleCloseModal.bind(this);
        this.openModal = this.openModal.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);
        this.validateConfig = this.validateConfig.bind(this);

        // Attribute Name
        this.handleAttributeNameSelect = (event, selection) => {
            if (this.state.attrNames.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        attrNames: prevState.attrNames.filter((item) => item !== selection),
                        isAttributeNameOpen: false
                    }), () => { this.validateConfig() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        attrNames: [...prevState.attrNames, selection],
                        isAttributeNameOpen: false
                    }), () => { this.validateConfig() }
                );
            }
        };
        this.handleAttributeNameToggle = isAttributeNameOpen => {
            this.setState({
                isAttributeNameOpen
            }, () => { this.validateConfig() });
        };
        this.handleAttributeNameClear = () => {
            this.setState({
                attrNames: [],
                isAttributeNameOpen: false
            }, () => { this.validateConfig() });
        };

        // Subtrees
        this.handleSubtreesSelect = (event, selection) => {
            if (this.state.subtrees.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        subtrees: prevState.subtrees.filter((item) => item !== selection),
                        isAttributeNameOpen: false
                    }), () => { this.validateConfig() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        subtrees: [...prevState.subtrees, selection],
                        isAttributeNameOpen: false
                    }), () => { this.validateConfig() }
                );
            }
        };
        this.handleSubtreesToggle = isSubtreesOpen => {
            this.setState({
                isSubtreesOpen
            }, () => { this.validateConfig() });
        };
        this.handleSubtreesClear = () => {
            this.setState({
                subtrees: [],
                isSubtreesOpen: false
            }, () => { this.validateConfig() });
        };
        this.handleSubtreesCreateOption = newValue => {
            if (!this.state.subtreesOptions.includes(newValue)) {
                this.setState({
                    subtreesOptions: [...this.state.subtreesOptions, newValue],
                    isSubtreesOpen: false
                }, () => { this.validateConfig() });
            }
        };
    }

    validateConfig() {
        const errObj = {};
        let all_good = true;

        // Must have a subtree and attribute set
        for (const attrList of ['attrNames', 'subtrees']) {
            if (this.state[attrList].length === 0) {
                errObj[attrList] = true;
                all_good = false;
            }
        }

        // Validate the subtree dn's
        for (const dn of this.state.subtrees) {
            if (!valid_dn(dn)) {
                errObj.subtrees = true;
                all_good = false;
                break;
            }
        }

        if (this.state.configName === "") {
            errObj.configName = true;
            all_good = false;
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrLists = [
                'subtrees', 'attrNames'
            ];
            for (const check_attr of attrLists) {
                if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                    all_good = true;
                    break;
                }
            }
            const configAttrs = [
                'acrossAllSubtrees', 'topEntryOc', 'subtreeEnriesOc',
                'configEnabled'
            ];
            for (const check_attr of configAttrs) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabled: !all_good,
            error: errObj
        });
    }

    handleSwitchChange(value) {
        this.setState({
            configEnabled: value
        }, () => { this.validateConfig() });
    }

    onChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    handleFieldChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validateConfig() });
    }

    handleTypeaheadChange(values) {
        // When typaheads allow new values, an object is returned
        // instead of string.  Grab the "label" in this case
        const new_values = [];
        for (let val of values) {
            if (val !== "") {
                if (typeof val === 'object') {
                    val = val.label;
                }
                new_values.push(val);
            }
        }
        this.setState({
            subtrees: new_values
        });
    }

    loadConfigs() {
        this.setState({
            firstLoad: false
        });
        this.props.pluginListHandler();
        // Get all the attributes and matching rules now
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "attr-uniq",
            "list"
        ];
        log_cmd("loadConfigs", "Get Attribute Uniqueness Plugin configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    const tableKey = this.state.tableKey + 1;
                    this.setState({
                        configRows: myObject.items.map(item => item.attrs),
                        tableKey
                    });
                })
                .fail(err => {
                    if (err !== 0) {
                        const errMsg = JSON.parse(err);
                        console.log("loadConfigs failed", errMsg.desc);
                    }
                });
    }

    showEditConfigModal(name) {
        this.openModal(name);
    }

    handleShowAddConfigModal(rowData) {
        this.openModal();
    }

    openModal(name) {
        if (!name) {
            this.setState({
                configEntryModalShow: true,
                newEntry: true,
                configEnabled: false,
                configName: "",
                attrNames: [],
                subtrees: [],
                acrossAllSubtrees: false,
                topEntryOc: "",
                subtreeEnriesOc: ""
            });
        } else {
            let configAttrNamesList = [];
            let configSubtreesList = [];
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "attr-uniq",
                "show",
                name
            ];

            log_cmd("openModal", "Fetch the Attribute Uniqueness Plugin config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const configEntry = JSON.parse(content).attrs;
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: false,
                            configName: configEntry.cn === undefined ? "" : configEntry.cn[0],
                            configEnabled: !(
                                configEntry["nsslapd-pluginenabled"] === undefined ||
                            configEntry["nsslapd-pluginenabled"][0] === "off"
                            ),
                            acrossAllSubtrees: !(
                                configEntry["uniqueness-across-all-subtrees"] === undefined ||
                            configEntry["uniqueness-across-all-subtrees"][0] === "off"
                            ),
                            topEntryOc:
                            configEntry["uniqueness-top-entry-oc"] === undefined
                                ? ""
                                : configEntry["uniqueness-top-entry-oc"][0],
                            subtreeEnriesOc:
                            configEntry["uniqueness-subtree-entries-oc"] === undefined
                                ? ""
                                : configEntry["uniqueness-subtree-entries-oc"][0],

                            _configEnabled: !(
                                configEntry["nsslapd-pluginenabled"] === undefined ||
                            configEntry["nsslapd-pluginenabled"][0] === "off"
                            ),
                            _acrossAllSubtrees: !(
                                configEntry["uniqueness-across-all-subtrees"] === undefined ||
                            configEntry["uniqueness-across-all-subtrees"][0] === "off"
                            ),
                            _topEntryOc:
                            configEntry["uniqueness-top-entry-oc"] === undefined
                                ? ""
                                : configEntry["uniqueness-top-entry-oc"][0],
                            _subtreeEnriesOc:
                            configEntry["uniqueness-subtree-entries-oc"] === undefined
                                ? ""
                                : configEntry["uniqueness-subtree-entries-oc"][0]
                        });

                        if (configEntry["uniqueness-attribute-name"] === undefined) {
                            this.setState({ attrNames: [], _attrNames: [] });
                        } else {
                            for (const value of configEntry["uniqueness-attribute-name"]) {
                                configAttrNamesList = [...configAttrNamesList, value];
                            }
                            this.setState({ attrNames: configAttrNamesList, _attrNames: [...configAttrNamesList] });
                        }
                        if (configEntry["uniqueness-subtrees"] === undefined) {
                            this.setState({ subtrees: [], _subtrees: [] });
                        } else {
                            for (const value of configEntry["uniqueness-subtrees"]) {
                                configSubtreesList = [...configSubtreesList, value];
                            }
                            this.setState({ subtrees: configSubtreesList, _subtrees: [...configSubtreesList] });
                        }
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configName: "",
                            attrNames: [],
                            subtrees: [],
                            acrossAllSubtrees: false,
                            topEntryOc: "",
                            subtreeEnriesOc: "",
                            configEnabled: false,
                        });
                    });
        }
    }

    handleCloseModal() {
        this.setState({ configEntryModalShow: false });
    }

    cmdOperation(action) {
        const {
            configName,
            configEnabled,
            attrNames,
            subtrees,
            acrossAllSubtrees,
            topEntryOc,
            subtreeEnriesOc
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "attr-uniq",
            action,
            configName,
            "--enabled",
            configEnabled ? "on" : "off",
            "--across-all-subtrees",
            acrossAllSubtrees ? "on" : "off"
        ];

        if (subtrees.length === 0 && subtreeEnriesOc.length === 0) {
            // There mustr a subtree or entry OC sets
            this.props.addNotification(
                "error",
                _("There must be at least one Subtree or Subtree Entries OC set")
            );
            return;
        }

        this.setState({
            saving: true
        });

        // Delete attributes if the user set an empty value to the field
        if (!(action === "add" && attrNames.length === 0)) {
            cmd = [...cmd, "--attr-name"];
            if (attrNames.length !== 0) {
                for (const value of attrNames) {
                    cmd = [...cmd, value];
                }
            } else if (action === "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }
        }

        if (!(action === "add" && subtrees.length === 0)) {
            cmd = [...cmd, "--subtree"];
            if (subtrees.length !== 0) {
                for (const value of subtrees) {
                    cmd = [...cmd, value];
                }
            } else if (action === "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }
        }

        cmd = [...cmd, "--top-entry-oc"];
        if (topEntryOc.length !== 0) {
            cmd = [...cmd, topEntryOc];
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--subtree-entries-oc"];
        if (subtreeEnriesOc.length !== 0) {
            cmd = [...cmd, subtreeEnriesOc];
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        log_cmd(
            "attrUniqOperation",
            `Do the ${action} operation on the Attribute Uniqueness Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("attrUniqOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        cockpit.format(_("The $0 operation was successfully done on \"$1\" entry"), action, configName)
                    );
                    this.loadConfigs();
                    this.handleCloseModal();
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry $0 operation - $1"), action, errMsg.desc)
                    );
                    this.loadConfigs();
                    this.handleCloseModal();
                    this.setState({
                        saving: false
                    });
                });
    }

    showConfirmDelete (name) {
        this.setState({
            showConfirmDelete: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: name
        });
    }

    closeConfirmDelete () {
        this.setState({
            showConfirmDelete: false,
            modalChecked: false,
            modalSpinning: false,
            deleteName: ""
        });
    }

    deleteConfig() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "attr-uniq",
            "delete",
            this.state.deleteName
        ];

        this.setState({
            modalSpinning: true
        });

        log_cmd("deleteConfig", "Delete the Attribute Uniqueness Plugin config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        cockpit.format(_("Config entry $0 was successfully deleted"), this.state.deleteName)
                    );
                    this.loadConfigs();
                    this.handleCloseModal();
                    this.closeConfirmDelete();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry removal operation - $0"), errMsg.desc)
                    );
                    this.loadConfigs();
                    this.closeConfirmDelete();
                    this.handleCloseModal();
                });
    }

    addConfig() {
        this.cmdOperation("add");
    }

    editConfig() {
        this.cmdOperation("set");
    }

    render() {
        const {
            configEntryModalShow,
            configName,
            attrNames,
            subtrees,
            acrossAllSubtrees,
            configEnabled,
            topEntryOc,
            subtreeEnriesOc,
            newEntry,
        } = this.state;

        const title = cockpit.format(_("$0 Attribute Uniqueness Plugin Config Entry"), (newEntry ? _("Add") : _("Edit")));
        let saveBtnName = (newEntry ? _("Add") : _("Save")) + _(" Config");
        const extraPrimaryProps = {};
        if (this.state.saving) {
            if (newEntry) {
                saveBtnName = _("Adding Config ...");
            } else {
                saveBtnName = _("Saving Config ...");
            }
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }
        return (
            <div className={this.state.saving || this.state.modalSpinning ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    title={title}
                    aria-labelledby="ds-modal"
                    isOpen={configEntryModalShow}
                    onClose={this.handleCloseModal}
                    actions={[
                        <Button
                            className="ds-margin-top"
                            key="confirm"
                            variant="primary"
                            onClick={newEntry ? this.addConfig : this.editConfig}
                            isDisabled={this.state.saveBtnDisabled || this.state.saving}
                            isLoading={this.state.saving}
                            spinnerAriaValueText={this.state.saving ? _("Saving") : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleCloseModal}>
                            {_("Cancel")}
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid
                            className="ds-margin-top"
                            title={_("Sets the name of the plug-in configuration record. (cn) You can use any string, but \"attribute_name Attribute Uniqueness\" is recommended.")}
                        >
                            <GridItem span={3} className="ds-label">
                                {_("Config Name")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={configName}
                                    type="text"
                                    id="configName"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="configName"
                                    isDisabled={!newEntry}
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={this.state.error.configName || this.state.configName === "" ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Sets the name of the attribute whose values must be unique. This attribute is multi-valued. (uniqueness-attribute-name)")}>
                            <GridItem span={3} className="ds-label">
                                {_("Attribute Names")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an attribute"
                                    onToggle={this.handleAttributeNameToggle}
                                    onSelect={this.handleAttributeNameSelect}
                                    onClear={this.handleAttributeNameClear}
                                    selections={attrNames}
                                    isOpen={this.state.isAttributeNameOpen}
                                    aria-labelledby="typeAhead-attr-name"
                                    placeholderText={_("Type an attribute name...")}
                                    noResultsFoundText={_("There are no matching attributes")}
                                    validated={this.state.error.attrNames ? "error" : "default"}
                                >
                                    {this.props.attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Sets the DN under which the plug-in checks for uniqueness of the attributes value. This attribute is multi-valued (uniqueness-subtrees)")}>
                            <GridItem span={3} className="ds-label">
                                {_("Subtrees")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a subtree DN"
                                    onToggle={this.handleSubtreesToggle}
                                    onSelect={this.handleSubtreesSelect}
                                    onClear={this.handleSubtreesClear}
                                    selections={subtrees}
                                    isOpen={this.state.isSubtreesOpen}
                                    aria-labelledby="typeAhead-subtrees"
                                    placeholderText={_("Type a subtree DN...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                    onCreateOption={this.handleSubtreesCreateOption}
                                    validated={this.state.error.subtrees ? "error" : "default"}
                                >
                                    {[""].map((dn, index) => (
                                        <SelectOption
                                            key={index}
                                            value={dn}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Verifies that the value of the attribute set in uniqueness-attribute-name is unique in this subtree (uniqueness-top-entry-oc)")}>
                            <GridItem span={3} className="ds-label">
                                {_("Top Entry OC")}
                            </GridItem>
                            <GridItem span={6}>
                                <FormSelect
                                    id="topEntryOc"
                                    value={topEntryOc}
                                    onChange={(value, event) => {
                                        this.handleFieldChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="no-setting" value="" label="-" />
                                    {this.props.objectClasses.map((attr, index) => (
                                        <FormSelectOption key={attr} value={attr} label={attr} />
                                    ))}
                                </FormSelect>
                            </GridItem>
                            <GridItem sm={3}>
                                <Checkbox
                                    id="acrossAllSubtrees"
                                    className="ds-left-margin"
                                    isChecked={acrossAllSubtrees}
                                    title={_("If enabled (on), the plug-in checks that the attribute is unique across all subtrees set. If you set the attribute to off, uniqueness is only enforced within the subtree of the updated entry (uniqueness-across-all-subtrees)")}
                                    onChange={(checked, e) => { this.handleFieldChange(e) }}
                                    label={_("Across All Subtrees")}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Verifies if an attribute is unique, if the entry contains the objectclass set in this parameter (uniqueness-subtree-entries-oc)")}>
                            <GridItem span={3} className="ds-label">
                                {_("Subtree Entry's OC")}
                            </GridItem>
                            <GridItem span={6}>
                                <FormSelect
                                    id="subtreeEnriesOc"
                                    value={subtreeEnriesOc}
                                    onChange={(value, event) => {
                                        this.handleFieldChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="no_setting" value="" label="-" />
                                    {this.props.objectClasses.map((attr, index) => (
                                        <FormSelectOption key={index} value={attr} label={attr} />
                                    ))}
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-bottom" title={_("Identifies whether or not the config is enabled.")}>
                            <GridItem span={3} className="ds-label">
                                {_("Enable config")}
                            </GridItem>
                            <GridItem span={9}>
                                <Switch
                                    id="configEnabled"
                                    label={_("Configuration is enabled")}
                                    labelOff={_("Configuration is disabled")}
                                    isChecked={configEnabled}
                                    onChange={this.handleSwitchChange}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>

                <PluginBasicConfig
                    removeSwitch
                    rows={this.props.rows}
                    key={this.state.configRows}
                    serverId={this.props.serverId}
                    cn="attribute uniqueness"
                    pluginName="Attribute Uniqueness"
                    cmdName="attr-uniq"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Grid>
                        <GridItem span={12}>
                            <AttrUniqConfigTable
                                key={this.state.tableKey}
                                rows={this.state.configRows}
                                editConfig={this.showEditConfigModal}
                                deleteConfig={this.showConfirmDelete}
                            />
                            <Button
                                key="add-config"
                                variant="primary"
                                onClick={this.handleShowAddConfigModal}
                            >
                                {_("Add Config")}
                            </Button>
                        </GridItem>
                    </Grid>
                </PluginBasicConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.onChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete Attribute Uniqueness Configuration")}
                    mMsg={_("Are you sure you want to delete this configuration?")}
                    mSpinningMsg={_("Deleting attribute uniqueness configuration...")}
                    mBtnName={_("Delete Configuration")}
                />
            </div>
        );
    }
}

AttributeUniqueness.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func,
    objectClasses: PropTypes.array,
};

AttributeUniqueness.defaultProps = {
    rows: [],
    serverId: "",
    objectClasses: [],
};

export default AttributeUniqueness;
