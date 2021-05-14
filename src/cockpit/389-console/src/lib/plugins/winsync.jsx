import cockpit from "cockpit";
import React from "react";
import {
    Row,
    Col,
    Form,
    FormControl,
    FormGroup,
    ControlLabel
} from "patternfly-react";
import {
    Button,
    Checkbox,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    // TextInput,
    noop
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd } from "../tools.jsx";

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

        this.handleCheckboxChange = this.handleCheckboxChange.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.updateFields = this.updateFields.bind(this);
        this.runFixup = this.runFixup.bind(this);
        this.toggleFixupModal = this.toggleFixupModal.bind(this);

        this.state = {
            posixWinsyncCreateMemberOfTask: false,
            posixWinsyncLowerCaseUID: false,
            posixWinsyncMapMemberUID: false,
            posixWinsyncMapNestedGrouping: false,
            posixWinsyncMsSFUSchema: false,

            fixupModalShow: false,
            fixupDN: "",
            fixupFilter: ""
        };
    }

    toggleFixupModal() {
        this.setState(prevState => ({
            fixupModalShow: !prevState.fixupModalShow,
            fixupDN: "",
            fixupFilter: ""
        }));
    }

    runFixup() {
        if (!this.state.fixupDN) {
            this.props.addNotification("warning", "Fixup DN is required.");
        } else {
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

            this.props.toggleLoadingHandler();
            log_cmd("runFixup", "Run Member UID task", cmd);
            cockpit
                    .spawn(cmd, {
                        superuser: true,
                        err: "message"
                    })
                    .done(content => {
                        this.props.addNotification(
                            "success",
                            `Fixup task for ${this.state.fixupDN} was successfull`
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            fixupModalShow: false
                        });
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.addNotification(
                            "error",
                            `Fixup task for ${this.state.fixupDN} has failed ${errMsg.desc}`
                        );
                        this.props.toggleLoadingHandler();
                        this.setState({
                            fixupModalShow: false
                        });
                    });
        }
    }

    handleCheckboxChange(checked, e) {
        this.setState({
            [e.target.id]: checked
        });
    }

    handleFieldChange(e) {
        this.setState({
            [e.target.id]: e.target.value
        });
    }

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "Posix Winsync API");

            this.setState({
                posixWinsyncCreateMemberOfTask: !(
                    pluginRow["posixwinsynccreatememberoftask"] === undefined ||
                    pluginRow["posixwinsynccreatememberoftask"][0] == "false"
                ),
                posixWinsyncLowerCaseUID: !(
                    pluginRow["posixwinsynclowercaseuid"] === undefined ||
                    pluginRow["posixwinsynclowercaseuid"][0] == "false"
                ),
                posixWinsyncMapMemberUID: !(
                    pluginRow["posixwinsyncmapmemberuid"] === undefined ||
                    pluginRow["posixwinsyncmapmemberuid"][0] == "false"
                ),
                posixWinsyncMapNestedGrouping: !(
                    pluginRow["posixwinsyncmapnestedgrouping"] === undefined ||
                    pluginRow["posixwinsyncmapnestedgrouping"][0] == "false"
                ),
                posixWinsyncMsSFUSchema: !(
                    pluginRow["posixwinsyncmssfuschema"] === undefined ||
                    pluginRow["posixwinsyncmssfuschema"][0] == "false"
                )
            });
        }
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
            fixupFilter
        } = this.state;

        let specificPluginCMD = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "posix-winsync",
            "set",
            "--create-memberof-task",
            posixWinsyncCreateMemberOfTask ? "true" : "false",
            "--lower-case-uid",
            posixWinsyncLowerCaseUID ? "true" : "false",
            "--map-member-uid",
            posixWinsyncMapMemberUID ? "true" : "false",
            "--map-nested-grouping",
            posixWinsyncMapNestedGrouping ? "true" : "false",
            "--ms-sfu-schema",
            posixWinsyncMsSFUSchema ? "true" : "false"
        ];
        return (
            <div>
                <Modal
                    variant={ModalVariant.small}
                    title="MemberOf Task"
                    isOpen={fixupModalShow}
                    aria-labelledby="ds-modal"
                    onClose={this.toggleFixupModal}
                    actions={[
                        <Button key="confirm" variant="primary" onClick={this.runFixup}>
                            Run
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.toggleFixupModal}>
                            Cancel
                        </Button>
                    ]}
                >
                    <Row>
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup controlId="fixupDN" key="fixupDN">
                                    <Col sm={3}>
                                        <ControlLabel title="Base DN that contains entries to fix up">
                                            Base DN
                                        </ControlLabel>
                                    </Col>
                                    <Col sm={9}>
                                        <FormControl
                                            type="text"
                                            value={fixupDN}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup controlId="fixupFilter" key="fixupFilter">
                                    <Col sm={3}>
                                        <ControlLabel title="Filter for entries to fix up. If omitted, all entries with objectclass inetuser/inetadmin/nsmemberof under the specified base will have their memberOf attribute regenerated.">
                                            Filter DN
                                        </ControlLabel>
                                    </Col>
                                    <Col sm={9}>
                                        <FormControl
                                            type="text"
                                            value={fixupFilter}
                                            onChange={this.handleFieldChange}
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
                </Modal>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Posix Winsync API"
                    pluginName="Posix Winsync API"
                    cmdName="posix-winsync"
                    specificPluginCMD={specificPluginCMD}
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Row>
                        <Col sm={12}>
                            <Form horizontal>
                                <FormGroup
                                    key="posixWinsyncCreateMemberOfTask"
                                    controlId="posixWinsyncCreateMemberOfTask"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={6}
                                        title="Sets whether to run the memberOf fix-up task immediately after a sync run in order to update group memberships for synced users"
                                    >
                                        Create MemberOf Task
                                    </Col>
                                    <Col sm={2}>
                                        <Checkbox
                                            id="posixWinsyncCreateMemberOfTask"
                                            isChecked={posixWinsyncCreateMemberOfTask}
                                            onChange={this.handleCheckboxChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="posixWinsyncLowerCaseUID"
                                    controlId="posixWinsyncLowerCaseUID"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={6}
                                        title="Sets whether to store (and, if necessary, convert) the UID value in the memberUID attribute in lower case"
                                    >
                                        Lower Case UID
                                    </Col>
                                    <Col sm={2}>
                                        <Checkbox
                                            id="posixWinsyncLowerCaseUID"
                                            isChecked={posixWinsyncLowerCaseUID}
                                            onChange={this.handleCheckboxChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="posixWinsyncMapMemberUID"
                                    controlId="posixWinsyncMapMemberUID"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={6}
                                        title="Sets whether to map the memberUID attribute in an Active Directory group to the uniqueMember attribute in a Directory Server group"
                                    >
                                        Map Member UID
                                    </Col>
                                    <Col sm={2}>
                                        <Checkbox
                                            id="posixWinsyncMapMemberUID"
                                            isChecked={posixWinsyncMapMemberUID}
                                            onChange={this.handleCheckboxChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="posixWinsyncMapNestedGrouping"
                                    controlId="posixWinsyncMapNestedGrouping"
                                >
                                    <Col
                                        title="Manages if nested groups are updated when memberUID attributes in an Active Directory POSIX group change"
                                        componentClass={ControlLabel}
                                        sm={6}
                                    >
                                        Map Nested Grouping
                                    </Col>
                                    <Col sm={2}>
                                        <Checkbox
                                            id="posixWinsyncMapNestedGrouping"
                                            isChecked={posixWinsyncMapNestedGrouping}
                                            onChange={this.handleCheckboxChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="posixWinsyncMsSFUSchema"
                                    controlId="posixWinsyncMsSFUSchema"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={6}
                                        title="Sets whether to the older Microsoft System Services  for Unix 3.0 (msSFU30) schema when syncing Posix attributes  from Active Directory"
                                    >
                                        Microsoft System Services for Unix 3.0 (msSFU30) schema
                                    </Col>
                                    <Col sm={2}>
                                        <Checkbox
                                            id="posixWinsyncMsSFUSchema"
                                            isChecked={posixWinsyncMsSFUSchema}
                                            onChange={this.handleCheckboxChange}
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={12}>
                            <Button
                                variant="primary"
                                onClick={this.toggleFixupModal}
                                title="Corrects mismatched member and uniquemember values"
                            >
                                Run MemberOf Task
                            </Button>
                        </Col>
                    </Row>
                </PluginBasicConfig>
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
    savePluginHandler: noop,
    pluginListHandler: noop,
    addNotification: noop,
    toggleLoadingHandler: noop
};

export default WinSync;
