import cockpit from "cockpit";
import React from "react";
import {
	Button,
	Checkbox,
	Form,
	FormHelperText,
	FormSelect,
	FormSelectOption,
	Grid,
	GridItem,
	Modal,
	ModalVariant,
    Tab,
    Tabs,
    TabTitleText,
	TextInput,
	Text,
	TextContent,
	TextVariants,
	ValidatedOptions
} from '@patternfly/react-core';
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import { log_cmd, valid_dn, listsEqual, parentExists, valid_filter } from "../tools.jsx";
import TypeaheadSelect from "../../dsBasicComponents.jsx";
import { MemberOfTable } from "./pluginTables.jsx";
import {
    MemberOfConfigEntryModal,
    MemberOfSpecificGroupFilterModal,
    MemberOfFixupTaskModal,
 } from "./memberOfModals.jsx";
import {
    WrenchIcon,
} from '@patternfly/react-icons';

const _ = cockpit.gettext;

class MemberOf extends React.Component {
    componentDidMount(prevProps) {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
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

        this.updateFields = this.updateFields.bind(this);
        this.onChange = this.onChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.handleOpenModal = this.handleOpenModal.bind(this);
        this.handleCloseModal = this.handleCloseModal.bind(this);
        this.handleAddConfig = this.handleAddConfig.bind(this);
        this.handleEditConfig = this.handleEditConfig.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.handleRunFixup = this.handleRunFixup.bind(this);
        this.handleToggleFixupModal = this.handleToggleFixupModal.bind(this);
        this.handleSaveConfig = this.handleSaveConfig.bind(this);
        this.handleShowConfirmDelete = this.handleShowConfirmDelete.bind(this);
        this.closeConfirmDelete = this.closeConfirmDelete.bind(this);
        this.validateConfig = this.validateConfig.bind(this);
        this.validateModal = this.validateModal.bind(this);
        this.validateFilterCreate = this.validateFilterCreate.bind(this);
        this.validateModalFilterCreate = this.validateModalFilterCreate.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleNavSelectModal = this.handleNavSelectModal.bind(this);
        this.openSpecificGroupAddModal = this.openSpecificGroupAddModal.bind(this);
        this.openSpecificExcludeGroupAddModal = this.openSpecificExcludeGroupAddModal.bind(this);
        this.closeSpecificGroupAddModal = this.closeSpecificGroupAddModal.bind(this);
        this.handleAddDelSpecificGroupFilter = this.handleAddDelSpecificGroupFilter.bind(this);
        this.handleExcludeSpecificGroupAdd = this.handleExcludeSpecificGroupAdd.bind(this);
        this.handleExcludeSpecificGroupDelete = this.handleExcludeSpecificGroupDelete.bind(this);
        this.openDeleteFilterConfirmation = this.openDeleteFilterConfirmation.bind(this);
        this.closeDeleteFilterConfirmation = this.closeDeleteFilterConfirmation.bind(this);
        this.openDeleteExcludeFilterConfirmation = this.openDeleteExcludeFilterConfirmation.bind(this);
        this.closeDeleteExcludeFilterConfirmation = this.closeDeleteExcludeFilterConfirmation.bind(this);
        this.validFilterChange = this.validFilterChange.bind(this);

        this.state = {
            activeTabKey: 0,
            activeTabModalKey: 0,
            firstLoad: true,
            error: {},
            errorModal: {},
            saveBtnDisabled: true,
            saveBtnDisabledModal: true,
            showConfirmDelete: false,
            modalChecked: false,
            modalSpinning: false,
            newEntry: true,
            fixupDN: "",
            fixupFilter: "",

            // Main settings
            memberOfAttr: "",
            memberOfGroupAttr: [],
            memberOfEntryScope: [],
            memberOfEntryScopeOptions: [],
            memberOfEntryScopeExcludeSubtree: [],
            memberOfEntryScopeExcludeOptions: [],
            memberOfSpecificGroup: [],
            memberOfSpecificGroupOptions: [],
            memberOfExcludeSpecificGroup: [],
            memberOfExcludeSpecificGroupOptions: [],
            memberOfSpecificGroupOC: [],
            memberOfSpecificGroupOCOptions: [],
            memberOfAutoAddOC: "",
            memberOfAllBackends: false,
            memberOfSkipNested: false,
            memberOfConfigEntry: "",
            configEntryModalShow: false,
            fixupModalShow: false,
            isSubtreeScopeOpen: false,
            isExcludeScopeOpen: false,
            isSpecificGroupOpen: false,
            isExcludeSpecificGroupOpen: false,
            isSpecificGroupOCOpen: false,
            isMemberOfAttrOpen: false,
            isMemberOfGroupAttrOpen: false,
            isMemberOfAutoAddOCOpen: false,

            // Modal settings
            configDN: "",
            configAttr: "",
            configGroupAttr: [],
            configEntryScope: [],
            configEntryScopeOptions: [],
            configEntryScopeExcludeSubtreeScope: [],
            configEntryScopeExcludeOptions: [],
            configSpecificGroupOptions: [],
            configExcludeSpecificGroupOptions: [],
            configAutoAddOC: "",
            configAllBackends: false,
            configSkipNested: false,
            configSpecificGroup: [],
            configExcludeSpecificGroup: [],
            configSpecificGroupOC: [],
            isConfigSubtreeScopeOpen: false,
            isConfigExcludeScopeOpen: false,
            isConfigSpecificGroupOCOpen: false,
            isConfigSpecificGroupOpen: false,
            isConfigExcludeSpecificGroupOpen: false,
            isConfigAttrOpen: false,
            isConfigGroupAttrOpen: false,
            isConfigAutoAddOCOpen: false,
            isSpecificGroupModalOpen: false,
            groupFilter: "",
            groupFilterType: "include",
            showDeleteFilterConfirmation: false,
            showDeleteExcludeFilterConfirmation: false,
        };

        // Config Attribute
        this.handleConfigAttrSelect = (event, selection) => {
            if (selection === this.state.configAttr) {
                this.handleConfigAttrClear();
            } else {
                this.setState({
                    configAttr: selection,
                    isConfigAttrOpen: false
                }, () => { this.validateModal() });
            }
        };
        this.handleConfigAttrToggle = (_event, isConfigAttrOpen) => {
            this.setState({
                isConfigAttrOpen
            });
        };
        this.handleConfigAttrClear = () => {
            this.setState({
                configAttr: "",
                isConfigAttrOpen: false
            }, () => { this.validateModal() });
        };

        // Config Group Attribute
        this.handleConfigGroupAttrSelect = (event, selection) => {
            this.setState({
                configGroupAttr: Array.isArray(selection) ? selection : [],
            }, () => { this.validateModal() });
        };
        this.handleConfigGroupAttrToggle = (_event, isConfigGroupAttrOpen) => {
            this.setState({
                isConfigGroupAttrOpen
            });
        };
        this.handleConfigGroupAttrClear = () => {
            this.setState({
                configGroupAttr: [],
                isConfigGroupAttrOpen: false
            }, () => { this.validateModal() });
        };

        // MemberOf Attribute
        this.handleMemberOfAttrSelect = (event, selection) => {
            this.setState({
                memberOfAttr: selection || '',
            }, () => { this.validateConfig() });
        };
        this.handleMemberOfAttrToggle = (_event, isMemberOfAttrOpen) => {
            this.setState({
                isMemberOfAttrOpen
            });
        };
        this.handleMemberOfAttrClear = () => {
            this.setState({
                memberOfAttr: '',
            }, () => { this.validateConfig() });
        };

        // MemberOf Group Attribute
        this.handleMemberOfGroupAttrSelect = (event, selection) => {
            this.setState({
                memberOfGroupAttr: Array.isArray(selection) ? selection : [],
            }, () => { this.validateConfig() });
        };
        this.handleMemberOfGroupAttrToggle = (_event, isMemberOfGroupAttrOpen) => {
            this.setState({
                isMemberOfGroupAttrOpen
            });
        };
        this.handleMemberOfGroupAttrClear = () => {
            this.setState({
                memberOfGroupAttr: [],
            }, () => { this.validateConfig() });
        };

        // Handle scope subtree
        this.handleSubtreeScopeSelect = (event, selection) => {
            this.setState({
                memberOfEntryScope: Array.isArray(selection) ? selection : [],
            }, () => { this.validateConfig() });
        };
        this.handleSubtreeScopeToggle = (_event, isSubtreeScopeOpen) => {
            this.setState({
                isSubtreeScopeOpen
            }, () => { this.validateConfig() });
        };
        this.handleSubtreeScopeClear = () => {
            this.setState({
                memberOfEntryScope: [],
            }, () => { this.validateConfig() });
        };
        this.handleSubtreeScopeCreateOption = newValue => {
            if (newValue.trim() && valid_dn(newValue) && !this.state.memberOfEntryScopeOptions.includes(newValue)) {
                this.setState({
                    memberOfEntryScopeOptions: [...this.state.memberOfEntryScopeOptions, newValue],
                    isSubtreeScopeOpen: false,
                }, () => { this.validateConfig() });
            }
        };

        // Handle Exclude Scope subtree
        this.handleExcludeScopeSelect = (event, selection) => {
            this.setState({
                memberOfEntryScopeExcludeSubtree: Array.isArray(selection) ? selection : [],
            }, () => { this.validateConfig() });
        };
        this.handleExcludeScopeToggle = (_event, isExcludeScopeOpen) => {
            this.setState({
                isExcludeScopeOpen
            }, () => { this.validateConfig() });
        };
        this.handleExcludeScopeClear = () => {
            this.setState({
                memberOfEntryScopeExcludeSubtree: [],
            }, () => { this.validateConfig() });
        };
        this.handleExcludeCreateOption = newValue => {
            if (newValue.trim() && valid_dn(newValue) && !this.state.memberOfEntryScopeExcludeOptions.includes(newValue)) {
                this.setState({
                    memberOfEntryScopeExcludeOptions: [...this.state.memberOfEntryScopeExcludeOptions, newValue],
                    isExcludeScopeOpen: false,
                }, () => { this.validateConfig() });
            }
        };

        // Modal scope and exclude Scope
        // Handle scope subtree
        this.handleConfigScopeSelect = (event, selection) => {
            this.setState({
                configEntryScope: Array.isArray(selection) ? selection : [],
                configEntryScopeOptions: [],
            }, () => { this.validateModal() });
        };
        this.handleConfigScopeToggle = (_event, isConfigSubtreeScopeOpen) => {
            this.setState({
                isConfigSubtreeScopeOpen
            }, () => { this.validateModal() });
        };
        this.handleConfigScopeClear = () => {
            this.setState({
                configEntryScope: [],
                configEntryScopeOptions: [],
            }, () => { this.validateModal() });
        };
        this.handleConfigCreateOption = newValue => {
            if (newValue.trim() && valid_dn(newValue) && !this.state.configEntryScopeOptions.includes(newValue)) {
                this.setState({
                    configEntryScopeOptions: [...this.state.configEntryScopeOptions, newValue],
                    isConfigSubtreeScopeOpen: false,
                }, () => { this.validateModal() });
            }
        };

        // Handle Exclude Scope subtree
        this.handleConfigExcludeScopeSelect = (event, selection) => {
            this.setState({
                configEntryScopeExcludeSubtreeScope: Array.isArray(selection) ? selection : [],
            }, () => { this.validateModal() });
        };
        this.handleConfigExcludeScopeToggle = (_event, isConfigExcludeScopeOpen) => {
            this.setState({
                isConfigExcludeScopeOpen
            }, () => { this.validateModal() });
        };
        this.handleConfigExcludeScopeClear = () => {
            this.setState({
                configEntryScopeExcludeSubtreeScope: [],
                configEntryScopeExcludeOptions: [],
            }, () => { this.validateModal() });
        };
        this.handleConfigExcludeCreateOption = newValue => {
            if (newValue.trim() && valid_dn(newValue) &&  !this.state.configEntryScopeExcludeOptions.includes(newValue)) {
                this.setState({
                    configEntryScopeExcludeOptions: [...this.state.configEntryScopeExcludeOptions, newValue],
                    isConfigExcludeScopeOpen: false,
                }, () => { this.validateModal() });
            }
        };

        // Handle Specific Group (modal)
        this.handleConfigSpecificGroupSelect = (event, selection) => {
            this.setState({
                configSpecificGroup: Array.isArray(selection) ? selection : [],
            }, () => { this.validateModal() });
        };
        this.handleConfigSpecificGroupToggle = (_event, isConfigSpecificGroupOpen) => {
            this.setState({
                isConfigSpecificGroupOpen
            }, () => { this.validateModal() });
        };
        this.handleConfigSpecificGroupClear = () => {
            this.setState({
                configSpecificGroup: [],
            }, () => { this.validateModal() });
        };
        this.handleConfigSpecificGroupCreateOption = newValue => {
            if (newValue.trim() && valid_filter(newValue) &&  !this.state.configSpecificGroupOptions.includes(newValue)) {
                this.setState({
                    configSpecificGroupOptions: [...this.state.configSpecificGroupOptions, newValue],
                    isConfigSpecificGroupOpen: false
                }, () => { this.validateModal() });
            }
        };

        // Handle Exclude Specific Group (modal)
        this.handleConfigExcludeSpecificGroupSelect = (event, selection) => {
            this.setState({
                configExcludeSpecificGroup: Array.isArray(selection) ? selection : [],
            }, () => { this.validateModal() });
        };
        this.handleConfigExcludeSpecificGroupToggle = (_event, isConfigExcludeSpecificGroupOpen) => {
            this.setState({
                isConfigExcludeSpecificGroupOpen
            }, () => { this.validateModal() });
        };
        this.handleConfigExcludeSpecificGroupClear = () => {
            this.setState({
                configExcludeSpecificGroup: [],
                configExcludeSpecificGroupOptions: [],
            }, () => { this.validateModal() });
        };
        this.handleConfigExcludeSpecificGroupCreateOption = newValue => {
            if (newValue.trim() && valid_filter(newValue) &&  !this.state.configExcludeSpecificGroupOptions.includes(newValue)) {
                this.setState({
                    configExcludeSpecificGroupOptions: [...this.state.configExcludeSpecificGroupOptions, newValue],
                    isConfigExcludeSpecificGroupOpen: false
                }, () => { this.validateModal() });
            }
        };

        // Handle Specific Group OC (modal)
        this.handleConfigSpecificGroupOCSelect = (event, selection) => {
            this.setState({
                configSpecificGroupOC: Array.isArray(selection) ? selection : [],
                isConfigSpecificGroupOCOpen: false
            }, () => { this.validateModal() });
        };
        this.handleConfigSpecificGroupOCClear = () => {
            this.setState({
                configSpecificGroupOC: [],
            }, () => { this.validateModal() });
        };
        this.handleConfigSpecificGroupOCToggle = (_event, isConfigSpecificGroupOCOpen) => {
            this.setState({
                isConfigSpecificGroupOCOpen
            }, () => { this.validateModal() });
        };

        // Handle Specific Group OC (main)
        this.handleSpecificGroupOCSelect = (event, selection) => {
            this.setState({
                memberOfSpecificGroupOC: Array.isArray(selection) ? selection : [],
                isSpecificGroupOCOpen: false
            }, () => { this.validateConfig() });
        };
        this.handleSpecificGroupOCClear = () => {
            this.setState({
                memberOfSpecificGroupOC: [],
            }, () => { this.validateConfig() });
        };
        this.handleSpecificGroupOCToggle = (_event, isSpecificGroupOCOpen) => {
            this.setState({
                isSpecificGroupOCOpen
            }, () => { this.validateConfig() });
        };
    }

    handleNavSelect(event, key) {
        this.setState({ activeTabKey: key });
    }

    handleNavSelectModal(event, key) {
        this.setState({ activeTabModalKey: key });
    }

    handleToggleFixupModal() {
        this.setState(prevState => ({
            fixupModalShow: !prevState.fixupModalShow,
            fixupDN: "",
            fixupFilter: "",
            savingModal: false,
        }));
    }

    validateFilterCreate(value, attr) {
        let result = valid_filter(value);
        let errObj = this.state.error;
        if (!result) {
            errObj[attr] = true;
        } else {
            errObj[attr] = false;
        }
        this.setState({
            error: errObj
        });
        return result;
    }

    validateModalFilterCreate(value, attr) {
        let result = valid_filter(value);
        let errObj = this.state.errorModal;
        if (!result) {
            errObj[attr] = true;
        } else {
            errObj[attr] = false;
        }
        this.setState({
            errorModal: errObj
        });
        return result;
    }

    validateConfig() {
        const errObj = {};
        let all_good = true;

        const reqAttrs = [
            'memberOfAttr'
        ];

        const reqLists = [
            'memberOfGroupAttr',
        ];

        const dnAttrs = [
            'memberOfConfigEntry'
        ];

        const dnLists = [
            'memberOfEntryScopeExcludeSubtree', 'memberOfEntryScope'
        ];

        // Check required attributes
        for (const attr of reqAttrs) {
            if (this.state[attr] === "") {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check required Lists are not empty
        for (const attr of reqLists) {
            if (this.state[attr].length === 0) {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check DN attrs
        for (const attr of dnAttrs) {
            if (this.state[attr] !== "" && !valid_dn(this.state[attr])) {
                errObj[attr] = true;
                all_good = false;
            }
        }

        // Validate the subtree lists
        for (const dn_list of dnLists) {
            for (const dn of this.state[dn_list]) {
                if (!valid_dn(dn)) {
                    errObj[dn_list] = true;
                    all_good = false;
                }
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;

            const attrLists = [
                'memberOfEntryScope',
                'memberOfEntryScopeExcludeSubtree', 'memberOfGroupAttr',
                'memberOfSpecificGroupOC',
            ];
            for (const check_attr of attrLists) {
                if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                    all_good = true;
                    break;
                }
            }

            const configAttrs = [
                'memberOfAllBackends', 'memberOfSkipNested',
                'memberOfConfigEntry', 'memberOfAttr', 'memberOfAutoAddOC'
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

    validateModal() {
        const errObj = {};
        let all_good = true;

        const reqAttrs = [
            'configDN', 'configAttr'
        ];

        const reqLists = [
            'configGroupAttr'
        ];

        const dnAttrs = [
            'configDN'
        ];

        const dnLists = [
            'configEntryScope', 'configEntryScopeExcludeSubtreeScope'
        ];

        // Check required attributes
        for (const attr of reqAttrs) {
            if (this.state[attr] === "") {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check required Lists are not empty
        for (const attr of reqLists) {
            if (this.state[attr].length === 0) {
                all_good = false;
                errObj[attr] = true;
            }
        }

        // Check DN attrs
        for (const attr of dnAttrs) {
            if (this.state[attr] !== "" && !valid_dn(this.state[attr])) {
                errObj[attr] = true;
                all_good = false;
                break;
            }
        }

        // Validate the subtree lists
        for (const dn_list of dnLists) {
            for (const dn of this.state[dn_list]) {
                if (!valid_dn(dn)) {
                    errObj[dn_list] = true;
                    all_good = false;
                }
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrLists = [
                'configEntryScope', 'configEntryScopeExcludeSubtreeScope',
                'configGroupAttr', 'configSpecificGroupOC',
            ];
            for (const check_attr of attrLists) {
                if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                    all_good = true;
                    break;
                }
            }

            const configAttrs = [
                'configDN', 'configAttr', 'configAutoAddOC',
                'configAllBackends', 'configSkipNested'
            ];
            for (const check_attr of configAttrs) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabledModal: !all_good,
            errorModal: errObj
        });
    }

    handleFieldChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validateConfig() });
    }

    handleModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validateModal() });
    }

    validFilterChange(value) {
        if (value.trim() && valid_filter(value) &&
            !this.state.configSpecificGroupOptions.includes(value) &&
            !this.state.configExcludeSpecificGroupOptions.includes(value) )
        {
            return true;
        }
        return false;
    }

    handleShowConfirmDelete() {
        this.setState({
            showConfirmDelete: true,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmDelete() {
        this.setState({
            showConfirmDelete: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    onChange(e) {
        // Generic handler for things that don't need validating
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value,
        });
    }

    toggleFixupModal() {
        this.setState(prevState => ({
            fixupModalShow: !prevState.fixupModalShow,
            fixupDN: "",
            fixupFilter: ""
        }));
    }

    handleRunFixup() {
        if (!this.state.fixupDN) {
            this.props.addNotification("warning", _("Fixup DN is required."));
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "memberof",
                "fixup",
                this.state.fixupDN
            ];

            if (this.state.fixupFilter) {
                cmd = [...cmd, "--filter", this.state.fixupFilter];
            }

            this.props.toggleLoadingHandler();
            log_cmd("handleRunFixup", "Run fixup MemberOf Plugin ", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        this.props.addNotification(
                            "success",
                            cockpit.format(_("Fixup task for $0 was successful"), this.state.fixupDN)
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            fixupModalShow: false
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Fixup task for $0 has failed $1"), this.state.fixupDN, errMsg.desc)
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            fixupModalShow: false
                        });
                    });
        }
    }

    handleOpenModal() {
        if (!this.state.memberOfConfigEntry) {
            this.setState({
                activeTabModalKey: 0,
                configEntryModalShow: true,
                newEntry: true,
                configDN: "",
                configAttr: "",
                configGroupAttr: [],
                configEntryScope: [],
                configEntryScopeExcludeSubtreeScope: [],
                configSpecificGroup: [],
                configExcludeSpecificGroup: [],
                configSpecificGroupOC: [],
                configAutoAddOC: "",
                configAllBackends: false,
                configSkipNested: false,
                saveBtnDisabledModal: true,
            });
        } else {
            let configScopeList = [];
            let configExcludeScopeList = [];
            let configGroupAttrObjectList = [];
            let configSpecificGroupList = [];
            let configExcludeSpecificGroupList = [];
            let configSpecificGroupOCList = [];
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "memberof",
                "config-entry",
                "show",
                this.state.memberOfConfigEntry
            ];

            this.props.toggleLoadingHandler();
            log_cmd("openMemberOfModal", "Fetch the MemberOf Plugin config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const configEntry = JSON.parse(content).attrs;
                        this.setState({
                            activeTabModalKey: 0,
                            configEntryModalShow: true,
                            newEntry: false,
                            saveBtnDisabledModal: true,
                            configDN: this.state.memberOfConfigEntry,
                            _configDN: this.state.memberOfConfigEntry,
                            configAttr:
                            configEntry.memberofattr === undefined
                                ? ""
                                : configEntry.memberofattr[0],
                            _configAttr: configEntry.memberofattr === undefined
                                ? ""
                                : configEntry.memberofattr[0],
                            configAutoAddOC:
                            configEntry.memberofautoaddoc === undefined
                                ? ""
                                : configEntry.memberofautoaddoc[0],
                            _configAutoAddOC: configEntry.memberofautoaddoc === undefined
                                ? ""
                                : configEntry.memberofautoaddoc[0],
                            configAllBackends: !(
                                configEntry.memberofallbackends === undefined ||
                            configEntry.memberofallbackends[0] === "off"
                            ),
                            _configAllBackends: !(
                                configEntry.memberofallbackends === undefined ||
                            configEntry.memberofallbackends[0] === "off"
                            ),
                            configSkipNested: !(
                                configEntry.memberofskipnested === undefined ||
                            configEntry.memberofskipnested[0] === "off"
                            ),
                            _configSkipNested: !(
                                configEntry.memberofskipnested === undefined ||
                            configEntry.memberofskipnested[0] === "off"
                            ),
                            configConfigEntry:
                            configEntry["nsslapd-pluginConfigArea"] === undefined
                                ? ""
                                : configEntry["nsslapd-pluginConfigArea"][0],
                            _configConfigEntry: configEntry["nsslapd-pluginConfigArea"] === undefined
                                ? ""
                                : configEntry["nsslapd-pluginConfigArea"][0],
                        });

                        if (configEntry.memberofgroupattr === undefined) {
                            this.setState({ configGroupAttr: [], _configGroupAttr: [] });
                        } else {
                            for (const value of configEntry.memberofgroupattr) {
                                configGroupAttrObjectList = [...configGroupAttrObjectList, value];
                            }
                            this.setState({
                                configGroupAttr: configGroupAttrObjectList,
                                _configGroupAttr: [...configGroupAttrObjectList],
                            });
                        }
                        if (configEntry.memberofentryscope === undefined) {
                            this.setState({ configEntryScope: [], _configEntryScope: [] });
                        } else {
                            for (const value of configEntry.memberofentryscope) {
                                configScopeList = [...configScopeList, value];
                            }
                            this.setState({
                                configEntryScope: configScopeList,
                                _configEntryScope: [...configScopeList],
                            });
                        }
                        if (configEntry.memberofentryscopeexcludesubtree === undefined) {
                            this.setState({ configEntryScopeExcludeSubtreeScope: [], _configEntryScopeExcludeSubtreeScope: [] });
                        } else {
                            for (const value of configEntry.memberofentryscopeexcludesubtree) {
                                configExcludeScopeList = [...configExcludeScopeList, value];
                            }
                            this.setState({
                                configEntryScopeExcludeSubtreeScope: configExcludeScopeList,
                                _configEntryScopeExcludeSubtreeScope: [...configExcludeScopeList],
                            });
                        }
                        if (configEntry.memberofspecificgroupfilter === undefined) {
                            this.setState({ configSpecificGroup: [], _configSpecificGroup: [] });
                        } else {
                            for (const value of configEntry.memberofspecificgroupfilter) {
                                configSpecificGroupList = [...configSpecificGroupList, value];
                            }
                            this.setState({
                                configSpecificGroup: configSpecificGroupList,
                                _configSpecificGroup: [...configSpecificGroupList],
                            });
                        }
                        if (configEntry.memberofexcludespecificgroupfilter === undefined) {
                            this.setState({ configExcludeSpecificGroup: [], _configExcludeSpecificGroup: [] });
                        } else {
                            for (const value of configEntry.memberofexcludespecificgroupfilter) {
                                configExcludeSpecificGroupList = [...configExcludeSpecificGroupList, value];
                            }
                            this.setState({
                                configExcludeSpecificGroup: configExcludeSpecificGroupList,
                                _configExcludeSpecificGroup: [...configExcludeSpecificGroupList],
                            });
                        }
                        if (configEntry.memberofspecificgroupoc === undefined) {
                            this.setState({ configSpecificGroupOC: [], _configSpecificGroupOC: [] });
                        } else {
                            for (const value of configEntry.memberofspecificgroupoc) {
                                configSpecificGroupOCList = [...configSpecificGroupOCList, value];
                            }
                            this.setState({
                                configSpecificGroupOC: configSpecificGroupOCList,
                                _configSpecificGroupOC: [...configSpecificGroupOCList],
                            });
                        }
                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configDN: this.state.memberOfConfigEntry,
                            configAttr: [],
                            configGroupAttr: [],
                            configEntryScope: [],
                            configEntryScopeExcludeSubtreeScope: [],
                            configSpecificGroup: [],
                            configExcludeSpecificGroup: [],
                            configSpecificGroupOC: [],
                            configAutoAddOC: "",
                            configAllBackends: false,
                            configSkipNested: false,
                            _configDN: this.state.memberOfConfigEntry,
                            _configAttr: "",
                            _configAutoAddOC: "",
                            _configAllBackends: false,
                            _configSkipNested: false,
                            _configConfigEntry: "",
                            _configGroupAttr: [],
                            _configEntryScope: [],
                            _configEntryScopeExcludeSubtreeScope: [],
                            _configSpecificGroup: [],
                            _configExcludeSpecificGroup: [],
                            _configSpecificGroupOC: [],
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    handleCloseModal() {
        this.setState({ configEntryModalShow: false });
    }

    // Modal config entry update
    cmdOperation(action) {
        const {
            configDN,
            configAttr,
            configGroupAttr,
            configEntryScope,
            configEntryScopeExcludeSubtreeScope,
            configAutoAddOC,
            configAllBackends,
            configSkipNested,
            configSpecificGroup,
            configExcludeSpecificGroup,
            configSpecificGroupOC,
        } = this.state;

        if (configAttr.length === 0 || configGroupAttr.length === 0) {
            this.props.addNotification(
                "warning",
                _("Config Attribute and Group Attribute are required.")
            );
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "memberof",
                "config-entry",
                action,
                configDN,
                "--attr",
                configAttr || action === "add" ? configAttr : "delete",
                "--allbackends",
                configAllBackends ? "on" : "off",
                "--skipnested",
                configSkipNested ? "on" : "off",
            ];

            // Delete attributes if the user set an empty value to the field
            if (configEntryScope.length !== 0) {
                cmd = [...cmd, "--scope"];
                for (const value of configEntryScope) {
                    cmd = [...cmd, value];
                }
            } else if (action !== "add") {
                cmd = [...cmd, "--scope", "delete"];
            }

            if (configAutoAddOC !== "") {
                cmd = [...cmd, "--autoaddoc", configAutoAddOC];
            } else if (action !== "add") {
                cmd = [...cmd, "--autoaddoc", "delete"];
            }

            if (configEntryScopeExcludeSubtreeScope.length !== 0) {
                cmd = [...cmd, "--exclude"];
                for (const value of configEntryScopeExcludeSubtreeScope) {
                    cmd = [...cmd, value];
                }
            } else if (action !== "add") {
                cmd = [...cmd, "--exclude", "delete"];
            }
            if (configSpecificGroup.length !== 0) {
                cmd = [...cmd, "--specific-group-filter"];
                for (const value of configSpecificGroup) {
                    cmd = [...cmd, value];
                }
            } else if (action !== "add") {
                cmd = [...cmd, "--specific-group", "delete"];
            }
            if (configExcludeSpecificGroup.length !== 0) {
                cmd = [...cmd, "--exclude-specific-group-filter"];
                for (const value of configExcludeSpecificGroup) {
                    cmd = [...cmd, value];
                }
            } else if (action !== "add") {
                cmd = [...cmd, "--exclude-specific-group", "delete"];
            }
            if (configSpecificGroupOC.length !== 0) {
                cmd = [...cmd, "--specific-group-oc"];
                for (const value of configSpecificGroupOC) {
                    cmd = [...cmd, value];
                }
            } else if (action !== "add") {
                cmd = [...cmd, "--specific-group-oc", "delete"];
            }

            if (configGroupAttr.length !== 0) {
                cmd = [...cmd, "--groupattr"];
                for (const value of configGroupAttr) {
                    cmd = [...cmd, value];
                }
            } else if (action !== "add") {
                cmd = [...cmd, "--groupattr", "delete"];
            }

            this.setState({
                savingModal: true,
            });

            log_cmd("memberOfOperation", `Do the ${action} operation on the MemberOf Plugin`, cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        console.info("memberOfOperation", "Result", content);
                        const value = action === "set" ? "edit" : "add";
                        this.props.addNotification(
                            "success",
                            cockpit.format(_("Config entry $0 was successfully $1"), configDN, value + "ed")
                        );
                        this.props.pluginListHandler();
                        this.handleCloseModal();
                        this.setState({
                            savingModal: false,
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error during the config entry $0 operation - $1"), action, errMsg.desc)
                        );
                        this.props.pluginListHandler();
                        this.handleCloseModal();
                        this.setState({
                            savingModal: false,
                        });
                    });
        }
    }

    deleteConfig() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "memberof",
            "config-entry",
            "delete",
            this.state.configDN
        ];

        this.setState({
            modalSpinning: true,
        });
        log_cmd("deleteConfig", "Delete the MemberOf Plugin config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        cockpit.format(_("Config entry $0 was successfully deleted"), this.state.configDN)
                    );
                    this.props.pluginListHandler();
                    this.handleCloseModal();
                    this.closeConfirmDelete();
                    this.setState({
                        modalSpinning: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry removal operation - $0"), errMsg.desc)
                    );
                    this.props.pluginListHandler();
                    this.handleCloseModal();
                    this.closeConfirmDelete();
                    this.setState({
                        modalSpinning: false,
                    });
                });
    }

    async handleAddConfig() {
        const params = {
            serverId: this.props.serverId,
            configDN: this.state.configDN,
        };

        try {
            const exists = await parentExists(params);
            if (exists) {
                this.cmdOperation("add");
            } else {
                this.props.addNotification(
                    "error",
                    cockpit.format(
                        _("Config DN \"$0\" does not exist, it must be a full DN!"),
                        params.configDN
                    ));
            }
        } catch (err) {
            console.error("Error checking DN:", err);
            this.props.addNotification("error", cockpit.format(_("Error checking DN")));
        }
    }

    handleEditConfig() {
        this.cmdOperation("set");
    }

    updateFields() {
        let memberOfGroupAttrObjectList = [];
        let memberOfEntryScopeList = [];
        let getSchemamemberOfExcludeScopeList = [];



        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "MemberOf Plugin");
            this.setState({
                memberOfAttr:
                    pluginRow.memberofattr === undefined
                        ? ""
                        : pluginRow.memberofattr[0],
                memberOfAutoAddOC:
                    pluginRow.memberofautoaddoc === undefined
                        ? ""
                        : pluginRow.memberofautoaddoc[0],
                memberOfAllBackends: !(
                    pluginRow.memberofallbackends === undefined ||
                    pluginRow.memberofallbackends[0] === "off"
                ),
                memberOfSkipNested: !(
                    pluginRow.memberofskipnested === undefined ||
                    pluginRow.memberofskipnested[0] === "off"
                ),
                memberOfConfigEntry:
                    pluginRow["nsslapd-pluginConfigArea"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginConfigArea"][0],
                memberOfSpecificGroup:
                    pluginRow.memberofspecificgroupfilter === undefined
                        ? []
                        : pluginRow.memberofspecificgroupfilter,
                memberOfExcludeSpecificGroup:
                    pluginRow.memberofexcludespecificgroupfilter === undefined
                        ? []
                        : pluginRow.memberofexcludespecificgroupfilter,
                memberOfSpecificGroupOC:
                    pluginRow.memberofspecificgroupoc === undefined
                        ? []
                        : pluginRow.memberofspecificgroupoc,
                _memberOfAttr:
                    pluginRow.memberofattr === undefined
                        ? ""
                        : pluginRow.memberofattr[0],
                _memberOfAutoAddOC:
                    pluginRow.memberofautoaddoc === undefined
                        ? ""
                        : pluginRow.memberofautoaddoc[0],
                _memberOfAllBackends: !(
                    pluginRow.memberofallbackends === undefined ||
                    pluginRow.memberofallbackends[0] === "off"
                ),
                _memberOfSkipNested: !(
                    pluginRow.memberofskipnested === undefined ||
                    pluginRow.memberofskipnested[0] === "off"
                ),
                _memberOfConfigEntry:
                    pluginRow["nsslapd-pluginConfigArea"] === undefined
                        ? ""
                        : pluginRow["nsslapd-pluginConfigArea"][0],
                _memberOfSpecificGroup:
                    pluginRow.memberofspecificgroup === undefined
                        ? []
                        : [...pluginRow.memberofspecificgroup],
                _memberOfExcludeSpecificGroup:
                    pluginRow.memberofexcludespecificgroup === undefined
                        ? []
                        : [...pluginRow.memberofexcludespecificgroup],
                _memberOfSpecificGroupOC:
                    pluginRow.memberofspecificgroupoc === undefined
                        ? []
                        : [...pluginRow.memberofspecificgroupoc],
                _memberOfSpecificGroupOpen: false,
                _memberOfExcludeSpecificGroupOpen: false,
                _memberOfSpecificGroupOCOpen: false,
            });
            if (pluginRow.memberofgroupattr === undefined) {
                this.setState({ memberOfGroupAttr: [], _memberOfGroupAttr: [] });
            } else {
                for (const value of pluginRow.memberofgroupattr) {
                    memberOfGroupAttrObjectList = [...memberOfGroupAttrObjectList, value];
                }
                this.setState({
                    memberOfGroupAttr: memberOfGroupAttrObjectList,
                    _memberOfGroupAttr: [...memberOfGroupAttrObjectList],
                });
            }
            if (pluginRow.memberofentryscope === undefined) {
                this.setState({ memberOfEntryScope: [], _memberOfEntryScope: [] });
            } else {
                for (const value of pluginRow.memberofentryscope) {
                    memberOfEntryScopeList = [...memberOfEntryScopeList, value];
                }
                this.setState({
                    memberOfEntryScope: memberOfEntryScopeList,
                    _memberOfEntryScope: [...memberOfEntryScopeList],
                });
            }
            if (pluginRow.memberofentryscopeexcludesubtree === undefined) {
                this.setState({ memberOfEntryScopeExcludeSubtree: [], _memberOfEntryScopeExcludeSubtree: [] });
            } else {
                for (const value of pluginRow.memberofentryscopeexcludesubtree) {
                    getSchemamemberOfExcludeScopeList = [...getSchemamemberOfExcludeScopeList, value];
                }
                this.setState({
                    memberOfEntryScopeExcludeSubtree: getSchemamemberOfExcludeScopeList,
                    _memberOfEntryScopeExcludeSubtree: [...getSchemamemberOfExcludeScopeList]
                });
            }
        }
        this.setState({
            firstLoad: false
        });
    }

    handleSaveConfig() {
        const {
            memberOfAttr,
            memberOfGroupAttr,
            memberOfEntryScope,
            memberOfEntryScopeExcludeSubtree,
            memberOfAutoAddOC,
            memberOfAllBackends,
            memberOfSkipNested,
            memberOfConfigEntry,
            memberOfSpecificGroup,
            memberOfExcludeSpecificGroup,
            memberOfSpecificGroupOC,
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "memberof",
            "set",
            "--attr",
            memberOfAttr || "delete",
            "--config-entry",
            memberOfConfigEntry || "delete",
            "--allbackends",
            memberOfAllBackends ? "on" : "off",
            "--skipnested",
            memberOfSkipNested ? "on" : "off",
            "--autoaddoc",
            memberOfAutoAddOC || "delete",
        ];

        // Delete attributes if the user set an empty value to the field
        cmd = [...cmd, "--scope"];
        if (memberOfEntryScope.length !== 0) {
            for (const value of memberOfEntryScope) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--exclude"];
        if (memberOfEntryScopeExcludeSubtree.length !== 0) {
            for (const value of memberOfEntryScopeExcludeSubtree) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--specific-group-filter"];
        if (memberOfSpecificGroup.length !== 0) {
            for (const value of memberOfSpecificGroup) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--exclude-specific-group-filter"];
        if (memberOfExcludeSpecificGroup.length !== 0) {
            for (const value of memberOfExcludeSpecificGroup) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--specific-group-oc"];
        if (memberOfSpecificGroupOC.length !== 0) {
            for (const value of memberOfSpecificGroupOC) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        cmd = [...cmd, "--groupattr"];
        if (memberOfGroupAttr.length !== 0) {
            for (const value of memberOfGroupAttr) {
                cmd = [...cmd, value];
            }
        } else {
            cmd = [...cmd, "delete"];
        }

        this.setState({
            saving: true
        });
        log_cmd("handleSaveConfig", `Save MemberOf Plugin`, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        _("Successfully updated MemberOf Plugin")
                    );
                    this.setState({
                        saving: false,
                        saveBtnDisabled: true,
                    });
                    this.props.pluginListHandler();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if ('info' in errMsg) {
                        errMsg = errMsg.desc + " " + errMsg.info;
                    } else {
                        errMsg = errMsg.desc;
                    }
                    this.props.addNotification(
                        "error", cockpit.format(_("Error during update - $0"), errMsg)
                    );
                    this.setState({
                        saving: false
                    });
                    this.props.pluginListHandler();
                });
    }

    openSpecificGroupAddModal() {
        this.setState({
            isSpecificGroupModalOpen: true,
            groupFilter: "",
            groupFilterType: "include",
        });
    }

    openSpecificExcludeGroupAddModal() {
        this.setState({
            isSpecificGroupModalOpen: true,
            groupFilter: "",
            groupFilterType: "exclude"
        });
    }

    closeSpecificGroupAddModal() {
        this.setState({
            isSpecificGroupModalOpen: false,
            groupFilter: "",
            groupFilterType: "include"
        });
    }

    openDeleteFilterConfirmation(filter) {
        this.setState({
            showDeleteFilterConfirmation: true,
            groupFilterType: "include",
            groupFilter: filter,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeDeleteFilterConfirmation() {
        this.setState({
            showDeleteFilterConfirmation: false,
            groupFilterType: "",
            groupFilter: "",
        });
    }

    openDeleteExcludeFilterConfirmation(filter) {
        this.setState({
            showDeleteExcludeFilterConfirmation: true,
            groupFilterType: "exclude",
            groupFilter: filter,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeDeleteExcludeFilterConfirmation() {
        this.setState({
            showDeleteExcludeFilterConfirmation: false,
            groupFilterType: "",
            groupFilter: "",
        });
    }

    handleAddDelSpecificGroupFilter(op, filter) {
        this.setState({
            modalSpinning: true
        });

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "memberof"
        ];

        if (this.state.configEntryModalShow) {
            cmd = [...cmd, "config-entry"];
            cmd = [...cmd, op === "add" ? "add-attr" : "del-attr", this.state.configDN];
        } else {
            cmd = [...cmd, op === "add" ? "add-attr" : "del-attr"];
        }
        cmd = [...cmd, this.state.groupFilterType === "include" ? "--specific-group-filter" : "--exclude-specific-group-filter", filter];

        log_cmd("handleAddDelSpecificGroupFilter", op, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    this.props.pluginListHandler();
                    this.props.addNotification(
                        "success",
                        _("Successfully updated MemberOf Plugin")
                    );
                    this.setState({
                        modalSpinning: false,
                        showDeleteFilterConfirmation: false,
                        showDeleteExcludeFilterConfirmation: false,
                        isSpecificGroupModalOpen: false,
                        isExcludeSpecificGroupModalOpen: false,
                    });
                    if (this.state.configEntryModalShow) {
                        this.handleOpenModal()
                    }
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if ('info' in errMsg) {
                        errMsg = errMsg.desc + " " + errMsg.info;
                    } else {
                        errMsg = errMsg.desc;
                    }
                    this.props.addNotification(
                        "error", cockpit.format(_("Error during update - $0"), errMsg)
                    );
                    this.setState({
                        modalSpinning: false,
                        showDeleteFilterConfirmation: false,
                        showDeleteExcludeFilterConfirmation: false,
                        isSpecificGroupModalOpen: false,
                        isExcludeSpecificGroupModalOpen: false,
                    });
                    this.props.pluginListHandler();
                    if (this.state.configEntryModalShow) {
                        this.handleOpenModal()
                    }
                });
    }

    handleExcludeSpecificGroupAdd() {
        this.setState({
            memberOfExcludeSpecificGroup: [...this.state.memberOfExcludeSpecificGroup, ""],
            isExcludeSpecificGroupOpen: true
        });
    }

    handleExcludeSpecificGroupDelete(index) {
        this.setState({
            memberOfExcludeSpecificGroup: this.state.memberOfExcludeSpecificGroup.filter((_, i) => i !== index)
        });
    }


    render() {
        const {
            memberOfAttr,
            memberOfGroupAttr,
            memberOfEntryScope,
            memberOfEntryScopeExcludeSubtree,
            memberOfAutoAddOC,
            memberOfAllBackends,
            memberOfSkipNested,
            memberOfConfigEntry,
            memberOfSpecificGroup,
            memberOfExcludeSpecificGroup,
            memberOfSpecificGroupOC,
            isSpecificGroupOCOpen,
            fixupModalShow,
            fixupDN,
            fixupFilter,
            error,
            saving,
            saveBtnDisabled,
            isSubtreeScopeOpen,
            isExcludeScopeOpen,
            // Filter modal
            isSpecificGroupModalOpen,
            groupFilter,
            groupFilterType,
        } = this.state;

        // Bundle up all the config entry modal functions and data
        const configModalHandlers = {
            handleEditConfig: this.handleEditConfig,
            handleAddConfig: this.handleAddConfig,
            handleCloseModal: this.handleCloseModal,
            handleNavSelectModal: this.handleNavSelectModal,
            handleModalChange: this.handleModalChange,
            handleFieldChange: this.handleFieldChange,
            handleConfigAttrSelect: this.handleConfigAttrSelect,
            handleConfigAttrClear: this.handleConfigAttrClear,
            handleConfigGroupAttrSelect: this.handleConfigGroupAttrSelect,
            handleConfigGroupAttrClear: this.handleConfigGroupAttrClear,
            handleConfigScopeSelect: this.handleConfigScopeSelect,
            handleConfigScopeClear: this.handleConfigScopeClear,
            handleConfigCreateOption: this.handleConfigCreateOption,
            handleConfigSpecificGroupSelect: this.handleConfigSpecificGroupSelect,
            handleConfigSpecificGroupClear: this.handleConfigSpecificGroupClear,
            handleConfigSpecificGroupCreateOption: this.handleConfigSpecificGroupCreateOption,
            handleConfigExcludeSpecificGroupSelect: this.handleConfigExcludeSpecificGroupSelect,
            handleConfigExcludeSpecificGroupClear: this.handleConfigExcludeSpecificGroupClear,
            handleConfigExcludeSpecificGroupCreateOption: this.handleConfigExcludeSpecificGroupCreateOption,
            handleConfigSpecificGroupOCToggle: this.handleConfigSpecificGroupOCToggle,
            handleConfigScopeToggle: this.handleConfigScopeToggle,
            handleConfigExcludeScopeToggle: this.handleConfigExcludeScopeToggle,
            handleConfigSpecificGroupOCSelect: this.handleConfigSpecificGroupOCSelect,
            handleConfigSpecificGroupOCClear: this.handleConfigSpecificGroupOCClear,
            handleConfigExcludeScopeSelect: this.handleConfigExcludeScopeSelect,
            handleConfigExcludeScopeClear: this.handleConfigExcludeScopeClear,
            handleConfigExcludeCreateOption: this.handleConfigExcludeCreateOption,
            handleConfigSpecificGroupToggle: this.handleConfigSpecificGroupToggle,
            openSpecificGroupAddModal: this.openSpecificGroupAddModal,
            openSpecificExcludeGroupAddModal: this.openSpecificExcludeGroupAddModal,
            openDeleteFilterConfirmation: this.openDeleteFilterConfirmation,
            openDeleteExcludeFilterConfirmation: this.openDeleteExcludeFilterConfirmation,
            validateModalFilterCreate: this.validateModalFilterCreate,
        };

        const configModalOpeners = {
            isConfigSubtreeScopeOpen: this.state.isConfigSubtreeScopeOpen,
            isConfigExcludeScopeOpen: this.state.isConfigExcludeScopeOpen,
            isConfigSpecificGroupOpen: this.state.isConfigSpecificGroupOpen,
            isConfigExcludeSpecificGroupOpen: this.state.isConfigExcludeSpecificGroupOpen,
            isConfigSpecificGroupOCOpen: this.state.isConfigSpecificGroupOCOpen,
        };

        const configModalSettings = {
            configDN: this.state.configDN,
            configAttr: this.state.configAttr,
            configGroupAttr: this.state.configGroupAttr,
            configEntryScope: this.state.configEntryScope,
            configEntryScopeExcludeSubtreeScope: this.state.configEntryScopeExcludeSubtreeScope,
            configAutoAddOC: this.state.configAutoAddOC,
            configAllBackends: this.state.configAllBackends,
            configSkipNested: this.state.configSkipNested,
            configSpecificGroupOC: this.state.configSpecificGroupOC,
            configExcludeSpecificGroup: this.state.configExcludeSpecificGroup,
            configSpecificGroup: this.state.configSpecificGroup,
            configEntryScopeOptions: this.state.configEntryScopeOptions,
            configEntryScopeExcludeOptions: this.state.configEntryScopeExcludeOptions,
            configSpecificGroupOptions: this.state.configSpecificGroupOptions,
            configExcludeSpecificGroupOptions: this.state.configExcludeSpecificGroupOptions,
        };

        let saveBtnName = _("Save Config");
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = _("Saving Config ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        return (
            <div>
                <MemberOfFixupTaskModal
                    fixupModalShow={fixupModalShow}
                    handleToggleFixupModal={this.handleToggleFixupModal}
                    handleRunFixup={this.handleRunFixup}
                    fixupDN={fixupDN}
                    fixupFilter={fixupFilter}
                    handleFieldChange={this.handleFieldChange}
                />
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="MemberOf Plugin"
                    pluginName="MemberOf"
                    cmdName="memberof"
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Tabs isFilled className="ds-margin-top-lgZZZ" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={0} title={<TabTitleText>{_("Plugin Settings")}</TabTitleText>}>
                            <Form isHorizontal autoComplete="off" className="ds-margin-top-xlg">
                                <Grid title={_("Specifies the attribute in the user entry for the Directory Server to manage to reflect group membership (memberOfAttr)")}>
                                    <GridItem className="ds-label" span={3}>
                                        {_("Membership Attribute")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TypeaheadSelect
                                            selected={memberOfAttr}
                                            onSelect={this.handleMemberOfAttrSelect}
                                            onClear={this.handleMemberOfAttrClear}
                                            options={["memberOf"]}
                                            placeholder={_("Type a member attribute...")}
                                            noResultsText={_("There are no matching entries")}
                                            validated={error.memberOfAttr ? "error" : "default"}
                                            ariaLabel="Type a member attribute"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title={_("Specifies the attribute in the group entry to use to identify the DNs of group members (memberOfGroupAttr)")}>
                                    <GridItem className="ds-label" span={3}>
                                        {_("Group Attribute")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TypeaheadSelect
                                            isMulti
                                            hasCheckbox
                                            selected={memberOfGroupAttr}
                                            onSelect={this.handleMemberOfGroupAttrSelect}
                                            onClear={this.handleMemberOfGroupAttrClear}
                                            options={["member", "memberCertificate", "uniqueMember"]}
                                            placeholder={_("Type a member group attribute...")}
                                            noResultsText={_("There are no matching entries")}
                                            validated={error.memberOfGroupAttr ? "error" : "default"}
                                            ariaLabel="Type a member group attribute"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("If an entry does not have an object class that allows the memberOf attribute then the memberOf plugin will automatically add the object class listed in the memberOfAutoAddOC parameter")}>
                                    <GridItem className="ds-label" span={3}>
                                        {_("Auto Add OC")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <FormSelect
                                            id="memberOfAutoAddOC"
                                            value={memberOfAutoAddOC}
                                            onChange={(event, value) => {
                                                this.handleFieldChange(event);
                                            }}
                                            aria-label="FormSelect Input"
                                        >
                                            <FormSelectOption key="no_setting" value="" label="-" />
                                            {this.props.objectClasses.map((attr) => (
                                                <FormSelectOption key={attr} value={attr.toLowerCase()} label={attr} />
                                            ))}
                                        </FormSelect>
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Specifies whether to skip nested groups or not (memberOfSkipNested)")}>
                                    <GridItem className="ds-left-margin" span={3}>
                                        <Checkbox
                                            id="memberOfSkipNested"
                                            isChecked={memberOfSkipNested}
                                            onChange={(e, checked) => { this.handleFieldChange(e) }}
                                            title={_("Specifies wherher to skip nested groups or not (memberOfSkipNested)")}
                                            label={_("Skip Nested Groups")}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Specifies whether to search the local suffix for user entries on all available suffixes (memberOfAllBackends)")}>
                                    <GridItem className="ds-left-margin" span={3}>
                                        <Checkbox
                                            id="memberOfAllBackends"
                                            isChecked={memberOfAllBackends}
                                            onChange={(e, checked) => { this.handleFieldChange(e) }}
                                            label={_("All Backends")}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("The value to set as nsslapd-pluginConfigArea")}>
                                    <GridItem className="ds-label" span={3}>
                                        {_("Shared Config Entry")}
                                    </GridItem>
                                    <GridItem className="ds-right-margin" span={9}>
                                        {memberOfConfigEntry !== "" && (
                                            <TextInput
                                                value={memberOfConfigEntry}
                                                type="text"
                                                id="memberOfConfigEntry"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="memberOfConfigEntry"
                                                readOnlyVariant={'plain'}
                                            />
                                        )}
                                        {memberOfConfigEntry === "" && (
                                            <Button
                                                variant="primary"
                                                onClick={this.handleOpenModal}
                                            >
                                                {_("Create Config")}
                                            </Button>
                                        )}
                                    </GridItem>
                                </Grid>
                                {memberOfConfigEntry !== "" && (
                                    <Grid>
                                        <GridItem offset={3} span={3}>
                                            <Button
                                                variant="primary"
                                                onClick={this.handleOpenModal}
                                            >
                                                {memberOfConfigEntry === "" ? _("Create Config") : _("Manage Config")}
                                            </Button>
                                        </GridItem>
                                    </Grid>
                                )}
                                <Grid title={_("The fixup task will add the memberOf attribute to entries that are missing it.  This is typically only run once after enabling or changing the plugin.")}>
                                    <GridItem className="ds-label ds-margin-top" span={3}>
                                        {_("MemberOf Fixup Task")}<WrenchIcon className="ds-left-margin" />
                                    </GridItem>
                                    <GridItem span={9}>
                                        <Button className="ds-margin-top" variant="secondary" onClick={this.handleToggleFixupModal}>
                                            {_("Run Task")}
                                        </Button>
                                    </GridItem>
                                </Grid>
                            </Form>
                        </Tab>
                        <Tab eventKey={1} title={<TabTitleText>{_("Subtree Scope")}</TabTitleText>}>
                            <Form isHorizontal autoComplete="off" className="ds-margin-top-xlg">
                                <Grid className="ds-margin-top" title={_("Specifies backends or multiple-nested suffixes for the MemberOf plug-in to work on (memberOfEntryScope)")}>
                                    <GridItem className="ds-label" span={3}>
                                        {_("Subtree Scope")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TypeaheadSelect
                                            isMulti
                                            selected={memberOfEntryScope}
                                            onSelect={this.handleSubtreeScopeSelect}
                                            onClear={this.handleSubtreeScopeClear}
                                            options={this.state.memberOfEntryScopeOptions}
                                            isCreatable
                                            onCreateOption={this.handleSubtreeScopeCreateOption}
                                            validateCreate={(value) => valid_dn(value)}
                                            placeholder={_("Type a subtree DN...")}
                                            noResultsText={_("There are no matching entries")}
                                            validated={error.memberOfEntryScope ? "error" : "default"}
                                            onToggle={this.handleSubtreeScopeToggle}
                                            isOpen={isSubtreeScopeOpen}
                                            ariaLabel="Type a subtree DN"
                                        />
                                        <FormHelperText  >
                                            {"Values must be valid DN's"}
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Specifies backends or multiple-nested suffixes for the MemberOf plug-in to exclude (memberOfEntryScopeExcludeSubtree)")}>
                                    <GridItem className="ds-label" span={3}>
                                        {_("Exclude Subtree")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TypeaheadSelect
                                            isMulti
                                            selected={memberOfEntryScopeExcludeSubtree}
                                            onSelect={this.handleExcludeScopeSelect}
                                            onClear={this.handleExcludeScopeClear}
                                            options={this.state.memberOfEntryScopeExcludeOptions}
                                            isCreatable
                                            onCreateOption={this.handleExcludeCreateOption}
                                            validateCreate={(value) => valid_dn(value)}
                                            placeholder={_("Type a subtree DN...")}
                                            noResultsText={_("There are no matching entries")}
                                            validated={error.memberOfEntryScopeExcludeSubtree ? "error" : "default"}
                                            onToggle={this.handleExcludeScopeToggle}
                                            isOpen={isExcludeScopeOpen}
                                            ariaLabel="Type a subtree DN"
                                        />
                                        <FormHelperText  >
                                            {_("Values must be valid DN's")}
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                            </Form>
                        </Tab>

                        <Tab eventKey={2} title={<TabTitleText>{_("Specific Group Scope")}</TabTitleText>}>
                            <MemberOfTable
                                title={"Specific Group Filters"}
                                rows={memberOfSpecificGroup}
                                deleteAttr={this.openDeleteFilterConfirmation}
                            />
                            <Button
                                id="specific-group-filter"
                                className="ds-margin-top"
                                key="specific-group-filter"
                                variant="secondary"
                                onClick={this.openSpecificGroupAddModal}
                            >
                                Add filter
                            </Button>
                            <MemberOfTable
                                title={"Specific Group Exclude Filters"}
                                rows={memberOfExcludeSpecificGroup}
                                deleteAttr={this.openDeleteExcludeFilterConfirmation}
                            />
                            <Button
                                id="specific-group-exclude-filter"
                                className="ds-margin-top"
                                key="specific-group-exclude-filter"
                                variant="secondary"
                                onClick={this.openSpecificExcludeGroupAddModal}
                            >
                                Add exclude filter
                            </Button>
                            <Form isHorizontal autoComplete="off" className="ds-margin-top-xlg">
                                <Grid title={_("Specifies the objectclasses for the specific groups to include/exclude (memberOfSpecificGroupOC)")}>
                                    <GridItem className="ds-label" span={3}>
                                        {"Specific Group OC"}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TypeaheadSelect
                                            isMulti
                                            selected={memberOfSpecificGroupOC}
                                            onSelect={this.handleSpecificGroupOCSelect}
                                            onClear={this.handleSpecificGroupOCClear}
                                            options={this.props.objectClasses}
                                            placeholder={_("Type a objectclass...")}
                                            noResultsText="There are no matching objectclasses"
                                            ariaLabel="Type a objectclass"
                                            onToggle={this.handleSpecificGroupOCToggle}
                                            isOpen={isSpecificGroupOCOpen}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                        </Tab>
                    </Tabs>
                    <Button
                        className="ds-margin-top-lg"
                        key="at"
                        isLoading={saving}
                        spinnerAriaValueText={saving ? _("Loading") : undefined}
                        variant="primary"
                        onClick={this.handleSaveConfig}
                        {...extraPrimaryProps}
                        isDisabled={saveBtnDisabled || saving}
                    >
                        {saveBtnName}
                    </Button>
                </PluginBasicConfig>

                <MemberOfConfigEntryModal
                    handlers={configModalHandlers}
                    openers={configModalOpeners}
                    settings={configModalSettings}
                    activeTabModalKey={this.state.activeTabModalKey}
                    objectClasses={this.props.objectClasses}
                    newEntry={this.state.newEntry}
                    errorModal={this.state.errorModal}
                    configEntryModalShow={this.state.configEntryModalShow}
                    saveBtnDisabledModal={this.state.saveBtnDisabledModal}
                    savingModal={this.state.savingModal}
                    extraPrimaryProps={this.state.extraPrimaryProps}
                    validateModalFilterCreate={this.validateModalFilterCreate}
                />

                <MemberOfSpecificGroupFilterModal
                    isSpecificGroupModalOpen={isSpecificGroupModalOpen}
                    closeSpecificGroupAddModal={this.closeSpecificGroupAddModal}
                    handleAddDelSpecificGroupFilter={this.handleAddDelSpecificGroupFilter}
                    groupFilter={groupFilter}
                    groupFilterType={groupFilterType}
                    handleFieldChange={this.handleFieldChange}
                    modalSpinning={this.state.modalSpinning}
                    validFilterChange={this.validFilterChange}
                />

                <DoubleConfirmModal
                    showModal={this.state.showConfirmDelete}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.onChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.configDN}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete MemberOf Config Entry")}
                    mMsg={_("Are you sure you want to delete this config entry?")}
                    mSpinningMsg={_("Deleting ...")}
                    mBtnName={_("Delete")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showDeleteFilterConfirmation}
                    closeHandler={this.closeDeleteFilterConfirmation}
                    handleChange={this.onChange}
                    actionHandler={() => this.handleAddDelSpecificGroupFilter("delete", this.state.groupFilter)}
                    spinning={this.state.modalSpinning}
                    item={this.state.groupFilter}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete Specific Group Filter")}
                    mMsg={_("Are you sure you want to delete this filter?")}
                    mSpinningMsg={_("Deleting ...")}
                    mBtnName={_("Delete")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showDeleteExcludeFilterConfirmation}
                    closeHandler={this.closeDeleteExcludeFilterConfirmation}
                    handleChange={this.onChange}
                    actionHandler={() => this.handleAddDelSpecificGroupFilter("delete", this.state.groupFilter)}
                    spinning={this.state.modalSpinning}
                    item={this.state.groupFilter}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete Exclude Specific Group Filter")}
                    mMsg={_("Are you sure you want to delete this filter?")}
                    mSpinningMsg={_("Deleting ...")}
                    mBtnName={_("Delete")}
                />
            </div>
        );
    }
}

MemberOf.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    objectClasses: PropTypes.array,
    toggleLoadingHandler: PropTypes.func
};

MemberOf.defaultProps = {
    rows: [],
    serverId: "",
    objectClasses: [],
};

export default MemberOf;
