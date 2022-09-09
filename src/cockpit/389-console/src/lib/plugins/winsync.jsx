import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Checkbox,
    Form,
    FormHelperText,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    TextInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd, valid_dn } from "../tools.jsx";

class WinSync extends React.Component {
    componentDidMount(prevProps) {
        this.updateFields();
    }

    componentDidUpdate(prevProps) {
        if (this.props.rows !== prevProps.rows) {
            this.updateFields();
        }
    }

    constructor(props) {
        super(props);

        this.state = {
            fixupModalShow: false,
            fixupDN: "",
            fixupFilter: "",
            saveBtnDisabled: true,
            saveBtnDisabledModal: true,
            saving: false,
            savingModal: false,
            error: {},
            // Settings
            posixWinsyncCreateMemberOfTask: false,
            posixWinsyncLowerCaseUID: false,
            posixWinsyncMapMemberUID: false,
            posixWinsyncMapNestedGrouping: false,
            posixWinsyncMsSFUSchema: false,
            // Original values
            _posixWinsyncCreateMemberOfTask: false,
            _posixWinsyncLowerCaseUID: false,
            _posixWinsyncMapMemberUID: false,
            _posixWinsyncMapNestedGrouping: false,
            _posixWinsyncMsSFUSchema: false,
        };

        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.updateFields = this.updateFields.bind(this);
        this.runFixup = this.runFixup.bind(this);
        this.toggleFixupModal = this.toggleFixupModal.bind(this);
        this.validateConfig = this.validateConfig.bind(this);
        this.validateModal = this.validateModal.bind(this);
        this.savePlugin = this.savePlugin.bind(this);
    }

    toggleFixupModal() {
        this.setState(prevState => ({
            fixupModalShow: !prevState.fixupModalShow,
            fixupDN: "",
            fixupFilter: "",
            savingModal: false,
        }));
    }

    runFixup() {
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "posix-winsync",
            "fixup",
            this.state.fixupDN
        ];

        if (this.state.fixupFilter) {
            cmd = [...cmd, "--filter", this.state.fixupFilter];
        }

        this.setState({
            savingModal: true
        });
        log_cmd("runFixup", "Run Member UID task", cmd);
        cockpit
                .spawn(cmd, {
                    superuser: true,
                    err: "message"
                })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Fixup task for ${this.state.fixupDN} was successfully started`
                    );
                    this.setState({
                        fixupModalShow: false,
                        savingModal: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Fixup task for ${this.state.fixupDN} has failed ${errMsg.desc}`
                    );
                    this.setState({
                        fixupModalShow: false,
                        savingModal: false
                    });
                });
    }

    validateConfig() {
        let all_good = false;

        const attrs = [
            'posixWinsyncCreateMemberOfTask', 'posixWinsyncLowerCaseUID',
            'posixWinsyncMapMemberUID', 'posixWinsyncMapNestedGrouping',
            'posixWinsyncMsSFUSchema'
        ];
        for (const check_attr of attrs) {
            if (this.state[check_attr] != this.state['_' + check_attr]) {
                all_good = true;
                break;
            }
        }

        this.setState({
            saveBtnDisabled: !all_good,
        });
    }

    validateModal() {
        const errObj = {};
        let all_good = true;

        if (!valid_dn(this.state.fixupDN)) {
            all_good = false;
            errObj.fixupDN = true;
        }
        if (this.state.fixupFilter == "") {
            all_good = false;
            errObj.fixupFilter = true;
        }

        this.setState({
            saveBtnDisabledModal: !all_good,
            error: errObj
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

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "Posix Winsync API");

            this.setState({
                posixWinsyncCreateMemberOfTask: !(
                    pluginRow.posixwinsynccreatememberoftask === undefined ||
                    pluginRow.posixwinsynccreatememberoftask[0] == "false"
                ),
                posixWinsyncLowerCaseUID: !(
                    pluginRow.posixwinsynclowercaseuid === undefined ||
                    pluginRow.posixwinsynclowercaseuid[0] == "false"
                ),
                posixWinsyncMapMemberUID: !(
                    pluginRow.posixwinsyncmapmemberuid === undefined ||
                    pluginRow.posixwinsyncmapmemberuid[0] == "false"
                ),
                posixWinsyncMapNestedGrouping: !(
                    pluginRow.posixwinsyncmapnestedgrouping === undefined ||
                    pluginRow.posixwinsyncmapnestedgrouping[0] == "false"
                ),
                posixWinsyncMsSFUSchema: !(
                    pluginRow.posixwinsyncmssfuschema === undefined ||
                    pluginRow.posixwinsyncmssfuschema[0] == "false"
                ),
                _posixWinsyncCreateMemberOfTask: !(
                    pluginRow.posixwinsynccreatememberoftask === undefined ||
                    pluginRow.posixwinsynccreatememberoftask[0] == "false"
                ),
                _posixWinsyncLowerCaseUID: !(
                    pluginRow.posixwinsynclowercaseuid === undefined ||
                    pluginRow.posixwinsynclowercaseuid[0] == "false"
                ),
                _posixWinsyncMapMemberUID: !(
                    pluginRow.posixwinsyncmapmemberuid === undefined ||
                    pluginRow.posixwinsyncmapmemberuid[0] == "false"
                ),
                _posixWinsyncMapNestedGrouping: !(
                    pluginRow.posixwinsyncmapnestedgrouping === undefined ||
                    pluginRow.posixwinsyncmapnestedgrouping[0] == "false"
                ),
                _posixWinsyncMsSFUSchema: !(
                    pluginRow.posixwinsyncmssfuschema === undefined ||
                    pluginRow.posixwinsyncmssfuschema[0] == "false"
                )
            });
        }
    }

    savePlugin() {
        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "posix-winsync",
            "set",
            "--create-memberof-task",
            this.state.posixWinsyncCreateMemberOfTask ? "true" : "false",
            "--lower-case-uid",
            this.state.posixWinsyncLowerCaseUID ? "true" : "false",
            "--map-member-uid",
            this.state.posixWinsyncMapMemberUID ? "true" : "false",
            "--map-nested-grouping",
            this.state.posixWinsyncMapNestedGrouping ? "true" : "false",
            "--ms-sfu-schema",
            this.state.posixWinsyncMsSFUSchema ? "true" : "false"
        ];

        this.setState({
            saving: true
        });

        log_cmd('savePlugin', 'Update Posix winsync plugin', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Successfully updated Posix winsync plugin`
                    );
                    this.props.pluginListHandler();
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to update Posix winsync plugin - ${errMsg.desc}`
                    );
                    this.props.pluginListHandler();
                    this.setState({
                        saving: false
                    });
                });
    }

    render() {
        const {
            posixWinsyncCreateMemberOfTask,
            posixWinsyncLowerCaseUID,
            posixWinsyncMapMemberUID,
            posixWinsyncMapNestedGrouping,
            posixWinsyncMsSFUSchema,
            fixupModalShow,
            fixupDN,
            fixupFilter,
            saving,
            savingModal,
            saveBtnDisabled,
            saveBtnDisabledModal,
            error
        } = this.state;

        let saveBtnName = "Save";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Saving ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }
        const saveBtnNameModal = "Run Task";
        if (savingModal) {
            saveBtnName = "Task running ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        return (
            <div className={saving || savingModal ? "ds-disabled" : ""}>
                <Modal
                    variant={ModalVariant.small}
                    title="MemberOf Task"
                    isOpen={fixupModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.toggleFixupModal}
                    actions={[
                        <Button
                            key="task"
                            variant="primary"
                            onClick={this.runFixup}
                            isDisabled={saveBtnDisabledModal || savingModal}
                            isLoading={savingModal}
                            spinnerAriaValueText={savingModal ? "Saving" : undefined}
                            {...extraPrimaryProps}
                        >
                            {saveBtnNameModal}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.toggleFixupModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title="Base DN that contains entries to fix up">
                            <GridItem className="ds-label" span={3}>
                                Base DN
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={fixupDN}
                                    type="text"
                                    id="fixupDN"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="fixupDN"
                                    onChange={(str, e) => {
                                        this.handleModalChange(e);
                                    }}
                                    validated={error.fixupDN ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                                <FormHelperText isError isHidden={!error.fixupDN}>
                                    Value must be a valid DN
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top" title="Filter for entries to fix up. If omitted, all entries with objectclass inetuser/inetadmin/nsmemberof under the specified base will have their memberOf attribute regenerated.">
                            <GridItem className="ds-label" span={3}>
                                Filter DN
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={fixupFilter}
                                    type="text"
                                    id="fixupFilter"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="fixupFilter"
                                    onChange={(str, e) => {
                                        this.handleModalChange(e);
                                    }}
                                    validated={error.fixupFilter ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                                <FormHelperText isError isHidden={!error.fixupDN}>
                                    Enter an LDAP search filter
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Posix Winsync API"
                    pluginName="Posix Winsync API"
                    cmdName="posix-winsync"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title="Sets whether to run the memberOf fix-up task immediately after a sync run in order to update group memberships for synced users">
                            <GridItem span={12}>
                                <Checkbox
                                    id="posixWinsyncCreateMemberOfTask"
                                    isChecked={posixWinsyncCreateMemberOfTask}
                                    onChange={(checked, e) => { this.handleFieldChange(e) }}
                                    label="Create MemberOf Task"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Sets whether to store (and, if necessary, convert) the UID value in the memberUID attribute in lower case">
                            <GridItem span={12}>
                                <Checkbox
                                    id="posixWinsyncLowerCaseUID"
                                    isChecked={posixWinsyncLowerCaseUID}
                                    onChange={(checked, e) => { this.handleFieldChange(e) }}
                                    label="Lower Case UID"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Sets whether to map the memberUID attribute in an Active Directory group to the uniqueMember attribute in a Directory Server group">
                            <GridItem span={12}>
                                <Checkbox
                                    id="posixWinsyncMapMemberUID"
                                    isChecked={posixWinsyncMapMemberUID}
                                    onChange={(checked, e) => { this.handleFieldChange(e) }}
                                    label="Map Member UID"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Manages if nested groups are updated when memberUID attributes in an Active Directory POSIX group change">
                            <GridItem span={12}>
                                <Checkbox
                                    id="posixWinsyncMapNestedGrouping"
                                    isChecked={posixWinsyncMapNestedGrouping}
                                    onChange={(checked, e) => { this.handleFieldChange(e) }}
                                    label="Map Nested Grouping"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="Sets whether to the older Microsoft System Services  for Unix 3.0 (msSFU30) schema when syncing Posix attributes from Active Directory">
                            <GridItem span={12}>
                                <Checkbox
                                    id="posixWinsyncMsSFUSchema"
                                    isChecked={posixWinsyncMsSFUSchema}
                                    onChange={(checked, e) => { this.handleFieldChange(e) }}
                                    label="Microsoft System Services for Unix 3.0 (msSFU30) schema"
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                    <Button
                        className="ds-margin-top-lg"
                        variant="primary"
                        onClick={this.toggleFixupModal}
                        title="Corrects mismatched member and uniquemember values"
                    >
                        Run MemberOf Task
                    </Button>
                </PluginBasicConfig>
                <Button
                    className="ds-margin-top-lg"
                    variant="primary"
                    onClick={this.savePlugin}
                    isDisabled={saveBtnDisabled || saving}
                    isLoading={saving}
                    spinnerAriaValueText={saving ? "Saving" : undefined}
                    {...extraPrimaryProps}
                >
                    {saveBtnName}
                </Button>
            </div>
        );
    }
}

WinSync.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

WinSync.defaultProps = {
    rows: [],
    serverId: "",
};

export default WinSync;
