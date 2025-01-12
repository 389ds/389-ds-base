import cockpit from "cockpit";
import React from "react";
import {
	Button,
	Form,
	Grid,
	GridItem,
	Modal,
	ModalVariant,
	TextInput,
	ValidatedOptions
} from '@patternfly/react-core';
import {
	Select,
	SelectVariant,
	SelectOption
} from '@patternfly/react-core/deprecated';
import { LinkedAttributesTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, valid_dn } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

const _ = cockpit.gettext;

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
        this.handleLinkTypeSelect = (event, selection) => {
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
        this.handleLinkTypeToggle = (_event, isLinkTypeOpen) => {
            this.setState({
                isLinkTypeOpen
            });
        };
        this.handleLinkTypeClear = () => {
            this.setState({
                linkType: [],
                isLinkTypeOpen: false
            }, () => { this.validate() });
        };

        // Managed Type
        this.handleManagedTypeSelect = (event, selection) => {
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
        this.handleManagedTypeToggle = (_event, isManagedTypeOpen) => {
            this.setState({
                isManagedTypeOpen
            });
        };
        this.handleManagedTypeClear = () => {
            this.setState({
                managedType: [],
                isManagedTypeOpen: false
            }, () => { this.validate() });
        };

        this.onFieldChange = this.onFieldChange.bind(this);
        this.loadConfigs = this.loadConfigs.bind(this);
        this.showEditConfigModal = this.showEditConfigModal.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
        this.handleShowAddConfigModal = this.handleShowAddConfigModal.bind(this);
        this.handleCloseModal = this.handleCloseModal.bind(this);
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

        if (this.state.configName === "") {
            errObj.configName = true;
            all_good = false;
        }
        if (this.state.linkScope === "" || !valid_dn(this.state.linkScope)) {
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

    onFieldChange(e) {
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
                        tableKey,
                        firstLoad: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err !== 0) {
                        console.log("loadConfigs failed", errMsg.desc);
                    }
                });
    }

    showEditConfigModal(rowData) {
        this.openModal(rowData);
    }

    handleShowAddConfigModal(rowData) {
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

    handleCloseModal() {
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
            linkScope || action === "add" ? linkScope : "delete"
        ];

        cmd = [...cmd, "--link-type"];
        if (linkType.length !== 0) {
            cmd = [...cmd, linkType[0]];
        } else if (action === "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--managed-type"];
        if (managedType.length !== 0) {
            cmd = [...cmd, managedType[0]];
        } else if (action === "add") {
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
                        cockpit.format(_("The $0 operation was successfully done on \"$1\" entry"), action, configName)
                    );
                    this.loadConfigs();
                    this.handleCloseModal();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry $0 operation - $1"), action, errMsg.desc)
                    );
                    this.loadConfigs();
                    this.handleCloseModal();
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
                    this.handleCloseModal();
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

        const title = cockpit.format(_("Linked Attributes Plugin Config Entry"), (newEntry ? _("Add") : _("Edit")));
        let saveBtnName = (newEntry ? _("Add") : _("Save")) + _(" Config");
        const extraPrimaryProps = {};
        if (saving) {
            if (newEntry) {
                saveBtnName = _("Adding Config ...");
            } else {
                saveBtnName = _("Saving Config ...");
            }
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }
        return (
            <div className={saving || firstLoad ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    title={title}
                    isOpen={configEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.handleCloseModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            onClick={newEntry ? this.addConfig : this.editConfig}
                            isDisabled={saveBtnDisabled || saving}
                            isLoading={saving}
                            spinnerAriaValueText={saving ? _("Saving") : undefined}
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
                        <Grid title={_("The Linked Attributes configuration name")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Config Name")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={configName}
                                    type="text"
                                    id="configName"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="configName"
                                    onChange={(e, str) => {
                                        this.onFieldChange(e);
                                    }}
                                    validated={error.configName ? ValidatedOptions.error : ValidatedOptions.default}
                                    isDisabled={!newEntry}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Sets the attribute that is managed manually by administrators (linkType)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Link Type")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={(event, isOpen) => this.handleLinkTypeToggle(event, isOpen)}
                                    onSelect={this.handleLinkTypeSelect}
                                    onClear={this.handleLinkTypeClear}
                                    selections={linkType}
                                    isOpen={this.state.isLinkTypeOpen}
                                    aria-labelledby="typeAhead-link-type"
                                    placeholderText={_("Type an attribute...")}
                                    noResultsFoundText={_("There are no matching entries")}
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
                        <Grid title={_("Sets the attribute that is created dynamically by the plugin (managedType)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Managed Type")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={(event, isOpen) => this.handleManagedTypeToggle(event, isOpen)}
                                    onSelect={this.handleManagedTypeSelect}
                                    onClear={this.handleManagedTypeClear}
                                    selections={managedType}
                                    isOpen={this.state.isManagedTypeOpen}
                                    placeholderText={_("Type an attribute...")}
                                    aria-labelledby="typeAhead-managed-type"
                                    noResultsFoundText={_("There are no matching entries")}
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
                        <Grid title={_("Sets the base DN that restricts the plugin to a specific part of the directory tree (linkScope)")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Link Scope")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={linkScope}
                                    type="text"
                                    id="linkScope"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="linkScope"
                                    onChange={(e, str) => {
                                        this.onFieldChange(e);
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
                    handleChange={this.onFieldChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete Linked Attribute Configuration")}
                    mMsg={_("Are you sure you want to delete this configuration?")}
                    mSpinningMsg={_("Deleting Configuration...")}
                    mBtnName={_("Delete Configuration")}
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
