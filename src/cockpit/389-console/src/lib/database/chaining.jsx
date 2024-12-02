import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { log_cmd, callCmdStreamPassword } from "../tools.jsx";
import {
	Button,
	Checkbox,
	ExpandableSection,
	Form,
	Grid,
	GridItem,
	Modal,
	ModalVariant,
	SimpleList,
	SimpleListItem,
	Tab,
	Tabs,
	TabTitleText,
	TextInput,
	Text,
	TextContent,
	TextVariants,
	ValidatedOptions
} from '@patternfly/react-core';
import {
	Select,
	SelectOption,
	SelectVariant
} from '@patternfly/react-core/deprecated';
import PropTypes from "prop-types";
import {
    SyncAltIcon,
    LinkIcon
} from '@patternfly/react-icons';

const _ = cockpit.gettext;

//
// This component is the global chaining & default configuration
//
export class ChainingDatabaseConfig extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            activeTabKey: this.props.activeKey,
            oids: this.props.data.oids,
            oidList: this.props.data.oidList,
            availableOids: this.props.data.availableOids,
            comps: this.props.data.comps,
            compList: this.props.data.compList,
            availableComps: this.props.data.availableComps,
            selectedOid: "",
            selectedComp: "",
            removeOid: "",
            removeComp: "",
            showConfirmCompDelete: false,
            showConfirmOidDelete: false,
            showOidModal: false,
            showCompsModal: false,
            isExpanded: false,
            saveBtnDisabled: true,
            isOpen: false,
            isModalSelectOpen: false,
            modalChecked: false,
            modalSpinning: false,
            // Chaining config settings
            defSearchCheck: this.props.data.defSearchCheck[0],
            defBindConnLimit: this.props.data.defBindConnLimit[0],
            defBindTimeout: this.props.data.defBindTimeout[0],
            defBindRetryLimit: this.props.data.defBindRetryLimit[0],
            defConcurLimit: this.props.data.defConcurLimit[0],
            defConcurOpLimit: this.props.data.defConcurOpLimit[0],
            defConnLife: this.props.data.defConnLife[0],
            defHopLimit: this.props.data.defHopLimit[0],
            defDelay: this.props.data.defDelay[0],
            defTestDelay: this.props.data.defTestDelay[0],
            defOpConnLimit: this.props.data.defOpConnLimit[0],
            defSizeLimit: this.props.data.defSizeLimit[0],
            defTimeLimit: this.props.data.defTimeLimit[0],
            defProxy: this.props.data.defProxy,
            defRefOnScoped: this.props.data.defRefOnScoped,
            defCheckAci: this.props.data.defCheckAci,
            defUseStartTLS: this.props.data.defUseStartTLS,
            // Original values used for saving config
            _defSearchCheck: this.props.data.defSearchCheck[0],
            _defBindConnLimit: this.props.data.defBindConnLimit[0],
            _defBindTimeout: this.props.data.defBindTimeout[0],
            _defBindRetryLimit: this.props.data.defBindRetryLimit[0],
            _defConcurLimit: this.props.data.defConcurLimit[0],
            _defConcurOpLimit: this.props.data.defConcurOpLimit[0],
            _defConnLife: this.props.data.defConnLife[0],
            _defHopLimit: this.props.data.defHopLimit[0],
            _defDelay: this.props.data.defDelay[0],
            _defTestDelay: this.props.data.defTestDelay[0],
            _defOpConnLimit: this.props.data.defOpConnLimit[0],
            _defSizeLimit: this.props.data.defSizeLimit[0],
            _defTimeLimit: this.props.data.defTimeLimit[0],
            _defProxy: this.props.data.defProxy,
            _defRefOnScoped: this.props.data.defRefOnScoped,
            _defCheckAci: this.props.data.defCheckAci,
            _defUseStartTLS: this.props.data.defUseStartTLS,
        };

        this.handleToggle = (_event, isExpanded) => {
            this.setState({
                isExpanded
            });
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.onModalToggle = isModalSelectOpen => {
            this.setState({
                isModalSelectOpen
            });
        };

        this.handleChange = this.handleChange.bind(this);
        this.onModalChange = this.onModalChange.bind(this);
        this.handleSaveChainingConfig = this.handleSaveChainingConfig.bind(this);
        // Chaining Control OIDs
        this.handleShowOidModal = this.handleShowOidModal.bind(this);
        this.closeOidModal = this.closeOidModal.bind(this);
        this.onOIDChange = this.onOIDChange.bind(this);
        this.saveOids = this.saveOids.bind(this);
        this.deleteOids = this.deleteOids.bind(this);
        this.handleSelectOids = this.handleSelectOids.bind(this);
        // Chaining comps
        this.handleShowCompsModal = this.handleShowCompsModal.bind(this);
        this.closeCompsModal = this.closeCompsModal.bind(this);
        this.onCompsChange = this.onCompsChange.bind(this);
        this.saveComps = this.saveComps.bind(this);
        this.deleteComps = this.deleteComps.bind(this);
        this.handleSelectComps = this.handleSelectComps.bind(this);
        this.showConfirmDelete = this.showConfirmDelete.bind(this);
        this.closeConfirmOidDelete = this.closeConfirmOidDelete.bind(this);
        this.closeConfirmCompDelete = this.closeConfirmCompDelete.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    onModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        });
    }

    handleChange(e) {
        let saveBtnDisabled = true;
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        const check_attrs = [
            "defSearchCheck", "defBindConnLimit", "defBindTimeout",
            "defBindRetryLimit", "defConcurLimit", "defConcurOpLimit",
            "defConnLife", "defHopLimit", "defDelay",
            "defTestDelay", "defOpConnLimit", "defSizeLimit",
            "defTimeLimit", "defProxy", "defRefOnScoped",
            "defCheckAci", "defUseStartTLS",
        ];

        for (const check_attr of check_attrs) {
            if (attr !== check_attr) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    saveBtnDisabled = false;
                }
            } else if (value !== this.state['_' + check_attr]) {
                saveBtnDisabled = false;
            }
        }

        this.setState({
            [attr]: value,
            saveBtnDisabled
        });
    }

    handleSaveChainingConfig () {
        // Build up the command list
        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'chaining', 'config-set-def'
        ];
        let val = "";
        if (this.state._defUseStartTLS !== this.state.defUseStartTLS) {
            val = "off";
            if (this.state.defUseStartTLS) {
                val = "on";
            }
            cmd.push("--use-starttls=" + val);
        }
        if (this.state._defCheckAci !== this.state.defCheckAci) {
            val = "off";
            if (this.state.defCheckAci) {
                val = "on";
            }
            cmd.push("--check-aci=" + val);
        }
        if (this.state._defRefOnScoped !== this.state.defRefOnScoped) {
            val = "off";
            if (this.state.defRefOnScoped) {
                val = "on";
            }
            cmd.push("--return-ref=" + val);
        }
        if (this.state._defProxy !== this.state.defProxy) {
            val = "off";
            if (this.state.defProxy) {
                val = "on";
            }
            cmd.push("--proxied-auth=" + val);
        }
        if (this.state._defTestDelay !== this.state.defTestDelay) {
            cmd.push("--test-response-delay=" + this.state.defTestDelay);
        }
        if (this.state._defOpConnLimit !== this.state.defOpConnLimit) {
            cmd.push("--conn-op-limit=" + this.state.defOpConnLimit);
        }
        if (this.state._defSizeLimit !== this.state.defSizeLimit) {
            cmd.push("--size-limit=" + this.state.defSizeLimit);
        }
        if (this.state._defTimeLimit !== this.state.defTimeLimit) {
            cmd.push("--time-limit=" + this.state.defTimeLimit);
        }
        if (this.state._defSearchCheck !== this.state.defSearchCheck) {
            cmd.push("--abandon-check-interval=" + this.state.defSearchCheck);
        }
        if (this.state._defBindTimeout !== this.state.defBindTimeout) {
            cmd.push("--bind-timeout=" + this.state.defBindTimeout);
        }
        if (this.state._defBindRetryLimit !== this.state.defBindRetryLimit) {
            cmd.push("--bind-attempts=" + this.state.defBindRetryLimit);
        }
        if (this.state._defConcurLimit !== this.state.defConcurLimit) {
            cmd.push("--bind-limit=" + this.state.defConcurLimit);
        }
        if (this.state._defConcurOpLimit !== this.state.defConcurOpLimit) {
            cmd.push("--op-limit=" + this.state.defConcurOpLimit);
        }
        if (this.state._defConnLife !== this.state.defConnLife) {
            cmd.push("--conn-lifetime=" + this.state.defConnLife);
        }
        if (this.state._defHopLimit !== this.state.defHopLimit) {
            cmd.push("--hop-limit=" + this.state.defHopLimit);
        }
        if (this.state._defDelay !== this.state.defDelay) {
            cmd.push("--response-delay=" + this.state.defDelay);
        }
        if (this.state._defBindConnLimit !== this.state.defBindConnLimit) {
            cmd.push("--conn-bind-limit=" + this.state.defBindConnLimit);
        }

        // If we have chaining mods, then apply them...
        if (cmd.length > 5) {
            this.setState({
                saving: true
            });
            log_cmd("handleSaveChainingConfig", "Applying default chaining config change", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        // Continue with the next mod
                        this.props.reload();
                        this.props.addNotification(
                            "success",
                            _("Successfully updated chaining configuration")
                        );
                        this.setState({
                            saving: false
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.reload();
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Error updating chaining configuration - $0"), errMsg.desc)
                        );
                        this.setState({
                            saving: false
                        });
                    });
        }
    }

    //
    // Chaining OID modal functions
    //
    handleShowOidModal () {
        this.setState({
            showOidModal: true,
            selectedOid: "",
        });
    }

    closeOidModal() {
        this.setState({
            showOidModal: false
        });
    }

    onOIDChange (selectedItem, selectedItemProps) {
        const oid = selectedItemProps.children;
        if (oid !== this.state.selectedOid) {
            this.setState({
                selectedOid: oid
            });
        }
    }

    saveOids () {
        // Save chaining control oids
        if (this.state.selectedOid === "") {
            return;
        }
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-set", "--add-control=" + this.state.selectedOid
        ];
        this.setState({
            selectedOid: "",
            modalSpinning: true,
        });

        log_cmd("saveOids", "Save new chaining OID controls", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeOidModal();
                    this.props.reload(1);
                    this.setState({
                        modalSpinning: false,
                    });
                    this.props.addNotification(
                        "success",
                        _("Successfully updated chaining controls")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.setState({
                        modalSpinning: false,
                    });
                    this.closeOidModal();
                    this.props.reload(1);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error updating chaining controls - $0,"), errMsg.desc)
                    );
                });
    }

    handleSelectOids (selectedItem, selectedItemProps) {
        const oid = selectedItemProps.children;
        if (oid !== this.state.removeOid) {
            this.setState({
                removeOid: oid
            });
        }
    }

    deleteOids(props) {
        // Remove chaining oid control
        if (this.state.removeOid === "") {
            return;
        }
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-set", "--del-control=" + this.state.removeOid
        ];
        this.setState({
            removeOid: ""
        });

        log_cmd("deleteOids", "Delete chaining control oid", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(1);
                    this.props.addNotification(
                        "success",
                        _("Successfully removed chaining controls")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error removing chaining controls - $0"), errMsg.desc)
                    );
                });
    }

    //
    // Chaining Component modal functions
    //
    handleShowCompsModal () {
        this.setState({
            showCompsModal: true,
            selectedComp: "",
        });
    }

    closeCompsModal() {
        this.setState({
            showCompsModal: false
        });
    }

    onCompsChange (selectedItem, selectedItemProps) {
        const comp = selectedItemProps.children;
        if (comp !== this.state.selectedComp) {
            this.setState({
                selectedComp: comp
            });
        }
    }

    handleSelectComps (selectedItem, selectedItemProps) {
        const comp = selectedItemProps.children;
        if (comp !== this.state.removeComp) {
            this.setState({
                removeComp: comp
            });
        }
    }

    saveComps () {
        // Save chaining control Components
        if (this.state.selectedComp === "") {
            return;
        }
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-set", "--add-comp=" + this.state.selectedComp
        ];

        this.setState({
            modalSpinning: true,
            selectedComp: "",
        });

        log_cmd("saveComps", "Save new chaining components", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.closeCompsModal();
                    this.props.reload(1);
                    this.setState({
                        modalSpinnming: false,
                    });
                    this.props.addNotification(
                        "success",
                        _("Successfully updated chaining components")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.closeCompsModal();
                    this.props.reload();
                    this.setState({
                        modalSpinnming: false,
                    });
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error updating chaining components - $0"), errMsg.desc)
                    );
                });
    }

    deleteComps(props) {
        if (this.state.removeComp === "") {
            return;
        }
        // Remove chaining comps
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "config-set", "--del-comp=" + this.state.removeComp
        ];
        this.setState({
            removeComp: "",
        });

        log_cmd("deleteComps", "Delete chaining components", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(1);
                    this.props.addNotification(
                        "success",
                        _("Successfully removed chaining components")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error removing chaining components - $0"), errMsg.desc)
                    );
                });
    }

    //
    // Confirm deletion functions
    //
    showConfirmDelete(item) {
        if (item === "oid") {
            if (this.state.removeOid.length) {
                this.setState({
                    showConfirmOidDelete: true
                });
            }
        } else if (item === "comp") {
            if (this.state.removeComp.length) {
                this.setState({
                    showConfirmCompDelete: true
                });
            }
        }
    }

    closeConfirmOidDelete() {
        this.setState({
            showConfirmOidDelete: false
        });
    }

    closeConfirmCompDelete() {
        this.setState({
            showConfirmCompDelete: false
        });
    }

    render() {
        // Get OIDs and comps
        let oids = this.state.oidList.map((oid) =>
            <SimpleListItem key={oid}>{oid}</SimpleListItem>
        );
        if (oids.length === 0) {
            oids = "";
        }
        let comps = this.state.compList.map((comps) =>
            <SimpleListItem key={comps}>{comps}</SimpleListItem>
        );
        if (comps.length === 0) {
            comps = "";
        }
        let saveBtnName = _("Save Settings");
        const extraPrimaryProps = {};
        if (this.props.refreshing) {
            saveBtnName = _("Saving settings ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        return (
            <div id="chaining-page" className={this.state.saving ? "ds-disabled" : ""}>
                <TextContent>
                    <Text className="ds-config-header" component={TextVariants.h2}>{_("Database Chaining Settings")}</Text>
                </TextContent>
                <Tabs className="ds-margin-top-xlg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>{_("Default Creation Settings")}</TabTitleText>}>
                        <div className="ds-indent ds-margin-bottom-md">
                            <Grid
                                title={_("The size limit of entries returned over a database link (nsslapd-sizelimit).")}
                                className="ds-margin-top-xlg"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Size Limit")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defSizeLimit}
                                        type="number"
                                        id="defSizeLimit"
                                        aria-describedby="defSizeLimit"
                                        name="defSizeLimit"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The maximum number of operations per connections. (nsconcurrentoperationslimit).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Max Operations Per Conn")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defConcurOpLimit}
                                        type="number"
                                        id="defConcurOpLimit"
                                        aria-describedby="defConcurOpLimit"
                                        name="defConcurOpLimit"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The time limit of an operation over a database link (nsslapd-timelimit).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Time Limit")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defTimeLimit}
                                        type="number"
                                        id="defTimeLimit"
                                        aria-describedby="defTimeLimit"
                                        name="defTimeLimit"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The maximum number of operations per connections. (nsconcurrentoperationslimit).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Connection Lifetime")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defConnLife}
                                        type="number"
                                        id="defConnLife"
                                        aria-describedby="defConnLife"
                                        name="defConnLife"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The maximum number of TCP connections the database link establishes with the remote server.  (nsbindconnectionslimit).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Max TCP Connections")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defBindConnLimit}
                                        type="number"
                                        id="defBindConnLimit"
                                        aria-describedby="defBindConnLimit"
                                        name="defBindConnLimit"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Max LDAP Connections")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defOpConnLimit}
                                        type="number"
                                        id="defOpConnLimit"
                                        aria-describedby="defOpConnLimit"
                                        name="defOpConnLimit"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The number of seconds that pass before the server checks for abandoned operations.  (nsabandonedsearchcheckinterval).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Abandoned Op Check Interval")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defSearchCheck}
                                        type="number"
                                        id="defSearchCheck"
                                        aria-describedby="defSearchCheck"
                                        name="defSearchCheck"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Max Binds Per Connection")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defConcurLimit}
                                        type="number"
                                        id="defConcurLimit"
                                        aria-describedby="defConcurLimit"
                                        name="defConcurLimit"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The maximum number of times a request can be forwarded from one database link to another.  (nshoplimit).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Database Link Hop Limit")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defHopLimit}
                                        type="number"
                                        id="defHopLimit"
                                        aria-describedby="defHopLimit"
                                        name="defHopLimit"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The amount of time before the bind attempt times out. (nsbindtimeout).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Bind Timeout")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defBindTimeout}
                                        type="number"
                                        id="defBindTimeout"
                                        aria-describedby="defBindTimeout"
                                        name="defBindTimeout"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("The number of times the database link tries to bind with the remote server after a connection failure. (nsbindretrylimit).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Bind Retry Limit")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defBindRetryLimit}
                                        type="number"
                                        id="defBindRetryLimit"
                                        aria-describedby="defBindRetryLimit"
                                        name="defBindRetryLimit"
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={12}>
                                    <Checkbox
                                        label={_("Check Local ACIs")}
                                        id="defCheckAci"
                                        isChecked={this.state.defCheckAci}
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                        aria-label="check aci"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={12}>
                                    <Checkbox
                                        label={_("Send Referral On Scoped Search")}
                                        id="defRefOnScoped"
                                        isChecked={this.state.defRefOnScoped}
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                        aria-label="send ref"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Sets whether proxied authentication is allowed. (nsproxiedauthorization).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={12}>
                                    <Checkbox
                                        label={_("Allow Proxied Authentication")}
                                        id="defProxy"
                                        isChecked={this.state.defProxy}
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                        aria-label="prox auth"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Use StartTLS for connections to remote server. (nsusestarttls).")}
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={12}>
                                    <Checkbox
                                        label={_("Use StartTLS")}
                                        id="defUseStartTLS"
                                        isChecked={this.state.defUseStartTLS}
                                        onChange={(e, str) => {
                                            this.handleChange(e);
                                        }}
                                        aria-label="startTLS"
                                    />
                                </GridItem>
                            </Grid>
                            <Button
                                className="ds-margin-top-xlg"
                                variant="primary"
                                onClick={this.handleSaveChainingConfig}
                                isDisabled={this.state.saveBtnDisabled || this.state.saving}
                                isLoading={this.state.saving}
                                spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                                {...extraPrimaryProps}
                            >
                                {saveBtnName}
                            </Button>
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>{_("Controls & Components")}</TabTitleText>}>
                        <div className="ds-indent">
                            <Grid className="ds-margin-top-xlg">
                                <GridItem
                                    span={4}
                                    title={_("A list of LDAP control OIDs to be forwarded through chaining.")}
                                >
                                    <TextContent>
                                        <Text component={TextVariants.h4}>{_("Forwarded LDAP Controls")}</Text>
                                    </TextContent>
                                    <div className="ds-box ds-margin-top">
                                        <SimpleList onSelect={this.handleSelectOids} aria-label="forward ctrls">
                                            {oids}
                                        </SimpleList>
                                    </div>
                                    <div className="ds-margin-bottom-md ds-container">
                                        <div className="ds-panel-left">
                                            <Button
                                                variant="primary"
                                                onClick={this.handleShowOidModal}
                                                className="ds-button-left"
                                            >
                                                {_("Add")}
                                            </Button>
                                        </div>
                                        <div className="ds-panel-right">
                                            <Button
                                                variant="primary"
                                                onClick={e => this.showConfirmDelete("oid")}
                                                className="ds-button-right"
                                                isDisabled={this.state.removeOid === ""}
                                            >
                                                {_("Delete")}
                                            </Button>
                                        </div>
                                    </div>
                                </GridItem>
                                <GridItem span={1} />
                                <GridItem span={4} title={_("A list of components to go through chaining")}>
                                    <TextContent>
                                        <Text component={TextVariants.h4}>{_("Components to Chain")}</Text>
                                    </TextContent>
                                    <div className="ds-box ds-margin-top">
                                        <SimpleList onSelect={this.handleSelectComps} aria-label="comps">
                                            {comps}
                                        </SimpleList>
                                    </div>
                                    <div className="ds-margin-bottom-md ds-container">
                                        <div className="ds-panel-left">
                                            <Button
                                                variant="primary"
                                                onClick={this.handleShowCompsModal}
                                                className="ds-button-left"
                                            >
                                                {_("Add")}
                                            </Button>
                                        </div>
                                        <div className="ds-panel-right">
                                            <Button
                                                variant="primary"
                                                onClick={e => this.showConfirmDelete("comp")}
                                                className="ds-button-right"
                                                isDisabled={this.state.removeComp === ""}
                                            >
                                                {_("Delete")}
                                            </Button>
                                        </div>
                                    </div>
                                </GridItem>
                            </Grid>
                        </div>
                    </Tab>
                </Tabs>

                <ChainControlsModal
                    showModal={this.state.showOidModal}
                    closeHandler={this.closeOidModal}
                    handleChange={this.onOIDChange}
                    saveHandler={this.saveOids}
                    oidList={this.state.availableOids}
                    spinning={this.state.modalSpinning}
                />
                <ChainCompsModal
                    showModal={this.state.showCompsModal}
                    closeHandler={this.closeCompsModal}
                    handleChange={this.onCompsChange}
                    saveHandler={this.saveComps}
                    compList={this.state.availableComps}
                    spinning={this.state.modalSpinning}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmOidDelete}
                    closeHandler={this.closeConfirmOidDelete}
                    handleChange={this.onModalChange}
                    actionHandler={this.deleteOids}
                    spinning={this.state.modalSpinning}
                    item={this.state.removeOid}
                    checked={this.state.modalChecked}
                    mTitle={_("Remove Chaining OID")}
                    mMsg={_("Are you sure you want to delete this OID?")}
                    mSpinningMsg={_("Deleting ...")}
                    mBtnName={_("Delete")}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmCompDelete}
                    closeHandler={this.closeConfirmCompDelete}
                    handleChange={this.onModalChange}
                    actionHandler={this.deleteComps}
                    spinning={this.state.modalSpinning}
                    item={this.state.removeComp}
                    checked={this.state.modalChecked}
                    mTitle={_("Remove Chaining Component")}
                    mMsg={_("Are you sure you want to delete this component?")}
                    mSpinningMsg={_("Deleting ...")}
                    mBtnName={_("Delete")}
                />
            </div>
        );
    }
}

//
// This is the component for the actual database link under a suffix
//
export class ChainingConfig extends React.Component {
    constructor(props) {
        super(props);

        if (this.props.data) {
            this.state = {
                errObj: {},
                showDeleteConfirm: false,
                linkPwdMatch: true,
                modalSpinning: false,
                modalChecked: false,
                saving: false,
                saveBtnDisabled: true,
                isOpen: false,
                // Settings
                nsfarmserverurl: this.props.data.nsfarmserverurl,
                nsmultiplexorbinddn: this.props.data.nsmultiplexorbinddn,
                nsmultiplexorcredentials: this.props.data.nsmultiplexorcredentials,
                nsmultiplexorcredentials_confirm: this.props.data.nsmultiplexorcredentials,
                sizelimit: this.props.data.sizelimit,
                timelimit: this.props.data.timelimit,
                bindconnlimit: this.props.data.bindconnlimit,
                opconnlimit: this.props.data.opconnlimit,
                concurrbindlimit: this.props.data.concurrbindlimit,
                bindtimeout: this.props.data.bindtimeout,
                bindretrylimit: this.props.data.bindretrylimit,
                concurroplimit: this.props.data.concurroplimit,
                connlifetime: this.props.data.connlifetime,
                searchcheckinterval: this.props.data.searchcheckinterval,
                hoplimit: this.props.data.hoplimit,
                nsbindmechanism: this.props.data.nsbindmechanism,
                nsusestarttls: this.props.data.nsusestarttls,
                nsreferralonscopedsearch: this.props.data.nsreferralonscopedsearch,
                nsproxiedauthorization: this.props.data.nsproxiedauthorization,
                nschecklocalaci: this.props.data.nschecklocalaci,
                // Original settings
                _nsfarmserverurl: this.props.data.nsfarmserverurl,
                _nsmultiplexorbinddn: this.props.data.nsmultiplexorbinddn,
                _nsmultiplexorcredentials: this.props.data.nsmultiplexorcredentials,
                _nsmultiplexorcredentials_confirm: this.props.data.nsmultiplexorcredentials,
                _sizelimit: this.props.data.sizelimit,
                _timelimit: this.props.data.timelimit,
                _bindconnlimit: this.props.data.bindconnlimit,
                _opconnlimit: this.props.data.opconnlimit,
                _concurrbindlimit: this.props.data.concurrbindlimit,
                _bindtimeout: this.props.data.bindtimeout,
                _bindretrylimit: this.props.data.bindretrylimit,
                _concurroplimit: this.props.data.concurroplimit,
                _connlifetime: this.props.data.connlifetime,
                _searchcheckinterval: this.props.data.searchcheckinterval,
                _hoplimit: this.props.data.hoplimit,
                _nsbindmechanism: this.props.data.nsbindmechanism,
                _nsusestarttls: this.props.data.nsusestarttls,
                _nsreferralonscopedsearch: this.props.data.nsreferralonscopedsearch,
                _nsproxiedauthorization: this.props.data.nsproxiedauthorization,
                _nschecklocalaci: this.props.data.nschecklocalaci,
            };
        } else {
            this.state = {
                errObj: {},
                showDeleteConfirm: false,
                linkPwdMatch: true,
            };
        }

        this.handleSelectToggle = (_event, isOpen) => {
            this.setState({
                isOpen
            });
        };

        this.handleSelect = (event, selection, isPlaceholder) => {
            let saveBtnDisabled = true;
            const check_attrs = [
                "nsfarmserverurl", "nsmultiplexorbinddn", "nsmultiplexorcredentials",
                "nsmultiplexorcredentials_confirm", "sizelimit", "timelimit",
                "bindconnlimit", "opconnlimit", "concurrbindlimit",
                "bindtimeout", "bindretrylimit", "concurroplimit",
                "connlifetime", "searchcheckinterval", "hoplimit",
                "nsbindmechanism", "nsusestarttls", "nsusestarttls",
                "nsreferralonscopedsearch", "nsproxiedauthorization", "nschecklocalaci"
            ];

            for (const check_attr of check_attrs) {
                if (check_attr !== "nsbindmechanism" && this.state[check_attr] !== this.state['_' + check_attr]) {
                    saveBtnDisabled = false;
                }
            }
            if (selection !== this.state._nsbindmechanism) {
                saveBtnDisabled = false;
            }
            this.setState({
                nsbindmechanism: selection,
                saveBtnDisabled,
                isOpen: false
            });
        };

        this.onChange = this.onChange.bind(this);
        this.handleSaveLink = this.handleSaveLink.bind(this);
        this.deleteLink = this.deleteLink.bind(this);
        this.handleShowDeleteConfirm = this.handleShowDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    handleShowDeleteConfirm () {
        this.setState({
            showDeleteConfirm: true,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    closeDeleteConfirm () {
        this.setState({
            showDeleteConfirm: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    checkPasswords() {
        let pwdMatch = false;
        if (this.state.nsmultiplexorcredentials === this.state.nsmultiplexorcredentials_confirm) {
            pwdMatch = true;
        }

        this.setState({
            linkPwdMatch: pwdMatch,
        });
    }

    onChange (e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        const attr = e.target.id;
        const errObj = this.state.errObj;
        let saveBtnDisabled = true;

        const check_attrs = [
            "nsfarmserverurl", "nsmultiplexorbinddn", "nsmultiplexorcredentials",
            "nsmultiplexorcredentials_confirm", "sizelimit", "timelimit",
            "bindconnlimit", "opconnlimit", "concurrbindlimit",
            "bindtimeout", "bindretrylimit", "concurroplimit",
            "connlifetime", "searchcheckinterval", "hoplimit",
            "nsbindmechanism", "nsusestarttls", "nsusestarttls",
            "nsreferralonscopedsearch", "nsproxiedauthorization", "nschecklocalaci"
        ];
        for (const check_attr of check_attrs) {
            if (attr !== check_attr) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    saveBtnDisabled = false;
                }
            } else if (value !== this.state['_' + check_attr]) {
                saveBtnDisabled = false;
            }
        }

        if (value === "") {
            valueErr = true;
        }
        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            errObj,
            saveBtnDisabled
        }, this.checkPasswords);
    }

    handleSaveLink() {
        const missingArgs = {};
        let bind_pw = "";
        let errors = false;

        if (this.state.nsfarmserverurl === "") {
            this.props.addNotification(
                "warning",
                _("Missing Remote Server LDAP URL")
            );
            missingArgs.nsfarmserverurl = true;
            errors = true;
        }
        if (this.state.nsmultiplexorbinddn === "") {
            this.props.addNotification(
                "warning",
                _("Missing Remote Bind DN")
            );
            missingArgs.nsmultiplexorbinddn = true;
            errors = true;
        }
        if (this.state.nsmultiplexorcredentials === "") {
            this.props.addNotification(
                "warning",
                _("Missing Remote Bind DN Password")
            );
            missingArgs.nsmultiplexorcredentials = true;
            errors = true;
        }
        if (this.state.nsmultiplexorcredentials_confirm === "") {
            this.props.addNotification(
                "warning",
                _("Missing Remote Bind DN Password Confirmation")
            );
            missingArgs.nsmultiplexorcredentials_confirm = true;
            errors = true;
        }
        if (this.state.nsmultiplexorcredentials !== this.state.nsmultiplexorcredentials_confirm) {
            this.props.addNotification(
                "warning",
                _("Remote Bind DN Password Do Not Match")
            );
            missingArgs.nsmultiplexorcredentials = true;
            missingArgs.nsmultiplexorcredentials_confirm = true;
            errors = true;
        }
        if (errors) {
            this.setState({
                errObj: missingArgs
            });
            return;
        }

        // Build up the command of all the changes we need to make
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "link-set", this.props.suffix
        ];

        if (this.state.nsfarmserverurl !== this.state._nsfarmserverurl) {
            cmd.push('--server-url=' + this.state.nsfarmserverurl);
        }
        if (this.state.nsmultiplexorbinddn !== this.state._nsmultiplexorbinddn) {
            cmd.push('--bind-dn=' + this.state.nsmultiplexorbinddn);
        }
        if (this.state.nsmultiplexorcredentials !== this.state._nsmultiplexorcredentials) {
            bind_pw = this.state.nsmultiplexorcredentials;
        }
        if (this.state.timelimit !== this.state._timelimit) {
            cmd.push('--time-limit=' + this.state.timelimit);
        }
        if (this.state.sizelimit !== this.state._sizelimit) {
            cmd.push('--size-limit=' + this.state.sizelimit);
        }
        if (this.state.bindconnlimit !== this.state._bindconnlimit) {
            cmd.push('--conn-bind-limit=' + this.state.bindconnlimit);
        }
        if (this.state.opconnlimit !== this.state._opconnlimit) {
            cmd.push('--conn-op-limit=' + this.state.opconnlimit);
        }
        if (this.state.concurrbindlimit !== this.state._concurrbindlimit) {
            cmd.push('--bind-limit=' + this.state.concurrbindlimit);
        }
        if (this.state.bindtimeout !== this.state._bindtimeout) {
            cmd.push('--bind-timeout=' + this.state.bindtimeout);
        }
        if (this.state.bindretrylimit !== this.state._bindretrylimit) {
            cmd.push('--bind-attempts=' + this.state.bindretrylimit);
        }
        if (this.state.concurroplimit !== this.state._concurroplimit) {
            cmd.push('--op-limit=' + this.state.concurroplimit);
        }
        if (this.state.connlifetime !== this.state._connlifetime) {
            cmd.push('--conn-lifetime=' + this.state.connlifetime);
        }
        if (this.state.searchcheckinterval !== this.state._searchcheckinterval) {
            cmd.push('--abandon-check-interval=' + this.state.searchcheckinterval);
        }
        if (this.state.hoplimit !== this.state._hoplimit) {
            cmd.push('--hop-limit=' + this.state.hoplimit);
        }
        if (this.state.nsbindmechanism !== this.state._nsbindmechanism) {
            cmd.push('--bind-mech=' + this.state.nsbindmechanism);
        }

        if (this.state.nsusestarttls !== this.state._nsusestarttls) {
            if (this.state.nsusestarttls) {
                cmd.push('--use-starttls=on');
            } else {
                cmd.push('--use-starttls=off');
            }
        }
        if (this.state.nsreferralonscopedsearch !== this.state._nsreferralonscopedsearch) {
            if (this.state.nsreferralonscopedsearch) {
                cmd.push('--return-ref=on');
            } else {
                cmd.push('--return-ref=off');
            }
        }
        if (this.state.nsproxiedauthorization !== this.state._nsproxiedauthorization) {
            if (this.state.nsproxiedauthorization) {
                cmd.push('--proxied-auth=on');
            } else {
                cmd.push('--proxied-auth=off');
            }
        }
        if (this.state.nschecklocalaci !== this.state._nschecklocalaci) {
            if (this.state.nschecklocalaci) {
                cmd.push('--check-aci=on');
            } else {
                cmd.push('--check-aci=off');
            }
        }

        if (cmd.length > 6) {
            this.setState({
                saving: true
            });
            // Something changed, perform the update
            const config = {
                cmd,
                promptArg: "--bind-pw-prompt",
                passwd: bind_pw,
                addNotification: this.props.addNotification,
                success_msg: _("Successfully Updated Link Configuration"),
                error_msg: _("Failed to update link configuration"),
                state_callback: () => { this.setState({ saving: false }) },
                reload_func: this.props.reload,
                reload_arg: this.props.suffix,
                funcName: "handleSaveLink",
                funcDesc: _("Save chaining link config")
            };
            callCmdStreamPassword(config);
        }
    }

    deleteLink() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "link-delete", this.props.suffix
        ];
        log_cmd("deleteLink", "Delete database chaining link", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.loadSuffixTree(true);
                    this.props.addNotification(
                        "success",
                        _("Successfully Deleted Database Link")
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.loadSuffixTree(true);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to delete database link - $0"), errMsg.desc)
                    );
                });
    }

    render () {
        const error = this.state.errObj;
        const extraPrimaryProps = {};
        let saveBtnName = _("Save Settings");
        if (this.state.loading) {
            saveBtnName = _("Saving settings ...");
            extraPrimaryProps.spinnerAriaValueText = _("Loading");
        }
        return (
            <div className={this.state.saving ? "ds-disabled" : ""}>
                <Grid>
                    <GridItem span={10} className="ds-word-wrap">
                        <TextContent>
                            <Text className="ds-suffix-header" component={TextVariants.h3}>
                                <LinkIcon />
                                &nbsp;&nbsp;{this.props.suffix} (<i>{this.props.bename}</i>)
                                <Button 
                                    variant="plain"
                                    aria-label={_("Refresh database link")}
                                    onClick={() => this.props.reload(this.props.suffix)}
                                >
                                    <SyncAltIcon />
                                </Button>
                            </Text>
                        </TextContent>
                    </GridItem>
                    <GridItem span={2}>
                        <Button
                            className="ds-float-right"
                            variant="danger"
                            onClick={this.handleShowDeleteConfirm}
                        >
                            {_("Delete Link")}
                        </Button>
                    </GridItem>
                </Grid>

                <Grid
                    title={_("The LDAP URL for the remote server.  Add additional failure server URLs by separating them with a space. (nsfarmserverurl).")}
                    className="ds-margin-top-lg"
                >
                    <GridItem className="ds-label" span={3}>
                        {_("Remote Server LDAP URL")}
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={this.state.nsfarmserverurl}
                            type="text"
                            id="nsfarmserverurl"
                            aria-describedby="nsfarmserverurl"
                            name="nsfarmserverurl"
                            onChange={(e, str) => {
                                this.onChange(e);
                            }}
                        />
                    </GridItem>
                </Grid>
                <Grid
                    title={_("The distinguished name (DN) of the entry to authenticate to the remote server. (nsmultiplexorbinddn).")}
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={3}>
                        {_("Remote Server Bind DN")}
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={this.state.nsmultiplexorbinddn}
                            type="text"
                            id="nsmultiplexorbinddn"
                            aria-describedby="nsmultiplexorbinddn"
                            name="nsmultiplexorbinddn"
                            onChange={(e, str) => {
                                this.onChange(e);
                            }}
                        />
                    </GridItem>
                </Grid>
                <Grid
                    title={_("The password for the authenticating entry. (nsmultiplexorcredentials).")}
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={3}>
                        {_("Bind DN Password")}
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={this.state.nsmultiplexorcredentials}
                            type="password"
                            id="nsmultiplexorcredentials"
                            aria-describedby="nsmultiplexorcredentials"
                            name="nsmultiplexorcredentials"
                            onChange={(e, str) => {
                                this.onChange(e);
                            }}
                            validated={(error.nsmultiplexorcredentials || !this.state.linkPwdMatch) ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                    </GridItem>
                </Grid>
                <Grid
                    title={_("Confirm the password for the authenticating entry. (nsmultiplexorcredentials).")}
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={3}>
                        {_("Confirm Password")}
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={this.state.nsmultiplexorcredentials_confirm}
                            type="password"
                            id="nsmultiplexorcredentials_confirm"
                            aria-describedby="nsmultiplexorcredentials_confirm"
                            name="nsmultiplexorcredentials_confirm"
                            onChange={(e, str) => {
                                this.onChange(e);
                            }}
                            validated={(error.nsmultiplexorcredentials_confirm || !this.state.linkPwdMatch) ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                    </GridItem>
                </Grid>
                <Grid
                    title={_("The authentication mechanism.  Simple (user name and password), SASL/DIGEST-MD5, or SASL>GSSAPI. (nsbindmechanism).")}
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={3}>
                        {_("Bind Method")}
                    </GridItem>
                    <GridItem span={9}>
                        <Select
                            variant={SelectVariant.single}
                            aria-label="Select Input"
                            onToggle={(event, isOpen) => this.handleSelectToggle(event, isOpen)}
                            onSelect={this.handleSelect}
                            selections={this.state.nsbindmechanism}
                            isOpen={this.state.isOpen}
                            aria-labelledby="UID"
                        >
                            <SelectOption key="Simple" value="Simple" />
                            <SelectOption key="SASL/DIGEST-MD5" value="SASL/DIGEST-MD5" />
                            <SelectOption key="SASL/GSSAPI" value="SASL/GSSAPI" />
                        </Select>
                    </GridItem>
                </Grid>
                <Grid
                    title={_("Use StartTLS for connections to the remote server. (nsusestarttls).")}
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={12}>
                        <Checkbox
                            label={_("Use StartTLS")}
                            id="nsusestarttls"
                            isChecked={this.state.nsusestarttls}
                            onChange={(e, str) => {
                                this.onChange(e);
                            }}
                            aria-label="check startTLS"
                        />
                    </GridItem>
                </Grid>

                <ExpandableSection
                    className="ds-margin-top-xlg"
                    toggleText={this.state.isExpanded ? _("Hide Advanced Settings") : _("Show Advanced Settings")}
                    onToggle={(event, isOpen) => this.handleToggle(event, isOpen)}
                    isExpanded={this.state.isExpanded}
                >
                    <div className="ds-margin-top ds-margin-left">
                        <Grid
                            title={_("The size limit of entries returned over a database link (nsslapd-sizelimit).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Size Limit")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.sizelimit}
                                    type="number"
                                    id="sizelimit"
                                    aria-describedby="sizelimit"
                                    name="sizelimit"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The time limit of an operation over a database link (nsslapd-timelimit).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Time Limit")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.sizelimit}
                                    type="number"
                                    id="timelimit"
                                    aria-describedby="timelimit"
                                    name="timelimit"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The maximum number of TCP connections the database link establishes with the remote server.  (nsbindconnectionslimit).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Max TCP Connections")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.bindconnlimit}
                                    type="number"
                                    id="bindconnlimit"
                                    aria-describedby="bindconnlimit"
                                    name="bindconnlimit"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Max LDAP Connections")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.opconnlimit}
                                    type="number"
                                    id="opconnlimit"
                                    aria-describedby="opconnlimit"
                                    name="opconnlimit"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The maximum number of concurrent bind operations per TCP connection. (nsconcurrentbindlimit).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Max Binds Per Connection")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.concurrbindlimit}
                                    type="number"
                                    id="concurrbindlimit"
                                    aria-describedby="concurrbindlimit"
                                    name="concurrbindlimit"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The amount of time before the bind attempt times out. (nsbindtimeout).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Bind Timeout")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.bindtimeout}
                                    type="number"
                                    id="bindtimeout"
                                    aria-describedby="bindtimeout"
                                    name="bindtimeout"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The number of times the database link tries to bind with the remote server after a connection failure. (nsbindretrylimit).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Bind Retry Limit")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.bindtimeout}
                                    type="number"
                                    id="bindretrylimit"
                                    aria-describedby="bindretrylimit"
                                    name="bindretrylimit"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The maximum number of operations per connections. (nsconcurrentoperationslimit).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Max Operations Per Connection")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.concurroplimit}
                                    type="number"
                                    id="concurroplimit"
                                    aria-describedby="concurroplimit"
                                    name="concurroplimit"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The life of a database link connection to the remote server in seconds.  0 is unlimited  (nsconnectionlife).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Connection Lifetime")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.connlifetime}
                                    type="number"
                                    id="connlifetime"
                                    aria-describedby="connlifetime"
                                    name="connlifetime"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The number of seconds that pass before the server checks for abandoned operations.  (nsabandonedsearchcheckinterval).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Abandoned Op Check Interval")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.searchcheckinterval}
                                    type="number"
                                    id="searchcheckinterval"
                                    aria-describedby="searchcheckinterval"
                                    name="searchcheckinterval"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("The maximum number of times a request can be forwarded from one database link to another.  (nshoplimit).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                {_("Hop Limit")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.hoplimit}
                                    type="number"
                                    id="hoplimit"
                                    aria-describedby="hoplimit"
                                    name="hoplimit"
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("Allow proxied authentication to the remote server. (nsproxiedauthorization).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={12}>
                                <Checkbox
                                    label={_("Allow Proxied Authentication")}
                                    id="nsproxiedauthorization"
                                    isChecked={this.state.nsproxiedauthorization}
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                    aria-label="send ref"
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={12}>
                                <Checkbox
                                    label={_("Check Local ACIs")}
                                    id="nschecklocalaci"
                                    isChecked={this.state.nschecklocalaci}
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                    aria-label="send ref"
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title={_("Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch).")}
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={12}>
                                <Checkbox
                                    label={_("Send Referral On Scoped Search")}
                                    id="nsreferralonscopedsearch"
                                    isChecked={this.state.nsreferralonscopedsearch}
                                    onChange={(e, str) => {
                                        this.onChange(e);
                                    }}
                                    aria-label="send ref"
                                />
                            </GridItem>
                        </Grid>
                    </div>
                    <hr />
                </ExpandableSection>
                <Button
                    className="ds-margin-top-lg"
                    onClick={this.handleSaveLink}
                    variant="primary"
                    isLoading={this.state.saving}
                    spinnerAriaValueText={this.state.saving ? _("Saving") : undefined}
                    {...extraPrimaryProps}
                    isDisabled={this.state.saveBtnDisabled || this.state.saving}
                >
                    {saveBtnName}
                </Button>
                <DoubleConfirmModal
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    handleChange={this.onChange}
                    actionHandler={this.deleteLink}
                    spinning={this.state.modalSpinning}
                    item={this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle={_("Delete Database Link")}
                    mMsg={_("Are you really sure you want to delete this database link?")}
                    mSpinningMsg={_("Deleting Database Link...")}
                    mBtnName={_("Delete Database Link")}
                />
            </div>
        );
    }
}

//
// Chaining modals
//

export class ChainControlsModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            oidList,
            spinning,
        } = this.props;

        const oids = oidList.map((oid) =>
            <SimpleListItem key={oid}>{oid}</SimpleListItem>
        );
        let btnName = _("Add New Controls");
        const extraPrimaryProps = {};
        if (spinning) {
            btnName = _("Saving Controls ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Chaining LDAP Controls")}
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        isDisabled={spinning}
                        spinnerAriaValueText={spinning ? _("Loading") : undefined}
                        {...extraPrimaryProps}
                    >
                        {btnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal>
                    <TextContent title={_("A list of LDAP control OIDs to be forwarded through chaining")}>
                        <Text component={TextVariants.h3}>
                            {_("Available LDAP Controls")}
                        </Text>
                    </TextContent>
                    <div className="ds-box ds-margin-top">
                        <SimpleList onSelect={handleChange} aria-label="comps">
                            {oids}
                        </SimpleList>
                    </div>
                </Form>
            </Modal>
        );
    }
}

export class ChainCompsModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            compList,
            spinning
        } = this.props;
        const comps = compList.map((comp) =>
            <SimpleListItem key={comp}>{comp}</SimpleListItem>
        );
        let btnName = _("Add New Components");
        const extraPrimaryProps = {};
        if (spinning) {
            btnName = _("Saving Components ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }
        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Chaining Components")}
                isOpen={showModal}
                onClose={closeHandler}
                aria-labelledby="ds-modal"
                actions={[
                    <Button
                        key="comps"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        isDisabled={spinning}
                        spinnerAriaValueText={spinning ? _("Loading") : undefined}
                        {...extraPrimaryProps}
                    >
                        {btnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal>
                    <TextContent title={_("A list of LDAP control components")}>
                        <Text component={TextVariants.h3}>
                            {_("Available Components")}
                        </Text>
                    </TextContent>
                    {_("Available LDAP Controls")}
                    <div className="ds-box ds-margin-top">
                        <SimpleList onSelect={handleChange} aria-label="comps">
                            {comps}
                        </SimpleList>
                    </div>
                </Form>
            </Modal>
        );
    }
}

// Property types and defaults

ChainCompsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    compList: PropTypes.array,
};

ChainCompsModal.defaultProps = {
    showModal: false,
    compList: [],
};

ChainControlsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    oidList: PropTypes.array,
};

ChainControlsModal.defaultProps = {
    showModal: false,
    oidList: [],
};

ChainingDatabaseConfig.propTypes = {
    serverId: PropTypes.string,
    addNotification: PropTypes.func,
    reload: PropTypes.func,
    data: PropTypes.object,
    enableTree: PropTypes.func,
};

ChainingDatabaseConfig.defaultProps = {
    serverId: "",
    data: {},
};

ChainingConfig.propTypes = {
    serverId: PropTypes.string,
    suffix: PropTypes.string,
    bename: PropTypes.string,
    loadSuffixTree: PropTypes.func,
    addNotification: PropTypes.func,
    data: PropTypes.object,
    reload: PropTypes.func,
    enableTree: PropTypes.func,
};

ChainingConfig.defaultProps = {
    serverId: "",
    suffix: "",
    bename: "",
    data: {},
};
