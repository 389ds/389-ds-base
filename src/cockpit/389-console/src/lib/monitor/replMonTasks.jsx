import React from "react";
import cockpit from "cockpit";
import PropTypes from "prop-types";
import {
    CleanALLRUVTable,
    AbortCleanALLRUVTable,
} from "./monitorTables.jsx";
import {
    TaskLogModal,
} from "./monitorModals.jsx";
import {
    Button,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import { SyncAltIcon } from "@patternfly/react-icons";

const _ = cockpit.gettext;

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
            if (task.attrs.cn[0] === name) {
                logData = task.attrs.nstasklog[0];
                break;
            }
        }
        this.setState({
            showLogModal: true,
            logData
        });
    }

    viewAbortLog (name) {
        let logData = "";
        for (const task of this.props.data.abortTasks) {
            if (task.attrs.cn[0] === name) {
                logData = task.attrs.nstasklog[0];
                break;
            }
        }
        this.setState({
            showLogModal: true,
            logData
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
                            {_("Monitor Replication Tasks")}
                            <Button 
                                variant="plain"
                                aria-label={_("Refresh replication monitor")}
                                onClick={this.props.handleReload}
                            >
                                <SyncAltIcon />
                            </Button>
                        </Text>
                    </TextContent>
                </div>
                <Tabs isBox className="ds-margin-top-lg" activeKey={this.state.activeTabTaskKey} onSelect={this.handleNavTaskSelect}>
                    <Tab eventKey={0} title={<TabTitleText>{_("CleanAllRUV Tasks ")}<font size="2">({cleanTasks.length})</font></TabTitleText>}>
                        <div className="ds-indent ds-margin-top-lg">
                            <CleanALLRUVTable
                                tasks={cleanTasks}
                                viewLog={this.viewCleanLog}
                            />
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>{_("Abort CleanAllRUV Tasks ")}<font size="2">({abortTasks.length})</font></TabTitleText>}>
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
    enableTree: PropTypes.func,
};

ReplMonTasks.defaultProps = {
    data: {},
};

export default ReplMonTasks;
