import React from "react";
import {
    Row,
    Col,
    Form,
    noop,
    FormGroup,
    Checkbox,
    ControlLabel
} from "patternfly-react";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import "../../css/ds.css";

class WinSync extends React.Component {
    componentWillMount(prevProps) {
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
        this.updateFields = this.updateFields.bind(this);

        this.state = {
            posixWinsyncCreateMemberOfTask: false,
            posixWinsyncLowerCaseUID: false,
            posixWinsyncMapMemberUID: false,
            posixWinsyncMapNestedGrouping: false,
            posixWinsyncMsSFUSchema: false
        };
    }

    handleCheckboxChange(e) {
        this.setState({
            [e.target.id]: e.target.checked
        });
    }

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(
                row => row.cn[0] === "Posix Winsync API"
            );

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
            posixWinsyncMsSFUSchema
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
                        <Col sm={9}>
                            <Form horizontal>
                                <FormGroup
                                    key="posixWinsyncCreateMemberOfTask"
                                    controlId="posixWinsyncCreateMemberOfTask"
                                >
                                    <Col
                                        componentClass={ControlLabel}
                                        sm={3}
                                        title="Sets whether to run the memberOf fix-up task immediately after a sync run in order to update group memberships for synced users"
                                    >
                                        Create MemberOf Task
                                    </Col>
                                    <Col sm={6}>
                                        <Checkbox
                                            id="posixWinsyncCreateMemberOfTask"
                                            checked={
                                                posixWinsyncCreateMemberOfTask
                                            }
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
                                        sm={3}
                                        title="Sets whether to store (and, if necessary, convert) the UID value in the memberUID attribute in lower case"
                                    >
                                        Lower Case UID
                                    </Col>
                                    <Col sm={6}>
                                        <Checkbox
                                            id="posixWinsyncLowerCaseUID"
                                            checked={posixWinsyncLowerCaseUID}
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
                                        sm={3}
                                        title="Sets whether to map the memberUID attribute in an Active Directory group to the uniqueMember attribute in a Directory Server group"
                                    >
                                        Map Member UID
                                    </Col>
                                    <Col sm={6}>
                                        <Checkbox
                                            id="posixWinsyncMapMemberUID"
                                            checked={posixWinsyncMapMemberUID}
                                            onChange={this.handleCheckboxChange}
                                        />
                                    </Col>
                                </FormGroup>
                                <FormGroup
                                    key="posixWinsyncMapNestedGrouping"
                                    controlId="posixWinsyncMapNestedGrouping"
                                >
                                    <Col componentClass={ControlLabel} sm={3}>
                                        Map Nested Grouping
                                    </Col>
                                    <Col sm={6}>
                                        <Checkbox
                                            id="posixWinsyncMapNestedGrouping"
                                            checked={
                                                posixWinsyncMapNestedGrouping
                                            }
                                            title="Manages if nested groups are updated when memberUID \
                                        attributes in an Active Directory POSIX group change"
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
                                        sm={3}
                                        title="Sets whether to the older Microsoft System Services  for Unix 3.0 (msSFU30) schema when syncing Posix attributes  from Active Directory"
                                    >
                                        Microsoft System Services for Unix 3.0
                                        (msSFU30) schema
                                    </Col>
                                    <Col sm={6}>
                                        <Checkbox
                                            id="posixWinsyncMsSFUSchema"
                                            checked={posixWinsyncMsSFUSchema}
                                            onChange={this.handleCheckboxChange}
                                        />
                                    </Col>
                                </FormGroup>
                            </Form>
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
