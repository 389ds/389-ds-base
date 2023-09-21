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
    Switch,
    NumberInput,
    Tooltip,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd } from "../tools.jsx";
import {
    WrenchIcon,
} from '@patternfly/react-icons';

const _ = cockpit.gettext;

class USNPlugin extends React.Component {
    componentDidMount() {
        if (this.props.wasActiveList.includes(5)) {
            if (this.state.firstLoad) {
                this.loadSuffixList();
                this.updateSwitch();
            }
        }
    }

    constructor(props) {
        super(props);

        this.handleRunCleanup = this.handleRunCleanup.bind(this);
        this.handleToggleCleanupModal = this.handleToggleCleanupModal.bind(this);
        this.updateSwitch = this.updateSwitch.bind(this);
        this.handleSwitchChange = this.handleSwitchChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.loadSuffixList = this.loadSuffixList.bind(this);

        this.state = {
            firstLoad: true,
            globalMode: false,
            disableSwitch: false,
            cleanupModalShow: false,
            cleanupSuffix: "",
            cleanupMaxUSN: 0,
            suffixList: [],
            pluginEnabled: false,
        };

        this.minValue = 0;
        this.maxValue = 20000000;
        this.handleMinusConfig = () => {
            this.setState({
                cleanupMaxUSN: Number(this.state.cleanupMaxUSN) - 1
            });
        };
        this.handleConfigChange = (event) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                cleanupMaxUSN: newValue > this.maxValue ? this.maxValue : newValue < this.minValue ? this.minValue : newValue
            });
        };
        this.handlePlusConfig = (id) => {
            this.setState({
                cleanupMaxUSN: Number(this.state.cleanupMaxUSN) + 1
            });
        };
    }

    loadSuffixList () {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "suffix", "list", "--suffix"
        ];
        log_cmd("loadSuffixList", "Get a list of all the suffixes", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const suffixList = JSON.parse(content);
                    this.setState({
                        suffixList: suffixList.items,
                        cleanupSuffix: suffixList.items[0]
                    });
                });
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    handleSwitchChange(value) {
        const { serverId, addNotification, toggleLoadingHandler } = this.props;
        const new_status = this.state.globalMode ? "off" : "on";
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + serverId + ".socket",
            "plugin",
            "usn",
            "global",
            new_status
        ];

        toggleLoadingHandler();
        this.setState({ disableSwitch: true });
        log_cmd("handleSwitchChange", "Switch global USN mode from the USN plugin tab", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    console.info("savePlugin", "Result", content);
                    this.updateSwitch();
                    addNotification(
                        "success",
                        cockpit.format(_("Global USN mode was successfully set to $0."), new_status)
                    );
                    toggleLoadingHandler();
                    this.setState({ disableSwitch: false });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    addNotification(
                        "error",
                        cockpit.format(_("Error during global USN mode modification - $0"), errMsg.desc)
                    );
                    toggleLoadingHandler();
                    this.setState({ disableSwitch: false });
                });
    }

    updateSwitch() {
        this.setState({
            firstLoad: false,
            disableSwitch: true
        });

        const pluginRow = this.props.rows.find(row => row.cn[0] === "USN");
        let pluginEnabled = false;
        if (pluginRow["nsslapd-pluginEnabled"][0] === "on") {
            pluginEnabled = true;
        }
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config",
            "get",
            "nsslapd-entryusn-global"
        ];
        this.props.toggleLoadingHandler();
        log_cmd("updateSwitch", "Get global USN status", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const myObject = JSON.parse(content);
                    const usnGlobalAttr = myObject.attrs["nsslapd-entryusn-global"][0];
                    this.setState({
                        globalMode: !(usnGlobalAttr === "off"),
                        pluginEnabled,
                        disableSwitch: false
                    });
                    this.props.toggleLoadingHandler();
                })
                .fail(err => {
                    if (err !== 0) {
                        const errMsg = JSON.parse(err);
                        console.log("Get global USN failed", errMsg.desc);
                    }
                    this.setState({
                        disableSwitch: false
                    });
                    this.props.toggleLoadingHandler();
                });
    }

    handleToggleCleanupModal() {
        this.setState(prevState => ({
            cleanupModalShow: !prevState.cleanupModalShow,
            cleanupSuffix: prevState.suffixList[0],
            cleanupMaxUSN: 0
        }));
    }

    handleRunCleanup() {
        if (!this.state.cleanupSuffix) {
            this.props.addNotification("warning", _("Suffix is required."));
        } else {
            let cmd = [
                "dsconf",
                "-j",
                "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                "plugin",
                "usn",
                "cleanup"
            ];

            if (this.state.cleanupSuffix) {
                cmd = [...cmd, "--suffix=" + this.state.cleanupSuffix];
            }
            if (this.state.cleanupMaxUSN > 0) {
                cmd = [...cmd, "--max-usn=" + this.state.cleanupMaxUSN];
            }

            this.props.toggleLoadingHandler();
            log_cmd("handleRunCleanup", "Run cleanup USN tombstones", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        this.props.addNotification(
                            "success",
                            _("Cleanup USN Tombstones task was successfull")
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            cleanupModalShow: false
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            cockpit.format(_("Cleanup USN Tombstones task has failed $0"), errMsg.desc)
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            cleanupModalShow: false
                        });
                    });
        }
    }

    render() {
        const {
            globalMode,
            disableSwitch,
            cleanupModalShow,
            cleanupSuffix,
            cleanupMaxUSN,
            suffixList
        } = this.state;

        return (
            <div>
                <Modal
                    variant={ModalVariant.small}
                    title={_("USN Tombstone Cleanup Task")}
                    aria-labelledby="ds-modal"
                    isOpen={cleanupModalShow}
                    onClose={this.handleToggleCleanupModal}
                    actions={[
                        <Button key="confirm" variant="primary" onClick={this.handleRunCleanup}>
                            {_("Run")}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.handleToggleCleanupModal}>
                            {_("Cancel")}
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title={_("Gives the suffix in the Directory Server to run the cleanup operation against")}>
                            <GridItem span={4}>
                                {_("Cleanup Suffix")}
                            </GridItem>
                            <GridItem span={8}>
                                <FormSelect
                                    id="configAutoAddOC"
                                    value={cleanupSuffix}
                                    onChange={(value, event) => {
                                        this.handleFieldChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                >
                                    {suffixList.map((attr) => (
                                        <FormSelectOption key={attr} value={attr} label={attr} />
                                    ))}
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Gives the highest USN value to delete when removing tombstone entries. All tombstone entries up to and including that number are deleted. Tombstone entries with higher USN values (that means more recent entries) are not deleted")}>
                            <GridItem span={4}>
                                {_("Cleanup Max USN")}
                            </GridItem>
                            <GridItem span={8}>
                                <NumberInput
                                    value={cleanupMaxUSN}
                                    min={this.minValue}
                                    max={this.maxValue}
                                    onMinus={this.handleMinusConfig}
                                    onChange={this.handleConfigChange}
                                    onPlus={this.handlePlusConfig}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={10}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="USN"
                    pluginName="Update Sequence Numbers"
                    cmdName="usn"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Tooltip
                        maxWidth={500}
                        content={
                            <div>
                                {_("The USN Plug-in enables LDAP clients and servers to identify if entries have been changed. When the USN Plug-in is enabled, update sequence numbers (USNs) are sequential numbers that are assigned to an entry whenever a write operation is performed against the entry. (Write operations include add, modify, modrdn, and delete operations. Internal database operations, like export operations, are not counted in the update sequence.) A USN counter keeps track of the most recently assigned USN.")}
                                <br /><br />
                                {_("The USN plug-in also moves entries to tombstone entries when the entry is deleted. If replication is enabled, then separate tombstone entries are kept by both the USN and Replication plug-in. Note that both tombstone entries are deleted by the replication process.")}
                            </div>
                        }
                    >
                        <a className="ds-font-size-sm">{_("What is the USN Plugin?")}</a>
                    </Tooltip>
                    <Form>
                        <Grid className="ds-margin-top-xlg" title={_("Enable entryUSN assignment across all backends, instead of per backend.")}>
                            <GridItem span={3} className="ds-label">
                                {_("USN Global")}
                            </GridItem>
                            <GridItem span={9}>
                                <Switch
                                    id="globalMode"
                                    isChecked={globalMode}
                                    onChange={() => this.handleSwitchChange(globalMode)}
                                    isDisabled={disableSwitch}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("This task deletes the tombstone entries maintained by the USN plugin.  This task should not run against a suffix that is being replicated")}>
                            <GridItem className="ds-label ds-margin-top" span={3}>
                                {_("Tombstone Cleanup Task")}<WrenchIcon className="ds-left-margin" />
                            </GridItem>
                            <GridItem span={9}>
                                <Button className="ds-margin-top" variant="primary" isDisabled={!this.state.pluginEnabled} onClick={this.handleToggleCleanupModal}>
                                    {_("Run Task")}
                                </Button>
                            </GridItem>
                        </Grid>
                    </Form>
                </PluginBasicConfig>
            </div>
        );
    }
}

USNPlugin.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

USNPlugin.defaultProps = {
    rows: [],
    serverId: "",
};

export default USNPlugin;
