import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Form,
    FormGroup,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Select,
    SelectOption,
    SelectVariant,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    TextInput,
    Tooltip,
    noop,
    ValidatedOptions,
} from "@patternfly/react-core";
import { DNATable, DNASharedTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, valid_dn } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import ExclamationCircleIcon from '@patternfly/react-icons/dist/js/icons/exclamation-circle-icon';

class DNA extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            configRows: [],
            sharedConfigRows: [],
            attributes: [],
            activeTabKey: 1,
            tableKey: 1,
            modalSpinning: false,
            modalChecked: false,
            deleteItem: "",
            loading: true,

            validatedBindMethod: ValidatedOptions.default,
            validatedBindMethodText: "",
            validatedConnProtocol: ValidatedOptions.default,
            validatedConnText: "",

            configName: "",
            prefix: "",
            nextValue: "",
            maxValue: "",
            interval: "",
            magicRegen: "",
            filter: "",
            scope: "",
            remoteBindDN: "",
            remoteBindCred: "",
            sharedConfigEntry: "",
            threshold: "",
            nextRange: "",
            rangeRequestTimeout: "",
            showDeleteConfirm: false,

            sharedBaseDN: "",
            sharedHostname: "",
            sharedPort: "",
            sharedSecurePort: "",
            sharedRemainingValues: "",
            sharedRemoteBindMethod: "",
            sharedRemoteConnProtocol: "",
            showSharedDeleteConfirm: false,

            newEntry: false,
            configEntryModalShow: false,
            sharedConfigEntryModalShow: false,
            // typeahead
            isOpen: false,
            selected: [],
        };

        this.onToggle = isOpen => {
            this.setState({
                isOpen
            });
        };

        this.toggleLoading = () => {
            this.setState(prevState => ({
                loading: !prevState.loading,
            }));
        };

        this.clearSelection = () => {
            this.setState({
                selected: [],
                isOpen: false
            });
        };

        this.onSelect = (event, selection) => {
            const { selected } = this.state;
            if (selected.includes(selection)) {
                this.setState(
                    prevState => ({
                        selected: prevState.selected.filter(item => item !== selection),
                        isOpen: false
                    })
                );
            } else {
                this.setState(
                    prevState => ({
                        selected: [...prevState.selected, selection],
                        isOpen: false,
                    })
                );
            }
        };

        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.loadConfigs = this.loadConfigs.bind(this);
        this.loadSharedConfigs = this.loadSharedConfigs.bind(this);
        this.getAttributes = this.getAttributes.bind(this);

        this.openModal = this.openModal.bind(this);
        this.closeModal = this.closeModal.bind(this);
        this.showEditConfigModal = this.showEditConfigModal.bind(this);
        this.showAddConfigModal = this.showAddConfigModal.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);

        this.openSharedModal = this.openSharedModal.bind(this);
        this.closeSharedModal = this.closeSharedModal.bind(this);
        this.showEditSharedConfigModal = this.showEditSharedConfigModal.bind(this);
        this.deleteSharedConfig = this.deleteSharedConfig.bind(this);
        this.editSharedConfig = this.editSharedConfig.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
        this.showSharedDeleteConfirm = this.showSharedDeleteConfirm.bind(this);
        this.closeSharedDeleteConfirm = this.closeSharedDeleteConfirm.bind(this);
        this.handleConfirmChange = this.handleConfirmChange.bind(this);
    }

    componentDidMount() {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.loadConfigs();
            }
        }
    }

    showDeleteConfirm(item) {
        this.setState({
            deleteItem: item,
            showDeleteConfirm: true,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeDeleteConfirm() {
        this.setState({
            deleteItem: "",
            showDeleteConfirm: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    showSharedDeleteConfirm(item) {
        this.setState({
            deleteItem: item,
            showSharedDeleteConfirm: true,
            modalSpinning: false,
            modalChecked: false
        });
    }

    closeSharedDeleteConfirm() {
        this.setState({
            deleteItem: "",
            showSharedDeleteConfirm: false,
            modalSpinning: false,
            modalChecked: false
        });
    }

    handleConfirmChange(e) {
        this.setState({
            modalChecked: e.target.checked
        });
    }

    handleFieldChange(str, e) {
        this.setState({
            [e.target.id]: e.target.value,
        });
    }

    loadConfigs() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "list",
            "configs"
        ];
        log_cmd("loadConfigs", "Get DNA Plugin configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    let tableKey = this.state.tableKey + 1;
                    this.setState({
                        configRows: myObject.items.map(item => item.attrs),
                        tableKey: tableKey
                    }, this.toggleLoading());
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadConfigs failed", errMsg.desc);
                    }
                    this.setState({
                        loading: false,
                    });
                });
    }

    loadSharedConfigs(basedn) {
        if (basedn == "") {
            // No shared configs, reset table rows
            this.setState({
                sharedConfigRows: [],
                loading: false
            });
            return;
        }

        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "list",
            "shared-configs",
            basedn
        ];
        log_cmd("loadSharedConfigs", "Get DNA Plugin shared configs", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let myObject = JSON.parse(content);
                    this.setState({
                        sharedConfigRows: myObject.items.map(item => item.attrs),
                        loading: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (err != 0) {
                        console.log("loadSharedConfigs failed", errMsg.desc);
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
        this.getAttributes();
        if (!name) {
            this.setState({
                configEntryModalShow: true,
                activeTabKey: 1,
                newEntry: true,
                configName: "",
                selected: [],
                prefix: "",
                nextValue: "",
                maxValue: "",
                interval: "",
                magicRegen: "",
                filter: "",
                scope: "",
                remoteBindDN: "",
                remoteBindCred: "",
                sharedConfigEntry: "",
                threshold: "",
                nextRange: "",
                rangeRequestTimeout: "",
                sharedConfigRows: [],
            });
        } else {
            let dnaTypeList = [];
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "dna",
                "config",
                name,
                "show"
            ];

            this.toggleLoading();
            log_cmd("openModal", "Fetch the DNA Plugin config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        let configEntry = JSON.parse(content).attrs;
                        let sharedCfgDN = "";
                        if ("dnasharedcfgdn" in configEntry) {
                            sharedCfgDN = configEntry["dnasharedcfgdn"][0];
                        }

                        // Initialize Settings
                        configEntry["dnaprefix"] === undefined
                            ? configEntry["dnaprefix"] = ""
                            : configEntry["dnaprefix"] = configEntry["dnaprefix"][0];
                        configEntry["dnanextvalue"] === undefined
                            ? configEntry["dnanextvalue"] = ""
                            : configEntry["dnanextvalue"] = configEntry["dnanextvalue"][0];
                        configEntry["dnamaxvalue"] === undefined
                            ? configEntry["dnamaxvalue"] = ""
                            : configEntry["dnamaxvalue"] = configEntry["dnamaxvalue"][0];
                        configEntry["dnainterval"] === undefined
                            ? configEntry["dnainterval"] = ""
                            : configEntry["dnainterval"] = configEntry["dnainterval"][0];
                        configEntry["dnamagicregen"] === undefined
                            ? configEntry["dnamagicregen"] = ""
                            : configEntry["dnamagicregen"] = configEntry["dnamagicregen"][0];
                        configEntry["dnafilter"] === undefined
                            ? configEntry["dnafilter"] = ""
                            : configEntry["dnafilter"] = configEntry["dnafilter"][0];
                        configEntry["dnascope"] === undefined
                            ? configEntry["dnascope"] = ""
                            : configEntry["dnascope"] = configEntry["dnascope"][0];
                        configEntry["dnaremotebinddn"] === undefined
                            ? configEntry["dnaremotebinddn"] = ""
                            : configEntry["dnaremotebinddn"] = configEntry["dnaremotebinddn"][0];
                        configEntry["dnaremotebindcred"] === undefined
                            ? configEntry["dnaremotebindcred"] = ""
                            : configEntry["dnaremotebindcred"] = configEntry["dnaremotebindcred"][0];
                        configEntry["dnasharedcfgdn"] === undefined
                            ? configEntry["dnasharedcfgdn"] = ""
                            : configEntry["dnasharedcfgdn"] = configEntry["dnasharedcfgdn"][0];
                        configEntry["dnathreshold"] === undefined
                            ? configEntry["dnathreshold"] = ""
                            : configEntry["dnathreshold"] = configEntry["dnathreshold"][0];
                        configEntry["dnanextrange"] === undefined
                            ? configEntry["dnanextrange"] = ""
                            : configEntry["dnanextrange"] = configEntry["dnanextrange"][0];
                        configEntry["dnarangerequesttimeout"] === undefined
                            ? configEntry["dnarangerequesttimeout"] = ""
                            : configEntry["dnarangerequesttimeout"] = configEntry["dnarangerequesttimeout"][0];

                        this.setState({
                            configEntryModalShow: true,
                            newEntry: false,
                            activeTabKey: 1,
                            configName: configEntry["cn"][0],
                            prefix: configEntry["dnaprefix"],
                            nextValue: configEntry["dnanextvalue"],
                            maxValue: configEntry["dnamaxvalue"],
                            interval: configEntry["dnainterval"],
                            magicRegen: configEntry["dnamagicregen"],
                            filter: configEntry["dnafilter"],
                            scope: configEntry["dnascope"],
                            remoteBindDN: configEntry["dnaremotebinddn"],
                            remoteBindCred: configEntry["dnaremotebindcred"],
                            sharedConfigEntry: configEntry["dnasharedcfgdn"],
                            threshold: configEntry["dnathreshold"],
                            nextRange: configEntry["dnanextrange"],
                            rangeRequestTimeout: configEntry["dnarangerequesttimeout"],
                            // Preserve original values
                            _prefix: configEntry["dnaprefix"],
                            _nextValue: configEntry["dnanextvalue"],
                            _maxValue: configEntry["dnamaxvalue"],
                            _interval: configEntry["dnainterval"],
                            _magicRegen: configEntry["dnamagicregen"],
                            _filter: configEntry["dnafilter"],
                            _scope: configEntry["dnascope"],
                            _remoteBindDN: configEntry["dnaremotebinddn"],
                            _remoteBindCred: configEntry["dnaremotebindcred"],
                            _sharedConfigEntry: configEntry["dnasharedcfgdn"],
                            _threshold: configEntry["dnathreshold"],
                            _nextRange: configEntry["dnanextrange"],
                            _rangeRequestTimeout: configEntry["dnarangerequesttimeout"],
                        }, this.loadSharedConfigs(sharedCfgDN));
                        if (configEntry["dnatype"] === undefined) {
                            this.setState({ selected: [] });
                        } else {
                            for (let value of configEntry["dnatype"]) {
                                dnaTypeList = [...dnaTypeList, value];
                            }
                            this.setState({ selected: dnaTypeList });
                        }
                    })
                    .fail(_ => {
                        this.setState({
                            configEntryModalShow: true,
                            newEntry: true,
                            configName: "",
                            selected: [],
                            prefix: "",
                            nextValue: "",
                            maxValue: "",
                            interval: "",
                            magicRegen: "",
                            filter: "",
                            scope: "",
                            remoteBindDN: "",
                            remoteBindCred: "",
                            sharedConfigEntry: "",
                            threshold: "",
                            nextRange: "",
                            rangeRequestTimeout: ""
                        }, this.toggleLoading());
                    });
        }
    }

    showEditSharedConfigModal(sharedName) {
        this.openSharedModal(sharedName);
    }

    closeModal() {
        this.setState({ configEntryModalShow: false });
    }

    closeSharedModal() {
        this.setState({ sharedConfigEntryModalShow: false });
    }

    cmdOperation(action, muteError) {
        const {
            configName,
            selected,
            prefix,
            nextValue,
            maxValue,
            interval,
            magicRegen,
            filter,
            scope,
            remoteBindDN,
            remoteBindCred,
            sharedConfigEntry,
            threshold,
            nextRange,
            rangeRequestTimeout
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "config",
            configName,
            action,
            "--prefix",
            prefix || action == "add" ? prefix : "delete",
            "--next-value",
            nextValue || action == "add" ? nextValue : "delete",
            "--max-value",
            maxValue || action == "add" ? maxValue : "delete",
            "--interval",
            interval || action == "add" ? interval : "delete",
            "--magic-regen",
            magicRegen || action == "add" ? magicRegen : "delete",
            "--filter",
            filter || action == "add" ? filter : "delete",
            "--scope",
            scope || action == "add" ? scope : "delete",
            "--remote-bind-dn",
            remoteBindDN || action == "add" ? remoteBindDN : "delete",
            "--remote-bind-cred",
            remoteBindCred || action == "add" ? remoteBindCred : "delete",
            "--shared-config-entry",
            sharedConfigEntry || action == "add" ? sharedConfigEntry : "delete",
            "--threshold",
            threshold || action == "add" ? threshold : "delete",
            "--next-range",
            nextRange || action == "add" ? nextRange : "delete",
            "--range-request-timeout",
            rangeRequestTimeout || action == "add" ? rangeRequestTimeout : "delete"
        ];

        // Delete attributes if the user set an empty value to the field
        if (!(action == "add" && selected.length == 0)) {
            cmd = [...cmd, "--type"];
            if (selected.length != 0) {
                for (let value of selected) {
                    cmd = [...cmd, value];
                }
            } else if (action == "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }
        }

        this.toggleLoading();
        log_cmd("DNAOperation", `Do the ${action} operation on the DNA Plugin`, cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("DNAOperation", "Result", content);
                    this.props.addNotification(
                        "success",
                        `The ${action} operation was successfully done on "${configName}" entry`
                    );
                    this.loadConfigs();
                    this.closeModal();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    if (muteError !== true) {
                        this.props.addNotification(
                            "error",
                            `Error during the config entry ${action} operation - ${errMsg.desc}`
                        );
                    }
                    this.loadConfigs();
                    this.closeModal();
                });
    }

    deleteConfig() {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "config",
            this.state.deleteItem,
            "delete"
        ];

        this.setState({
            modalSpinning: true,
        });

        this.toggleLoading();
        log_cmd("deleteConfig", "Delete the DNA Plugin config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Config entry ${this.state.deleteItem} was successfully deleted`
                    );
                    this.closeDeleteConfirm();
                    this.loadConfigs();
                    this.closeModal();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry removal operation - ${errMsg.desc}`
                    );
                    this.closeDeleteConfirm();
                    this.loadConfigs();
                    this.closeModal();
                });
    }

    addConfig(muteError) {
        this.cmdOperation("add", muteError);
    }

    editConfig(muteError) {
        this.cmdOperation("set", muteError);
    }

    openSharedModal(sharedName) {
        if (sharedName) {
            // Get all the attributes and matching rules now
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "dna",
                "config",
                this.state.configName,
                "shared-config-entry",
                sharedName,
                "show"
            ];
            this.toggleLoading();

            log_cmd("openSharedModal", "Fetch the DNA Plugin shared config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        let configEntry = JSON.parse(content).attrs;
                        // Initialize settings
                        configEntry["dnahostname"] === undefined
                            ? configEntry["dnahostname"] = ""
                            : configEntry["dnahostname"] = configEntry["dnahostname"][0];

                        configEntry["dnaportnum"] === undefined
                            ? configEntry["dnaportnum"] = ""
                            : configEntry["dnaportnum"] = configEntry["dnaportnum"][0];

                        configEntry["dnasecureportnum"] === undefined
                            ? configEntry["dnasecureportnum"] = ""
                            : configEntry["dnasecureportnum"] = configEntry["dnasecureportnum"][0];

                        configEntry["dnaremainingvalues"] === undefined
                            ? configEntry["dnaremainingvalues"] = ""
                            : configEntry["dnaremainingvalues"] = configEntry["dnaremainingvalues"][0];

                        configEntry["dnaremotebindmethod"] === undefined
                            ? configEntry["dnaremotebindmethod"] = ""
                            : configEntry["dnaremotebindmethod"] = configEntry["dnaremotebindmethod"][0];

                        configEntry["dnaremoteconnprotocol"] === undefined
                            ? configEntry["dnaremoteconnprotocol"] = ""
                            : configEntry["dnaremoteconnprotocol"] = configEntry["dnaremoteconnprotocol"][0];

                        this.setState({
                            sharedConfigEntryModalShow: true,
                            sharedHostname: configEntry["dnahostname"],
                            sharedPort: configEntry["dnaportnum"],
                            sharedSecurePort: configEntry["dnasecureportnum"],
                            sharedRemainingValues: configEntry["dnaremainingvalues"],
                            sharedRemoteBindMethod: configEntry["dnaremotebindmethod"],
                            sharedRemoteConnProtocol: configEntry["dnaremoteconnprotocol"],
                            // Preserve settings
                            _sharedRemoteBindMethod: configEntry["dnaremotebindmethod"],
                            _sharedRemoteConnProtocol: configEntry["dnaremoteconnprotocol"],
                        });
                    })
                    .fail(_ => {
                        this.setState({
                            sharedConfigEntryModalShow: true,
                            sharedBaseDN: "",
                            sharedHostname: "",
                            sharedPort: "",
                            sharedSecurePort: "",
                            sharedRemainingValues: "",
                            sharedRemoteBindMethod: "",
                            sharedRemoteConnProtocol: "",
                        }, this.toggleLoading());
                    });
        } else {
            this.setState({
                sharedConfigEntryModalShow: true,
                sharedBaseDN: "",
                sharedHostname: "",
                sharedPort: "",
                sharedSecurePort: "",
                sharedRemainingValues: "",
                sharedRemoteBindMethod: "",
                sharedRemoteConnProtocol: ""
            });
        }
    }

    editSharedConfig() {
        const {
            configName,
            sharedHostname,
            sharedPort,
            sharedRemoteBindMethod,
            sharedRemoteConnProtocol
        } = this.state;

        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "config",
            configName,
            "shared-config-entry",
            sharedHostname + ":" + sharedPort,
            "set",
            "--remote-bind-method",
            sharedRemoteBindMethod || "delete",
            "--remote-conn-protocol",
            sharedRemoteConnProtocol || "delete",
        ];

        this.toggleLoading();
        log_cmd(
            "editSharedConfig",
            `Do the set operation on the DNA Plugin Shared Entry`,
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
                        `The set operation was successfully done on "${sharedHostname}:${sharedPort}" entry`
                    );
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                    this.closeSharedModal();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the config entry set operation - ${errMsg.desc}`
                    );
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                });
    }

    deleteSharedConfig() {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "dna",
            "config",
            this.state.configName,
            "shared-config-entry",
            this.state.deleteItem,
            "delete"
        ];

        this.setState({
            modalSpinning: true,
        });

        this.toggleLoading();
        log_cmd("deleteSharedConfig", "Delete the DNA Plugin Shared config entry", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    console.info("deleteSharedConfig", "Result", content);
                    this.props.addNotification(
                        "success",
                        `Shared config entry ${this.state.deleteItem} was successfully deleted`
                    );
                    this.closeSharedDeleteConfirm();
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error during the shared config entry removal operation - ${errMsg.desc}`
                    );
                    this.closeSharedDeleteConfirm();
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                });
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
                        firstLoad: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification("error", `Failed to get attributes - ${errMsg.desc}`);
                });
    }

    validateSharedConfig(adding) {
        let validatedBindMethod = ValidatedOptions.default;
        let validatedBindMethodText = "";
        let validatedConnProtocol = ValidatedOptions.default;
        let validatedConnText = "";

        // Handle the shared config remote bind settings validation
        if (this.state.sharedRemoteBindMethod == "SSL" && this.state.sharedRemoteConnProtocol != "TLS") {
            validatedBindMethodText = "You can not use 'SSL' if the connection protocol is not 'TLS'";
            validatedBindMethod = ValidatedOptions.error;
        } else if (this.state.sharedRemoteBindMethod.startsWith("SASL") && this.state.sharedRemoteConnProtocol == "TLS") {
            validatedBindMethodText = "You can not use a 'SASL' method if the connection protocol is 'TLS'";
            validatedBindMethod = ValidatedOptions.error;
        } else if ((this.state.sharedRemoteBindMethod == "" && this.state.sharedRemoteConnProtocol != "") ||
            (this.state.sharedRemoteBindMethod != "" && this.state.sharedRemoteConnProtocol == "")) {
            validatedBindMethodText = "You must either set, or unset, both preferences";
            validatedBindMethod = ValidatedOptions.error;
        }

        if (this.state.sharedRemoteConnProtocol == "TLS" && this.state.sharedRemoteBindMethod.startsWith("SASL/")) {
            validatedConnText = "You can not use the 'TLS' protocol if the BindMethod is a 'SASL' method";
            validatedConnProtocol = ValidatedOptions.error;
        } else if (this.state.sharedRemoteConnProtocol == "LDAP" && this.state.sharedRemoteBindMethod == "SSL") {
            validatedConnText = "You can not use the 'LDAP' protocol if the BindMethod is a 'TLS'";
            validatedConnProtocol = ValidatedOptions.error;
        } else if ((this.state.sharedRemoteConnProtocol == "" && this.state.sharedRemoteBindMethod != "") ||
                   (this.state.sharedRemoteConnProtocol != "" && this.state.sharedRemoteBindMethod == "")) {
            validatedConnText = "You must either set, or unset, both preferences";
            validatedConnProtocol = ValidatedOptions.error;
        }

        let saveNotOK = (
            validatedBindMethod != ValidatedOptions.default ||
            validatedConnProtocol != ValidatedOptions.default
        );

        if (!adding && !saveNotOK) {
            // Everything is valid, but if we are editing we can only save if
            // something was actually changed
            if (this.state._sharedRemoteConnProtocol == this.state.sharedRemoteConnProtocol &&
                this.state._sharedRemoteBindMethod == this.state.sharedRemoteBindMethod) {
                // Nothing changed, so there is nothing to save
                saveNotOK = true;
            }
        }
        return {
            validatedBindMethodText: validatedBindMethodText,
            validatedBindMethod: validatedBindMethod,
            validatedConnText: validatedConnText,
            validatedConnProtocol: validatedConnProtocol,
            saveSharedNotOK: saveNotOK
        };
    }

    validateConfigResult(adding) {
        let saveConfigNotOK = false;

        if (this.state.sharedConfigEntry != "" && !valid_dn(this.state.sharedConfigEntry)) {
            saveConfigNotOK = true;
        } else if (this.state.configName == "") {
            saveConfigNotOK = true;
        } else if (this.state.nextValue == "" || parseInt(this.state.nextValue < 1)) {
            saveConfigNotOK = true;
        } else if (this.state.maxValue != "" &&
                   this.state.maxValue == "0" &&
                   parseInt(this.state.maxValue) < -1) {
            saveConfigNotOK = true;
        } else if (this.state.filter == "") {
            saveConfigNotOK = true;
        } else if (this.state.scope == "") {
            saveConfigNotOK = true;
        } else if (this.state.selected == "" || this.state.selected.length == 0) {
            saveConfigNotOK = true;
        }

        // If editing an entry we need to check if values were changed before
        // enabling the save button
        if (!adding && !saveConfigNotOK) {
            if (this.state._prefix == this.state.prefix &&
                this.state._maxValue == this.state.maxValue &&
                this.state._interval == this.state.interval &&
                this.state._magicRegen == this.state.magicRegen &&
                this.state._filter == this.state.filter &&
                this.state._scope == this.state.scope &&
                this.state._nextValue == this.state.nextValue &&
                this.state._remoteBindDN == this.state.remoteBindDN &&
                this.state._remoteBindCred == this.state.remoteBindCred &&
                this.state._sharedConfigEntry == this.state.sharedConfigEntry &&
                this.state._threshold == this.state.threshold &&
                this.state._nextRange == this.state.nextRange &&
                this.state._rangeRequestTimeout == this.state.rangeRequestTimeout) {
                // Nothing changed, so there is nothing to save
                saveConfigNotOK = true;
            }
        }

        return saveConfigNotOK;
    }

    render() {
        const {
            sharedConfigRows,
            configEntryModalShow,
            configName,
            newEntry,
            sharedConfigEntry,
            sharedConfigEntryModalShow,
            sharedHostname,
            sharedPort,
            sharedSecurePort,
            sharedRemainingValues,
            sharedRemoteBindMethod,
            sharedRemoteConnProtocol,
            magicRegen,
            scope,
            filter,
            attributes,
            selected,
            nextValue,
            maxValue,
        } = this.state;
        let saveConfigNotOK = false;
        let sharedResult = {};

        let bindMethodOptions = [
            { value: '', label: 'None' },
            { value: 'SIMPLE', label: 'SIMPLE' },
            { value: 'SSL', label: 'SSL' },
            { value: 'SASL/GSSAPI', label: 'SASL/GSSAPI' },
            { value: 'SASL/DIGEST-MD5', label: 'SASL/DIGEST-MD5' },
        ];
        let connProtocolOptions = [
            { value: '', label: 'None' },
            { value: 'LDAP', label: 'LDAP' },
            { value: 'TLS', label: 'TLS' },
        ];

        // Optional config settings
        const modalConfigFields = {
            prefix: {
                name: "Prefix",
                value: this.state.prefix,
                id: 'prefix',
                type: 'text',
                help:
                    "Defines a prefix that can be prepended to the generated number values for the attribute (dnaPrefix)"
            },
            remoteBindDN: {
                name: "Remote Bind DN",
                value: this.state.remoteBindDN,
                id: 'remoteBindDN',
                type: 'text',
                help: "Specifies the Replication Manager DN (dnaRemoteBindDN)"
            },
            remoteBindCred: {
                name: "Remote Bind Credentials",
                value: this.state.remoteBindCred,
                id: 'remoteBindCred',
                type: 'text',
                help: "Specifies the Replication Manager's password (dnaRemoteBindCred)"
            },
            threshold: {
                name: "Threshold",
                value: this.state.threshold,
                id: 'threshold',
                type: 'number',
                help:
                    "Sets a threshold of remaining available numbers in the range. When the server hits the threshold, it sends a request for a new range (dnaThreshold)"
            },
            nextRange: {
                name: "Next Range",
                value: this.state.nextRange,
                id: 'nextRange',
                type: 'text',
                help:
                    "Defines the next range to use when the current range is exhausted (dnaNextRange)"
            },
            rangeRequestTimeout: {
                name: "Range Request Timeout",
                value: this.state.rangeRequestTimeout,
                id: 'rangeRequestTimeout',
                type: 'number',
                help:
                    "Sets a timeout period, in seconds, for range requests so that the server does not stall waiting on a new range from one server and can request a range from a new server (dnaRangeRequestTimeout)"
            }
        };

        saveConfigNotOK = this.validateConfigResult(newEntry);
        sharedResult = this.validateSharedConfig(newEntry);

        return (
            <div>
                <Modal
                    variant={ModalVariant.medium}
                    title={newEntry ? "Create DNA Config Entry" : "Edit DNA Config Entry"}
                    isOpen={configEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeModal}
                    actions={[
                        <Button key="saveshared" isDisabled={saveConfigNotOK} variant="primary" onClick={newEntry ? this.addConfig : this.editConfig}>
                            Save Config
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={1} title={<TabTitleText><b>DNA Configuration</b></TabTitleText>}>
                            <Form className="ds-margin-top-xlg" isHorizontal>
                                <FormGroup
                                    label="Config Name"
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
                                        isDisabled={!newEntry}
                                        isRequired
                                    />
                                </FormGroup>
                                <FormGroup
                                    label="Attributes"
                                    fieldId="Attributes"
                                    title="Sets which attributes have unique numbers being generated for them (dnaType)."
                                    isRequired
                                >
                                    <Select
                                        variant={SelectVariant.typeaheadMulti}
                                        typeAheadAriaLabel="Type an attribute"
                                        onToggle={this.onToggle}
                                        onSelect={this.onSelect}
                                        onClear={this.clearSelection}
                                        selections={selected}
                                        isOpen={this.state.isOpen}
                                        aria-labelledby="typeAhead-1"
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
                                    label="Magic Regen"
                                    fieldId="magicRegen"
                                    title="Sets a user-defined value that instructs the plug-in to assign a new value for the entry (dnaMagicRegen)"
                                    isRequired
                                >
                                    <TextInput
                                        value={magicRegen}
                                        type="text"
                                        id="magicRegen"
                                        aria-describedby="magicRegen"
                                        name="magicRegen"
                                        onChange={this.handleFieldChange}
                                        isRequired
                                    />
                                </FormGroup>
                                <FormGroup
                                    label="Filter"
                                    fieldId="filter"
                                    title="Sets an LDAP filter to use to search for and identify the entries to which to apply the distributed numeric assignment range (dnaFilter)"
                                    isRequired
                                >
                                    <TextInput
                                        value={filter}
                                        type="text"
                                        id="filter"
                                        aria-describedby="filter"
                                        name="filter"
                                        onChange={this.handleFieldChange}
                                        isRequired
                                    />
                                </FormGroup>
                                <FormGroup
                                    label="Scope"
                                    fieldId="scope"
                                    title="Sets the base DN to search for entries to which to apply the distributed numeric assignment (dnaScope)"
                                    helperTextInvalid={
                                        scope != "" ? "A valid LDAP DN must be provided" : ""
                                    }
                                    validated={scope != "" && !valid_dn(scope) ? ValidatedOptions.error : ValidatedOptions.default}
                                    isRequired
                                >
                                    <TextInput
                                        value={scope}
                                        type="text"
                                        id="scope"
                                        aria-describedby="scope"
                                        name="scope"
                                        onChange={this.handleFieldChange}
                                        validated={scope != "" && !valid_dn(scope) ? ValidatedOptions.error : ValidatedOptions.default}
                                        isRequired
                                    />
                                </FormGroup>
                                <FormGroup
                                    label="Next Value"
                                    fieldId="nextValue"
                                    title="Gives the next available number which can be assigned (dnaNextValue)"
                                    helperTextInvalid={parseInt(nextValue) < 1 ? "Value must be greater than 0" : ""}
                                    validated={parseInt(nextValue) < 1 ? ValidatedOptions.error : ValidatedOptions.default}
                                    isRequired
                                >
                                    <TextInput
                                        value={nextValue}
                                        type="number"
                                        id="nextValue"
                                        aria-describedby="nextValue"
                                        name="nextValue"
                                        onChange={this.handleFieldChange}
                                        validated={parseInt(nextValue) < 1 ? ValidatedOptions.error : ValidatedOptions.default}
                                        isRequired
                                    />
                                </FormGroup>
                                <FormGroup
                                    label="Max Value"
                                    fieldId="maxValue"
                                    title="Sets the maximum value that can be assigned for the range, default is -1 (dnaMaxValue)"
                                    helperTextInvalid={
                                        parseInt(maxValue) < 1
                                        ? "Value values are -1, or a number greater than 0"
                                        : ""
                                    }
                                    validated={
                                        maxValue != "" && (maxValue == "0" || parseInt(maxValue) < -1)
                                        ? ValidatedOptions.error : ValidatedOptions.default
                                    }
                                >
                                    <TextInput
                                        value={maxValue}
                                        type="number"
                                        id="maxValue"
                                        aria-describedby="maxValue"
                                        name="maxValue"
                                        onChange={this.handleFieldChange}
                                        validated={
                                            maxValue != "" && (maxValue == "0" || parseInt(maxValue) < -1)
                                            ? ValidatedOptions.error : ValidatedOptions.default
                                        }
                                    />
                                </FormGroup>
                                {Object.entries(modalConfigFields).map(([id, content]) => (
                                    <FormGroup
                                        label={content.name}
                                        fieldId={content.id}
                                        key={content.id}
                                        title={content.help}
                                    >
                                        <TextInput
                                            value={content.value}
                                            type={content.type}
                                            id={content.id}
                                            aria-describedby={content.name}
                                            name={content.name}
                                            key={content.name}
                                            onChange={this.handleFieldChange}
                                        />
                                    </FormGroup>
                                ))}
                            </Form>
                        </Tab>
                        <Tab
                            eventKey={2}
                            title={<TabTitleText>Shared Config Settings</TabTitleText>}
                        >
                            <div className="ds-margin-top-lg">
                                <Tooltip
                                    content={
                                        <div>
                                            The DNA Shared Config Entry is an entry in the database that
                                            DNA configurations about remote replicas are stored under.
                                            Think of the Shared Config Entry as a container of DNA
                                            remote configurations.  These remote configurations are only
                                            used if the database is being replicated, otherwise these
                                            settings can be ignored. This entry must already be created
                                            prior to setting the
                                            <b> Shared Config Entry</b> field.
                                        </div>
                                    }
                                >
                                    <a className="ds-font-size-sm">What is a Shared Config Entry?</a>
                                </Tooltip>
                            </div>
                            <Form className="ds-margin-top-lg" isHorizontal>
                                <FormGroup
                                    label="Shared Config Entry"
                                    fieldId="sharedConfigEntry"
                                    title="Defines a container entry for DNA remote server configuration that the servers can use to transfer ranges between one another (dnaSharedCfgDN)"
                                    helperTextInvalid="The DN of the suffix is invalid"
                                    helperTextInvalidIcon={<ExclamationCircleIcon />}
                                    validated={
                                        this.state.sharedConfigEntry != "" && !valid_dn(this.state.sharedConfigEntry)
                                        ? ValidatedOptions.error
                                        : ValidatedOptions.default
                                    }
                                >
                                    <TextInput
                                        isRequired
                                        type="text"
                                        id="sharedConfigEntry"
                                        aria-describedby="createSuffix"
                                        name="sharedConfigEntry"
                                        value={sharedConfigEntry}
                                        onChange={this.handleFieldChange}
                                        validated={
                                            this.state.sharedConfigEntry != "" && !valid_dn(this.state.sharedConfigEntry)
                                            ? ValidatedOptions.error : ValidatedOptions.default
                                        }
                                    />
                                </FormGroup>
                            </Form>
                            <div className="ds-margin-top-xlg">
                                <DNASharedTable
                                    rows={sharedConfigRows}
                                    key={sharedConfigRows}
                                    editConfig={this.showEditSharedConfigModal}
                                    deleteConfig={this.showSharedDeleteConfirm}
                                />
                                <hr />
                            </div>
                        </Tab>
                    </Tabs>
                </Modal>

                <Modal
                    variant={ModalVariant.medium}
                    title="Manage DNA Plugin Shared Config Entry"
                    isOpen={sharedConfigEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.closeSharedModal}
                    actions={[
                        <Button key="confirm" isDisabled={sharedResult.saveSharedNotOK} variant="primary" onClick={this.editSharedConfig}>
                            Save Shared Config
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeSharedModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Grid hasGutter className="ds-margin-top-xlg">
                        <GridItem span={3}>
                            Hostname:
                        </GridItem>
                        <GridItem span={4}>
                            <b>{sharedHostname}</b>
                        </GridItem>
                        <GridItem span={2}>
                            Port:
                        </GridItem>
                        <GridItem span={3}>
                            <b>{sharedPort}</b>
                        </GridItem>
                        <GridItem span={3}>
                            Remaining Values:
                        </GridItem>
                        <GridItem span={4}>
                            <b>{sharedRemainingValues}</b>
                        </GridItem>
                        <GridItem span={2}>
                            Secure Port:
                        </GridItem>
                        <GridItem span={3}>
                            <b>{sharedSecurePort}</b>
                        </GridItem>
                        <hr />
                        <h5 className="ds-center" title="Used with Replication to share ranges between replicas">
                            Remote Authentication Preferences
                        </h5>
                        <Form isHorizontal>
                            <FormGroup
                                label="Remote Bind Method"
                                type="string"
                                helperTextInvalid={sharedResult.validatedBindMethodText}
                                fieldId="validatedBindMethodTextSelection"
                                validated={sharedResult.validatedBindMethod}
                            >
                                <FormSelect
                                    id="sharedRemoteBindMethod"
                                    value={sharedRemoteBindMethod}
                                    onChange={this.handleFieldChange}
                                    aria-label="FormSelect Input"
                                    validated={sharedResult.validatedBindMethod}
                                >
                                    {bindMethodOptions.map((option, index) => (
                                        <FormSelectOption key={index} value={option.value} label={option.label} />
                                    ))}
                                </FormSelect>
                            </FormGroup>
                            <FormGroup
                                label="Connection Protocol"
                                type="string"
                                helperTextInvalid={sharedResult.validatedConnText}
                                fieldId="validatedConnProtTextSelection"
                                validated={sharedResult.validatedConnProtocol}
                            >
                                <FormSelect
                                    id="sharedRemoteConnProtocol"
                                    value={sharedRemoteConnProtocol}
                                    onChange={this.handleFieldChange}
                                    aria-label="FormSelect Input"
                                    validated={sharedResult.validatedConnProtocol}
                                >
                                    {connProtocolOptions.map((option, index) => (
                                        <FormSelectOption key={index} value={option.value} label={option.label} />
                                    ))}
                                </FormSelect>
                            </FormGroup>
                        </Form>
                    </Grid>
                </Modal>
                <div className="ds-center" hidden={!this.state.loading}>
                    <h4>Loading DNA configuration</h4>
                    <Spinner size="lg" />
                </div>
                <div hidden={this.state.loading}>
                    <PluginBasicConfig
                        rows={this.props.rows}
                        key={this.state.configRows}
                        serverId={this.props.serverId}
                        cn="Distributed Numeric Assignment Plugin"
                        pluginName="Distributed Numeric Assignment"
                        cmdName="dna"
                        savePluginHandler={this.props.savePluginHandler}
                        pluginListHandler={this.props.pluginListHandler}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.toggleLoading}
                    >
                        <div>
                            <DNATable
                                rows={this.state.configRows}
                                key={this.state.tableKey}
                                editConfig={this.showEditConfigModal}
                                deleteConfig={this.showDeleteConfirm}
                            />
                            <Button
                                className="ds-margin-top"
                                variant="primary"
                                onClick={this.showAddConfigModal}
                            >
                                Add Config
                            </Button>
                        </div>
                    </PluginBasicConfig>
                </div>
                <DoubleConfirmModal
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.configName}
                    checked={this.state.modalChecked}
                    mTitle="Delete DNA Configuration"
                    mMsg="Are you really sure you want to delete this DNA configuration?"
                    mSpinningMsg="Deleting config ..."
                    mBtnName="Delete config"
                />
                <DoubleConfirmModal
                    showModal={this.state.showSharedDeleteConfirm}
                    closeHandler={this.closeSharedDeleteConfirm}
                    handleChange={this.handleConfirmChange}
                    actionHandler={this.deleteSharedConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.sharedConfigEntry}
                    checked={this.state.modalChecked}
                    mTitle="Delete Shared Config Entry"
                    mMsg="Are you really sure you want to delete this Shared Config Entry?"
                    mSpinningMsg="Deleting Shared Config ..."
                    mBtnName="Delete Shared Config"
                />
            </div>
        );
    }
}

DNA.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
};

DNA.defaultProps = {
    rows: [],
    serverId: "",
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
};

export default DNA;
