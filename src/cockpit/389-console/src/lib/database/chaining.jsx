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
    Select,
    SelectOption,
    SelectVariant,
    SimpleList,
    SimpleListItem,
    Tab,
    Tabs,
    TabTitleText,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faLink,
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';

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

        this.onToggle = (isExpanded) => {
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
        this.handleModalChange = this.handleModalChange.bind(this);
        this.save_chaining_config = this.save_chaining_config.bind(this);
        // Chaining Control OIDs
        this.showOidModal = this.showOidModal.bind(this);
        this.closeOidModal = this.closeOidModal.bind(this);
        this.handleOidChange = this.handleOidChange.bind(this);
        this.saveOids = this.saveOids.bind(this);
        this.deleteOids = this.deleteOids.bind(this);
        this.handleSelectOids = this.handleSelectOids.bind(this);
        // Chaining comps
        this.showCompsModal = this.showCompsModal.bind(this);
        this.closeCompsModal = this.closeCompsModal.bind(this);
        this.handleCompsChange = this.handleCompsChange.bind(this);
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

    handleModalChange(e) {
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
            if (attr != check_attr) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    saveBtnDisabled = false;
                }
            } else if (value != this.state['_' + check_attr]) {
                saveBtnDisabled = false;
            }
        }

        this.setState({
            [attr]: value,
            saveBtnDisabled: saveBtnDisabled
        });
    }

    save_chaining_config () {
        // Build up the command list
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'chaining', 'config-set-def'
        ];
        let val = "";
        if (this.state._defUseStartTLS != this.state.defUseStartTLS) {
            val = "off";
            if (this.state.defUseStartTLS) {
                val = "on";
            }
            cmd.push("--use-starttls=" + val);
        }
        if (this.state._defCheckAci != this.state.defCheckAci) {
            val = "off";
            if (this.state.defCheckAci) {
                val = "on";
            }
            cmd.push("--check-aci=" + val);
        }
        if (this.state._defRefOnScoped != this.state.defRefOnScoped) {
            val = "off";
            if (this.state.defRefOnScoped) {
                val = "on";
            }
            cmd.push("--return-ref=" + val);
        }
        if (this.state._defProxy != this.state.defProxy) {
            val = "off";
            if (this.state.defProxy) {
                val = "on";
            }
            cmd.push("--proxied-auth=" + val);
        }
        if (this.state._defTestDelay != this.state.defTestDelay) {
            cmd.push("--test-response-delay=" + this.state.defTestDelay);
        }
        if (this.state._defOpConnLimit != this.state.defOpConnLimit) {
            cmd.push("--conn-op-limit=" + this.state.defOpConnLimit);
        }
        if (this.state._defSizeLimit != this.state.defSizeLimit) {
            cmd.push("--size-limit=" + this.state.defSizeLimit);
        }
        if (this.state._defTimeLimit != this.state.defTimeLimit) {
            cmd.push("--time-limit=" + this.state.defTimeLimit);
        }
        if (this.state._defSearchCheck != this.state.defSearchCheck) {
            cmd.push("--abandon-check-interval=" + this.state.defSearchCheck);
        }
        if (this.state._defBindTimeout != this.state.defBindTimeout) {
            cmd.push("--bind-timeout=" + this.state.defBindTimeout);
        }
        if (this.state._defBindRetryLimit != this.state.defBindRetryLimit) {
            cmd.push("--bind-attempts=" + this.state.defBindRetryLimit);
        }
        if (this.state._defConcurLimit != this.state.defConcurLimit) {
            cmd.push("--bind-limit=" + this.state.defConcurLimit);
        }
        if (this.state._defConcurOpLimit != this.state.defConcurOpLimit) {
            cmd.push("--op-limit=" + this.state.defConcurOpLimit);
        }
        if (this.state._defConnLife != this.state.defConnLife) {
            cmd.push("--conn-lifetime=" + this.state.defConnLife);
        }
        if (this.state._defHopLimit != this.state.defHopLimit) {
            cmd.push("--hop-limit=" + this.state.defHopLimit);
        }
        if (this.state._defDelay != this.state.defDelay) {
            cmd.push("--response-delay=" + this.state.defDelay);
        }
        if (this.state._defBindConnLimit != this.state.defBindConnLimit) {
            cmd.push("--conn-bind-limit=" + this.state.defBindConnLimit);
        }

        // If we have chaining mods, then apply them...
        if (cmd.length > 5) {
            this.setState({
                saving: true
            });
            log_cmd("save_chaining_config", "Applying default chaining config change", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        // Continue with the next mod
                        this.props.reload();
                        this.props.addNotification(
                            "success",
                            `Successfully updated chaining configuration`
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
                            `Error updating chaining configuration - ${errMsg.desc}`
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
    showOidModal () {
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

    handleOidChange (selectedItem, selectedItemProps) {
        const oid = selectedItemProps.children;
        if (oid != this.state.selectedOid) {
            this.setState({
                selectedOid: oid
            });
        }
    }

    saveOids () {
        // Save chaining control oids
        if (this.state.selectedOid == "") {
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
                        `Successfully updated chaining controls`
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
                        `Error updating chaining controls - ${errMsg.desc}`
                    );
                });
    }

    handleSelectOids (selectedItem, selectedItemProps) {
        const oid = selectedItemProps.children;
        if (oid != this.state.removeOid) {
            this.setState({
                removeOid: oid
            });
        }
    }

    deleteOids(props) {
        // Remove chaining oid control
        if (this.state.removeOid == "") {
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
                        `Successfully removed chaining controls`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        `Error removing chaining controls - ${errMsg.desc}`
                    );
                });
    }

    //
    // Chaining Component modal functions
    //
    showCompsModal () {
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

    handleCompsChange (selectedItem, selectedItemProps) {
        const comp = selectedItemProps.children;
        if (comp != this.state.selectedComp) {
            this.setState({
                selectedComp: comp
            });
        }
    }

    handleSelectComps (selectedItem, selectedItemProps) {
        const comp = selectedItemProps.children;
        if (comp != this.state.removeComp) {
            this.setState({
                removeComp: comp
            });
        }
    }

    saveComps () {
        // Save chaining control Components
        if (this.state.selectedComp == "") {
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
                        `Successfully updated chaining components`
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
                        `Error updating chaining components - ${errMsg.desc}`
                    );
                });
    }

    deleteComps(props) {
        if (this.state.removeComp == "") {
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
                        `Successfully removed chaining components`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload();
                    this.props.addNotification(
                        "error",
                        `Error removing chaining components - ${errMsg.desc}`
                    );
                });
    }

    //
    // Confirm deletion functions
    //
    showConfirmDelete(item) {
        if (item == "oid") {
            if (this.state.removeOid.length) {
                this.setState({
                    showConfirmOidDelete: true
                });
            }
        } else if (item == "comp") {
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
        if (oids.length == 0) {
            oids = "";
        }
        let comps = this.state.compList.map((comps) =>
            <SimpleListItem key={comps}>{comps}</SimpleListItem>
        );
        if (comps.length == 0) {
            comps = "";
        }
        let saveBtnName = "Save Settings";
        const extraPrimaryProps = {};
        if (this.props.refreshing) {
            saveBtnName = "Saving settings ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <div id="chaining-page" className={this.state.saving ? "ds-disabled" : ""}>
                <TextContent>
                    <Text className="ds-config-header" component={TextVariants.h2}>Database Chaining Settings</Text>
                </TextContent>
                <Tabs className="ds-margin-top-xlg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Default Creation Settings</TabTitleText>}>
                        <div className="ds-indent ds-margin-bottom-md">
                            <Grid
                                title="The size limit of entries returned over a database link (nsslapd-sizelimit)."
                                className="ds-margin-top-xlg"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Size Limit
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defSizeLimit}
                                        type="number"
                                        id="defSizeLimit"
                                        aria-describedby="defSizeLimit"
                                        name="defSizeLimit"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The maximum number of operations per connections. (nsconcurrentoperationslimit)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Max Operations Per Conn
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defConcurOpLimit}
                                        type="number"
                                        id="defConcurOpLimit"
                                        aria-describedby="defConcurOpLimit"
                                        name="defConcurOpLimit"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The time limit of an operation over a database link (nsslapd-timelimit)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Time Limit
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defTimeLimit}
                                        type="number"
                                        id="defTimeLimit"
                                        aria-describedby="defTimeLimit"
                                        name="defTimeLimit"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The maximum number of operations per connections. (nsconcurrentoperationslimit)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Connection Lifetime
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defConnLife}
                                        type="number"
                                        id="defConnLife"
                                        aria-describedby="defConnLife"
                                        name="defConnLife"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The maximum number of TCP connections the database link establishes with the remote server.  (nsbindconnectionslimit)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Max TCP Connections
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defBindConnLimit}
                                        type="number"
                                        id="defBindConnLimit"
                                        aria-describedby="defBindConnLimit"
                                        name="defBindConnLimit"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Max LDAP Connections
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defOpConnLimit}
                                        type="number"
                                        id="defOpConnLimit"
                                        aria-describedby="defOpConnLimit"
                                        name="defOpConnLimit"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The number of seconds that pass before the server checks for abandoned operations.  (nsabandonedsearchcheckinterval)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Abandoned Op Check Interval
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defSearchCheck}
                                        type="number"
                                        id="defSearchCheck"
                                        aria-describedby="defSearchCheck"
                                        name="defSearchCheck"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Max Binds Per Connection
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defConcurLimit}
                                        type="number"
                                        id="defConcurLimit"
                                        aria-describedby="defConcurLimit"
                                        name="defConcurLimit"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The maximum number of times a request can be forwarded from one database link to another.  (nshoplimit)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Database Link Hop Limit
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defHopLimit}
                                        type="number"
                                        id="defHopLimit"
                                        aria-describedby="defHopLimit"
                                        name="defHopLimit"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The amount of time before the bind attempt times out. (nsbindtimeout)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Bind Timeout
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defBindTimeout}
                                        type="number"
                                        id="defBindTimeout"
                                        aria-describedby="defBindTimeout"
                                        name="defBindTimeout"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="The number of times the database link tries to bind with the remote server after a connection failure. (nsbindretrylimit)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Bind Retry Limit
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.state.defBindRetryLimit}
                                        type="number"
                                        id="defBindRetryLimit"
                                        aria-describedby="defBindRetryLimit"
                                        name="defBindRetryLimit"
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={12}>
                                    <Checkbox
                                        label="Check Local ACIs"
                                        id="defCheckAci"
                                        isChecked={this.state.defCheckAci}
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                        aria-label="check aci"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={12}>
                                    <Checkbox
                                        label="Send Referral On Scoped Search"
                                        id="defRefOnScoped"
                                        isChecked={this.state.defRefOnScoped}
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                        aria-label="send ref"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Sets whether proxied authentication is allowed. (nsproxiedauthorization)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={12}>
                                    <Checkbox
                                        label="Allow Proxied Authentication"
                                        id="defProxy"
                                        isChecked={this.state.defProxy}
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                        aria-label="prox auth"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Use StartTLS for connections to remote server. (nsusestarttls)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={12}>
                                    <Checkbox
                                        label="Use StartTLS"
                                        id="defUseStartTLS"
                                        isChecked={this.state.defUseStartTLS}
                                        onChange={(str, e) => {
                                            this.handleChange(e);
                                        }}
                                        aria-label="startTLS"
                                    />
                                </GridItem>
                            </Grid>
                            <Button
                                className="ds-margin-top-xlg"
                                variant="primary"
                                onClick={this.save_chaining_config}
                                isDisabled={this.state.saveBtnDisabled || this.state.saving}
                                isLoading={this.state.saving}
                                spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                                {...extraPrimaryProps}
                            >
                                {saveBtnName}
                            </Button>
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>Controls & Components</TabTitleText>}>
                        <div className="ds-indent">
                            <Grid className="ds-margin-top-xlg">
                                <GridItem
                                    span={4}
                                    title="A list of LDAP control OIDs to be forwarded through chaining."
                                >
                                    <TextContent>
                                        <Text component={TextVariants.h4}>Forwarded LDAP Controls</Text>
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
                                                onClick={this.showOidModal}
                                                className="ds-button-left"
                                            >
                                                Add
                                            </Button>
                                        </div>
                                        <div className="ds-panel-right">
                                            <Button
                                                variant="primary"
                                                onClick={e => this.showConfirmDelete("oid")}
                                                className="ds-button-right"
                                                isDisabled={this.state.removeOid == ""}
                                            >
                                                Delete
                                            </Button>
                                        </div>
                                    </div>
                                </GridItem>
                                <GridItem span={1} />
                                <GridItem span={4} title="A list of components to go through chaining">
                                    <TextContent>
                                        <Text component={TextVariants.h4}>Components to Chain</Text>
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
                                                onClick={this.showCompsModal}
                                                className="ds-button-left"
                                            >
                                                Add
                                            </Button>
                                        </div>
                                        <div className="ds-panel-right">
                                            <Button
                                                variant="primary"
                                                onClick={e => this.showConfirmDelete("comp")}
                                                className="ds-button-right"
                                                isDisabled={this.state.removeComp == ""}
                                            >
                                                Delete
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
                    handleChange={this.handleOidChange}
                    saveHandler={this.saveOids}
                    oidList={this.state.availableOids}
                    spinning={this.state.modalSpinning}
                />
                <ChainCompsModal
                    showModal={this.state.showCompsModal}
                    closeHandler={this.closeCompsModal}
                    handleChange={this.handleCompsChange}
                    saveHandler={this.saveComps}
                    compList={this.state.availableComps}
                    spinning={this.state.modalSpinning}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmOidDelete}
                    closeHandler={this.closeConfirmOidDelete}
                    handleChange={this.handleModalChange}
                    actionHandler={this.deleteOids}
                    spinning={this.state.modalSpinning}
                    item={this.state.removeOid}
                    checked={this.state.modalChecked}
                    mTitle="Remove Chaining OID"
                    mMsg="Are you sure you want to delete this OID?"
                    mSpinningMsg="Deleting ..."
                    mBtnName="Delete"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmCompDelete}
                    closeHandler={this.closeConfirmCompDelete}
                    handleChange={this.handleModalChange}
                    actionHandler={this.deleteComps}
                    spinning={this.state.modalSpinning}
                    item={this.state.removeComp}
                    checked={this.state.modalChecked}
                    mTitle="Remove Chaining Component"
                    mMsg="Are you sure you want to delete this component?"
                    mSpinningMsg="Deleting ..."
                    mBtnName="Delete"
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

        this.onSelectToggle = isOpen => {
            this.setState({
                isOpen
            });
        };

        this.onSelect = (event, selection, isPlaceholder) => {
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
                if (check_attr != "nsbindmechanism" && this.state[check_attr] !== this.state['_' + check_attr]) {
                    saveBtnDisabled = false;
                }
            }
            if (selection != this.state._nsbindmechanism) {
                saveBtnDisabled = false;
            }
            this.setState({
                nsbindmechanism: selection,
                saveBtnDisabled: saveBtnDisabled,
                isOpen: false
            });
        };

        this.handleChange = this.handleChange.bind(this);
        this.saveLink = this.saveLink.bind(this);
        this.deleteLink = this.deleteLink.bind(this);
        this.showDeleteConfirm = this.showDeleteConfirm.bind(this);
        this.closeDeleteConfirm = this.closeDeleteConfirm.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    showDeleteConfirm () {
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
        if (this.state.nsmultiplexorcredentials == this.state.nsmultiplexorcredentials_confirm) {
            pwdMatch = true;
        }

        this.setState({
            linkPwdMatch: pwdMatch,
        });
    }

    handleChange (e) {
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
            if (attr != check_attr) {
                if (this.state[check_attr] != this.state['_' + check_attr]) {
                    saveBtnDisabled = false;
                }
            } else if (value != this.state['_' + check_attr]) {
                saveBtnDisabled = false;
            }
        }

        if (value == "") {
            valueErr = true;
        }
        errObj[attr] = valueErr;
        this.setState({
            [attr]: value,
            errObj: errObj,
            saveBtnDisabled: saveBtnDisabled
        }, this.checkPasswords);
    }

    saveLink() {
        const missingArgs = {};
        let bind_pw = "";
        let errors = false;

        if (this.state.nsfarmserverurl == "") {
            this.props.addNotification(
                "warning",
                `Missing Remote Server LDAP URL`
            );
            missingArgs.nsfarmserverurl = true;
            errors = true;
        }
        if (this.state.nsmultiplexorbinddn == "") {
            this.props.addNotification(
                "warning",
                `Missing Remote Bind DN`
            );
            missingArgs.nsmultiplexorbinddn = true;
            errors = true;
        }
        if (this.state.nsmultiplexorcredentials == "") {
            this.props.addNotification(
                "warning",
                `Missing Remote Bind DN Password`
            );
            missingArgs.nsmultiplexorcredentials = true;
            errors = true;
        }
        if (this.state.nsmultiplexorcredentials_confirm == "") {
            this.props.addNotification(
                "warning",
                `Missing Remote Bind DN Password Confirmation`
            );
            missingArgs.nsmultiplexorcredentials_confirm = true;
            errors = true;
        }
        if (this.state.nsmultiplexorcredentials != this.state.nsmultiplexorcredentials_confirm) {
            this.props.addNotification(
                "warning",
                `Remote Bind DN Password Do Not Match`
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
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "chaining", "link-set", this.props.suffix
        ];

        if (this.state.nsfarmserverurl != this.state._nsfarmserverurl) {
            cmd.push('--server-url=' + this.state.nsfarmserverurl);
        }
        if (this.state.nsmultiplexorbinddn != this.state._nsmultiplexorbinddn) {
            cmd.push('--bind-dn=' + this.state.nsmultiplexorbinddn);
        }
        if (this.state.nsmultiplexorcredentials != this.state._nsmultiplexorcredentials) {
            bind_pw = this.state.nsmultiplexorcredentials
        }
        if (this.state.timelimit != this.state._timelimit) {
            cmd.push('--time-limit=' + this.state.timelimit);
        }
        if (this.state.sizelimit != this.state._sizelimit) {
            cmd.push('--size-limit=' + this.state.sizelimit);
        }
        if (this.state.bindconnlimit != this.state._bindconnlimit) {
            cmd.push('--conn-bind-limit=' + this.state.bindconnlimit);
        }
        if (this.state.opconnlimit != this.state._opconnlimit) {
            cmd.push('--conn-op-limit=' + this.state.opconnlimit);
        }
        if (this.state.concurrbindlimit != this.state._concurrbindlimit) {
            cmd.push('--bind-limit=' + this.state.concurrbindlimit);
        }
        if (this.state.bindtimeout != this.state._bindtimeout) {
            cmd.push('--bind-timeout=' + this.state.bindtimeout);
        }
        if (this.state.bindretrylimit != this.state._bindretrylimit) {
            cmd.push('--bind-attempts=' + this.state.bindretrylimit);
        }
        if (this.state.concurroplimit != this.state._concurroplimit) {
            cmd.push('--op-limit=' + this.state.concurroplimit);
        }
        if (this.state.connlifetime != this.state._connlifetime) {
            cmd.push('--conn-lifetime=' + this.state.connlifetime);
        }
        if (this.state.searchcheckinterval != this.state._searchcheckinterval) {
            cmd.push('--abandon-check-interval=' + this.state.searchcheckinterval);
        }
        if (this.state.hoplimit != this.state._hoplimit) {
            cmd.push('--hop-limit=' + this.state.hoplimit);
        }
        if (this.state.nsbindmechanism != this.state._nsbindmechanism) {
            cmd.push('--bind-mech=' + this.state.nsbindmechanism);
        }

        if (this.state.nsusestarttls != this.state._nsusestarttls) {
            if (this.state.nsusestarttls) {
                cmd.push('--use-starttls=on');
            } else {
                cmd.push('--use-starttls=off');
            }
        }
        if (this.state.nsreferralonscopedsearch != this.state._nsreferralonscopedsearch) {
            if (this.state.nsreferralonscopedsearch) {
                cmd.push('--return-ref=on');
            } else {
                cmd.push('--return-ref=off');
            }
        }
        if (this.state.nsproxiedauthorization != this.state._nsproxiedauthorization) {
            if (this.state.nsproxiedauthorization) {
                cmd.push('--proxied-auth=on');
            } else {
                cmd.push('--proxied-auth=off');
            }
        }
        if (this.state.nschecklocalaci != this.state._nschecklocalaci) {
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
                cmd: cmd,
                promptArg: "--bind-pw-prompt",
                passwd: bind_pw,
                addNotification: this.props.addNotification,
                success_msg: "Successfully Updated Link Configuration",
                error_msg: "Failed to update link configuration",
                state_callback: () => { this.setState({ saving: false }) },
                reload_func: this.props.reload,
                reload_arg: this.props.suffix,
                funcName: "saveLink",
                funcDesc: "Save chaining link config"
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
                        `Successfully Deleted Database Link`
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.loadSuffixTree(true);
                    this.props.addNotification(
                        "error",
                        `Failed to delete database link - ${errMsg.desc}`
                    );
                });
    }

    render () {
        const error = this.state.errObj;
        const extraPrimaryProps = {};
        let saveBtnName = "Save Settings";
        if (this.state.loading) {
            saveBtnName = "Saving settings ...";
            extraPrimaryProps.spinnerAriaValueText = "Loading";
        }
        return (
            <div className={this.state.saving ? "ds-disabled" : ""}>
                <Grid>
                    <GridItem span={10} className="ds-word-wrap">
                        <TextContent>
                            <Text className="ds-suffix-header" component={TextVariants.h3}>
                                <FontAwesomeIcon size="sm" icon={faLink} /> {this.props.suffix} (<i><font size="3">{this.props.bename}</font></i>)
                                <FontAwesomeIcon
                                    size="lg"
                                    className="ds-left-margin ds-refresh"
                                    icon={faSyncAlt}
                                    title="Refresh database link"
                                    onClick={() => this.props.reload(this.props.suffix)}
                                />
                            </Text>
                        </TextContent>
                    </GridItem>
                    <GridItem span={2}>
                        <Button
                            className="ds-float-right"
                            variant="danger"
                            onClick={this.showDeleteConfirm}
                        >
                            Delete Link
                        </Button>
                    </GridItem>
                </Grid>

                <Grid
                    title="The LDAP URL for the remote server.  Add additional failure server URLs by separating them with a space. (nsfarmserverurl)."
                    className="ds-margin-top-lg"
                >
                    <GridItem className="ds-label" span={3}>
                        Remote Server LDAP URL
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={this.state.nsfarmserverurl}
                            type="text"
                            id="nsfarmserverurl"
                            aria-describedby="nsfarmserverurl"
                            name="nsfarmserverurl"
                            onChange={(str, e) => {
                                this.handleChange(e);
                            }}
                        />
                    </GridItem>
                </Grid>
                <Grid
                    title="The distinguished name (DN) of the entry to authenticate to the remote server. (nsmultiplexorbinddn)."
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={3}>
                        Remote Server Bind DN
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={this.state.nsmultiplexorbinddn}
                            type="text"
                            id="nsmultiplexorbinddn"
                            aria-describedby="nsmultiplexorbinddn"
                            name="nsmultiplexorbinddn"
                            onChange={(str, e) => {
                                this.handleChange(e);
                            }}
                        />
                    </GridItem>
                </Grid>
                <Grid
                    title="The password for the authenticating entry. (nsmultiplexorcredentials)."
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={3}>
                        Bind DN Password
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={this.state.nsmultiplexorcredentials}
                            type="password"
                            id="nsmultiplexorcredentials"
                            aria-describedby="nsmultiplexorcredentials"
                            name="nsmultiplexorcredentials"
                            onChange={(str, e) => {
                                this.handleChange(e);
                            }}
                            validated={(error.nsmultiplexorcredentials || !this.state.linkPwdMatch) ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                    </GridItem>
                </Grid>
                <Grid
                    title="Confirm the password for the authenticating entry. (nsmultiplexorcredentials)."
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={3}>
                        Confirm Password
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={this.state.nsmultiplexorcredentials_confirm}
                            type="password"
                            id="nsmultiplexorcredentials_confirm"
                            aria-describedby="nsmultiplexorcredentials_confirm"
                            name="nsmultiplexorcredentials_confirm"
                            onChange={(str, e) => {
                                this.handleChange(e);
                            }}
                            validated={(error.nsmultiplexorcredentials_confirm || !this.state.linkPwdMatch) ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                    </GridItem>
                </Grid>
                <Grid
                    title="The authentication mechanism.  Simple (user name and password), SASL/DIGEST-MD5, or SASL>GSSAPI. (nsbindmechanism)."
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={3}>
                        Bind Method
                    </GridItem>
                    <GridItem span={9}>
                        <Select
                            variant={SelectVariant.single}
                            aria-label="Select Input"
                            onToggle={this.onSelectToggle}
                            onSelect={this.onSelect}
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
                    title="Use StartTLS for connections to the remote server. (nsusestarttls)."
                    className="ds-margin-top"
                >
                    <GridItem className="ds-label" span={12}>
                        <Checkbox
                            label="Use StartTLS"
                            id="nsusestarttls"
                            isChecked={this.state.nsusestarttls}
                            onChange={(str, e) => {
                                this.handleChange(e);
                            }}
                            aria-label="check startTLS"
                        />
                    </GridItem>
                </Grid>

                <ExpandableSection
                    className="ds-margin-top-xlg"
                    toggleText={this.state.isExpanded ? 'Hide Advanced Settings' : 'Show Advanced Settings'}
                    onToggle={this.onToggle}
                    isExpanded={this.state.isExpanded}
                >
                    <div className="ds-margin-top ds-margin-left">
                        <Grid
                            title="The size limit of entries returned over a database link (nsslapd-sizelimit)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Size Limit
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.sizelimit}
                                    type="number"
                                    id="sizelimit"
                                    aria-describedby="sizelimit"
                                    name="sizelimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The time limit of an operation over a database link (nsslapd-timelimit)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Time Limit
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.sizelimit}
                                    type="number"
                                    id="timelimit"
                                    aria-describedby="timelimit"
                                    name="timelimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The maximum number of TCP connections the database link establishes with the remote server.  (nsbindconnectionslimit)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Max TCP Connections
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.bindconnlimit}
                                    type="number"
                                    id="bindconnlimit"
                                    aria-describedby="bindconnlimit"
                                    name="bindconnlimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The maximum number of connections allowed over the database link.  (nsoperationconnectionslimit)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Max LDAP Connections
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.opconnlimit}
                                    type="number"
                                    id="opconnlimit"
                                    aria-describedby="opconnlimit"
                                    name="opconnlimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The maximum number of concurrent bind operations per TCP connection. (nsconcurrentbindlimit)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Max Binds Per Connection
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.concurrbindlimit}
                                    type="number"
                                    id="concurrbindlimit"
                                    aria-describedby="concurrbindlimit"
                                    name="concurrbindlimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The amount of time before the bind attempt times out. (nsbindtimeout)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Bind Timeout
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.bindtimeout}
                                    type="number"
                                    id="bindtimeout"
                                    aria-describedby="bindtimeout"
                                    name="bindtimeout"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The number of times the database link tries to bind with the remote server after a connection failure. (nsbindretrylimit)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Bind Retry Limit
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.bindtimeout}
                                    type="number"
                                    id="bindretrylimit"
                                    aria-describedby="bindretrylimit"
                                    name="bindretrylimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The maximum number of operations per connections. (nsconcurrentoperationslimit)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Max Operations Per Connection
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.concurroplimit}
                                    type="number"
                                    id="concurroplimit"
                                    aria-describedby="concurroplimit"
                                    name="concurroplimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The life of a database link connection to the remote server in seconds.  0 is unlimited  (nsconnectionlife)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Connection Lifetime
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.connlifetime}
                                    type="number"
                                    id="connlifetime"
                                    aria-describedby="connlifetime"
                                    name="connlifetime"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The number of seconds that pass before the server checks for abandoned operations.  (nsabandonedsearchcheckinterval)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Abandoned Op Check Interval
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.searchcheckinterval}
                                    type="number"
                                    id="searchcheckinterval"
                                    aria-describedby="searchcheckinterval"
                                    name="searchcheckinterval"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The maximum number of times a request can be forwarded from one database link to another.  (nshoplimit)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={3}>
                                Hop Limit
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.hoplimit}
                                    type="number"
                                    id="hoplimit"
                                    aria-describedby="hoplimit"
                                    name="hoplimit"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="Allow proxied authentication to the remote server. (nsproxiedauthorization)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={12}>
                                <Checkbox
                                    label="Allow Proxied Authentication"
                                    id="nsproxiedauthorization"
                                    isChecked={this.state.nsproxiedauthorization}
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    aria-label="send ref"
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="Sets whether ACIs are evaluated on the database link as well as the remote data server (nschecklocalaci)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={12}>
                                <Checkbox
                                    label="Check Local ACIs"
                                    id="nschecklocalaci"
                                    isChecked={this.state.nschecklocalaci}
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    aria-label="send ref"
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="Sets whether referrals are returned by scoped searches (meaning 'one-level' or 'subtree' scoped searches). (nsreferralonscopedsearch)."
                            className="ds-margin-top"
                        >
                            <GridItem className="ds-label" span={12}>
                                <Checkbox
                                    label="Send Referral On Scoped Search"
                                    id="nsreferralonscopedsearch"
                                    isChecked={this.state.nsreferralonscopedsearch}
                                    onChange={(str, e) => {
                                        this.handleChange(e);
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
                    onClick={this.saveLink}
                    variant="primary"
                    isLoading={this.state.saving}
                    spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                    {...extraPrimaryProps}
                    isDisabled={this.state.saveBtnDisabled || this.state.saving}
                >
                    {saveBtnName}
                </Button>
                <DoubleConfirmModal
                    showModal={this.state.showDeleteConfirm}
                    closeHandler={this.closeDeleteConfirm}
                    handleChange={this.handleChange}
                    actionHandler={this.deleteLink}
                    spinning={this.state.modalSpinning}
                    item={this.props.suffix}
                    checked={this.state.modalChecked}
                    mTitle="Delete Database Link"
                    mMsg="Are you really sure you want to delete this database link?"
                    mSpinningMsg="Deleting Database Link..."
                    mBtnName="Delete Database Link"
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
        let btnName = "Add New Controls";
        const extraPrimaryProps = {};
        if (spinning) {
            btnName = "Saving Controls ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Chaining LDAP Controls"
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
                        spinnerAriaValueText={spinning ? "Loading" : undefined}
                        {...extraPrimaryProps}
                    >
                        {btnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal>
                    <TextContent title="A list of LDAP control OIDs to be forwarded through chaining">
                        <Text component={TextVariants.h3}>
                            Available LDAP Controls
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
        let btnName = "Add New Components";
        const extraPrimaryProps = {};
        if (spinning) {
            btnName = "Saving Components ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }
        return (
            <Modal
                variant={ModalVariant.medium}
                title="Chaining Components"
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
                        spinnerAriaValueText={spinning ? "Loading" : undefined}
                        {...extraPrimaryProps}
                    >
                        {btnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal>
                    <TextContent title="A list of LDAP control components">
                        <Text component={TextVariants.h3}>
                            Available Components
                        </Text>
                    </TextContent>
                    Available LDAP Controls
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
