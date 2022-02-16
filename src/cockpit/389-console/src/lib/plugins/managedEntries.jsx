import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Form,
    FormGroup,
    FormSelect,
    FormSelectOption,
    Modal,
    ModalVariant,
    Select,
    SelectOption,
    SelectVariant,
    SimpleList,
    SimpleListItem,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    Tooltip,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions,
} from "@patternfly/react-core";

import { ManagedDefinitionTable, ManagedTemplateTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

class ManagedEntries extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            configArea: "",
            attributes: [],
            activeTabKey: 1,
            tableKey: 1,
            wasActiveList: [],
            saveNotOK: false,
            createNotOK: false,
            loading: false,

            // Templates
            templateRows: [],
            templateOptions: [],
            isRDNOpen: false,
            templateDN: "",
            templateRDNAttr: "",
            templateStaticAttr: [],
            templateMappedAttr: [],
            selectedMappedAttr: "",
            selectedStaticAttr: "",
            staticAttr: "",
            staticValue: "",
            mappedValue: "",
            mappedAttr: "",
            templateAddStaticShow: false,
            templateAddMappedShow: false,
            templateModalShow: false,
            showTempDeleteConfirm: false,
            newTemplateEntry: false,
            showStaticAttrDeleteConfirm: false,
            showMappedAttrDeleteConfirm: false,

            // Definitions
            saveDefOK: false,
            configRows: [],
            configName: "",
            originScope: "",
            originFilter: "",
            managedBase: "",
            managedTemplate: "",
            newDefEntry: false,
            configEntryModalShow: false,
            defCreateDisabled: false,
            showDefDeleteConfirm: false,

            // Validation
            addStaticValueValidated: ValidatedOptions.default,
            addMappedValueValidated: ValidatedOptions.default,
        };

        this.handleNavSelect = (event, tabIndex) => {
            const { wasActiveList } = this.state;
            if (!wasActiveList.includes(tabIndex)) {
                const newList = wasActiveList.concat(tabIndex);
                this.setState({
                    wasActiveList: newList,
                    activeTabKey: tabIndex
                });
            } else {
                this.setState({
                    activeTabKey: tabIndex
                });
            }
        };

        // Template RDN
        this.onRDNSelect = (event, selection) => {
            this.setState({
                templateRDNAttr: selection,
                isRDNOpen: false
            });
        };
        this.onRDNToggle = isRDNOpen => {
            this.setState({
                isRDNOpen
            });
        };
        this.clearRDNSelection = () => {
            this.setState({
                templateRDNAttr: "",
                isRDNOpen: false
            });
        };

        // Template Add static attrs
        this.onStaticSelect = (event, selection) => {
            this.setState({
                staticAttr: selection,
                isStaticOpen: false
            });
        };

        this.onStaticToggle = isStaticOpen => {
            this.setState({
                isStaticOpen
            });
        };

        this.toggleLoading = () => {
            this.setState(prevState => ({
                loading: !prevState.loading,
            }));
        };

        this.clearStaticSelection = () => {
            this.setState({
                staticAttr: [],
                isStaticOpen: false
            });
        };

        // Template Add Mapped attrs
        this.onMappedSelect = (event, selection) => {
            this.setState({
                mappedAttr: selection,
                isMappedOpen: false
            });
        };

        this.onMappedToggle = isMappedOpen => {
            this.setState({
                isMappedOpen
            });
        };

        this.clearMappedSelection = () => {
            this.setState({
                mappedAttr: [],
                isMappedOpen: false
            });
        };

        // Template static attributes simple list selection
        this.onStaticListSelect = (selectedItem, selectedItemProps) => {
            this.setState({
                selectedStaticAttr: selectedItem.current.textContent
            });
        };

        // Template mapped attributes sinmple list selection
        this.onMappedListSelect = (selectedItem, selectedItemProps) => {
            this.setState({
                selectedMappedAttr: selectedItem.current.textContent
            });
        };

        // General
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.updateFields = this.updateFields.bind(this);
        this.loadConfigs = this.loadConfigs.bind(this);
        this.getAttributes = this.getAttributes.bind(this);
        this.handleConfirmChange = this.handleConfirmChange.bind(this);
        // Definition functions
        this.openDefModal = this.openDefModal.bind(this);
        this.closeDefModal = this.closeDefModal.bind(this);
        this.showEditDefModal = this.showEditDefModal.bind(this);
        this.cmdDefOperation = this.cmdDefOperation.bind(this);
        this.showEditDefModal = this.showEditDefModal.bind(this);
        this.showAddDefModal = this.showAddDefModal.bind(this);
        this.deleteDefConfig = this.deleteDefConfig.bind(this);
        this.addDefConfig = this.addDefConfig.bind(this);
        this.editDefConfig = this.editDefConfig.bind(this);
        this.showDefDeleteConfirm = this.showDefDeleteConfirm.bind(this);
        this.closeDefDeleteConfirm = this.closeDefDeleteConfirm.bind(this);
        // Template functions
        this.cmdTemplateOperation = this.cmdTemplateOperation.bind(this);
        this.showEditTempModal = this.showEditTempModal.bind(this);
        this.openTempModal = this.openTempModal.bind(this);
        this.closeTempModal = this.closeTempModal.bind(this);
        this.showAddTempModal = this.showAddTempModal.bind(this);
        this.deleteTemplate = this.deleteTemplate.bind(this);
        this.addTemplate = this.addTemplate.bind(this);
        this.editTemplate = this.editTemplate.bind(this);
        this.openStaticAttrModal = this.openStaticAttrModal.bind(this);
        this.closeStaticAttrModal = this.closeStaticAttrModal.bind(this);
        this.deleteStaticAttribute = this.deleteStaticAttribute.bind(this);
        this.openMappedAttrModal = this.openMappedAttrModal.bind(this);
        this.closeMappedAttrModal = this.closeMappedAttrModal.bind(this);
        this.deleteMappedAttribute = this.deleteMappedAttribute.bind(this);
        this.addStaticAttrToList = this.addStaticAttrToList.bind(this);
        this.addMappedAttrToList = this.addMappedAttrToList.bind(this);
        this.showTempDeleteConfirm = this.showTempDeleteConfirm.bind(this);
        this.closeTempDeleteConfirm = this.closeTempDeleteConfirm.bind(this);
        this.showMappedAttrDeleteConfirm = this.showMappedAttrDeleteConfirm.bind(this);
        this.closeMappedAttrDeleteConfirm = this.closeMappedAttrDeleteConfirm.bind(this);
        this.showStaticAttrDeleteConfirm = this.showStaticAttrDeleteConfirm.bind(this);
        this.closeStaticAttrDeleteConfirm = this.closeStaticAttrDeleteConfirm.bind(this);
    }

    componentDidMount() {
        this.updateFields();
    }

    handleFieldChange(str, e) {
        const value = e.target.value;
        const id = e.target.id;
        this.setState({
            [id]: value
        });
    }

    showTempDeleteConfirm (dn) {
        this.setState({
            templateDN: dn,
            showTempDeleteConfirm: true,
            modalChecked: false,
        });
    }

    closeTempDeleteConfirm () {
        this.setState({
            showTempDeleteConfirm: false
        });
    }

    showDefDeleteConfirm (name) {
        this.setState({
            configName: name,
            showDefDeleteConfirm: true,
            modalChecked: false,
        });
    }

    closeDefDeleteConfirm () {
        this.setState({
            showDefDeleteConfirm: false
        });
    }

    loadConfigs() {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "list",
            "templates"
        ];

        log_cmd("loadConfigs", "Get Managed Entries templates", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    let defCreateDisabled = false;
                    const templateOptions = [];
                    if (myObject.items.length == 0) {
                        // We loaded the templates first to set the definition's
                        // save btn.  You must have templates before you can
                        // create the definition.
                        defCreateDisabled = true;
                    } else {
                        for (const temp of myObject.items) {
                            templateOptions.push({
                                value: temp.dn,
                                label: temp.dn
                            });
                        }
                    }
                    this.setState({
                        showDefDeleteConfirm: false,
                        templateRows: myObject.items.map(item => item.attrs),
                        defCreateDisabled: defCreateDisabled,
                        templateOptions: templateOptions,
                        managedTemplate: templateOptions.length ? templateOptions[0].value : ""
                    });
                    //
                    // Get the Definitions
                    //
                    cmd = [
                        "dsconf",
                        "-j",
                        "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                        "plugin",
                        "managed-entries",
                        "list",
                        "configs"
                    ];
                    log_cmd("loadConfigs", "Get Managed Entries Plugin definitions", cmd);
                    cockpit
                            .spawn(cmd, { superuser: true, err: "message" })
                            .done(content => {
                                const myObject = JSON.parse(content);
                                const tableKey = this.state.tableKey + 1;
                                this.setState({
                                    configRows: myObject.items.map(item => item.attrs),
                                    tableKey: tableKey,
                                    loading: false
                                });
                            })
                            .fail(err => {
                                const errMsg = JSON.parse(err);
                                if (err != 0) {
                                    console.log("loadConfigs failed getting definitions", errMsg.desc);
                                }
                                this.setState({
                                    loading: false
                                });
                            });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadConfigs failed getting templates", errMsg.desc);
                    }
                    this.setState({
                        loading: false
                    });
                });
    }

    showEditTempModal(entrydn) {
        this.openTempModal(entrydn);
    }

    showAddTempModal() {
        this.openTempModal();
    }

    openTempModal(entrydn) {
        this.getAttributes();
        if (entrydn == "" || entrydn === undefined) {
            this.setState({
                newTemplateEntry: true,
                templateDN: "",
                templateRDNAttr: "",
                templateStaticAttr: [],
                templateMappedAttr: [],
                saveTempOK: false,
                templateModalShow: true,
            });
        } else {
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "managed-entries",
                "template",
                entrydn,
                "show"
            ];

            this.toggleLoading();
            log_cmd("openTempModal", "Fetch the Managed Entries template entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const configEntry = JSON.parse(content).attrs;

                        configEntry.meprdnattr === undefined
                            ? configEntry.meprdnattr = ""
                            : configEntry.meprdnattr = configEntry.meprdnattr[0];
                        if (configEntry.mepstaticattr === undefined) {
                            configEntry.mepstaticattr = [];
                        }
                        if (configEntry.mepmappedattr === undefined) {
                            configEntry.mepmappedattr = [];
                        }

                        this.setState({
                            newTemplateEntry: false,
                            templateModalShow: true,
                            saveTempOK: false,
                            templateDN: entrydn,
                            templateRDNAttr: configEntry.meprdnattr,
                            templateStaticAttr: configEntry.mepstaticattr,
                            templateMappedAttr: configEntry.mepmappedattr,
                            // Preserve original Settings
                            _templateDN: entrydn,
                            _templateRDNAttr: configEntry.meprdnattr,
                            _templateStaticAttr: [...configEntry.mepstaticattr],
                            _templateMappedAttr: [...configEntry.mepmappedattr]
                        }, this.toggleLoading());
                    })
                    .fail(_ => {
                        this.setState({
                            newTemplateEntry: true,
                            templateDN: "",
                            templateRDNAttr: "",
                            templateStaticAttr: [],
                            templateMappedAttr: [],
                        }, this.toggleLoading());
                    });
        }
    }

    addStaticAttrToList() {
        // Add the selected static attribute to the state list
        const value = this.state.staticAttr + ": " + this.state.staticValue;
        const values = this.state.templateStaticAttr;
        values.push(value);
        this.setState({
            templateStaticAttr: values,
            templateAddStaticShow: false,
        });
    }

    deleteStaticAttribute () {
        // delete the selected static attribute from the state list
        const attrList = this.state.templateStaticAttr;
        for (let i = 0; i < attrList.length; i++) {
            if (attrList[i] === this.state.selectedStaticAttr) {
                attrList.splice(i, 1);
            }
        }
        this.setState({
            selectedStaticAttr: "",
            templateStaticAttr: attrList,
            showStaticAttrDeleteConfirm: false,
            modalChecked: false,
        });
    }

    addMappedAttrToList() {
        // Add the selected mapped attribute to the state list
        const values = this.state.templateMappedAttr;
        values.push(this.state.mappedAttr + ": " + this.state.mappedValue);
        this.setState(prevState => ({
            templateMappedAttr: values,
            templateAddMappedShow: false,
        }));
    }

    showMappedAttrDeleteConfirm() {
        if (this.state.selectedMappedAttr != "") {
            this.setState({
                showMappedAttrDeleteConfirm: true,
                modalChecked: false
            });
        }
    }

    closeMappedAttrDeleteConfirm() {
        this.setState({
            showMappedAttrDeleteConfirm: false,
        });
    }

    showStaticAttrDeleteConfirm() {
        if (this.state.selectedStaticAttr != "") {
            this.setState({
                showStaticAttrDeleteConfirm: true,
                modalChecked: false
            });
        }
    }

    closeStaticAttrDeleteConfirm() {
        this.setState({
            showStaticAttrDeleteConfirm: false,
        });
    }

    deleteMappedAttribute () {
        // delete the selected static attribute from the state list
        const attrList = this.state.templateMappedAttr;
        for (let i = 0; i < attrList.length; i++) {
            if (attrList[i] === this.state.selectedMappedAttr) {
                attrList.splice(i, 1);
            }
        }
        this.setState({
            selectedMappedAttr: "",
            templateMappedAttr: attrList,
            showMappedAttrDeleteConfirm: false,
            modalChecked: false,
        });
    }

    closeTempModal() {
        this.setState({ templateModalShow: false });
    }

    openMappedAttrModal () {
        this.setState({
            templateAddMappedShow: true,
            modalChecked: false,
            modalSpinning: false,
            mappedValue: "",
            mappedAttr: ""
        });
    }

    closeMappedAttrModal() {
        this.setState({
            templateAddMappedShow: false,
        });
    }

    openStaticAttrModal () {
        this.setState({
            templateAddStaticShow: true,
            modalChecked: false,
            modalSpinning: false,
            staticValue: "",
            staticAttr: ""
        });
    }

    closeStaticAttrModal() {
        this.setState({ templateAddStaticShow: false });
    }

    showEditDefModal(rowData) {
        this.openDefModal(rowData);
    }

    showAddDefModal(rowData) {
        this.openDefModal();
    }

    // Definition modal
    openDefModal(name) {
        this.getAttributes();
        if (!name) {
            this.setState({
                configEntryModalShow: true,
                newDefEntry: true,
                configName: "",
                originScope: "",
                originFilter: "",
                managedBase: "",
            });
        } else {
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "managed-entries",
                "config",
                name,
                "show"
            ];

            this.toggleLoading();
            log_cmd("openDefModal", "Fetch the Managed Entries Plugin definition config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const configEntry = JSON.parse(content).attrs;
                        // Initialize settings
                        configEntry.originscope === undefined
                            ? configEntry.originscope = ""
                            : configEntry.originscope = configEntry.originscope[0];
                        configEntry.originfilter === undefined
                            ? configEntry.originfilter = ""
                            : configEntry.originfilter = configEntry.originfilter[0];
                        configEntry.managedbase === undefined
                            ? configEntry.managedbase = ""
                            : configEntry.managedbase = configEntry.managedbase[0];
                        configEntry.managedtemplate === undefined
                            ? configEntry.managedtemplate = ""
                            : configEntry.managedtemplate = configEntry.managedtemplate[0];

                        this.setState({
                            configEntryModalShow: true,
                            newDefEntry: false,
                            configName: configEntry.cn[0],
                            originScope: configEntry.originscope,
                            originFilter: configEntry.originfilter,
                            managedBase: configEntry.managedbase,
                            managedTemplate: configEntry.managedtemplate,
                            // Preserve original settings
                            _originScope: configEntry.originscope,
                            _originFilter: configEntry.originfilter,
                            _managedBase: configEntry.managedbase,
                            _managedTemplate: configEntry.managedtemplate,
                        }, this.toggleLoading());
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newDefEntry: true,
                            configName: "",
                            originScope: "",
                            originFilter: "",
                            managedBase: "",
                        }, this.toggleLoading());
                    });
        }
    }

    closeDefModal() {
        this.setState({ configEntryModalShow: false });
    }

    deleteDefConfig() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "config",
            this.state.configName,
            "delete"
        ];

        this.toggleLoading();
        log_cmd("deleteDefConfig", "Delete the Managed Entries Plugin definition entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteDefConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Definition entry ${this.state.configName} was successfully deleted`
                    );
                    this.loadConfigs();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the definition entry removal operation - ${errMsg.desc}`
                    );
                    this.loadConfigs();
                });
    }

    addDefConfig() {
        this.cmdDefOperation("add");
    }

    editDefConfig() {
        this.cmdDefOperation("set");
    }

    cmdDefOperation(action) {
        // Update definition
        const { configName, originScope, originFilter, managedBase, managedTemplate } = this.state;

        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "config",
            configName,
            action,
            "--scope",
            originScope || action == "add" ? originScope : "delete",
            "--filter",
            originFilter || action == "add" ? originFilter : "delete",
            "--managed-base",
            managedBase || action == "add" ? managedBase : "delete",
            "--managed-template",
            managedTemplate || action == "add" ? managedTemplate : "delete"
        ];

        this.toggleLoading();
        log_cmd("cmdDefOperation", `Do the ${action} operation on the Managed Entries Plugin`, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("cmdDefOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        `The ${action} operation was successfully done on "${configName}" entry`
                    );
                    this.loadConfigs();
                    this.closeDefModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry ${action} operation - ${errMsg.desc}`
                    );
                    this.loadConfigs();
                    this.closeDefModal();
                });
    }

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "Managed Entries");
            this.setState({
                loading: true,
                configArea:
                    pluginRow["nsslapd-pluginConfigArea"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginConfigArea"][0]
            }, this.loadConfigs());
        }
    }

    cmdTemplateOperation(action) {
        const { templateDN, templateRDNAttr, templateStaticAttr, templateMappedAttr } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "template",
            templateDN,
            action
        ];

        cmd = [...cmd, "--rdn-attr"];
        if (templateRDNAttr != "") {
            cmd = [...cmd, templateRDNAttr];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        if (templateStaticAttr.length != 0) {
            cmd = [...cmd, "--static-attr"];
            for (const value of templateStaticAttr) {
                cmd = [...cmd, value];
            }
        } else if (action != "add") {
            cmd = [...cmd, "--static-attr", "delete"];
        }

        if (templateMappedAttr.length != 0) {
            cmd = [...cmd, "--mapped-attr"];
            for (const value of templateMappedAttr) {
                cmd = [...cmd, value];
            }
        } else if (action != "add") {
            cmd = [...cmd, "--mapped-attr", "delete"];
        }

        this.toggleLoading();
        log_cmd(
            "cmdTemplateOperation",
            `Do the ${action} operation on the Managed Entries template`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Config entry ${templateDN} was successfully ${action}ed`
                    );
                    this.loadConfigs();
                    this.closeTempModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.closeTempModal();
                    this.props.addNotification(
                        "error",
                        `Error during the template entry ${action} operation - ${errMsg.desc}`
                    );
                    this.toggleLoading();
                });
    }

    deleteTemplate() {
        const { templateDN } = this.state;

        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "template",
            templateDN,
            "delete"
        ];

        this.toggleLoading();
        log_cmd("deleteTemplate", "Delete the Managed Entries Plugin template entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Template entry ${templateDN} was successfully deleted`
                    );
                    this.loadConfigs();
                    this.closeTempDeleteConfirm();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the template entry removal operation - ${errMsg.desc}`
                    );
                    this.closeTempDeleteConfirm();
                    this.toggleLoading();
                });
    }

    addTemplate() {
        this.cmdTemplateOperation("add");
    }

    editTemplate() {
        this.cmdTemplateOperation("set");
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
                    const attrs = [];
                    for (const content of attrContent.items) {
                        attrs.push(content.name[0]);
                    }
                    this.setState({
                        attributes: attrs
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification("error", `Failed to get attributes - ${errMsg.desc}`);
                });
    }

    handleConfirmChange(e) {
        this.setState({
            modalChecked: e.target.checked
        });
    }

    validateDef(newDefEntry) {
        let defSaveDisabled = false;
        if (this.state.configName == "" ||
            (this.state.originScope == "" || !valid_dn(this.state.originScope)) ||
            this.state.originFilter == "" ||
            (this.state.managedBase == "" || !valid_dn(this.state.managedBase)) ||
            this.state.managedTemplate == "") {
            defSaveDisabled = true;
        }

        // If editing an entry we need to check if values were changed before
        // enabling the save button
        if (!newDefEntry && !defSaveDisabled) {
            if (this.state.originScope == this.state._originScope &&
                this.state.originFilter == this.state._originFilter &&
                this.state.managedBase == this.state._managedBase &&
                this.state.managedTemplate == this.state._managedTemplate) {
                defSaveDisabled = true;
            }
        }
        return defSaveDisabled;
    }

    validateTempRdnToAttrMapping() {
        // The template RDN must have a mapped attribute
        let mappedMatchesRDN = false;
        for (const mAttr of this.state.templateMappedAttr) {
            if (mAttr.startsWith(this.state.templateRDNAttr + ":")) {
                mappedMatchesRDN = true;
                break;
            }
        }
        return mappedMatchesRDN;
    }

    validateTemp(newTemplateEntry) {
        let tempSaveDisabled = false;

        if (this.state.templateDN == "" ||
            (this.state.templateDN != "" && !valid_dn(this.state.templateDN)) ||
            this.state.templateRDNAttr == "") {
            tempSaveDisabled = true;
        }

        // The template RDN must have a mapped attribute
        if (!this.validateTempRdnToAttrMapping()) {
            tempSaveDisabled = true;
        }

        // If editing an entry we need to check if values were changed before
        // enabling the save button
        if (!newTemplateEntry && !tempSaveDisabled) {
            if (this.state.templateDN == this.state._templateDN &&
                this.state.templateRDNAttr == this.state._templateRDNAttr &&
                listsEqual(this.state.templateMappedAttr, this.state._templateMappedAttr) &&
                listsEqual(this.state.templateStaticAttr, this.state._templateStaticAttr)) {
                tempSaveDisabled = true;
            }
        }
        return tempSaveDisabled;
    }

    render() {
        const {
            configArea,
            configRows,
            attributes,
            configName,
            originScope,
            originFilter,
            managedBase,
            managedTemplate,
            templateDN,
            templateRDNAttr,
            templateStaticAttr,
            templateMappedAttr,
            templateOptions,
            newDefEntry,
            configEntryModalShow,
            newTemplateEntry,
            templateAddStaticShow,
            templateAddMappedShow,
        } = this.state;

        const specificPluginCMD = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "managed-entries",
            "set",
            "--config-area",
            configArea || "delete"
        ];

        const templateTitle = (newTemplateEntry ? "Add" : "Edit") + " Managed Entries Template Entry";
        const title = (newDefEntry ? "Add" : "Edit") + " Managed Entries Definition Entry";
        const defSaveDisabled = this.validateDef(newDefEntry);
        const tempSaveDisabled = this.validateTemp(newTemplateEntry);
        const templateRdnMappingOK = this.validateTempRdnToAttrMapping();

        return (
            <div>
                <Modal
                    variant={ModalVariant.medium}
                    aria-labelledby="ds-modal"
                    title={title}
                    isOpen={configEntryModalShow}
                    onClose={this.closeDefModal}
                    actions={[
                        <Button
                            key="saveshared"
                            isDisabled={defSaveDisabled}
                            variant="primary"
                            onClick={newDefEntry ? this.addDefConfig : this.editDefConfig}
                        >
                            Save Definition
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeDefModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form className="ds-margin-top-xlg" isHorizontal autoComplete="off">
                        <FormGroup
                            label="Definition Name"
                            fieldId="configName"
                            isRequired
                        >
                            <TextInput
                                value={configName}
                                type="text"
                                id="configName"
                                aria-describedby="configName"
                                name="configName"
                                onChange={this.handleFieldChange}
                                isDisabled={!newDefEntry}
                                isRequired
                            />
                        </FormGroup>
                        <FormGroup
                            label="Subtree Scope"
                            fieldId="originScope"
                            helperTextInvalid="A valid DN must be provided"
                            validated={
                                originScope != "" && !valid_dn(originScope)
                                    ? ValidatedOptions.error
                                    : ValidatedOptions.default
                            }
                            title="Sets the search base DN to use to find candidate entries (originScope)"
                            isRequired
                        >
                            <TextInput
                                value={originScope}
                                type="text"
                                id="originScope"
                                aria-describedby="originScope"
                                name="originScope"
                                onChange={this.handleFieldChange}
                                validated={
                                    originScope != "" && !valid_dn(originScope)
                                        ? ValidatedOptions.error
                                        : ValidatedOptions.default
                                }
                                isRequired
                            />
                        </FormGroup>
                        <FormGroup
                            label="Filter"
                            fieldId="originFilter"
                            title="Sets the search filter to use to search for and identify the entries within the subtree which require a managed entry (originFilter)"
                            isRequired
                        >
                            <TextInput
                                value={originFilter}
                                type="text"
                                id="originFilter"
                                aria-describedby="originFilter"
                                name="originFilter"
                                onChange={this.handleFieldChange}
                                isRequired
                            />
                        </FormGroup>
                        <FormGroup
                            label="Managed Base"
                            fieldId="managedBase"
                            helperTextInvalid="A valid DN must be provided"
                            validated={
                                managedBase != "" && !valid_dn(managedBase)
                                    ? ValidatedOptions.error
                                    : ValidatedOptions.default
                            }
                            title="Sets the subtree where the managed entries are created (managedBase)"
                            isRequired
                        >
                            <TextInput
                                value={managedBase}
                                type="text"
                                id="managedBase"
                                aria-describedby="managedBase"
                                name="managedBase"
                                onChange={this.handleFieldChange}
                                validated={
                                    managedBase != "" && !valid_dn(managedBase)
                                        ? ValidatedOptions.error
                                        : ValidatedOptions.default
                                }
                                isRequired
                            />
                        </FormGroup>
                        <FormGroup
                            label="Template"
                            fieldId="managedTemplate"
                            title="Choose which template to use for this definition"
                            isRequired
                        >
                            <FormSelect
                                id="managedTemplate"
                                value={managedTemplate}
                                onChange={this.handleFieldChange}
                                aria-label="FormSelect Input"
                            >
                                {templateOptions.map((option, index) => (
                                    <FormSelectOption key={index} value={option.value} label={option.label} />
                                ))}
                            </FormSelect>
                        </FormGroup>
                    </Form>
                </Modal>

                <Modal
                    variant={ModalVariant.medium}
                    aria-labelledby="ds-modal"
                    title={templateTitle}
                    isOpen={this.state.templateModalShow}
                    onClose={this.closeTempModal}
                    actions={[
                        <Button
                            key="savetemp"
                            variant="primary"
                            onClick={newTemplateEntry ? this.addTemplate : this.editTemplate}
                            isDisabled={tempSaveDisabled}
                        >
                            Save Template
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeTempModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form className="ds-margin-top-xlg" isHorizontal autoComplete="off">
                        <FormGroup
                            label="Template DN"
                            fieldId="templateDN"
                            helperTextInvalid="The template DN must be set to a valid DN"
                            validated={
                                templateDN != "" && !valid_dn(templateDN)
                                    ? ValidatedOptions.error
                                    : ValidatedOptions.default
                            }
                            title="DN of the template entry"
                            isRequired
                        >
                            <TextInput
                                value={templateDN}
                                type="text"
                                id="templateDN"
                                aria-describedby="templateDN"
                                name="templateDN"
                                onChange={this.handleFieldChange}
                                isDisabled={!newTemplateEntry}
                                validated={
                                    templateDN != "" && !valid_dn(templateDN)
                                        ? ValidatedOptions.error
                                        : ValidatedOptions.default
                                }
                            />
                        </FormGroup>
                        <FormGroup
                            label="RDN Attribute"
                            fieldId="templateDN"
                            title="DN of the template entry"
                            isRequired
                        >
                            <Select
                                variant={SelectVariant.typeahead}
                                typeAheadAriaLabel="Type an attribute"
                                onToggle={this.onRDNToggle}
                                onSelect={this.onRDNSelect}
                                onClear={this.clearRDNSelection}
                                selections={templateRDNAttr}
                                isOpen={this.state.isRDNOpen}
                                aria-labelledby="typeAhead-rdn"
                                placeholderText="Type an attribute..."
                            >
                                {attributes.map((attr) => (
                                    <SelectOption
                                        key={attr}
                                        value={attr}
                                    />
                                ))}
                            </Select>
                        </FormGroup>
                    </Form>
                    <Form className="ds-margin-top-lg" autoComplete="off">
                        <FormGroup
                            label="Mapped Attributes"
                            fieldId="templateDN"
                            title="Dynamic attribute mappings"
                            isRequired
                        >
                            <SimpleList
                                className="ds-modal-list"
                                onSelect={this.onMappedListSelect}
                                aria-label="mappedAttrList"
                            >
                                {templateMappedAttr.length ? templateMappedAttr.map((attr) => (
                                    <SimpleListItem id={attr} key={attr}>
                                        {attr}
                                    </SimpleListItem>
                                )) : ""}
                            </SimpleList>
                            <p
                                hidden={templateRdnMappingOK}
                                className="ds-error-msg"
                            >
                                You must have at least one Mapped Attribute that matches the RDN Attribute
                            </p>
                            <div className="ds-margin-top">
                                <Button
                                    className="ds-margin-top"
                                    key="addstatic"
                                    variant="primary"
                                    onClick={this.openMappedAttrModal}
                                >
                                    Add Attribute
                                </Button>
                                <Button
                                    className="ds-left-margin"
                                    key="delstatic"
                                    variant="primary"
                                    isDisabled={this.state.selectedMappedAttr == ""}
                                    onClick={this.showMappedAttrDeleteConfirm}
                                >
                                    Delete Attribute
                                </Button>
                            </div>
                        </FormGroup>
                        <FormGroup
                            className="ds-margin-top-lg"
                            label="Static Attributes"
                            fieldId="templateDN"
                            title="Static attribute mappings"
                        >
                            <SimpleList
                                className="ds-modal-list"
                                onSelect={this.onStaticListSelect}
                                aria-label="Simple List Example"
                            >
                                {templateStaticAttr.length ? templateStaticAttr.map((attr) => (
                                    <SimpleListItem id={attr} key={attr}>
                                        {attr}
                                    </SimpleListItem>
                                )) : ""}
                            </SimpleList>
                            <div className="ds-margin-top">
                                <Button
                                    key="addstatic"
                                    variant="primary"
                                    onClick={this.openStaticAttrModal}
                                >
                                    Add Attribute
                                </Button>
                                <Button
                                    className="ds-left-margin"
                                    key="delstatic"
                                    variant="primary"
                                    isDisabled={this.state.selectedStaticAttr == ""}
                                    onClick={this.showStaticAttrDeleteConfirm}
                                >
                                    Delete Attribute
                                </Button>
                                <hr />
                            </div>
                        </FormGroup>
                    </Form>
                </Modal>

                <Modal
                    variant={ModalVariant.small}
                    aria-labelledby="ds-modal"
                    title="Add Static Attribute"
                    isOpen={templateAddStaticShow}
                    onClose={this.closeStaticAttrModal}
                    actions={[
                        <Button
                            key="saveshared"
                            variant="primary"
                            onClick={this.addStaticAttrToList}
                            isDisabled={!this.state.staticAttr.length || this.state.staticValue == ""}
                        >
                            Add Attribute
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeStaticAttrModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form className="ds-margin-top" autoComplete="off">
                        <FormGroup
                            label="Static Attribute"
                            fieldId="staticAttr"
                            title="The attribute that is set in the managed entry"
                        >
                            <Select
                                variant={SelectVariant.typeahead}
                                typeAheadAriaLabel="Type an attribute"
                                onToggle={this.onStaticToggle}
                                onSelect={this.onStaticSelect}
                                onClear={this.clearStaticSelection}
                                selections={this.state.staticAttr}
                                isOpen={this.state.isStaticOpen}
                                aria-labelledby="typeAhead-static"
                                placeholderText="Type an attribute..."
                            >
                                {attributes.map((attr) => (
                                    <SelectOption
                                        key={attr}
                                        value={attr}
                                    />
                                ))}
                            </Select>
                        </FormGroup>
                        <FormGroup
                            className="ds-margin-top-lg"
                            label="Attribute Value"
                            fieldId="staticValue"
                            title="The value set for the static attribute in the entry"
                        >
                            <TextInput
                                value={this.state.staticValue}
                                type="text"
                                id="staticValue"
                                aria-describedby="staticValue"
                                name="staticValue"
                                onChange={this.handleFieldChange}
                                isRequired
                            />
                        </FormGroup>
                        <hr />
                    </Form>
                </Modal>

                <Modal
                    variant={ModalVariant.small}
                    aria-labelledby="ds-modal"
                    title="Add Mapped Attribute"
                    isOpen={templateAddMappedShow}
                    onClose={this.closeMappedAttrModal}
                    actions={[
                        <Button
                            key="savemapped"
                            variant="primary"
                            onClick={this.addMappedAttrToList}
                            isDisabled={!this.state.mappedAttr.length || this.state.mappedValue == ""}
                        >
                            Add Attribute
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeMappedAttrModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form className="ds-margin-top" autoComplete="off">
                        <FormGroup
                            label="Mapped Attribute"
                            fieldId="mappedAttr"
                            title="The attribute that is set in the managed entry"
                        >
                            <Select
                                variant={SelectVariant.typeahead}
                                typeAheadAriaLabel="Type an attribute"
                                onToggle={this.onMappedToggle}
                                onSelect={this.onMappedSelect}
                                onClear={this.clearMappedSelection}
                                selections={this.state.mappedAttr}
                                isOpen={this.state.isMappedOpen}
                                aria-labelledby="typeAhead-static"
                                placeholderText="Type an attribute..."
                            >
                                {attributes.map((attr) => (
                                    <SelectOption
                                        key={attr}
                                        value={attr}
                                    />
                                ))}
                            </Select>
                        </FormGroup>
                        <FormGroup
                            label="Mapped Value"
                            fieldId="mappedValue"
                            className="ds-margin-top-lg"
                            title="The value set for the mapped attribute"
                        >
                            <TextInput
                                value={this.state.mappedValue}
                                type="text"
                                id="mappedValue"
                                aria-describedby="mappedValue"
                                name="mappedValue"
                                onChange={this.handleFieldChange}
                            />
                        </FormGroup>
                        <hr />
                    </Form>
                </Modal>

                <div className="ds-center ds-margin-top-xlg" hidden={!this.state.loading}>
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Loading Managed Entries Configuration ...
                        </Text>
                    </TextContent>
                    <Spinner className="ds-margin-top" size="lg" />
                </div>

                <div hidden={this.state.loading}>
                    <PluginBasicConfig
                        rows={this.props.rows}
                        serverId={this.props.serverId}
                        cn="Managed Entries"
                        pluginName="Managed Entries"
                        cmdName="managed-entries"
                        specificPluginCMD={specificPluginCMD}
                        savePluginHandler={this.props.savePluginHandler}
                        pluginListHandler={this.props.pluginListHandler}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                    >
                        <div className="ds-margin-top-lg">
                            <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                                <Tab
                                    eventKey={1} title={
                                        <TabTitleText>
                                            <b>Templates</b>
                                        </TabTitleText>
                                    }
                                >
                                    <div className="ds-margin-top-lg">
                                        <Tooltip
                                            content={
                                                <div>
                                                    Templates are used by Definitions to construct
                                                    the "Managed Entry".  You must have at least one
                                                    Template before you can create a Definition entry.
                                                </div>
                                            }
                                        >
                                            <a className="ds-font-size-sm">What is a Template?</a>
                                        </Tooltip>
                                    </div>
                                    <ManagedTemplateTable
                                        rows={this.state.templateRows}
                                        key={this.state.templateRows}
                                        editConfig={this.showEditTempModal}
                                        deleteConfig={this.showTempDeleteConfirm}
                                    />
                                    <Button
                                        className="ds-left-margin"
                                        variant="primary"
                                        onClick={this.showAddTempModal}
                                    >
                                        Create Template
                                    </Button>
                                </Tab>
                                <Tab
                                    eventKey={2} title={
                                        <TabTitleText>
                                            <b>Definitions</b>
                                        </TabTitleText>
                                    }
                                >
                                    <div className="ds-margin-top-lg">
                                        <Tooltip
                                            content={
                                                <div>
                                                    Definitions describe the criteria that entries
                                                    must meet in order for its "managed entry" to
                                                    be created.  The Managed entry will be created
                                                    using the Template specified in its configuration.
                                                    You can not create a Definition until there is
                                                    at least one Template.
                                                </div>
                                            }
                                        >
                                            <a className="ds-font-size-sm">What is a Definition?</a>
                                        </Tooltip>
                                    </div>
                                    <ManagedDefinitionTable
                                        rows={configRows}
                                        key={this.state.tableKey}
                                        editConfig={this.showEditDefModal}
                                        deleteConfig={this.showDefDeleteConfirm}
                                    />
                                    <Button
                                        className="ds-left-margin"
                                        variant="primary"
                                        onClick={this.showAddDefModal}
                                        isDisabled={this.state.defCreateDisabled}
                                    >
                                        Add Definition
                                    </Button>
                                </Tab>
                            </Tabs>
                            <div className="ds-margin-top-xlg" />
                            <hr />
                            <Form className="ds-margin-top-xlg" isHorizontal autoComplete="off">
                                <FormGroup
                                    label="Shared Config Area"
                                    fieldId="configArea"
                                    helperTextInvalid="The DN for the shared conifig area is invalid"
                                    validated={
                                        configArea != "" && !valid_dn(configArea)
                                            ? ValidatedOptions.error
                                            : ValidatedOptions.default
                                    }
                                >
                                    <TextInput
                                        value={configArea}
                                        type="text"
                                        id="configArea"
                                        aria-describedby="configArea"
                                        name="configArea"
                                        onChange={this.handleFieldChange}
                                    />
                                </FormGroup>
                            </Form>
                        </div>
                    </PluginBasicConfig>
                </div>
                <DoubleConfirmModal
                    showModal={this.state.showTempDeleteConfirm}
                    closeHandler={this.closeTempDeleteConfirm}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.deleteTemplate}
                    spinning={this.state.modalSpinning}
                    item={this.state.templateDN}
                    checked={this.state.modalChecked}
                    mTitle="Delete Template Entry"
                    mMsg="Are you really sure you want to delete this template entry?"
                    mSpinningMsg="Deleting template ..."
                    mBtnName="Delete Template"
                />
                <DoubleConfirmModal
                    showModal={this.state.showDefDeleteConfirm}
                    closeHandler={this.closeDefDeleteConfirm}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.deleteDefConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.configName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Definition Entry"
                    mMsg="Are you really sure you want to delete this definition entry?"
                    mSpinningMsg="Deleting definition ..."
                    mBtnName="Delete Definition"
                />
                <DoubleConfirmModal
                    showModal={this.state.showMappedAttrDeleteConfirm}
                    closeHandler={this.closeMappedAttrDeleteConfirm}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.deleteMappedAttribute}
                    spinning={this.state.modalSpinning}
                    item={this.state.selectedMappedAttr}
                    checked={this.state.modalChecked}
                    mTitle="Delete Mapped Attribute From Template"
                    mMsg="Are you really sure you want to delete this mapped attribute?"
                    mSpinningMsg="Deleting mapped attribute ..."
                    mBtnName="Delete Attribute"
                />
                <DoubleConfirmModal
                    showModal={this.state.showStaticAttrDeleteConfirm}
                    closeHandler={this.closeStaticAttrDeleteConfirm}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.deleteStaticAttribute}
                    spinning={this.state.modalSpinning}
                    item={this.state.selectedStaticAttr}
                    checked={this.state.modalChecked}
                    mTitle="Delete Static Attribute From Template"
                    mMsg="Are you really sure you want to delete this static attribute?"
                    mSpinningMsg="Deleting static attribute ..."
                    mBtnName="Delete Attribute"
                />
            </div>
        );
    }
}

ManagedEntries.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
};

ManagedEntries.defaultProps = {
    rows: [],
    serverId: "",
};

export default ManagedEntries;
