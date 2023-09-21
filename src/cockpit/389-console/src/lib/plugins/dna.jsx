import cockpit from "cockpit";
import React from "react";
import {
    Button,
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
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    Tooltip,
    ValidatedOptions,
} from "@patternfly/react-core";
import { DNATable, DNASharedTable } from "./pluginTables.jsx";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import PropTypes from "prop-types";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";

const _ = cockpit.gettext;

class DNAPlugin extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            firstLoad: true,
            configRows: [],
            sharedConfigRows: [],
            activeTabKey: 1,
            tableKey: 1,
            modalSpinning: false,
            modalChecked: false,
            deleteItem: "",
            loading: true,
            error: {},

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
            saving: false,

            sharedBaseDN: "",
            sharedHostname: "",
            sharedPort: "",
            sharedSecurePort: "",
            sharedRemainingValues: "",
            sharedRemoteBindMethod: "",
            sharedRemoteConnProtocol: "",
            showSharedDeleteConfirm: false,
            savingShared: false,

            newEntry: false,
            configEntryModalShow: false,
            sharedConfigEntryModalShow: false,
            // typeahead
            isOpen: false,
            selected: [],
            saveBtnDisabled: true,
        };

        this.handleToggle = isOpen => {
            this.setState({
                isOpen
            });
        };

        this.handleClearSelection = () => {
            this.setState({
                selected: [],
                isOpen: false
            }, () => { this.validateConfig() });
        };

        this.handleSelect = (event, selection) => {
            const { selected } = this.state;
            if (selected.includes(selection)) {
                this.setState(
                    prevState => ({
                        selected: prevState.selected.filter(item => item !== selection),
                        isOpen: false
                    }), () => { this.validateConfig() }
                );
            } else {
                this.setState(
                    prevState => ({
                        selected: [...prevState.selected, selection],
                        isOpen: false,
                    }), () => { this.validateConfig() }
                );
            }
        };

        this.maxValue = 20000000;
        this.onMinusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validateConfig() });
        };
        this.onConfigChange = (event, id, min) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > this.maxValue ? this.maxValue : newValue < min ? min : newValue
            }, () => { this.validateConfig() });
        };
        this.onPlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validateConfig() });
        };

        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.loadConfigs = this.loadConfigs.bind(this);
        this.loadSharedConfigs = this.loadSharedConfigs.bind(this);
        this.validateConfig = this.validateConfig.bind(this);

        this.openModal = this.openModal.bind(this);
        this.handleCloseModal = this.handleCloseModal.bind(this);
        this.showEditConfigModal = this.showEditConfigModal.bind(this);
        this.handleShowAddConfigModal = this.handleShowAddConfigModal.bind(this);
        this.cmdOperation = this.cmdOperation.bind(this);
        this.deleteConfig = this.deleteConfig.bind(this);
        this.addConfig = this.addConfig.bind(this);
        this.editConfig = this.editConfig.bind(this);

        this.openSharedModal = this.openSharedModal.bind(this);
        this.handleCloseSharedModal = this.handleCloseSharedModal.bind(this);
        this.showEditSharedConfigModal = this.showEditSharedConfigModal.bind(this);
        this.deleteSharedConfig = this.deleteSharedConfig.bind(this);
        this.handleEditSharedConfig = this.handleEditSharedConfig.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
        this.showSharedDeleteConfirm = this.showSharedDeleteConfirm.bind(this);
        this.closeSharedDeleteConfirm = this.closeSharedDeleteConfirm.bind(this);
        this.onConfirmChange = this.onConfirmChange.bind(this);
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

    onConfirmChange(e) {
        this.setState({
            modalChecked: e.target.checked
        });
    }

    handleFieldChange(str, e) {
        this.setState({
            [e.target.id]: e.target.value,
        }, () => { this.validateConfig() });
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
                    const myObject = JSON.parse(content);
                    const tableKey = this.state.tableKey + 1;
                    this.setState({
                        configRows: myObject.items.map(item => item.attrs),
                        tableKey,
                        loading: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err !== 0) {
                        console.log("loadConfigs failed", errMsg.desc);
                    }
                    this.setState({
                        loading: false,
                    });
                });
    }

    loadSharedConfigs(basedn) {
        if (basedn === "") {
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
                    const myObject = JSON.parse(content);
                    this.setState({
                        sharedConfigRows: myObject.items.map(item => item.attrs),
                        firstLoad: false,
                        loading: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    if (err !== 0) {
                        console.log("loadSharedConfigs failed", errMsg.desc);
                    }
                });
    }

    showEditConfigModal(rowData) {
        this.openModal(rowData);
    }

    handleShowAddConfigModal() {
        this.openModal();
    }

    openModal(name) {
        if (!name) {
            this.setState({
                configEntryModalShow: true,
                activeTabKey: 1,
                newEntry: true,
                configName: "",
                selected: [],
                prefix: "",
                nextValue: 1,
                maxValue: -1,
                interval: 1,
                magicRegen: "",
                filter: "",
                scope: "",
                remoteBindDN: "",
                remoteBindCred: "",
                sharedConfigEntry: "",
                threshold: 1,
                nextRange: "",
                rangeRequestTimeout: 600,
                sharedConfigRows: [],
                saveBtnDisabled: true,
                error: {},
            });
        } else {
            let dnaTypeList = [];
            const cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "dna",
                "config",
                name,
                "show"
            ];

            log_cmd("openModal", "Fetch the DNA Plugin config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const configEntry = JSON.parse(content).attrs;
                        let sharedCfgDN = "";
                        if ("dnasharedcfgdn" in configEntry) {
                            sharedCfgDN = configEntry.dnasharedcfgdn[0];
                        }

                        // Initialize Settings
                        configEntry.dnaprefix === undefined
                            ? configEntry.dnaprefix = ""
                            : configEntry.dnaprefix = configEntry.dnaprefix[0];
                        configEntry.dnanextvalue === undefined
                            ? configEntry.dnanextvalue = ""
                            : configEntry.dnanextvalue = configEntry.dnanextvalue[0];
                        configEntry.dnamaxvalue === undefined
                            ? configEntry.dnamaxvalue = ""
                            : configEntry.dnamaxvalue = configEntry.dnamaxvalue[0];
                        configEntry.dnainterval === undefined
                            ? configEntry.dnainterval = ""
                            : configEntry.dnainterval = configEntry.dnainterval[0];
                        configEntry.dnamagicregen === undefined
                            ? configEntry.dnamagicregen = ""
                            : configEntry.dnamagicregen = configEntry.dnamagicregen[0];
                        configEntry.dnafilter === undefined
                            ? configEntry.dnafilter = ""
                            : configEntry.dnafilter = configEntry.dnafilter[0];
                        configEntry.dnascope === undefined
                            ? configEntry.dnascope = ""
                            : configEntry.dnascope = configEntry.dnascope[0];
                        configEntry.dnaremotebinddn === undefined
                            ? configEntry.dnaremotebinddn = ""
                            : configEntry.dnaremotebinddn = configEntry.dnaremotebinddn[0];
                        configEntry.dnaremotebindcred === undefined
                            ? configEntry.dnaremotebindcred = ""
                            : configEntry.dnaremotebindcred = configEntry.dnaremotebindcred[0];
                        configEntry.dnasharedcfgdn === undefined
                            ? configEntry.dnasharedcfgdn = ""
                            : configEntry.dnasharedcfgdn = configEntry.dnasharedcfgdn[0];
                        configEntry.dnathreshold === undefined
                            ? configEntry.dnathreshold = ""
                            : configEntry.dnathreshold = configEntry.dnathreshold[0];
                        configEntry.dnanextrange === undefined
                            ? configEntry.dnanextrange = ""
                            : configEntry.dnanextrange = configEntry.dnanextrange[0];
                        configEntry.dnarangerequesttimeout === undefined
                            ? configEntry.dnarangerequesttimeout = ""
                            : configEntry.dnarangerequesttimeout = configEntry.dnarangerequesttimeout[0];

                        this.setState({
                            configEntryModalShow: true,
                            newEntry: false,
                            activeTabKey: 1,
                            configName: configEntry.cn[0],
                            prefix: configEntry.dnaprefix,
                            nextValue: Number(configEntry.dnanextvalue) === 0 ? 1 : Number(configEntry.dnanextvalue),
                            maxValue: Number(configEntry.dnamaxvalue) === 0 ? -1 : Number(configEntry.dnamaxvalue),
                            interval: Number(configEntry.dnainterval) === 0 ? 1 : Number(configEntry.dnainterval),
                            magicRegen: configEntry.dnamagicregen,
                            filter: configEntry.dnafilter,
                            scope: configEntry.dnascope,
                            remoteBindDN: configEntry.dnaremotebinddn,
                            remoteBindCred: configEntry.dnaremotebindcred,
                            sharedConfigEntry: configEntry.dnasharedcfgdn,
                            threshold: Number(configEntry.dnathreshold) === 0 ? 1 : Number(configEntry.dnathreshold),
                            nextRange: configEntry.dnanextrange,
                            rangeRequestTimeout: Number(configEntry.dnarangerequesttimeout) === 0 ? 600 : Number(configEntry.dnarangerequesttimeout),
                            // Preserve original values
                            _prefix: configEntry.dnaprefix,
                            _nextValue: Number(configEntry.dnanextvalue) === 0 ? 1 : Number(configEntry.dnanextvalue),
                            _maxValue: Number(configEntry.dnamaxvalue) === 0 ? -1 : Number(configEntry.dnamaxvalue),
                            _interval: Number(configEntry.dnainterval) === 0 ? 1 : Number(configEntry.dnainterval),
                            _magicRegen: configEntry.dnamagicregen,
                            _filter: configEntry.dnafilter,
                            _scope: configEntry.dnascope,
                            _remoteBindDN: configEntry.dnaremotebinddn,
                            _remoteBindCred: configEntry.dnaremotebindcred,
                            _sharedConfigEntry: configEntry.dnasharedcfgdn,
                            _threshold: Number(configEntry.dnathreshold) === 0 ? 1 : Number(configEntry.dnathreshold),
                            _nextRange: configEntry.dnanextrange,
                            _rangeRequestTimeout: Number(configEntry.dnarangerequesttimeout) === 0 ? 600 : Number(configEntry.dnarangerequesttimeout),
                            saveBtnDisabled: true,
                            error: {},
                        }, () => { this.loadSharedConfigs(sharedCfgDN) });
                        if (configEntry.dnatype === undefined) {
                            this.setState({ selected: [], _selected: [] });
                        } else {
                            for (const value of configEntry.dnatype) {
                                dnaTypeList = [...dnaTypeList, value];
                            }
                            this.setState({ selected: dnaTypeList, _selected: [...dnaTypeList] });
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
                            rangeRequestTimeout: "",
                            saveBtnDisabled: true,
                        });
                    });
        }
    }

    showEditSharedConfigModal(sharedName) {
        this.openSharedModal(sharedName);
    }

    handleCloseModal() {
        this.setState({ configEntryModalShow: false });
    }

    handleCloseSharedModal() {
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
            prefix || action === "add" ? prefix : "delete",
            "--next-value=" + (nextValue || action === "add" ? nextValue : "delete"),
            "--max-value=" + (maxValue || action === "add" ? maxValue : "delete"),
            "--interval=" + (interval || action === "add" ? interval : "delete"),
            "--magic-regen",
            magicRegen || action === "add" ? magicRegen : "delete",
            "--filter",
            filter || action === "add" ? filter : "delete",
            "--scope",
            scope || action === "add" ? scope : "delete",
            "--remote-bind-dn",
            remoteBindDN || action === "add" ? remoteBindDN : "delete",
            "--remote-bind-cred",
            remoteBindCred || action === "add" ? remoteBindCred : "delete",
            "--shared-config-entry",
            sharedConfigEntry || action === "add" ? sharedConfigEntry : "delete",
            "--threshold=" + (threshold || action === "add" ? threshold : "delete"),
            "--next-range",
            nextRange || action === "add" ? nextRange : "delete",
            "--range-request-timeout=" + (rangeRequestTimeout || action === "add" ? rangeRequestTimeout : "delete"),

        ];

        // Delete attributes if the user set an empty value to the field
        if (!(action === "add" && selected.length === 0)) {
            cmd = [...cmd, "--type"];
            if (selected.length !== 0) {
                for (const value of selected) {
                    cmd = [...cmd, value];
                }
            } else if (action === "add") {
                cmd = [...cmd, ""];
            } else {
                cmd = [...cmd, "delete"];
            }
        }

        this.setState({
            saving: true
        });

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
                    if (muteError !== true) {
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error during the config entry $0 operation - $1"), action, errMsg.desc)
                        );
                    }
                    this.loadConfigs();
                    this.handleCloseModal();
                    this.setState({
                        saving: false
                    });
                });
    }

    deleteConfig() {
        const cmd = [
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
                        cockpit.format(_("Config entry $0 was successfully deleted"), this.state.deleteItem)
                    );
                    this.closeDeleteConfirm();
                    this.loadConfigs();
                    this.handleCloseModal();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry removal operation - $0"), errMsg.desc)
                    );
                    this.closeDeleteConfirm();
                    this.loadConfigs();
                    this.handleCloseModal();
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
            const cmd = [
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

            log_cmd("openSharedModal", "Fetch the DNA Plugin shared config entry", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        const configEntry = JSON.parse(content).attrs;
                        // Initialize settings
                        configEntry.dnahostname === undefined
                            ? configEntry.dnahostname = ""
                            : configEntry.dnahostname = configEntry.dnahostname[0];

                        configEntry.dnaportnum === undefined
                            ? configEntry.dnaportnum = ""
                            : configEntry.dnaportnum = configEntry.dnaportnum[0];

                        configEntry.dnasecureportnum === undefined
                            ? configEntry.dnasecureportnum = ""
                            : configEntry.dnasecureportnum = configEntry.dnasecureportnum[0];

                        configEntry.dnaremainingvalues === undefined
                            ? configEntry.dnaremainingvalues = ""
                            : configEntry.dnaremainingvalues = configEntry.dnaremainingvalues[0];

                        configEntry.dnaremotebindmethod === undefined
                            ? configEntry.dnaremotebindmethod = ""
                            : configEntry.dnaremotebindmethod = configEntry.dnaremotebindmethod[0];

                        configEntry.dnaremoteconnprotocol === undefined
                            ? configEntry.dnaremoteconnprotocol = ""
                            : configEntry.dnaremoteconnprotocol = configEntry.dnaremoteconnprotocol[0];

                        this.setState({
                            sharedConfigEntryModalShow: true,
                            sharedHostname: configEntry.dnahostname,
                            sharedPort: configEntry.dnaportnum,
                            sharedSecurePort: configEntry.dnasecureportnum,
                            sharedRemainingValues: configEntry.dnaremainingvalues,
                            sharedRemoteBindMethod: configEntry.dnaremotebindmethod,
                            sharedRemoteConnProtocol: configEntry.dnaremoteconnprotocol,
                            // Preserve settings
                            _sharedRemoteBindMethod: configEntry.dnaremotebindmethod,
                            _sharedRemoteConnProtocol: configEntry.dnaremoteconnprotocol,
                            saveBtnDisabled: true,
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
                        });
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

    handleEditSharedConfig() {
        const {
            configName,
            sharedHostname,
            sharedPort,
            sharedRemoteBindMethod,
            sharedRemoteConnProtocol
        } = this.state;

        const cmd = [
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

        this.setState({
            savingShared: true
        });
        log_cmd(
            "handleEditSharedConfig",
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
                        cockpit.format(_("The set operation was successfully done on \"$0:$1\" entry"), sharedHostname, sharedPort)
                    );
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                    this.handleCloseSharedModal();
                    this.setState({
                        savingShared: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the config entry set operation - $0"), errMsg.desc)
                    );
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                    this.handleCloseSharedModal();
                    this.setState({
                        savingShared: false
                    });
                });
    }

    deleteSharedConfig() {
        const cmd = [
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
                        cockpit.format(_("Shared config entry $0 was successfully deleted"), this.state.deleteItem)
                    );
                    this.closeSharedDeleteConfirm();
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error during the shared config entry removal operation - $0"), errMsg.desc)
                    );
                    this.closeSharedDeleteConfirm();
                    this.loadSharedConfigs(this.state.sharedConfigEntry);
                });
    }

    validateSharedConfig(adding) {
        let validatedBindMethod = ValidatedOptions.default;
        let validatedBindMethodText = "";
        let validatedConnProtocol = ValidatedOptions.default;
        let validatedConnText = "";

        // Handle the shared config remote bind settings validation
        if (this.state.sharedRemoteBindMethod === "SSL" && this.state.sharedRemoteConnProtocol !== "TLS") {
            validatedBindMethodText = _("You can not use 'SSL' if the connection protocol is not 'TLS'");
            validatedBindMethod = ValidatedOptions.error;
        } else if (this.state.sharedRemoteBindMethod.startsWith("SASL") && this.state.sharedRemoteConnProtocol === "TLS") {
            validatedBindMethodText = _("You can not use a 'SASL' method if the connection protocol is 'TLS'");
            validatedBindMethod = ValidatedOptions.error;
        } else if ((this.state.sharedRemoteBindMethod === "" && this.state.sharedRemoteConnProtocol !== "") ||
            (this.state.sharedRemoteBindMethod !== "" && this.state.sharedRemoteConnProtocol === "")) {
            validatedBindMethodText = _("You must either set, or unset, both preferences");
            validatedBindMethod = ValidatedOptions.error;
        }

        if (this.state.sharedRemoteConnProtocol === "TLS" && this.state.sharedRemoteBindMethod.startsWith("SASL/")) {
            validatedConnText = _("You can not use the 'TLS' protocol if the BindMethod is a 'SASL' method");
            validatedConnProtocol = ValidatedOptions.error;
        } else if (this.state.sharedRemoteConnProtocol === "LDAP" && this.state.sharedRemoteBindMethod === "SSL") {
            validatedConnText = _("You can not use the 'LDAP' protocol if the BindMethod is a 'TLS'");
            validatedConnProtocol = ValidatedOptions.error;
        } else if ((this.state.sharedRemoteConnProtocol === "" && this.state.sharedRemoteBindMethod !== "") ||
                   (this.state.sharedRemoteConnProtocol !== "" && this.state.sharedRemoteBindMethod === "")) {
            validatedConnText = _("You must either set, or unset, both preferences");
            validatedConnProtocol = ValidatedOptions.error;
        }

        let saveNotOK = (
            validatedBindMethod !== ValidatedOptions.default ||
            validatedConnProtocol !== ValidatedOptions.default
        );

        if (!adding && !saveNotOK) {
            // Everything is valid, but if we are editing we can only save if
            // something was actually changed
            if (this.state._sharedRemoteConnProtocol === this.state.sharedRemoteConnProtocol &&
                this.state._sharedRemoteBindMethod === this.state.sharedRemoteBindMethod) {
                // Nothing changed, so there is nothing to save
                saveNotOK = true;
            }
        }
        return {
            validatedBindMethodText,
            validatedBindMethod,
            validatedConnText,
            validatedConnProtocol,
            saveSharedNotOK: saveNotOK
        };
    }

    validateConfig() {
        const error = {};
        let all_good = true;

        const dnAttrs = [
            'sharedConfigEntry', 'scope'
        ];
        const reqAttrs = [
            'configName', 'nextValue', 'filter', 'scope',
        ];

        if (this.state.selected === "" || this.state.selected.length === 0) {
            all_good = false;
        }

        for (const attr of reqAttrs) {
            if (this.state[attr] === "") {
                error[attr] = true;
                all_good = false;
            }
        }

        for (const attr of dnAttrs) {
            if (this.state[attr] !== "" && !valid_dn(this.state[attr])) {
                error[attr] = true;
                all_good = false;
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;
            const attrs = [
                'maxValue', 'interval', 'magicRegen', 'filter', 'scope',
                'nextValue', 'remoteBindDN', 'remoteBindCred', 'sharedConfigEntry',
                'threshold', 'nextRange', 'rangeRequestTimeout'
            ];
            for (const check_attr of attrs) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    all_good = true;
                    break;
                }
            }
            if (!listsEqual(this.state.selected, this.state._selected)) {
                all_good = true;
            }
        }

        this.setState({
            saveBtnDisabled: !all_good,
            error
        });
    }

    render() {
        const {
            sharedConfigRows,
            configEntryModalShow,
            configName,
            newEntry,
            rangeRequestTimeout,
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
            selected,
            nextValue,
            maxValue,
            interval,
            saving,
            savingShared,
            saveBtnDisabled,
            threshold,
            error,
        } = this.state;
        let sharedResult = {};

        const bindMethodOptions = [
            { value: '', label: 'None' },
            { value: 'SIMPLE', label: 'SIMPLE' },
            { value: 'SSL', label: 'SSL' },
            { value: 'SASL/GSSAPI', label: 'SASL/GSSAPI' },
            { value: 'SASL/DIGEST-MD5', label: 'SASL/DIGEST-MD5' },
        ];
        const connProtocolOptions = [
            { value: '', label: 'None' },
            { value: 'LDAP', label: 'LDAP' },
            { value: 'TLS', label: 'TLS' },
        ];

        // Optional config settings
        const modalConfigFields = {
            prefix: {
                name: _("Prefix"),
                value: this.state.prefix,
                id: 'prefix',
                type: 'text',
                help:
                    _("Defines a prefix that can be prepended to the generated number values for the attribute (dnaPrefix)")
            },
            remoteBindDN: {
                name: _("Remote Bind DN"),
                value: this.state.remoteBindDN,
                id: 'remoteBindDN',
                type: 'text',
                help: _("Specifies the Replication Manager DN (dnaRemoteBindDN)")
            },
            remoteBindCred: {
                name: _("Remote Bind Credentials"),
                value: this.state.remoteBindCred,
                id: 'remoteBindCred',
                type: 'text',
                help: _("Specifies the Replication Manager's password (dnaRemoteBindCred)")
            },
            nextRange: {
                name: _("Next Range"),
                value: this.state.nextRange,
                id: 'nextRange',
                type: 'text',
                help:
                    _("Defines the next range to use when the current range is exhausted.  Format is '####-####', or '1000-5000' (dnaNextRange).")
            },
        };

        sharedResult = this.validateSharedConfig(newEntry);

        let saveBtnName;
        let saveSharedBtnName = _("Save Shared Config");
        if (newEntry) {
            saveBtnName = _("Add Config");
        } else {
            saveBtnName = _("Save Config");
        }
        const extraPrimaryProps = {};
        if (saving) {
            if (newEntry) {
                saveBtnName = _("Adding Config ...");
            } else {
                saveBtnName = _("Saving Config ...");
            }
            saveSharedBtnName = _("Saving Config ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        return (
            <div className={saving || savingShared ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.medium}
                    title={newEntry ? _("Create DNA Config Entry") : _("Edit DNA Config Entry")}
                    isOpen={configEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.handleCloseModal}
                    actions={[
                        <Button
                            key="saveshared"
                            isDisabled={saveBtnDisabled || saving}
                            variant="primary"
                            onClick={newEntry ? this.addConfig : this.editConfig}
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
                    <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={1} title={<TabTitleText><b>{_("DNA Configuration")}</b></TabTitleText>}>
                            <Form className="ds-margin-top-xlg" isHorizontal autoComplete="off">
                                <Grid>
                                    <GridItem span={3} className="ds-label">
                                        {_("Config Name")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={configName}
                                            type="text"
                                            id="configName"
                                            aria-describedby="configName"
                                            name="configName"
                                            onChange={this.handleFieldChange}
                                            isDisabled={!newEntry}
                                            validated={error.configName ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Sets which attributes that will have unique numbers generated for them (dnaType).")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("DNA Managed Attributes")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <Select
                                            variant={SelectVariant.typeaheadMulti}
                                            typeAheadAriaLabel="Type an attribute"
                                            onToggle={this.handleToggle}
                                            onSelect={this.handleSelect}
                                            onClear={this.handleClearSelection}
                                            selections={selected}
                                            isOpen={this.state.isOpen}
                                            aria-labelledby="typeAhead-1"
                                            placeholderText={_("Type an attribute...")}
                                            validated={selected.length === 0 ? 'error' : 'default'}
                                        >
                                            {this.props.attributes.map((attr) => (
                                                <SelectOption
                                                    key={attr}
                                                    value={attr}
                                                />
                                            ))}
                                        </Select>
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Sets an LDAP filter to use to search for and identify the entries to which to apply the distributed numeric assignment range (dnaFilter)")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Filter")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={filter}
                                            type="text"
                                            id="filter"
                                            aria-describedby="filter"
                                            name="filter"
                                            onChange={this.handleFieldChange}
                                            validated={error.filter ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Sets the base DN to search for entries to which to apply the distributed numeric assignment (dnaScope)")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Subtree Scope")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={scope}
                                            type="text"
                                            id="scope"
                                            aria-describedby="scope"
                                            name="scope"
                                            onChange={this.handleFieldChange}
                                            validated={error.scope ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Gives the next available number which can be assigned (dnaNextValue)")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Next Value")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={nextValue}
                                            min={1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("nextValue") }}
                                            onChange={(e) => { this.onConfigChange(e, "nextValue", 1) }}
                                            onPlus={() => { this.onPlusConfig("nextValue") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Sets the maximum value that can be assigned for the range, default is -1 (dnaMaxValue)")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Max Value")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={maxValue}
                                            min={-1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("maxValue") }}
                                            onChange={(e) => { this.onConfigChange(e, "maxValue", -1) }}
                                            onPlus={() => { this.onPlusConfig("maxValue") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Sets a user-defined value that instructs the plug-in to assign a new value for the entry (dnaMagicRegen)")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Magic Regeneration Value")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={magicRegen}
                                            type="text"
                                            id="magicRegen"
                                            aria-describedby="magicRegen"
                                            name="magicRegen"
                                            onChange={this.handleFieldChange}
                                            validated={error.magicRegen ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                {Object.entries(modalConfigFields).map(([id, content]) => (
                                    <Grid key={content.name} title={content.help}>
                                        <GridItem className="ds-label" span={3}>
                                            {content.name}
                                        </GridItem>
                                        <GridItem span={9}>
                                            <TextInput
                                                value={content.value}
                                                type={content.type}
                                                id={content.id}
                                                aria-describedby={content.name}
                                                name={content.name}
                                                key={content.name}
                                                onChange={this.handleFieldChange}
                                                validated={error[content.id] ? ValidatedOptions.error : ValidatedOptions.default}
                                            />
                                        </GridItem>
                                    </Grid>
                                ))}
                                <Grid title={_("Sets a threshold of remaining available numbers in the range. When the server hits the threshold, it sends a request for a new range (dnaThreshold)")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Threshold")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={threshold}
                                            min={1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("threshold") }}
                                            onChange={(e) => { this.onConfigChange(e, "threshold", 1) }}
                                            onPlus={() => { this.onPlusConfig("threshold") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Sets a timeout period, in seconds, for range requests so that the server does not stall waiting on a new range from one server and can request a range from a new server (dnaRangeRequestTimeout)")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Range Request Timeout")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={rangeRequestTimeout}
                                            min={1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("rangeRequestTimeout") }}
                                            onChange={(e) => { this.onConfigChange(e, "rangeRequestTimeout", 1) }}
                                            onPlus={() => { this.onPlusConfig("rangeRequestTimeout") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("Sets an interval, or increment, to add to the next DNA assigned number. If the next DNA number is '10', and the 'interval' is '1000', then the assigned value in the entry will be '1010' (dnaInterval)")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Number Interval")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={interval}
                                            min={1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("interval") }}
                                            onChange={(e) => { this.onConfigChange(e, "interval", 1) }}
                                            onPlus={() => { this.onPlusConfig("interval") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <hr />
                            </Form>
                        </Tab>
                        <Tab
                            eventKey={2}
                            title={<TabTitleText>{_("Shared Config Settings")}</TabTitleText>}
                        >
                            <div className="ds-margin-top-lg">
                                <Tooltip
                                    content={
                                        <div>
                                            {_("The DNA Shared Config Entry is an entry in the database that DNA configurations about remote replicas are stored under.  Think of the Shared Config Entry as a container of DNA remote configurations.  These remote configurations are only used if the database is being replicated, otherwise these settings can be ignored. This entry must already be created prior to setting the <b> Shared Config Entry</b> field.")}
                                        </div>
                                    }
                                >
                                    <a className="ds-font-size-sm">{_("What is a Shared Config Entry?")}</a>
                                </Tooltip>
                            </div>
                            <Form className="ds-margin-top-lg" isHorizontal autoComplete="off">
                                <Grid title={_("Defines a container entry DN for DNA remote server configuration that the servers can use to transfer ranges between one another (dnaSharedCfgDN)")}>
                                    <GridItem span={3} className="ds-label">
                                        {_("Shared Config Entry DN")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={sharedConfigEntry}
                                            type="text"
                                            id="sharedConfigEntry"
                                            aria-describedby="sharedConfigEntry"
                                            name="sharedConfigEntry"
                                            onChange={this.handleFieldChange}
                                            validated={error.sharedConfigEntry ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
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
                    title={_("Manage DNA Plugin Shared Config Entry")}
                    isOpen={sharedConfigEntryModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.handleCloseSharedModal}
                    actions={[
                        <Button
                            key="confirm"
                            isDisabled={sharedResult.saveSharedNotOK || savingShared}
                            variant="primary"
                            onClick={this.handleEditSharedConfig}
                            isLoading={savingShared}
                            spinnerAriaValueText={savingShared ? _("Saving") : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveSharedBtnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleCloseSharedModal}>
                            {_("Cancel")}
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
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
                            <TextContent>
                                <Text className="ds-center" title={_("Used with Replication to share ranges between replicas")} component={TextVariants.h3}>
                                    {_("Remote Authentication Preferences")}
                                </Text>
                            </TextContent>
                            <GridItem span={3} className="ds-label">
                                {_("Remote Bind Method")}
                            </GridItem>
                            <GridItem span={9}>
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
                            </GridItem>

                            <GridItem span={3} className="ds-label">
                                Connection Protocol
                            </GridItem>
                            <GridItem span={9}>
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
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>
                <div className="ds-margin-top-xlg ds-center" hidden={!this.state.loading}>
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Loading DNA configuration")}
                        </Text>
                    </TextContent>
                    <Spinner size="lg" />
                </div>
                <div hidden={this.state.loading}>
                    <PluginBasicConfig
                        rows={this.props.rows}
                        key={this.state.configRows}
                        serverId={this.props.serverId}
                        cn="Distributed Numeric Assignment Plugin"
                        pluginName={_("Distributed Numeric Assignment")}
                        cmdName="dna"
                        savePluginHandler={this.props.savePluginHandler}
                        pluginListHandler={this.props.pluginListHandler}
                        addNotification={this.props.addNotification}
                        toggleLoadingHandler={this.props.toggleLoadingHandler}
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
                                onClick={this.handleShowAddConfigModal}
                            >
                                {_("Add Config")}
                            </Button>
                        </div>
                    </PluginBasicConfig>
                </div>
                <DoubleConfirmModal
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    handleChange={this.onConfirmChange}
                    actionHandler={this.deleteConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.configName}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete DNA Configuration")}
                    mMsg={_("Are you really sure you want to delete this DNA configuration?")}
                    mSpinningMsg={_("Deleting config ...")}
                    mBtnName={_("Delete config")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showSharedDeleteConfirm}
                    closeHandler={this.closeSharedDeleteConfirm}
                    handleChange={this.onConfirmChange}
                    actionHandler={this.deleteSharedConfig}
                    spinning={this.state.modalSpinning}
                    item={this.state.sharedConfigEntry}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete Shared Config Entry")}
                    mMsg={_("Are you really sure you want to delete this Shared Config Entry?")}
                    mSpinningMsg={_("Deleting Shared Config ...")}
                    mBtnName={_("Delete Shared Config")}
                />
            </div>
        );
    }
}

DNAPlugin.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

DNAPlugin.defaultProps = {
    rows: [],
    serverId: "",
};

export default DNAPlugin;
