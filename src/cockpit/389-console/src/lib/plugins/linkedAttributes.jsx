import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Form,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Select,
    SelectVariant,
    SelectOption,
    TextInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import { LinkedAttributesTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, valid_dn } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

class LinkedAttributes extends React.Component {
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
            tableKey: 1,
            error: {},
            saveBtnDisabled: true,

            configName: "",
            linkType: [],
            managedType: [],
            linkScope: "",

            newEntry: false,
            showConfigModal: false,
            showConfirmDelete: false,
            modalChecked: false,
            modalSpinning: false,

            isLinkTypeOpen: false,
            isManagedTypeOpen: false,
        };

        // Link Type
        this.onLinkTypeSelect = (event, selection) => {
            if (this.state.linkType.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        linkType: prevState.linkType.filter((item) => item !== selection),
                        isLinkTypeOpen: false
                    }), () => { this.validate() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        linkType: [...prevState.linkType, selection],
                        isLinkTypeOpen: false
                    }), () => { this.validate() }
                );
            }
        };
        this.onLinkTypeToggle = isLinkTypeOpen => {
            this.setState({
                isLinkTypeOpen
            });
        };
        this.onLinkTypeClear = () => {
            this.setState({
                linkType: [],
                isLinkTypeOpen: false
            }, () => { this.validate() });
        };

        // Managed Type
        this.onManagedTypeSelect = (event, selection) => {
            if (this.state.managedType.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        managedType: prevState.managedType.filter((item) => item !== selection),
                        isManagedTypeOpen: false
                    }), () => { this.validate() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        managedType: [...prevState.managedType, selection],
                        isManagedTypeOpen: false
                    }), () => { this.validate() }
                );
            }
        };
        this.onManagedTypeToggle = isManagedTypeOpen => {
            this.setState({
                isManagedTypeOpen
            });
        };
        this.onManagedTypeClear = () => {
            this.setState({
                managedType: [],
                isManagedTypeOpen: false
            }, () => { this.validate() });
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.loadConfigs = this.loadConfigs.bind(this);
        this.showEditConfigModal = this.showEditConfigModal.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
        this.showAddConfigModal = this.showAddConfigModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.openModal = this.openModal.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
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

    validate () {
        const errObj = {};
        let all_good = true;

        if (this.state.configName == "") {
            errObj.configName = true;
            all_good = false;
        }
        if (this.state.linkScope == "" || !valid_dn(this.state.linkScope)) {
            errObj.linkScope = true;
            all_good = false;
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrs = [
                'linkScope', 'managedType', 'linkType', 'configName'
            ];
            for (const check_attr of attrs) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
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

    handleFieldChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validate() });
    }

    loadConfigs() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "linked-attr",
            "list"
        ];
        log_cmd("loadConfigs", "Get Linked Attributes Plugin configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    const tableKey = this.state.tableKey + 1;
                    this.setState({
                        configRows: myObject.items.map(item => item.attrs),
                        tableKey: tableKey,
                        firstLoad: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadConfigs failed", errMsg.desc);
                    }
                });
    }

    showEditConfigModal(rowData) {
        this.openModal(rowData);
    }

    showAddConfigModal(rowData) {
        this.openModal();
    }

    openModal(name) {
        if (!name) {
            this.setState({
                configEntryModalShow: true,
                newEntry: true,
                configName: "",
                linkType: [],
                managedType: [],
                linkScope: "",
                saveBtnDisabled: true,
            });
        } else {
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "linked-attr",
                "config",
                name,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openModal", "Fetch the Linked Attributes Plugin config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const configEntry = JSON.parse(content).attrs;
                        this.setState({
                            configEntryModalShow: true,
                            saveBtnDisabled: true,
                            newEntry: false,
                            configName: configEntry.cn === undefined ? "" : configEntry.cn[0],
                            linkType:
                            configEntry.linktype === undefined
                                ? []
                                : [configEntry.linktype[0]],
                            managedType:
                            configEntry.managedtype === undefined
                                ? []
                                : [configEntry.managedtype[0]],
                            linkScope:
                            configEntry.linkscope === undefined
                                ? ""
                                : configEntry.linkscope[0]
                        });

                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configName: "",
                            linkType: [],
                            managedType: [],
                            linkScope: "",
                            saveBtnDisabled: true,
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    closeModal() {
        this.setState({ configEntryModalShow: false });
    }

    cmdOperation(action) {
        const { configName, linkType, managedType, linkScope } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "linked-attr",
            "config",
            configName,
            action,
            "--link-scope",
            linkScope || action == "add" ? linkScope : "delete"
        ];

        cmd = [...cmd, "--link-type"];
        if (linkType.length != 0) {
            cmd = [...cmd, linkType[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--managed-type"];
        if (managedType.length != 0) {
            cmd = [...cmd, managedType[0]];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        this.props.toggleLoadingHandler();
        log_cmd(
            "linkedAttributesOperation",
            `Do the ${action} operation on the Linked Attributes Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("linkedAttributesOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        `The ${action} operation was successfully done on "${configName}" entry`
                    );
                    this.loadConfigs();
                    this.closeModal();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry ${action} operation - ${errMsg.desc}`
                    );
                    this.loadConfigs();
                    this.closeModal();
                    this.props.toggleLoadingHandler();
                });
    }

    deleteConfig() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "linked-attr",
            "config",
            this.state.deleteName,
            "delete"
        ];

        this.setState({
            modalSpinning: true
        });

        log_cmd("deleteConfig", "Delete the Linked Attributes Plugin config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Config entry ${this.state.deleteName} was successfully deleted`
                    );
                    this.loadConfigs();
                    this.closeModal();
                    this.closeConfirmDelete();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry removal operation - ${errMsg.desc}`
                    );
                    this.loadConfigs();
                    this.closeModal();
                    this.closeConfirmDelete();
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
            linkType,
            managedType,
            linkScope,
            newEntry,
            error,
            saveBtnDisabled,
            saving,
            firstLoad
        } = this.state;

        const title = (newEntry ? "Add" : "Edit") + " Linked Attributes Plugin Config Entry";
        let saveBtnName = (newEntry ? "Add" : "Save") + " Config";
        const extraPrimaryProps = {};
        if (saving) {
            if (newEntry) {
                saveBtnName = "Adding Config ...";
            } else {
                saveBtnName = "Saving Config ...";
            }
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }
        return (
            <div className={saving || firstLoad ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    title={title}
                    isOpen={configEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            onClick={newEntry ? this.addConfig : this.editConfig}
                            isDisabled={saveBtnDisabled || saving}
                            isLoading={saving}
                            spinnerAriaValueText={saving ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title="The Linked Attributes configuration name">
                            <GridItem className="ds-label" span={3}>
                                Config Name
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={configName}
                                    type="text"
                                    id="configName"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="configName"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={error.configName ? ValidatedOptions.error : ValidatedOptions.default}
                                    isDisabled={!newEntry}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Sets the attribute that is managed manually by administrators (linkType)">
                            <GridItem className="ds-label" span={3}>
                                Link Type
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={this.onLinkTypeToggle}
                                    onSelect={this.onLinkTypeSelect}
                                    onClear={this.onLinkTypeClear}
                                    selections={linkType}
                                    isOpen={this.state.isLinkTypeOpen}
                                    aria-labelledby="typeAhead-link-type"
                                    placeholderText="Type an attribute..."
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid title="Sets the attribute that is created dynamically by the plugin (managedType)">
                            <GridItem className="ds-label" span={3}>
                                Managed Type
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={this.onManagedTypeToggle}
                                    onSelect={this.onManagedTypeSelect}
                                    onClear={this.onManagedTypeClear}
                                    selections={managedType}
                                    isOpen={this.state.isManagedTypeOpen}
                                    placeholderText="Type an attribute..."
                                    aria-labelledby="typeAhead-managed-type"
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid title="Sets the base DN that restricts the plugin to a specific part of the directory tree (linkScope)">
                            <GridItem className="ds-label" span={3}>
                                Link Scope
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={linkScope}
                                    type="text"
                                    id="linkScope"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="linkScope"
                                    onChange={(str, e) => {
                                        this.handleFieldChange(e);
                                    }}
                                    validated={error.linkScope ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>

                <PluginBasicConfig
                    rows={this.props.rows}
                    key={this.state.configRows}
                    serverId={this.props.serverId}
                    cn="Linked Attributes"
                    pluginName="Linked Attributes"
                    cmdName="linked-attr"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Grid>
                        <GridItem span={12}>
                            <LinkedAttributesTable
                                rows={this.state.configRows}
                                key={this.state.tableKey}
                                editConfig={this.showEditConfigModal}
                                deleteConfig={this.showConfirmDelete}
                            />
                            <Button
                                key="addconf"
                                variant="primary"
                                onClick={this.showAddConfigModal}
                            >
                                Add Config
                            </Button>
                        </GridItem>
                    </Grid>
                </PluginBasicConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Linked Attribute Configuration"
                    mMsg="Are you sure you want to delete this configuration?"
                    mSpinningMsg="Deleting Configuration..."
                    mBtnName="Delete Configuration"
                />
            </div>
        );
    }
}

LinkedAttributes.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

LinkedAttributes.defaultProps = {
    rows: [],
    serverId: "",
};

export default LinkedAttributes;
