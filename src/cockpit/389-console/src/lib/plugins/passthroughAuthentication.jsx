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
    NumberInput,
    Select,
    SelectOption,
    SelectVariant,
    Tab,
    Tabs,
    TabTitleText,
    TextInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import { PassthroughAuthURLsTable, PassthroughAuthConfigsTable } from "./pluginTables.jsx";
import PluginBasicPAMConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

class PassthroughAuthentication extends React.Component {
    componentDidMount() {
        this.loadPAMConfigs();
        this.loadURLs();
    }

    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            pamConfigRows: [],
            urlRows: [],
            tableKey: 1,
            activeTabKey: 0,
            error: {},
            modalSpinning: false,
            modalChecked: false,

            pamConfigName: "",
            pamExcludeSuffix: [],
            pamIncludeSuffix: [],
            pamMissingSuffix: "",
            pamFilter: "",
            pamIDAttr: "",
            pamIDMapMethod: "RDN",
            pamFallback: false,
            pamSecure: false,
            pamService: "ldapserver",
            _pamConfigName: "",
            _pamExcludeSuffix: [],
            _pamIncludeSuffix: [],
            _pamMissingSuffix: "",
            _pamFilter: "",
            _pamIDAttr: "",
            _pamIDMapMethod: "RDN",
            _pamFallback: false,
            _pamSecure: false,
            _pamService: "ldapserver",
            isExcludeOpen: false,
            excludeOptions: [],
            isIncludeOpen: false,
            includeOptions: [],
            showConfirmDeleteConfig: false,
            saveBtnDisabledPAM: true,
            savingPAM: false,

            oldURL: "",
            urlConnType: "ldaps",
            urlAuthDS: "",
            urlSubtree: "",
            urlMaxConns: 3,
            urlMaxOps: 5,
            urlTimeout: 300,
            urlLDVer: "3",
            urlConnLifeTime: 300,
            urlStartTLS: false,
            _urlConnType: "ldaps",
            _urlAuthDS: "",
            _urlSubtree: "",
            _urlMaxConns: 3,
            _urlMaxOps: 5,
            _urlTimeout: 300,
            _urlLDVer: "3",
            _urlConnLifeTime: 300,
            _urlStartTLS: false,
            showConfirmDeleteURL: false,
            saveBtnDisabledPassthru: true,
            savingPassthru: false,

            newPAMConfigEntry: false,
            newURLEntry: false,
            pamConfigEntryModalShow: false,
            urlEntryModalShow: false
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.onExcludeToggle = isExcludeOpen => {
            this.setState({
                isExcludeOpen
            });
        };
        this.clearExcludeSelection = () => {
            this.setState({
                pamExcludeSuffix: [],
                isExcludeOpen: false
            }, () => { this.validatePAM() });
        };
        this.onExcludeSelect = (event, selection) => {
            if (this.state.pamExcludeSuffix.includes(selection)) {
                this.setState(
                    prevState => ({
                        pamExcludeSuffix: prevState.pamExcludeSuffix.filter(item => item !== selection),
                        isExcludeOpen: false
                    }), () => { this.validatePAM() }
                );
            } else {
                this.setState(
                    prevState => ({
                        pamExcludeSuffix: [...prevState.pamExcludeSuffix, selection],
                        isExcludeOpen: false
                    }), () => { this.validatePAM() }
                );
            }
        };
        this.onCreateExcludeOption = newValue => {
            if (!this.state.excludeOptions.includes(newValue)) {
                this.setState({
                    excludeOptions: [...this.state.excludeOptions, newValue],
                    isExcludeOpen: false
                }, () => { this.validatePaAM() });
            }
        };

        this.onIncludeToggle = isIncludeOpen => {
            this.setState({
                isIncludeOpen
            });
        };
        this.clearIncludeSelection = () => {
            this.setState({
                pamIncludeSuffix: [],
                isIncludeOpen: false
            }, () => { this.validatePAM() });
        };
        this.onIncludeSelect = (event, selection) => {
            if (this.state.pamIncludeSuffix.includes(selection)) {
                this.setState(
                    prevState => ({
                        pamIncludeSuffix: prevState.pamIncludeSuffix.filter(item => item !== selection),
                        isIncludeOpen: false
                    })
                );
            } else {
                this.setState(
                    prevState => ({
                        pamIncludeSuffix: [...prevState.pamIncludeSuffix, selection],
                        isIncludeOpen: false
                    })
                );
            }
        };
        this.onCreateIncludeOption = newValue => {
            if (!this.state.includeOptions.includes(newValue)) {
                this.setState({
                    includeOptions: [...this.state.includeOptions, newValue],
                    isIncludeOpen: false
                });
            }
        };

        this.maxValue = 20000000;
        this.onMinusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validatePassthru() });
        };
        this.onConfigChange = (event, id, min) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > this.maxValue ? this.maxValue : newValue < min ? min : newValue
            }, () => { this.validatePassthru() });
        };
        this.onPlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validatePassthru() });
        };

        // This vastly improves rendering performance during handleChange()
        this.attrRows = this.props.attributes.map((attr) => (
            <FormSelectOption key={attr} value={attr} label={attr} />
        ));

        this.handlePassthruChange = this.handlePassthruChange.bind(this);
        this.handlePAMChange = this.handlePAMChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.validatePassthru = this.validatePassthru.bind(this);
        this.validatePAM = this.validatePAM.bind(this);

        this.loadPAMConfigs = this.loadPAMConfigs.bind(this);
        this.loadURLs = this.loadURLs.bind(this);

        this.openPAMModal = this.openPAMModal.bind(this);
        this.closePAMModal = this.closePAMModal.bind(this);
        this.showEditPAMConfigModal = this.showEditPAMConfigModal.bind(this);
        this.showAddPAMConfigModal = this.showAddPAMConfigModal.bind(this);
        this.cmdPAMOperation = this.cmdPAMOperation.bind(this);
        this.deletePAMConfig = this.deletePAMConfig.bind(this);
        this.addPAMConfig = this.addPAMConfig.bind(this);
        this.editPAMConfig = this.editPAMConfig.bind(this);

        this.openURLModal = this.openURLModal.bind(this);
        this.closeURLModal = this.closeURLModal.bind(this);
        this.showEditURLModal = this.showEditURLModal.bind(this);
        this.showAddURLModal = this.showAddURLModal.bind(this);
        this.cmdURLOperation = this.cmdURLOperation.bind(this);
        this.deleteURL = this.deleteURL.bind(this);
        this.addURL = this.addURL.bind(this);
        this.editURL = this.editURL.bind(this);
        this.showConfirmDeleteConfig = this.showConfirmDeleteConfig.bind(this);
        this.showConfirmDeleteURL = this.showConfirmDeleteURL.bind(this);
        this.closeConfirmDeleteConfig = this.closeConfirmDeleteConfig.bind(this);
        this.closeConfirmDeleteURL = this.closeConfirmDeleteURL.bind(this);
    }

    validatePassthru() {
        const errObj = {};
        let all_good = true;

        const reqAttrs = ['urlAuthDS', 'urlSubtree'];
        const dnAttrs = ['urlSubtree'];

        // Check we have our required attributes set
        for (const attr of reqAttrs) {
            if (this.state[attr] == "") {
                errObj[attr] = true;
                all_good = false;
                break;
            }
        }

        // Check the DN's of our lists
        for (const attr of dnAttrs) {
            if (!valid_dn(this.state[attr])) {
                errObj[attr] = true;
                all_good = false;
                break;
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;

            const configAttrs = [
                'urlSubtree', 'urlConnType', 'urlAuthDS', 'urlMaxConns',
                'urlMaxOps', 'urlTimeout', 'urlLDVer', 'urlConnLifeTime',
                'urlStartTLS'
            ];
            for (const check_attr of configAttrs) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabledPassthru: !all_good,
            error: errObj
        });
    }

    validatePAM() {
        const errObj = {};
        let all_good = true;

        const reqAttrs = ['pamConfigName', 'pamIDAttr'];
        const dnAttrLists = ['pamExcludeSuffix', 'pamExcludeSuffix'];

        // Check we have our required attributes set
        for (const attr of reqAttrs) {
            if (this.state[attr] == "") {
                errObj[attr] = true;
                all_good = false;
                break;
            }
        }

        // Check the DN's of our lists
        for (const attrList of dnAttrLists) {
            for (const dn of this.state[attrList]) {
                if (!valid_dn(dn)) {
                    errObj[attrList] = true;
                    all_good = false;
                    break;
                }
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrLists = ['pamExcludeSuffix', 'pamIncludeSuffix'];
            for (const check_attr of attrLists) {
                if (!listsEqual(this.state[check_attr], this.state['_' + check_attr])) {
                    all_good = true;
                    break;
                }
            }

            const configAttrs = [
                'pamConfigName', 'pamFilter', 'pamMissingSuffix', 'pamIDMapMethod',
                'pamIDAttr', 'pamFallback', 'pamSecure', 'pamService'
            ];
            for (const check_attr of configAttrs) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabledPAM: !all_good,
            error: errObj
        });
    }

    handlePassthruChange(e) {
        // Pass thru
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validatePassthru() });
    }

    handlePAMChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validatePAM() });
    }

    handleModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    showConfirmDeleteConfig (name) {
        this.setState({
            showConfirmDeleteConfig: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: name
        });
    }

    closeConfirmDeleteConfig () {
        this.setState({
            showConfirmDeleteConfig: false,
            modalChecked: false,
            modalSpinning: false,
            deleteName: ""
        });
    }

    showConfirmDeleteURL (name) {
        this.setState({
            showConfirmDeleteURL: true,
            modalChecked: false,
            modalSpinning: false,
            deleteName: name
        });
    }

    closeConfirmDeleteURL () {
        this.setState({
            showConfirmDeleteURL: false,
            modalChecked: false,
            modalSpinning: false,
            deleteName: ""
        });
    }

    loadPAMConfigs() {
        this.setState({
            firstLoad: false
        });
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "list",
            "pam-configs"
        ];
        log_cmd("loadPAMConfigs", "Get PAM Passthough Authentication Plugin pamConfigs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    const tableKey = this.state.tableKey + 1;
                    this.setState({
                        pamConfigRows: myObject.items.map(item => item.attrs),
                        tableKey: tableKey,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadPAMConfigs failed", errMsg.desc);
                    }
                });
    }

    loadURLs() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "list",
            "urls"
        ];
        this.props.toggleLoadingHandler();
        log_cmd("loadURLs", "Get PAM Passthough Authentication Plugin pamConfigs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    const tableKey = this.state.tableKey + 1;
                    this.setState({
                        urlRows: myObject.items,
                        tableKey: tableKey
                    });
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadURLs failed", errMsg.desc);
                    }
                    this.props.toggleLoadingHandler();
                });
    }

    showEditPAMConfigModal(rowData) {
        this.openPAMModal(rowData);
    }

    showAddPAMConfigModal() {
        this.openPAMModal();
    }

    openPAMModal(name) {
        if (!name) {
            this.setState({
                pamConfigEntryModalShow: true,
                newPAMConfigEntry: true,
                pamConfigName: "",
                pamExcludeSuffix: [],
                pamIncludeSuffix: [],
                pamMissingSuffix: "",
                pamFilter: "",
                pamIDAttr: "",
                pamIDMapMethod: "RDN",
                pamFallback: false,
                pamSecure: false,
                pamService: "ldapserver",
                saveBtnDisabledPAM: true,
            });
        } else {
            let pamExcludeSuffixList = [];
            let pamIncludeSuffixList = [];
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "pass-through-auth",
                "pam-config",
                name,
                "show"
            ];

            this.props.toggleLoadingHandler();
            log_cmd(
                "openModal",
                "Fetch the PAM Passthough Authentication Plugin pamConfig entry",
                cmd
            );
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const pamConfigEntry = JSON.parse(content).attrs;
                        const tableKey = this.state.tableKey + 1;

                        if (pamConfigEntry.pamexcludesuffix !== undefined) {
                            for (const value of pamConfigEntry.pamexcludesuffix) {
                                pamExcludeSuffixList = [...pamExcludeSuffixList, value];
                            }
                        }
                        if (pamConfigEntry.pamincludesuffix !== undefined) {
                            for (const value of pamConfigEntry.pamincludesuffix) {
                                pamIncludeSuffixList = [...pamIncludeSuffixList, value];
                            }
                        }

                        this.setState({
                            tableKey: tableKey,
                            saveBtnDisabledPAM: true,
                            pamConfigEntryModalShow: true,
                            newPAMConfigEntry: false,
                            pamExcludeSuffix: pamExcludeSuffixList,
                            pamIncludeSuffix: pamIncludeSuffixList,
                            pamIDAttr: pamConfigEntry.pamidattr[0],
                            pamConfigName:
                            pamConfigEntry.cn === undefined ? "" : pamConfigEntry.cn[0],
                            pamMissingSuffix:
                            pamConfigEntry.pammissingsuffix === undefined
                                ? ""
                                : pamConfigEntry.pammissingsuffix[0],
                            pamFilter:
                            pamConfigEntry.pamfilter === undefined
                                ? ""
                                : pamConfigEntry.pamfilter[0],
                            pamIDMapMethod:
                            pamConfigEntry.pamidmapmethod === undefined
                                ? "RDN"
                                : pamConfigEntry.pamidmapmethod[0],
                            pamFallback: !(
                                pamConfigEntry.pamfallback === undefined ||
                            pamConfigEntry.pamfallback[0] == "FALSE"
                            ),
                            pamSecure: !(
                                pamConfigEntry.pamsecure === undefined ||
                            pamConfigEntry.pamsecure[0] == "FALSE"
                            ),
                            pamService:
                            pamConfigEntry.pamservice === undefined
                                ? "ldapserver"
                                : pamConfigEntry.pamservice[0],
                            // Backup values
                            _pamExcludeSuffix: [...pamExcludeSuffixList],
                            _pamIncludeSuffix: [...pamIncludeSuffixList],
                            _pamIDAttr: pamConfigEntry.pamidattr[0],
                            _pamConfigName:
                            pamConfigEntry.cn === undefined ? "" : pamConfigEntry.cn[0],
                            _pamMissingSuffix:
                            pamConfigEntry.pammissingsuffix === undefined
                                ? ""
                                : pamConfigEntry.pammissingsuffix[0],
                            _pamFilter:
                            pamConfigEntry.pamfilter === undefined
                                ? ""
                                : pamConfigEntry.pamfilter[0],
                            _pamIDMapMethod:
                            pamConfigEntry.pamidmapmethod === undefined
                                ? "ldapserver"
                                : pamConfigEntry.pamidmapmethod[0],
                            _pamFallback: !(
                                pamConfigEntry.pamfallback === undefined ||
                            pamConfigEntry.pamfallback[0] == "FALSE"
                            ),
                            _pamSecure: !(
                                pamConfigEntry.pamsecure === undefined ||
                            pamConfigEntry.pamsecure[0] == "FALSE"
                            ),
                            _pamService:
                            pamConfigEntry.pamservice === undefined
                                ? "ldapserver"
                                : pamConfigEntry.pamservice[0],
                        });
                        this.props.toggleLoadingHandler();
                    })
                    .fail(_ => {
                        this.setState({
                            pamConfigEntryModalShow: true,
                            newPAMConfigEntry: true,
                            pamConfigName: "",
                            pamExcludeSuffix: [],
                            pamIncludeSuffix: [],
                            pamMissingSuffix: "",
                            pamFilter: "",
                            pamIDAttr: "",
                            pamIDMapMethod: "",
                            pamFallback: false,
                            pamSecure: false,
                            pamService: "",
                            saveBtnDisabledPAM: true,
                        });
                        this.props.toggleLoadingHandler();
                    });
        }
    }

    closePAMModal() {
        this.setState({
            pamConfigEntryModalShow: false,
            savingPAM: false,
        });
    }

    deletePAMConfig() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "pam-config",
            this.state.deleteName,
            "delete"
        ];

        this.setState({
            modalSpinning: true
        });

        this.props.toggleLoadingHandler();
        log_cmd(
            "deletePAMConfig",
            "Delete the PAM Passthough Authentication Plugin pamConfig entry",
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deletePAMConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `PAMConfig entry ${this.state.deleteName} was successfully deleted`
                    );
                    this.loadPAMConfigs();
                    this.closeConfirmDeleteConfig();
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the pamConfig entry removal operation - ${errMsg.desc}`
                    );
                    this.loadPAMConfigs();
                    this.closeConfirmDeleteConfig();
                    this.props.toggleLoadingHandler();
                });
    }

    addPAMConfig() {
        this.cmdPAMOperation("add");
    }

    editPAMConfig() {
        this.cmdPAMOperation("set");
    }

    cmdPAMOperation(action) {
        // Save table here too
        const {
            pamConfigName,
            pamExcludeSuffix,
            pamIncludeSuffix,
            pamMissingSuffix,
            pamFilter,
            pamIDAttr,
            pamIDMapMethod,
            pamFallback,
            pamSecure,
            pamService
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "pam-config",
            pamConfigName,
            action,
            "--missing-suffix",
            pamMissingSuffix || action == "add" ? pamMissingSuffix : "delete",
            "--filter",
            pamFilter || action == "add" ? pamFilter : "delete",
            "--id_map_method",
            pamIDMapMethod || action == "add" ? pamIDMapMethod : "delete",
            "--fallback",
            pamFallback ? "TRUE" : "FALSE",
            "--secure",
            pamSecure ? "TRUE" : "FALSE",
            "--service",
            pamService || action == "add" ? pamService : "delete"
        ];

        cmd = [...cmd, "--exclude-suffix"];
        if (pamExcludeSuffix.length != 0) {
            for (const value of pamExcludeSuffix) {
                cmd = [...cmd, value];
            }
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }
        cmd = [...cmd, "--include-suffix"];
        if (pamIncludeSuffix.length != 0) {
            for (const value of pamIncludeSuffix) {
                cmd = [...cmd, value];
            }
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }
        cmd = [...cmd, "--id-attr"];
        if (pamIDAttr != "") {
            cmd = [...cmd, pamIDAttr];
        } else if (action == "add") {
            cmd = [...cmd, ""];
        } else {
            cmd = [...cmd, "delete"];
        }

        this.setState({
            savingPAM: true
        });
        log_cmd(
            "pamPassthroughAuthOperation",
            `Do the ${action} operation on the PAM Passthough Authentication Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("pamPassthroughAuthOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        `The ${action} operation was successfully done on "${pamConfigName}" entry`
                    );
                    this.loadPAMConfigs();
                    this.closePAMModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the pamConfig entry ${action} operation - ${errMsg.desc}`
                    );
                    this.loadPAMConfigs();
                    this.closePAMModal();
                });
    }

    showEditURLModal(rowData) {
        this.openURLModal(rowData);
    }

    showAddURLModal() {
        this.openURLModal();
    }

    openURLModal(url) {
        if (!url) {
            this.setState({
                urlEntryModalShow: true,
                newURLEntry: true,
                oldURL: "",
                urlConnType: "ldap",
                urlAuthDS: "",
                urlSubtree: "",
                urlMaxConns: "3",
                urlMaxOps: "5",
                urlTimeout: "300",
                urlLDVer: "3",
                urlConnLifeTime: "300",
                urlStartTLS: false,
                saveBtnDisabledPassthru: true,
            });
        } else {
            const link = url.split(" ")[0];
            const params = url.split(" ")[1];
            this.setState({
                urlEntryModalShow: true,
                oldURL: url,
                newURLEntry: false,
                urlConnType: link.split(":")[0],
                urlAuthDS: link.split("/")[2],
                urlSubtree: link.split("/")[3],
                urlMaxConns: params.split(",")[0],
                urlMaxOps: params.split(",")[1],
                urlTimeout: params.split(",")[2],
                urlLDVer: params.split(",")[3],
                urlConnLifeTime: params.split(",")[4],
                urlStartTLS: !(params.split(",")[5] == "0"),
                saveBtnDisabledPassthru: true,
            });
        }
    }

    closeURLModal() {
        this.setState({
            urlEntryModalShow: false,
            savingPassthru: false
        });
    }

    deleteURL() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "url",
            "delete",
            this.state.deleteName
        ];

        this.setState({
            modalSpinning: true
        });

        log_cmd("deleteURL", "Delete the Passthough Authentication Plugin URL entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteURL", "Result", content);
                    this.props.addNotification("success", `URL ${this.state.deleteName} was successfully deleted`);
                    this.loadURLs();
                    this.closeConfirmDeleteURL();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the URL removal operation - ${errMsg.desc}`
                    );
                    this.loadURLs();
                    this.closeConfirmDeleteURL();
                });
    }

    addURL() {
        this.cmdURLOperation("add");
    }

    editURL() {
        this.cmdURLOperation("modify");
    }

    cmdURLOperation(action) {
        const {
            oldURL,
            urlConnType,
            urlAuthDS,
            urlSubtree,
            urlMaxConns,
            urlMaxOps,
            urlTimeout,
            urlLDVer,
            urlConnLifeTime,
            urlStartTLS
        } = this.state;

        const constructedURL = `${urlConnType}://${urlAuthDS}/${urlSubtree} ${urlMaxConns},${urlMaxOps},${urlTimeout},${urlLDVer},${urlConnLifeTime},${
            urlStartTLS ? "1" : "0"
        }`;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "pass-through-auth",
            "url",
            action
        ];
        if (oldURL != "" && action == "modify") {
            cmd = [...cmd, oldURL, constructedURL];
        } else {
            cmd = [...cmd, constructedURL];
        }

        this.setState({
            savingPassthru: true
        });
        log_cmd(
            "PassthroughAuthOperation",
            `Do the ${action} operation on the Passthough Authentication Plugin`,
            cmd
        );
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("PassthroughAuthOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        `The ${action} operation was successfully done on "${constructedURL}" entry`
                    );
                    this.loadURLs();
                    this.closeURLModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the URL ${action} operation - ${errMsg.desc}`
                    );
                    this.loadURLs();
                    this.closeURLModal();
                });
    }

    render() {
        const {
            urlRows,
            pamConfigRows,
            pamConfigName,
            pamMissingSuffix,
            pamExcludeSuffix,
            pamIncludeSuffix,
            pamFilter,
            pamIDAttr,
            pamIDMapMethod,
            pamFallback,
            pamSecure,
            pamService,
            urlConnType,
            urlLDVer,
            urlAuthDS,
            urlSubtree,
            urlMaxConns,
            urlMaxOps,
            urlTimeout,
            urlConnLifeTime,
            urlStartTLS,
            newPAMConfigEntry,
            newURLEntry,
            pamConfigEntryModalShow,
            urlEntryModalShow,
            error,
            savingPAM,
            savingPassthru
        } = this.state;

        const modalURLFields = {
            urlAuthDS: {
                name: "Authentication Hostname",
                id: 'urlAuthDS',
                value: urlAuthDS,
                help: `The authenticating directory host name. The port number of the Directory Server can be given by adding a colon and then the port number. For example, dirserver.example.com:389. If the port number is not specified, the PTA server attempts to connect using either of the standard ports: Port 389 if ldap:// is specified in the URL. Port 636 if ldaps:// is specified in the URL.`
            },
            urlSubtree: {
                name: "Subtree",
                id: 'urlSubtree',
                value: urlSubtree,
                help: `The pass-through subtree. The PTA Directory Server passes through bind requests to the authenticating Directory Server from all clients whose DN is in this subtree.`
            },
        };
        const modalURLNumberFields = {
            urlMaxConns: {
                name: "Maximum Number of Connections",
                value: urlMaxConns,
                id: 'urlMaxConns',
                help: `The maximum number of connections the PTA directory can simultaneously open to the authenticating directory.`
            },
            urlMaxOps: {
                name: "Maximum Number of Simultaneous Operations",
                value: urlMaxOps,
                id: 'urlMaxOps',
                help: `The maximum number of simultaneous operations (usually bind requests) the PTA directory can send to the authenticating directory within a single connection.`
            },
            urlTimeout: {
                name: "Timeout",
                value: urlTimeout,
                id: 'urlTimeout',
                help: `The time limit, in seconds, that the PTA directory waits for a response from the authenticating Directory Server. If this timeout is exceeded, the server returns an error to the client. The default is 300 seconds (five minutes). Specify zero (0) to indicate no time limit should be enforced.`
            },
            urlConnLifeTime: {
                name: "Connection Life Time",
                value: urlConnLifeTime,
                id: 'urlConnLifeTime',
                help: `The time limit, in seconds, within which a connection may be used.`
            }
        };

        let saveBtnName = "Save Config";
        const extraPrimaryProps = {};
        if (this.state.savingPassthru || this.state.savingPAM) {
            saveBtnName = "Saving Config ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        const title = (newPAMConfigEntry ? "Add" : "Edit") + " PAM Passthough Auth Plugin Config Entry";
        const title_url = (newPAMConfigEntry ? "Add " : "Edit ") + "Pass-Though Authentication Plugin URL";

        return (
            <div className={savingPAM || savingPassthru ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    aria-labelledby="ds-modal"
                    title={title}
                    isOpen={pamConfigEntryModalShow}
                    onClose={this.closePAMModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            onClick={newPAMConfigEntry ? this.addPAMConfig : this.editPAMConfig}
                            isDisabled={this.state.saveBtnDisabledPAM || this.state.savingPAM}
                            isLoading={this.state.savingPAM}
                            spinnerAriaValueText={this.state.savingPAM ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closePAMModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid className="ds-margin-top">
                            <GridItem className="ds-label" span={3}>
                                Config Name
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={pamConfigName}
                                    type="text"
                                    id="pamConfigName"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="pamConfigName"
                                    onChange={(str, e) => {
                                        this.handlePAMChange(e);
                                    }}
                                    isDisabled={!newPAMConfigEntry}
                                    validated={error.pamConfigName ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem
                                className="ds-label"
                                span={3}
                                title="Specifies a suffix to exclude from PAM authentication (pamExcludeSuffix)"
                            >
                                Exclude Suffix
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    isCreatable
                                    onCreateOption={this.onCreateExcludeOption}
                                    typeAheadAriaLabel="Add a suffix"
                                    onToggle={this.onExcludeToggle}
                                    onSelect={this.onExcludeSelect}
                                    onClear={this.clearExcludeSelection}
                                    selections={pamExcludeSuffix}
                                    isOpen={this.state.isExcludeOpen}
                                    aria-labelledby="Add a suffix"
                                    placeholderText="Type a suffix DN ..."
                                >
                                    {this.state.excludeOptions.map((suffix, index) => (
                                        <SelectOption
                                            key={index}
                                            value={suffix}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem
                                className="ds-label"
                                span={3}
                                title="Sets a suffix to include for PAM authentication (pamIncludeSuffix)"
                            >
                                Include Suffix
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    isCreatable
                                    onCreateOption={this.onCreateIncludeOption}
                                    typeAheadAriaLabel="Add an include suffix"
                                    onToggle={this.onIncludeToggle}
                                    onSelect={this.onIncludeSelect}
                                    onClear={this.clearIncludeSelection}
                                    selections={pamIncludeSuffix}
                                    isOpen={this.state.isIncludeOpen}
                                    aria-labelledby="Add an include suffix"
                                    placeholderText="Type a suffix DN ..."
                                >
                                    {this.state.includeOptions.map((suffix, index) => (
                                        <SelectOption
                                            key={index}
                                            value={suffix}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={3} title="Contains the attribute name which is used to hold the PAM user ID (pamIDAttr)">
                                ID Attribute
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="pamIDAttr"
                                    value={pamIDAttr}
                                    onChange={(value, event) => {
                                        this.handlePAMChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="no_setting2" value="" label="Choose an attribute..." />
                                    {this.attrRows}
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title="Identifies how to handle missing include or exclude suffixes (pamMissingSuffix)">
                            <GridItem className="ds-label" span={3}>
                                Missing Suffix
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="pamMissingSuffix"
                                    value={pamMissingSuffix}
                                    onChange={(value, event) => {
                                        this.handlePAMChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="ERROR" value="ERROR" label="ERROR" />
                                    <FormSelectOption key="ALLOW" value="ALLOW" label="ALLOW" />
                                    <FormSelectOption key="IGNORE" value="IGNORE" label="IGNORE" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title="Sets an LDAP filter to use to identify specific entries within the included suffixes for which to use PAM pass-through authentication (pamFilter)">
                            <GridItem className="ds-label" span={3}>
                                Filter
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={pamFilter}
                                    type="text"
                                    id="pamFilter"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="pamFilter"
                                    onChange={(str, e) => {
                                        this.handlePAMChange(e);
                                    }}
                                    validated={error.pamFilter ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Gives the method to use to map the LDAP bind DN to a PAM identity (pamIDMapMethod)">
                            <GridItem className="ds-label" span={3}>
                                Map Method
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="pamIDMapMethod"
                                    value={pamIDMapMethod}
                                    onChange={(value, event) => {
                                        this.handlePAMChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="RDN" value="RDN" label="RDN" />
                                    <FormSelectOption key="DN" value="DN" label="DN" />
                                    <FormSelectOption key="ENTRY" value="ENTRY" label="ENTRY" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title="Contains the service name to pass to PAM (pamService)">
                            <GridItem className="ds-label" span={3}>
                                Service
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="pamService"
                                    value={pamService}
                                    onChange={(value, event) => {
                                        this.handlePAMChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="ldapserver" value="ldapserver" label="ldapserver" />
                                    <FormSelectOption key="system-auth" value="system-auth" label="system-auth (For AD)" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem span={3} className="ds-label">
                                Fallback Auth Enabled
                            </GridItem>
                            <GridItem span={9}>
                                <Checkbox
                                    id="pamFallback"
                                    isChecked={pamFallback}
                                    onChange={(checked, e) => { this.handlePAMChange(e) }}
                                    title="Sets whether to fallback to regular LDAP authentication if PAM authentication fails (pamFallback)"
                                />
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top">
                            <GridItem span={3} className="ds-label">
                                Require Secure Connection
                            </GridItem>
                            <GridItem span={9}>
                                <Checkbox
                                    id="pamSecure"
                                    isChecked={pamSecure}
                                    onChange={(checked, e) => { this.handlePAMChange(e) }}
                                    title="Requires secure TLS connection for PAM authentication (pamSecure)"
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>

                <Modal
                    variant={ModalVariant.medium}
                    title={title_url}
                    isOpen={urlEntryModalShow}
                    onClose={this.closeURLModal}
                    actions={[
                        <Button
                            key="confirm"
                            variant="primary"
                            onClick={newURLEntry ? this.addURL : this.editURL}
                            isDisabled={this.state.saveBtnDisabledPassthru || this.state.savingPassthru}
                            isLoading={this.state.savingPassthru}
                            spinnerAriaValueText={this.state.savingPassthru ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeURLModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid className="ds-margin-top">
                            <GridItem
                                className="ds-label"
                                span={5}
                                title="Defines whether TLS is used for communication between the two Directory Servers."
                            >
                                Connection Type
                            </GridItem>
                            <GridItem span={7}>
                                <FormSelect
                                    id="urlConnType"
                                    value={urlConnType}
                                    onChange={(value, event) => {
                                        this.handlePassthruChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="urlldaps" value="ldaps" label="ldaps" />
                                    <FormSelectOption key="urlldap" value="ldap" label="ldap" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        {Object.entries(modalURLFields).map(([id, content]) => (
                            <Grid key={id}>
                                <GridItem className="ds-label" span={5} title={content.help}>
                                    {content.name}
                                </GridItem>
                                <GridItem span={7}>
                                    <TextInput
                                        value={content.value}
                                        type="text"
                                        id={content.id}
                                        aria-describedby="horizontal-form-name-helper"
                                        name={content.name}
                                        onChange={(str, e) => {
                                            this.handlePassthruChange(e);
                                        }}
                                        validated={error[content.id] ? ValidatedOptions.error : ValidatedOptions.default}
                                    />
                                </GridItem>
                            </Grid>
                        ))}
                        {Object.entries(modalURLNumberFields).map(([id, content]) => (
                            <Grid key={id}>
                                <GridItem className="ds-label" span={5} title={content.help}>
                                    {content.name}
                                </GridItem>
                                <GridItem span={7}>
                                    <NumberInput
                                        value={content.value}
                                        min={-1}
                                        max={this.maxValue}
                                        onMinus={() => { this.onMinusConfig(content.id) }}
                                        onChange={(e) => { this.onConfigChange(e, content.id, -1) }}
                                        onPlus={() => { this.onPlusConfig(content.id) }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={8}
                                    />
                                </GridItem>
                            </Grid>
                        ))}
                        <Grid>
                            <GridItem
                                className="ds-label"
                                span={5}
                                title="The version of the LDAP protocol used to connect to the authenticating directory. Directory Server supports LDAP version 2 and 3. The default is version 3, and Red Hat strongly recommends against using LDAPv2, which is old and will be deprecated."
                            >
                                Version
                            </GridItem>
                            <GridItem span={7}>
                                <FormSelect
                                    id="urlLDVer"
                                    value={urlLDVer}
                                    onChange={(value, event) => {
                                        this.handlePassthruChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key="LDAPv3" value="3" label="LDAPv3" />
                                    <FormSelectOption key="LDAPv2" value="2" label="LDAPv2" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={5}>
                                <Checkbox
                                    id="urlStartTLS"
                                    isChecked={urlStartTLS}
                                    onChange={(checked, e) => { this.handlePassthruChange(e) }}
                                    title="A flag of whether to use Start TLS for the connection to the authenticating directory. Start TLS establishes a secure connection over the standard port, so it is useful for connecting using LDAP instead of LDAPS. The TLS server and CA certificates need to be available on both of the servers. To use Start TLS, the LDAP URL must use ldap:, not ldaps:."
                                    label="Enable StartTLS"
                                />
                            </GridItem>
                        </Grid>
                        <hr />
                        <Grid title="The URL that will be added or modified after you click 'Save'">
                            <GridItem className="ds-label" span={5}>
                                Result URL
                            </GridItem>
                            <GridItem span={7}>
                                <b>
                                    {urlConnType}://{urlAuthDS}/{urlSubtree}{" "}
                                    {urlMaxConns},{urlMaxOps},{urlTimeout},
                                    {urlLDVer},{urlConnLifeTime},
                                    {urlStartTLS ? "1" : "0"}
                                </b>
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>

                <PluginBasicPAMConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Pass Through Authentication"
                    pluginName="Pass Through Authentication"
                    cmdName="pass-through-auth"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={0} title={<TabTitleText>PTA Configurations</TabTitleText>}>
                            <div className="ds-indent">
                                <PassthroughAuthURLsTable
                                    rows={urlRows}
                                    key={this.state.tableKey}
                                    editConfig={this.showEditURLModal}
                                    deleteConfig={this.showConfirmDeleteURL}
                                />
                                <Button
                                    variant="primary"
                                    onClick={this.showAddURLModal}
                                >
                                    Add URL
                                </Button>
                            </div>
                        </Tab>
                        <Tab eventKey={1} title={<TabTitleText>PAM Configurations</TabTitleText>}>
                            <div className="ds-indent">
                                <PassthroughAuthConfigsTable
                                    rows={pamConfigRows}
                                    key={this.state.tableKey}
                                    editConfig={this.showEditPAMConfigModal}
                                    deleteConfig={this.showConfirmDeleteConfig}
                                />
                                <Button
                                    variant="primary"
                                    onClick={this.showAddPAMConfigModal}
                                >
                                    Add Config
                                </Button>
                            </div>
                        </Tab>
                    </Tabs>
                </PluginBasicPAMConfig>
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteConfig}
                    closeHandler={this.closeConfirmDelete}
                    handleChange={this.handleModalChange}
                    actionHandler={this.deletePAMConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle="Delete PAM Passthrough Configuration"
                    mMsg="Are you sure you want to delete this configuration?"
                    mSpinningMsg="Deleting Configuration..."
                    mBtnName="Delete Configuration"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteURL}
                    closeHandler={this.closeConfirmDeleteURL}
                    handleChange={this.handleModalChange}
                    actionHandler={this.deleteURL}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Passthru Authentication URL"
                    mMsg="Are you sure you want to delete this URL?"
                    mSpinningMsg="Deleting URL..."
                    mBtnName="Delete URL"
                />
            </div>
        );
    }
}

PassthroughAuthentication.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

PassthroughAuthentication.defaultProps = {
    rows: [],
    serverId: "",
};

export default PassthroughAuthentication;
