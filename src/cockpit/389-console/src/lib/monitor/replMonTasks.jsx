import React from "react";
import cockpit from "cockpit";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import {
    CleanALLRUVTable,
    AbortCleanALLRUVTable,
} from "./monitorTables.jsx";
import {
    TaskLogModal,
} from "./monitorModals.jsx";
import {
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faSyncAlt } from '@fortawesome/free-solid-svg-icons';

export class ReplMonTasks extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            showLogModal: false,
            logData: "",
            activeTabTaskKey: 0,
        };

        this.handleNavTaskSelect = (event, tabIndex) => {
            this.setState({
                activeTabTaskKey: tabIndex
            });
        };

        this.viewCleanLog = this.viewCleanLog.bind(this);
        this.viewAbortLog = this.viewAbortLog.bind(this);
        this.closeLogModal = this.closeLogModal.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    viewCleanLog (name) {
        let logData = "";
        for (const task of this.props.data.cleanTasks) {
            if (task.attrs.cn[0] == name) {
                logData = task.attrs.nstasklog[0];
                break;
            }
        }
        this.setState({
            showLogModal: true,
            logData: logData
        });
    }

    viewAbortLog (name) {
        let logData = "";
        for (const task of this.props.data.abortTasks) {
            if (task.attrs.cn[0] == name) {
                logData = task.attrs.nstasklog[0];
                break;
            }
        }
        this.setState({
            showLogModal: true,
            logData: logData
        });
    }

    closeLogModal() {
        this.setState({
            showLogModal: false
        });
    }

    render () {
        const cleanTasks = this.props.data.cleanTasks;
        const abortTasks = this.props.data.abortTasks;

        return (
            <div>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Monitor Replication Tasks
                            <FontAwesomeIcon
                                size="lg"
                                className="ds-left-margin ds-refresh"
                                icon={faSyncAlt}
                                title="Refresh replication monitor"
                                onClick={this.props.reload}
                            />
                        </Text>
                    </TextContent>
                </div>
                <Tabs isBox className="ds-margin-top-lg" activeKey={this.state.activeTabTaskKey} onSelect={this.handleNavTaskSelect}>
                    <Tab eventKey={0} title={<TabTitleText>CleanAllRUV Tasks <font size="2">({cleanTasks.length})</font></TabTitleText>}>
                        <div className="ds-indent ds-margin-top-lg">
                            <CleanALLRUVTable
                                tasks={cleanTasks}
                                viewLog={this.viewCleanLog}
                            />
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>Abort CleanAllRUV Tasks <font size="2">({abortTasks.length})</font></TabTitleText>}>
                        <div className="ds-indent ds-margin-top-lg">
                            <AbortCleanALLRUVTable
                                tasks={abortTasks}
                                viewLog={this.viewAbortLog}
                            />
                        </div>
                    </Tab>
                </Tabs>
                <TaskLogModal
                    showModal={this.state.showLogModal}
                    closeHandler={this.closeLogModal}
                    logData={this.state.logData}
                />
            </div>
        );
    }
}

// Props and defaultProps

ReplMonTasks.propTypes = {
    data: PropTypes.object,
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    addNotification: PropTypes.func,
    enableTree: PropTypes.func,
};

ReplMonTasks.defaultProps = {
    data: {},
    suffix: "",
    serverId: "",
};

export default ReplMonTasks;
