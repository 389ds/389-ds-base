import cockpit from "cockpit";
import React from "react";
import { log_cmd, valid_dn } from "../tools.jsx";
import { ConfirmPopup } from "../notifications.jsx";
import CustomCollapse from "../customCollapse.jsx";
import { ManagerTable } from "./replTables.jsx";
import { AddManagerModal, ChangeReplRoleModal } from "./replModals.jsx";
import {
    Button,
    Row,
    Checkbox,
    Col,
    ControlLabel,
    Form,
    FormControl,
    Spinner,
    // noop,
} from "patternfly-react";
// import PropTypes from "prop-types";

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
            newRole: this.props.role == "Supplier" ? "Hub" : "Supplier",
            newRID: "1",
            modalChecked: false,
            errObj: {},
            // Config Settings
            nsds5replicabinddn: this.props.data['nsds5replicabinddn'],
            nsds5replicabinddngroup: this.props.data['nsds5replicabinddngroup'],
            nsds5replicabinddngroupcheckinterval: this.props.data['nsds5replicabinddngroupcheckinterval'],
            nsds5replicareleasetimeout: this.props.data['nsds5replicareleasetimeout'],
            nsds5replicapurgedelay: this.props.data['nsds5replicapurgedelay'],
            nsds5replicatombstonepurgeinterval: this.props.data['nsds5replicatombstonepurgeinterval'],
            nsds5replicaprecisetombstonepurging: this.props.data['nsds5replicaprecisetombstonepurging'],
            nsds5replicaprotocoltimeout: this.props.data['nsds5replicaprotocoltimeout'],
            nsds5replicabackoffmin: this.props.data['nsds5replicabackoffmin'],
            nsds5replicabackoffmax: this.props.data['nsds5replicabackoffmax'],
            clMaxEntries: this.props.data['clMaxEntries'],
            clMaxAge: this.props.data['clMaxAge'],
            clTrimInt: this.props.data['clTrimInt'],
            clEncrypt: this.props.data['clEncrypt'],
            // Original settings
            _nsds5replicabinddn: this.props.data['nsds5replicabinddn'],
            _nsds5replicabinddngroup: this.props.data['nsds5replicabinddngroup'],
            _nsds5replicabinddngroupcheckinterval: this.props.data['nsds5replicabinddngroupcheckinterval'],
            _nsds5replicareleasetimeout: this.props.data['nsds5replicareleasetimeout'],
            _nsds5replicapurgedelay: this.props.data['nsds5replicapurgedelay'],
            _nsds5replicatombstonepurgeinterval: this.props.data['nsds5replicatombstonepurgeinterval'],
            _nsds5replicaprecisetombstonepurging: this.props.data['nsds5replicaprecisetombstonepurging'],
            _nsds5replicaprotocoltimeout: this.props.data['nsds5replicaprotocoltimeout'],
            _nsds5replicabackoffmin: this.props.data['nsds5replicabackoffmin'],
            _nsds5replicabackoffmax: this.props.data['nsds5replicabackoffmax'],
            _clMaxEntries: this.props.data['clMaxEntries'],
            _clMaxAge: this.props.data['clMaxAge'],
            _clTrimInt: this.props.data['clTrimInt'],
            _clEncrypt: this.props.data['clEncrypt']

        };

        this.confirmManagerDelete = this.confirmManagerDelete.bind(this);
        this.closeConfirmManagerDelete = this.closeConfirmManagerDelete.bind(this);
        this.deleteManager = this.deleteManager.bind(this);
        this.showAddManager = this.showAddManager.bind(this);
        this.closeAddManagerModal = this.closeAddManagerModal.bind(this);
        this.addManager = this.addManager.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleManagerChange = this.handleManagerChange.bind(this);
        this.showPromoteDemoteModal = this.showPromoteDemoteModal.bind(this);
        this.closePromoteDemoteModal = this.closePromoteDemoteModal.bind(this);
        this.doRoleChange = this.doRoleChange.bind(this);
        this.saveConfig = this.saveConfig.bind(this);
    }

    doRoleChange (changeType) {
        let action = "demote";
        if (changeType == "Promoting") {
            action = "promote";
        }
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket', 'replication', action,
            '--suffix=' + this.props.suffix, "--newrole=" + this.state.newRole];
        if (this.state.newRole == "Supplier") {
            let ridNum = parseInt(this.state.newRID, 10);
            if (ridNum < 1 || ridNum >= 65535) {
                this.props.addNotification(
                    "error",
                    "A Supplier replica requires a unique numerical identifier.  Please enter an ID between 1 and 65534"
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
                        `Successfully ${action}d replica to a ${this.state.newRole}`
                    );
                    this.setState({
                        roleChangeSpinning: false,
                        showPromoteDemoteModal: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        `Failed to ${action} replica - ${errMsg.desc}`
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

    showPromoteDemoteModal () {
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

    showAddManager () {
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
                `Invalid DN for the Replication Manager: ${this.state.manager}`
            );
            return;
        }

        if (this.state.manager_passwd == "" || this.state.manager_passwd_confirm == "") {
            this.props.addNotification(
                "error", "You must provide a password for the Replication Manager"
            );
            return;
        }
        if (this.state.manager_passwd != this.state.manager_passwd_confirm) {
            this.props.addNotification(
                "error", "Passwords do not match"
            );
            return;
        }

        this.setState({
            addManagerSpinning: true
        });

        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "create-manager", "--suffix=" + this.props.suffix, "--name=" + this.state.manager,
            "--passwd=" + this.state.manager_passwd
        ];

        log_cmd("addManager", "Adding Replication Manager", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConfig(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        `Successfully added Replication Manager`
                    );
                    this.setState({
                        addManagerSpinning: false,
                        showAddManagerModal: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reloadConfig(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failure adding Replication Manager - ${errMsg.desc}`
                    );
                    this.setState({
                        addManagerSpinning: false,
                        showAddManagerModal: false
                    });
                });
    }

    handleChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        let errObj = this.state.errObj;
        if (value == "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        });
    }

    handleManagerChange(e) {
        let value = e.target.value;
        let attr = e.target.id;
        let valueErr = false;
        let errObj = this.state.errObj;
        if (value == "") {
            valueErr = true;
        }
        // Handle password chnages
        if (attr == "manager_passwd") {
            if (value != this.state.manager_passwd_confirm) {
                // No match
                valueErr = true;
            } else {
                errObj[attr] = false;
                errObj['manager_passwd_confirm'] = false;
            }
        } else if (attr == "manager_passwd_confirm") {
            if (value != this.state.manager_passwd) {
                // No match
                valueErr = true;
            } else {
                errObj[attr] = false;
                errObj['manager_passwd'] = false;
            }
        }

        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            errObj: errObj
        });
    }

    confirmManagerDelete (item) {
        this.setState({
            showConfirmManagerDelete: true,
            manager: item.name,
        });
    }

    closeConfirmManagerDelete () {
        this.setState({
            showConfirmManagerDelete: false,
            manager: "",
        });
    }

    deleteManager (dn) {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "replication", "delete-manager", "--suffix=" + this.props.suffix, "--name=" + dn
        ];
        log_cmd("deleteManager", "Deleting Replication Manager", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConfig(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        `Successfully removed Replication Manager`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reloadConfig(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Failure removing Replication Manager - ${errMsg.desc}`
                    );
                });
    }

    saveConfig () {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'set', '--suffix=' + this.props.suffix
        ];

        if (this.state.nsds5replicabackoffmax != this.state._nsds5replicabackoffmax) {
            cmd.push("--repl-backoff-max=" + this.state.nsds5replicabackoffmax);
        }
        if (this.state.nsds5replicabackoffmin != this.state._nsds5replicabackoffmin) {
            cmd.push("--repl-backoff-min=" + this.state.nsds5replicabackoffmin);
        }

        if (this.state.nsds5replicaprotocoltimeout != this.state._nsds5replicaprotocoltimeout) {
            cmd.push("--repl-protocol-timeout=" + this.state.nsds5replicaprotocoltimeout);
        }
        if (this.state.nsds5replicaprecisetombstonepurging != this.state._nsds5replicaprecisetombstonepurging) {
            if (this.state.nsds5replicaprecisetombstonepurging) {
                cmd.push("--repl-fast-tombstone-purging=on");
            } else {
                cmd.push("--repl-fast-tombstone-purging=off");
            }
        }
        if (this.state.nsds5replicatombstonepurgeinterval != this.state._nsds5replicatombstonepurgeinterval) {
            cmd.push("--repl-tombstone-purge-interval=" + this.state.nsds5replicatombstonepurgeinterval);
        }
        if (this.state.nsds5replicabinddngroup != this.state._nsds5replicabinddngroup) {
            cmd.push("--repl-bind-group=" + this.state.nsds5replicabinddngroup);
        }
        if (this.state.nsds5replicabinddngroupcheckinterval != this.state._nsds5replicabinddngroupcheckinterval) {
            cmd.push("--repl-bind-group-interval=" + this.state.nsds5replicabinddngroupcheckinterval);
        }
        if (this.state.nsds5replicareleasetimeout != this.state._nsds5replicareleasetimeout) {
            cmd.push("--repl-release-timeout=" + this.state.nsds5replicareleasetimeout);
        }
        if (this.state.nsds5replicapurgedelay != this.state._nsds5replicapurgedelay) {
            cmd.push("--repl-purge-delay=" + this.state.nsds5replicapurgedelay);
        }
        if (cmd.length > 6) {
            this.setState({
                // Start the spinner
                saving: true
            });
            log_cmd("saveConfig", "Applying replication changes", cmd);
            let msg = "Successfully updated replication configuration.";
            cockpit
                    .spawn(cmd, {superuser: true, "err": "message"})
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
                        let errMsg = JSON.parse(err);
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
                            `Error updating replication configuration - ${msg}`
                        );
                    });
        }
    }

    render() {
        let content = "";
        let roleButton = "";
        let manager_rows = [];
        for (let row of this.props.data.nsds5replicabinddn) {
            manager_rows.push({'name': row});
        }

        if (this.props.role == "Supplier") {
            roleButton =
                <Button
                    bsStyle="primary"
                    onClick={this.showPromoteDemoteModal}
                    title="Demote this Supplier replica to a Hub or Consumer"
                    className="ds-inline-btn"
                >
                    Demote
                </Button>;
        } else if (this.props.role == "Hub") {
            roleButton =
                <Button
                    bsStyle="primary"
                    onClick={this.showPromoteDemoteModal}
                    title="Promote or Demote this Hub replica to a Supplier or Consumer"
                    className="ds-inline-btn"
                >
                    Promote/Demote
                </Button>;
        } else {
            // Consumer
            roleButton =
                <Button
                    bsStyle="primary"
                    onClick={this.showPromoteDemoteModal}
                    title="Promote this Consumer replica to a Supplier or Hub"
                    className="ds-inline-btn"
                >
                    Promote
                </Button>;
        }

        if (this.state.saving) {
            content =
                <div className="ds-margin-top-xxlg ds-loading-spinner-tree ds-center">
                    <h4>Saving replication configuration ...</h4>
                    <Spinner loading size="md" />
                </div>;
        } else {
            content =
                <div className="ds-margin-top-xxlg ds-left-margin">
                    <Form horizontal>
                        <Row className="ds-margin-top-xlg">
                            <Col sm={2}>
                                <ControlLabel>
                                    Replica Role
                                </ControlLabel>
                            </Col>
                            <Col sm={2}>
                                <FormControl
                                    type="text"
                                    defaultValue={this.props.role}
                                    disabled
                                />
                            </Col>
                            <Col sm={1}>
                                {roleButton}
                            </Col>
                        </Row>
                        <Row className="ds-margin-top">
                            <Col sm={2}>
                                <ControlLabel>
                                    Replica ID
                                </ControlLabel>
                            </Col>
                            <Col sm={2}>
                                <FormControl
                                    type="text"
                                    defaultValue={this.props.data.nsds5replicaid}
                                    size="10"
                                    disabled
                                />
                            </Col>
                        </Row>
                        <hr />
                        <Row className="ds-margin-top">
                            <Col sm={9}>
                                <ManagerTable
                                    rows={manager_rows}
                                    confirmDelete={this.confirmManagerDelete}
                                />
                            </Col>
                        </Row>
                        <Row className="ds-margin-top">
                            <Col sm={4}>
                                <Button
                                    bsStyle="primary"
                                    onClick={this.showAddManager}
                                >
                                    Add Replication Manager
                                </Button>
                            </Col>
                        </Row>
                        <CustomCollapse>
                            <div className="ds-margin-top">
                                <div className="ds-margin-left">
                                    <Row className="ds-margin-top-lg" title="The DN of the replication manager">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Bind DN Group
                                        </Col>
                                        <Col sm={6}>
                                            <FormControl
                                                id="nsds5replicabinddngroup"
                                                type="text"
                                                defaultValue={this.state.nsds5replicabinddngroup}
                                                onChange={this.handleChange}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="The interval to check for any changes in the group memebrship specified in the Bind DN Group and automatically rebuilds the list for the replication managers accordingly.  (nsds5replicabinddngroupcheckinterval).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Bind DN Group Check Interval
                                        </Col>
                                        <Col sm={6}>
                                            <FormControl
                                                id="nsds5replicabinddngroupcheckinterval"
                                                type="text"
                                                defaultValue={this.state.nsds5replicabinddngroupcheckinterval}
                                                onChange={this.handleChange}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="This controls the maximum age of deleted entries (tombstone entries), and entry state information.  (nsds5replicapurgedelay).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Purge Delay
                                        </Col>
                                        <Col sm={6}>
                                            <FormControl
                                                id="nsds5replicapurgedelay"
                                                type="text"
                                                defaultValue={this.state.nsds5replicapurgedelay}
                                                onChange={this.handleChange}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="This attribute specifies the time interval in seconds between purge operation cycles.  (nsds5replicatombstonepurgeinterval).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Tombstone Purge Interval
                                        </Col>
                                        <Col sm={6}>
                                            <FormControl
                                                id="nsds5replicatombstonepurgeinterval"
                                                type="text"
                                                defaultValue={this.state.nsds5replicatombstonepurgeinterval}
                                                onChange={this.handleChange}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="A time limit (in seconds) that tells a replication session to yield if other replicas are trying to acquire this one (nsds5replicareleasetimeout).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Replica Release Timeout
                                        </Col>
                                        <Col sm={6}>
                                            <FormControl
                                                id="nsds5replicareleasetimeout"
                                                type="text"
                                                defaultValue={this.state.nsds5replicareleasetimeout}
                                                onChange={this.handleChange}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="A timeout on how long to wait before stopping a replication session when the server is being stopped, replication is being disabled, or when removing a replication agreement. (nsds5replicaprotocoltimeout).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Replication Timeout
                                        </Col>
                                        <Col sm={6}>
                                            <FormControl
                                                id="nsds5replicaprotocoltimeout"
                                                type="text"
                                                defaultValue={this.state.nsds5replicaprotocoltimeout}
                                                onChange={this.handleChange}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="This is the minimum amount of time in seconds that a replication will go into a backoff state  (nsds5replicabackoffmin).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Back Off Minimum
                                        </Col>
                                        <Col sm={6}>
                                            <FormControl
                                                id="nsds5replicabackoffmin"
                                                type="text"
                                                defaultValue={this.state.nsds5replicabackoffmin}
                                                onChange={this.handleChange}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="This is the maximum amount of time in seconds that a replication will go into a backoff state  (nsds5replicabackoffmax).">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Back Off Maximum
                                        </Col>
                                        <Col sm={6}>
                                            <FormControl
                                                id="nsds5replicabackoffmax"
                                                type="text"
                                                defaultValue={this.state.nsds5replicabackoffmax}
                                                onChange={this.handleChange}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="Enables faster tombstone purging (nsds5replicaprecisetombstonepurging)">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Fast Tombstone Purging
                                        </Col>
                                        <Col sm={6}>
                                            <Checkbox
                                                id="nsds5replicaprecisetombstonepurging"
                                                defaultChecked={this.props.data.nsds5replicaprecisetombstonepurging}
                                                onChange={this.handleChange}
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top-lg">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            <Button
                                                bsStyle="primary"
                                                onClick={this.saveConfig}
                                            >
                                                Save Configuration
                                            </Button>
                                        </Col>
                                    </Row>
                                </div>
                            </div>
                        </CustomCollapse>
                    </Form>
                    <ConfirmPopup
                        showModal={this.state.showConfirmManagerDelete}
                        closeHandler={this.closeConfirmManagerDelete}
                        actionFunc={this.deleteManager}
                        actionParam={this.state.manager}
                        msg="Are you sure you want to remove this Replication Manager?"
                        msgContent={this.state.manager}
                    />
                    <AddManagerModal
                        showModal={this.state.showAddManagerModal}
                        closeHandler={this.closeAddManagerModal}
                        handleChange={this.handleManagerChange}
                        saveHandler={this.addManager}
                        spinning={this.state.addManagerSpinning}
                        error={this.state.errObj}
                    />
                    <ChangeReplRoleModal
                        showModal={this.state.showPromoteDemoteModal}
                        closeHandler={this.closePromoteDemoteModal}
                        handleChange={this.handleChange}
                        saveHandler={this.doRoleChange}
                        spinning={this.state.roleChangeSpinning}
                        role={this.props.role}
                        newRole={this.state.newRole}
                        checked={this.state.modalChecked}
                    />
                </div>;
        }

        return (
            <div>
                {content}
            </div>
        );
    }
}
