import cockpit from "cockpit";
import React from "react";
import { log_cmd, valid_dn, callCmdStreamPassword } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import { ManagerTable } from "./replTables.jsx";
import { AddManagerModal, ChangeReplRoleModal } from "./replModals.jsx";
import {
    Button,
    Checkbox,
    ExpandableSection,
    Form,
    Grid,
    GridItem,
    NumberInput,
    TextInput,
    ValidatedOptions
} from "@patternfly/react-core";
import PropTypes from "prop-types";

const _ = cockpit.gettext;

export class ReplConfig extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            saving: false,
            showConfirmManagerDelete: false,
            showAddManagerModal: false,
            showPromoteDemoteModal: false,
            addManagerSpinning: false,
            roleChangeSpinning: false,
            manager: "cn=replication manager,cn=config",
            manager_passwd: "",
            manager_passwd_confirm: "",
            newRole: this.props.role === "Supplier" ? "Hub" : "Supplier",
            newRID: 1,
            modalSpinning: false,
            modalChecked: false,
            errObj: {},
            saveBtnDisabled: true,
            isExpanded: false,
            // Config Settings
            nsds5replicabinddn: this.props.data?.nsds5replicabinddn || [],
            nsds5replicabinddngroup: this.props.data?.nsds5replicabinddngroup || "",
            nsds5replicabinddngroupcheckinterval: Number(this.props.data.nsds5replicabinddngroupcheckinterval) === 0 ? -1 : Number(this.props.data.nsds5replicabinddngroupcheckinterval),
            nsds5replicareleasetimeout: Number(this.props.data.nsds5replicareleasetimeout),
            nsds5replicapurgedelay: Number(this.props.data.nsds5replicapurgedelay) === 0 ? 604800 : Number(this.props.data.nsds5replicapurgedelay),
            nsds5replicatombstonepurgeinterval: Number(this.props.data.nsds5replicatombstonepurgeinterval) === 0 ? 86400 : Number(this.props.data.nsds5replicatombstonepurgeinterval),
            nsds5replicaprecisetombstonepurging: Number(this.props.data.nsds5replicaprecisetombstonepurging),
            nsds5replicaprotocoltimeout: Number(this.props.data.nsds5replicaprotocoltimeout) === 0 ? 120 : Number(this.props.data.nsds5replicaprotocoltimeout),
            nsds5replicabackoffmin: Number(this.props.data.nsds5replicabackoffmin) === 0 ? 3 : Number(this.props.data.nsds5replicabackoffmin),
            nsds5replicabackoffmax: Number(this.props.data.nsds5replicabackoffmax) === 0 ? 300 : Number(this.props.data.nsds5replicabackoffmax),
            nsds5replicakeepaliveupdateinterval: Number(this.props.data.nsds5replicakeepaliveupdateinterval) === 0 ? 3600 : Number(this.props.data.nsds5replicakeepaliveupdateinterval),
            // Original settings
            _nsds5replicabinddn: this.props.data?.nsds5replicabinddn || [],
            _nsds5replicabinddngroup: this.props.data?.nsds5replicabinddngroup || "",
            _nsds5replicabinddngroupcheckinterval: Number(this.props.data.nsds5replicabinddngroupcheckinterval) === 0 ? -1 : Number(this.props.data.nsds5replicabinddngroupcheckinterval),
            _nsds5replicareleasetimeout: this.props.data.nsds5replicareleasetimeout,
            _nsds5replicapurgedelay: Number(this.props.data.nsds5replicapurgedelay) === 0 ? 604800 : Number(this.props.data.nsds5replicapurgedelay),
            _nsds5replicatombstonepurgeinterval: Number(this.props.data.nsds5replicatombstonepurgeinterval) === 0 ? 86400 : Number(this.props.data.nsds5replicatombstonepurgeinterval),
            _nsds5replicaprecisetombstonepurging: this.props.data.nsds5replicaprecisetombstonepurging,
            _nsds5replicaprotocoltimeout: Number(this.props.data.nsds5replicaprotocoltimeout) === 0 ? 120 : Number(this.props.data.nsds5replicaprotocoltimeout),
            _nsds5replicabackoffmin: Number(this.props.data.nsds5replicabackoffmin) === 0 ? 3 : Number(this.props.data.nsds5replicabackoffmin),
            _nsds5replicabackoffmax: Number(this.props.data.nsds5replicabackoffmax) === 0 ? 300 : Number(this.props.data.nsds5replicabackoffmax),
            _nsds5replicakeepaliveupdateinterval: Number(this.props.data.nsds5replicakeepaliveupdateinterval) === 0 ? 3600 : Number(this.props.data.nsds5replicakeepaliveupdateinterval),
        };

        this.handleToggle = (_event, isExpanded) => {
            this.setState({
                isExpanded
            });
        };

        this.handleMinusChange = () => {
            this.setState({
                newRID: Number(this.state.newRID) - 1
            });
        };
        this.handleNumberChange = (event) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                newRID: newValue > 65534 ? 65534 : newValue < 1 ? 1 : newValue
            });
        };
        this.handlePlusChange = () => {
            this.setState({
                newRID: Number(this.state.newRID) + 1
            });
        };

        this.maxValue = 20000000;
        this.onMinusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validateSaveBtn() });
        };
        this.onConfigChange = (event, id, min) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > this.maxValue ? this.maxValue : newValue < min ? min : newValue
            }, () => { this.validateSaveBtn() });
        };
        this.onPlusConfig = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validateSaveBtn() });
        };

        this.confirmManagerDelete = this.confirmManagerDelete.bind(this);
        this.closeConfirmManagerDelete = this.closeConfirmManagerDelete.bind(this);
        this.deleteManager = this.deleteManager.bind(this);
        this.handleShowAddManager = this.handleShowAddManager.bind(this);
        this.closeAddManagerModal = this.closeAddManagerModal.bind(this);
        this.addManager = this.addManager.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.onModalChange = this.onModalChange.bind(this);
        this.onManagerChange = this.onManagerChange.bind(this);
        this.handleShowPromoteDemoteModal = this.handleShowPromoteDemoteModal.bind(this);
        this.closePromoteDemoteModal = this.closePromoteDemoteModal.bind(this);
        this.doRoleChange = this.doRoleChange.bind(this);
        this.handleSaveConfig = this.handleSaveConfig.bind(this);
        this.validateSaveBtn = this.validateSaveBtn.bind(this);
    }

    doRoleChange (changeType) {
        let action = "demote";
        if (changeType === "Promoting") {
            action = "promote";
        }
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket', 'replication', action,
            '--suffix=' + this.props.suffix, "--newrole=" + this.state.newRole];
        if (this.state.newRole === "Supplier") {
            const ridNum = parseInt(this.state.newRID, 10);
            if (ridNum < 1 || ridNum >= 65535) {
                this.props.addNotification(
                    "error",
                    _("A Supplier replica requires a unique numerical identifier.  Please enter an ID between 1 and 65534")
                );
                return;
            }
            cmd.push("--replica-id=" + this.state.newRID);
        }
        this.setState({
            roleChangeSpinning: true
        });
        log_cmd('doRoleChange', 'change replica role', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload();
                    this.props.addNotification(
                        "success",
                        cockpit.format(_("Successfully $0d replica to a $1"), action, this.state.newRole)
                    );
                    this.setState({
                        roleChangeSpinning: false,
                        showPromoteDemoteModal: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to $0 replica - $1"), action, errMsg.desc)
                    );
                    this.setState({
                        roleChangeSpinning: false,
                        showPromoteDemoteModal: false
                    });
                });
    }

    closePromoteDemoteModal () {
        this.setState({
            showPromoteDemoteModal: false
        });
    }

    handleShowPromoteDemoteModal () {
        this.setState({
            showPromoteDemoteModal: true,
            modalChecked: false,
        });
    }

    closeAddManagerModal () {
        this.setState({
            showAddManagerModal: false
        });
    }

    handleShowAddManager () {
        this.setState({
            showAddManagerModal: true,
            manager: "cn=replication manager,cn=config",
            manager_passwd: "",
            manager_passwd_confirm: "",
            errObj: {
                manager_passwd: true,
                manager_passwd_confirm: true,
            }
        });
    }

    addManager () {
        // Validate DN
        if (!valid_dn(this.state.manager)) {
            this.props.addNotification(
                "error",
                cockpit.format(_("Invalid DN for the Replication Manager: $0"), this.state.manager)
            );
            return;
        }

        if (this.state.manager_passwd === "" || this.state.manager_passwd_confirm === "") {
            this.props.addNotification(
                "error", _("You must provide a password for the Replication Manager")
            );
            return;
        }
        if (this.state.manager_passwd !== this.state.manager_passwd_confirm) {
            this.props.addNotification(
                "error", _("Passwords do not match")
            );
            return;
        }

        this.setState({
            addManagerSpinning: true
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "create-manager", "--suffix=" + this.props.suffix, "--name=" + this.state.manager,
        ];

        // Something changed, perform the update
        const config = {
            cmd,
            promptArg: "", // repl manager auto prompts when passwd is missing
            passwd: this.state.manager_passwd,
            addNotification: this.props.addNotification,
            msg: _("Replication Manager"),
            success_msg: _("Successfully added Replication Manager"),
            error_msg: _("Failure adding Replication Manager"),
            state_callback: () => {
                this.setState({
                    addManagerSpinning: false,
                    showAddManagerModal: false
                });
            },
            reload_func: this.props.reloadConfig,
            reload_arg: this.props.suffix,
            funcName: "addManager",
            funcDesc: _("Adding Replication Manager")
        };
        callCmdStreamPassword(config);
    }

    onModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        this.setState({
            [attr]: value,
        });
    }

    validateSaveBtn() {
        let saveBtnDisabled = true;
        const config_attrs = [
            'nsds5replicabinddngroup', 'nsds5replicabinddngroupcheckinterval',
            'nsds5replicapurgedelay', 'nsds5replicatombstonepurgeinterval',
            'nsds5replicareleasetimeout', 'nsds5replicaprotocoltimeout',
            'nsds5replicabackoffmin', 'nsds5replicabackoffmax',
            'nsds5replicaprecisetombstonepurging', 'nsds5replicakeepaliveupdateinterval',
        ];
        // Check if a setting was changed, if so enable the save button
        for (const config_attr of config_attrs) {
            if (this.state[config_attr] !== this.state['_' + config_attr]) {
                saveBtnDisabled = false;
                break;
            }
        }
        this.setState({
            saveBtnDisabled,
        });
    }

    handleChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let saveBtnDisabled = true;
        let valueErr = false;
        const errObj = this.state.errObj;

        const config_attrs = [
            'nsds5replicabinddngroup', 'nsds5replicabinddngroupcheckinterval',
            'nsds5replicapurgedelay', 'nsds5replicatombstonepurgeinterval',
            'nsds5replicareleasetimeout', 'nsds5replicaprotocoltimeout',
            'nsds5replicabackoffmin', 'nsds5replicabackoffmax',
            'nsds5replicaprecisetombstonepurging', 'nsds5replicakeepaliveupdateinterval',
        ];
        // Check if a setting was changed, if so enable the save button
        for (const config_attr of config_attrs) {
            if (attr === config_attr && this.state['_' + config_attr] !== value) {
                saveBtnDisabled = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const config_attr of config_attrs) {
            if (attr !== config_attr && this.state['_' + config_attr] !== this.state[config_attr]) {
                saveBtnDisabled = false;
                break;
            }
        }
        if (attr === 'nsds5replicabinddngroup') {
            if (!valid_dn(value)) {
                valueErr = true;
                saveBtnDisabled = true;
            }
        } else if (this.state.nsds5replicabinddngroup !== "" && !valid_dn(this.state.nsds5replicabinddngroup)) {
            saveBtnDisabled = true;
        }

        errObj[e.target.id] = valueErr;
        this.setState({
            [attr]: value,
            saveBtnDisabled,
            errObj
        });
    }

    onManagerChange(e) {
        const value = e.target.value;
        const attr = e.target.id;
        let valueErr = false;
        const errObj = this.state.errObj;
        if (value === "") {
            valueErr = true;
        }
        // Handle password chnages
        if (attr === "manager_passwd") {
            if (value !== this.state.manager_passwd_confirm) {
                // No match
                valueErr = true;
            } else {
                errObj[attr] = false;
                errObj.manager_passwd_confirm = false;
            }
        } else if (attr === "manager_passwd_confirm") {
            if (value !== this.state.manager_passwd) {
                // No match
                valueErr = true;
            } else {
                errObj[attr] = false;
                errObj.manager_passwd = false;
            }
        }

        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            errObj
        });
    }

    confirmManagerDelete (name) {
        this.setState({
            showConfirmManagerDelete: true,
            modalChecked: false,
            modalSpinning: false,
            manager: name,
        });
    }

    closeConfirmManagerDelete () {
        this.setState({
            showConfirmManagerDelete: false,
            manager: "",
        });
    }

    deleteManager() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "delete-manager", "--suffix=" + this.props.suffix, "--name=" + this.state.manager
        ];
        this.setState({
            modalSpinning: true
        });
        log_cmd("deleteManager", "Deleting Replication Manager", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConfig(this.props.suffix);
                    this.setState({
                        modalSpinning: false
                    });
                    this.props.addNotification(
                        "success",
                        _("Successfully removed Replication Manager")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reloadConfig(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failure removing Replication Manager - $0"), errMsg.desc)
                    );
                    this.setState({
                        modalSpinning: false
                    });
                });
    }

    handleSaveConfig () {
        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'set', '--suffix=' + this.props.suffix
        ];

        if (this.state.nsds5replicabackoffmax !== this.state._nsds5replicabackoffmax) {
            cmd.push("--repl-backoff-max=" + this.state.nsds5replicabackoffmax);
        }
        if (this.state.nsds5replicabackoffmin !== this.state._nsds5replicabackoffmin) {
            cmd.push("--repl-backoff-min=" + this.state.nsds5replicabackoffmin);
        }

        if (this.state.nsds5replicaprotocoltimeout !== this.state._nsds5replicaprotocoltimeout) {
            cmd.push("--repl-protocol-timeout=" + this.state.nsds5replicaprotocoltimeout);
        }
        if (this.state.nsds5replicaprecisetombstonepurging !== this.state._nsds5replicaprecisetombstonepurging) {
            if (this.state.nsds5replicaprecisetombstonepurging) {
                cmd.push("--repl-fast-tombstone-purging=on");
            } else {
                cmd.push("--repl-fast-tombstone-purging=off");
            }
        }
        if (this.state.nsds5replicatombstonepurgeinterval !== this.state._nsds5replicatombstonepurgeinterval) {
            cmd.push("--repl-tombstone-purge-interval=" + this.state.nsds5replicatombstonepurgeinterval);
        }
        if (this.state.nsds5replicabinddngroup !== this.state._nsds5replicabinddngroup) {
            cmd.push("--repl-bind-group=" + this.state.nsds5replicabinddngroup);
        }
        if (this.state.nsds5replicabinddngroupcheckinterval !== this.state._nsds5replicabinddngroupcheckinterval) {
            cmd.push("--repl-bind-group-interval=" + this.state.nsds5replicabinddngroupcheckinterval);
        }
        if (this.state.nsds5replicakeepaliveupdateinterval !== this.state._nsds5replicakeepaliveupdateinterval) {
            cmd.push("--repl-keepalive-update-interval=" + this.state.nsds5replicakeepaliveupdateinterval);
        }
        if (this.state.nsds5replicareleasetimeout !== this.state._nsds5replicareleasetimeout) {
            cmd.push("--repl-release-timeout=" + this.state.nsds5replicareleasetimeout);
        }
        if (this.state.nsds5replicapurgedelay !== this.state._nsds5replicapurgedelay) {
            cmd.push("--repl-purge-delay=" + this.state.nsds5replicapurgedelay);
        }
        if (cmd.length > 6) {
            this.setState({
                // Start the spinner
                saving: true
            });
            log_cmd("handleSaveConfig", "Applying replication changes", cmd);
            const msg = _("Successfully updated replication configuration.");
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.props.reloadConfig(this.props.suffix);
                        this.props.addNotification(
                            "success",
                            msg
                        );
                        this.setState({
                            saving: false
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.reloadConfig(this.props.suffix);
                        this.setState({
                            saving: false
                        });
                        let msg = errMsg.desc;
                        if ('info' in errMsg) {
                            msg = errMsg.desc + " - " + errMsg.info;
                        }
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error updating replication configuration - $0"), msg)
                        );
                    });
        }
    }

    render() {
        let roleButton = "";
        const manager_rows = [];
        let saveBtnName = _("Save Configuration");
        const extraPrimaryProps = {};
        if (this.state.saving) {
            saveBtnName = _("Saving Config ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }
        for (const row of this.props.data.nsds5replicabinddn) {
            manager_rows.push(row);
        }

        if (this.props.role === "Supplier") {
            roleButton = (
                <Button
                    variant="primary"
                    onClick={this.handleShowPromoteDemoteModal}
                    title={_("Demote this Supplier replica to a Hub or Consumer")}
                    className="ds-left-margin"
                >
                    {_("Change Role")}
                </Button>
            );
        } else if (this.props.role === "Hub") {
            roleButton = (
                <Button
                    variant="primary"
                    onClick={this.handleShowPromoteDemoteModal}
                    title={_("Promote or Demote this Hub replica to a Supplier or Consumer")}
                    className="ds-left-margin"
                >
                    {_("Change Role")}
                </Button>
            );
        } else {
            // Consumer
            roleButton = (
                <Button
                    variant="primary"
                    onClick={this.handleShowPromoteDemoteModal}
                    title={_("Promote this Consumer replica to a Supplier or Hub")}
                    className="ds-left-margin"
                >
                    {_("Change Role")}
                </Button>
            );
        }

        return (
            <div className={this.state.saving ? "ds-disabled" : ""}>
                <div className="ds-margin-top-xxlg ds-left-margin">
                    <Form isHorizontal autoComplete="off">
                        <Grid>
                            <GridItem className="ds-label" span={2}>
                                {_("Replica Role")}
                            </GridItem>
                            <GridItem span={2}>
                                <TextInput
                                    value={this.props.role}
                                    type="text"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="replrole"
                                    id="replrole"
                                    isDisabled
                                />
                            </GridItem>
                            <GridItem span={1}>
                                {roleButton}
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={2}>
                                {_("Replica ID")}
                            </GridItem>
                            <GridItem span={2}>
                                <TextInput
                                    value={this.props.data.nsds5replicaid}
                                    type="text"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="replid"
                                    id="replid"
                                    isDisabled
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={12}>
                                {_("Replication Managers")}
                            </GridItem>
                            <GridItem className="ds-margin-top" span={9}>
                                <ManagerTable
                                    rows={manager_rows}
                                    confirmDelete={this.confirmManagerDelete}
                                />
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top">
                            <GridItem span={2}>
                                <Button
                                    variant="secondary"
                                    onClick={this.handleShowAddManager}
                                >
                                    {_("Add Replication Manager")}
                                </Button>
                            </GridItem>
                        </Grid>
                        <ExpandableSection
                            toggleText={this.state.isExpanded ? _("Hide Advanced Settings") : _("Show Advanced Settings")}
                            onToggle={(event, isExpanded) => this.handleToggle(event, isExpanded)}
                            isExpanded={this.state.isExpanded}
                        >
                            <div className="ds-margin-top ds-margin-left ds-margin-bottom-md">
                                <Grid
                                    title={_("The DN of the replication manager group")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Bind DN Group")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state.nsds5replicabinddngroup}
                                            type="text"
                                            id="nsds5replicabinddngroup"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="nsds5replicabinddngroup"
                                            onChange={(e, str) => {
                                                this.handleChange(e);
                                            }}
                                            validated={this.state.errObj.nsds5replicabinddngroup && this.state.nsds5replicabinddngroup !== "" ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("The interval to check for any changes in the group memebrship specified in the Bind DN Group and automatically rebuilds the list for the replication managers accordingly.  (nsds5replicabinddngroupcheckinterval).")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Bind DN Group Check Interval")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={this.state.nsds5replicabinddngroupcheckinterval}
                                            min={-1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("nsds5replicabinddngroupcheckinterval") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsds5replicabinddngroupcheckinterval", -1) }}
                                            onPlus={() => { this.onPlusConfig("nsds5replicabinddngroupcheckinterval") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("This controls the maximum age of deleted entries (tombstone entries), and entry state information.  (nsds5replicapurgedelay).")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Purge Delay")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={this.state.nsds5replicapurgedelay}
                                            min={-1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("nsds5replicapurgedelay") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsds5replicapurgedelay", -1) }}
                                            onPlus={() => { this.onPlusConfig("nsds5replicapurgedelay") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("This attribute specifies the time interval in seconds between purge operation cycles.  (nsds5replicatombstonepurgeinterval).")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Tombstone Purge Interval")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={this.state.nsds5replicatombstonepurgeinterval}
                                            min={-1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("nsds5replicatombstonepurgeinterval") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsds5replicatombstonepurgeinterval", -1) }}
                                            onPlus={() => { this.onPlusConfig("nsds5replicatombstonepurgeinterval") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("A time limit (in seconds) that tells a replication session to yield if other replicas are trying to acquire this one (nsds5replicareleasetimeout).")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Replica Release Timeout")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={this.state.nsds5replicareleasetimeout}
                                            min={0}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("nsds5replicareleasetimeout") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsds5replicareleasetimeout", 0) }}
                                            onPlus={() => { this.onPlusConfig("nsds5replicareleasetimeout") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("A timeout on how long to wait before stopping a replication session when the server is being stopped, replication is being disabled, or when removing a replication agreement. (nsds5replicaprotocoltimeout).")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Replication Timeout")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={this.state.nsds5replicaprotocoltimeout}
                                            min={1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("nsds5replicaprotocoltimeout") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsds5replicaprotocoltimeout", 1) }}
                                            onPlus={() => { this.onPlusConfig("nsds5replicaprotocoltimeout") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("This is the minimum amount of time in seconds that a replication session will go into a backoff state  (nsds5replicabackoffmin).")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Back Off Minimum")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={this.state.nsds5replicabackoffmin}
                                            min={1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("nsds5replicabackoffmin") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsds5replicabackoffmin", 1) }}
                                            onPlus={() => { this.onPlusConfig("nsds5replicabackoffmin") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("This is the maximum amount of time in seconds that a replication session will go into a backoff state  (nsds5replicabackoffmax).")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Back Off Maximum")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={this.state.nsds5replicabackoffmax}
                                            min={1}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("nsds5replicabackoffmax") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsds5replicabackoffmax", 1) }}
                                            onPlus={() => { this.onPlusConfig("nsds5replicabackoffmax") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("The interval in seconds that the server will apply an internal update to get the RUV from getting stale. (nsds5replicakeepaliveupdateinterval).")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Refresh RUV Interval")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <NumberInput
                                            value={this.state.nsds5replicakeepaliveupdateinterval}
                                            min={60}
                                            max={this.maxValue}
                                            onMinus={() => { this.onMinusConfig("nsds5replicakeepaliveupdateinterval") }}
                                            onChange={(e) => { this.onConfigChange(e, "nsds5replicakeepaliveupdateinterval", 60) }}
                                            onPlus={() => { this.onPlusConfig("nsds5replicakeepaliveupdateinterval") }}
                                            inputName="input"
                                            inputAriaLabel="number input"
                                            minusBtnAriaLabel="minus"
                                            plusBtnAriaLabel="plus"
                                            widthChars={8}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("Enables faster tombstone purging (nsds5replicaprecisetombstonepurging).")}
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Fast Tombstone Purging")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <Checkbox
                                            id="nsds5replicaprecisetombstonepurging"
                                            isChecked={this.state.nsds5replicaprecisetombstonepurging}
                                            onChange={(e, str) => {
                                                this.handleChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top-xlg">
                                    <GridItem span={2}>
                                        <Button
                                            variant="primary"
                                            onClick={this.handleSaveConfig}
                                            isDisabled={this.state.saveBtnDisabled}
                                            isLoading={this.state.saving}
                                            spinnerAriaValueText={this.state.saving ? _("Saving") : undefined}
                                            {...extraPrimaryProps}
                                        >
                                            {saveBtnName}
                                        </Button>
                                    </GridItem>
                                </Grid>
                            </div>
                        </ExpandableSection>
                    </Form>
                    <DoubleConfirmModal
                        showModal={this.state.showConfirmManagerDelete}
                        closeHandler={this.closeConfirmManagerDelete}
                        handleChange={this.onModalChange}
                        actionHandler={this.deleteManager}
                        spinning={this.state.modalSpinning}
                        item={this.state.manager}
                        checked={this.state.modalChecked}
                        mTitle={_("Remove Replication Manager")}
                        mMsg={_("Are you sure you want to remove this Replication Manager?")}
                        mSpinningMsg={_("Removing Manager ...")}
                        mBtnName={_("Remove Manager")}
                    />
                    <AddManagerModal
                        showModal={this.state.showAddManagerModal}
                        closeHandler={this.closeAddManagerModal}
                        handleChange={this.onManagerChange}
                        saveHandler={this.addManager}
                        spinning={this.state.addManagerSpinning}
                        manager={this.state.manager}
                        manager_passwd={this.state.manager_passwd}
                        manager_passwd_confirm={this.state.manager_passwd_confirm}
                        error={this.state.errObj}
                    />
                    <ChangeReplRoleModal
                        showModal={this.state.showPromoteDemoteModal}
                        closeHandler={this.closePromoteDemoteModal}
                        handleChange={this.onModalChange}
                        saveHandler={this.doRoleChange}
                        spinning={this.state.roleChangeSpinning}
                        role={this.props.role}
                        newRole={this.state.newRole}
                        checked={this.state.modalChecked}
                        newRID={this.state.newRID}
                        onMinus={this.handleMinusChange}
                        onNumberChange={this.handleNumberChange}
                        onPlus={this.handlePlusChange}
                    />
                </div>
            </div>
        );
    }
}

ReplConfig.propTypes = {
    role: PropTypes.string,
    suffix: PropTypes.string,
    data: PropTypes.object,
    serverId: PropTypes.string,
};

ReplConfig.defaultProps = {
    role: "",
    suffix: "",
    data: {},
    serverId: "",
};
