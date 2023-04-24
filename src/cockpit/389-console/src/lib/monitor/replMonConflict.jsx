import React from "react";
import cockpit from "cockpit";
import { log_cmd } from "../tools.jsx";
import PropTypes from "prop-types";
import {
    ConflictTable,
    GlueTable,
} from "./monitorTables.jsx";
import {
    TaskLogModal,
    ConflictCompareModal,
} from "./monitorModals.jsx";
import {
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
    Tooltip,
} from "@patternfly/react-core";
import { DoubleConfirmModal } from "../notifications.jsx";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faSyncAlt } from '@fortawesome/free-solid-svg-icons';

export class ReplMonConflict extends React.Component {
    constructor (props) {
        super(props);

        this.state = {
            showConfirmConvertConflict: false,
            showConfirmSwapConflict: false,
            showConfirmDeleteConflict: false,
            showCompareModal: false,
            showConfirmDeleteGlue: false,
            showConfirmConvertGlue: false,
            showConfirmConvertGlue: false,
            swapConflictRadio: false,
            deleteConflictRadio: false,
            convertConflictRadio: false,
            convertRDN: "",
            glueEntry: "",
            conflictEntry: "",

            modalChecked: false,
            modalSpinning: false,
            activeTabConflictKey: 0,
        };

        this.handleNavConflictSelect = (event, tabIndex) => {
            this.setState({
                activeTabConflictKey: tabIndex
            });
        };

        this.convertConflict = this.convertConflict.bind(this);
        this.swapConflict = this.swapConflict.bind(this);
        this.deleteConflict = this.deleteConflict.bind(this);
        this.resolveConflict = this.resolveConflict.bind(this);
        this.convertGlue = this.convertGlue.bind(this);
        this.deleteGlue = this.deleteGlue.bind(this);
        this.closeCompareModal = this.closeCompareModal.bind(this);
        this.confirmDeleteGlue = this.confirmDeleteGlue.bind(this);
        this.confirmConvertGlue = this.confirmConvertGlue.bind(this);
        this.closeConfirmDeleteGlue = this.closeConfirmDeleteGlue.bind(this);
        this.closeConfirmConvertGlue = this.closeConfirmConvertGlue.bind(this);
        this.handleRadioChange = this.handleRadioChange.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleConflictConversion = this.handleConflictConversion.bind(this);
        this.confirmDeleteConflict = this.confirmDeleteConflict.bind(this);
        this.confirmConvertConflict = this.confirmConvertConflict.bind(this);
        this.confirmSwapConflict = this.confirmSwapConflict.bind(this);
        this.closeConfirmDeleteConflict = this.closeConfirmDeleteConflict.bind(this);
        this.closeConfirmConvertConflict = this.closeConfirmConvertConflict.bind(this);
        this.closeConfirmSwapConflict = this.closeConfirmSwapConflict.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    handleRadioChange(value, evt) {
        // Handle the radio button changes
        const radioID = {
            swapConflictRadio: false,
            deleteConflictRadio: false,
            convertConflictRadio: false,
        };

        radioID[evt.target.id] = value;
        this.setState({
            swapConflictRadio: radioID.swapConflictRadio,
            deleteConflictRadio: radioID.deleteConflictRadio,
            convertConflictRadio: radioID.convertConflictRadio,
        });
    }

    handleChange(value, evt) {
        // PF 4 version
        if (evt.target.type === 'number') {
            if (value) {
                value = parseInt(value);
            } else {
                value = 1;
            }
        }
        this.setState({
            [evt.target.id]: value
        });
    }

    convertConflict () {
        this.setState({ modalSpinning: true });
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "convert", this.state.conflictEntry, "--new-rdn=" + this.state.convertRDN];
        log_cmd("convertConflict", "convert conflict entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication conflict entry was converted into a valid entry`
                    );
                    this.setState({
                        showCompareModal: false,
                    });
                    this.closeConfirmConvertConflict();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to convert conflict entry entry: ${this.state.conflictEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmConvertConflict();
                });
    }

    swapConflict () {
        this.setState({ modalSpinning: true });
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "swap", this.state.conflictEntry];
        log_cmd("swapConflict", "swap in conflict entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication Conflict Entry is now the Valid Entry`
                    );
                    this.setState({
                        showCompareModal: false,
                    });
                    this.closeConfirmSwapConflict();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to swap in conflict entry: ${this.state.conflictEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmSwapConflict();
                });
    }

    deleteConflict () {
        this.setState({ modalSpinning: true });
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "delete", this.state.conflictEntry];

        log_cmd("deleteConflict", "Delete conflict entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication conflict entry was deleted`
                    );
                    this.setState({
                        showCompareModal: false,
                    });
                    this.closeConfirmConvertConflict();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete conflict entry: ${this.state.conflictEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmDeleteConflict();
                });
    }

    resolveConflict (dn) {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "compare", dn];
        log_cmd("resolveConflict", "Compare conflict entry with valid entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const entries = JSON.parse(content);
                    this.setState({
                        cmpConflictEntry: entries.items[0],
                        cmpValidEntry: entries.items[1],
                        showCompareModal: true,
                        deleteConflictRadio: true,
                        swapConflictRadio: false,
                        convertConflictRadio: false,
                        convertRDN: "",
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to get conflict entries: ${dn} - ${errMsg.desc}`
                    );
                });
    }

    confirmConvertGlue (dn) {
        this.setState({
            showConfirmConvertGlue: true,
            glueEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmConvertGlue () {
        this.setState({
            showConfirmConvertGlue: false,
            glueEntry: "",
            modalChecked: false,
            modalSpinning: false,
        });
    }

    convertGlue () {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "convert-glue", this.state.glueEntry];
        log_cmd("convertGlue", "Convert glue entry to normal entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication glue entry was converted`
                    );
                    this.closeConfirmConvertGlue();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to convert glue entry: ${this.state.glueEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmConvertGlue();
                });
    }

    confirmDeleteGlue (dn) {
        this.setState({
            showConfirmDeleteGlue: true,
            glueEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    deleteGlue () {
        const cmd = ["dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "repl-conflict", "delete-glue", this.state.glueEntry];
        log_cmd("deleteGlue", "Delete glue entry", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reloadConflicts();
                    this.props.addNotification(
                        "success",
                        `Replication glue entry was deleted`
                    );
                    this.closeConfirmDeleteGlue();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to delete glue entry: ${this.state.glueEntry} - ${errMsg.desc}`
                    );
                    this.closeConfirmDeleteGlue();
                });
    }

    closeConfirmDeleteGlue () {
        this.setState({
            showConfirmDeleteGlue: false,
            glueEntry: "",
            modalChecked: false,
            modalSpinning: false,
        });
    }

    handleConflictConversion (dn) {
        // Follow the radio button and perform the conflict resolution
        if (this.state.deleteConflictRadio) {
            this.confirmDeleteConflict(dn);
        } else if (this.state.swapConflictRadio) {
            this.confirmSwapConflict(dn);
        } else {
            this.confirmConvertConflict(dn);
        }
    }

    confirmConvertConflict (dn) {
        if (this.state.convertRDN == "") {
            this.props.addNotification(
                "error",
                `You must provide a RDN if you want to convert the Conflict Entry`
            );
            return;
        }
        this.setState({
            showConfirmConvertConflict: true,
            conflictEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmConvertConflict () {
        this.setState({
            showConfirmConvertConflict: false,
            conflictEntry: "",
            modalChecked: false,
            modalSpinning: false,
            convertRDN: "",
        });
    }

    confirmSwapConflict (dn) {
        this.setState({
            showConfirmSwapConflict: true,
            conflictEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmSwapConflict () {
        this.setState({
            showConfirmSwapConflict: false,
            conflictEntry: "",
            modalChecked: false,
            modalSpinning: false,
        });
    }

    confirmDeleteConflict (dn) {
        this.setState({
            showConfirmDeleteConflict: true,
            conflictEntry: dn,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmDeleteConflict () {
        this.setState({
            showConfirmDeleteConflict: false,
            conflictEntry: "",
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeCompareModal () {
        this.setState({
            showCompareModal: false,
            modalChecked: false,
            modalSpinning: false,
        });
    }


    render () {
        const conflictEntries = this.props.data.conflicts;
        const glueEntries = this.props.data.glues;

        return (
            <div>
                <div className="ds-container">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Monitor Conflict and Glue Entries
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
                <Tabs isBox className="ds-margin-top-lg" activeKey={this.state.activeTabConflictKey} onSelect={this.handleNavConflictSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Conflict Entries <font size="2">({conflictEntries.length})</font></TabTitleText>}>
                        <div className="ds-indent ds-margin-top-lg">
                            <Tooltip
                                content={
                                    <div>
                                        Replication conflict entries occur when two entries are created with the
                                        same DN (or name) on different servers at about the same time.  The automatic conflict
                                        resolution procedure renames the entry created last.  Its RDN is changed
                                        into a multi-valued RDN that includes the entry's original RDN and it's unique
                                        identifier (nsUniqueId).  There are several ways to resolve a conflict,
                                        but choosing which option to use is up to you.
                                    </div>
                                }
                            >
                                <a className="ds-indent ds-font-size-sm">What Is A Replication Conflict Entry?</a>
                            </Tooltip>
                            <ConflictTable
                                conflicts={conflictEntries}
                                resolveConflict={this.resolveConflict}
                                key={conflictEntries}
                            />
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>Glue Entries <font size="2">({glueEntries.length})</font></TabTitleText>}>
                        <div className="ds-indent ds-margin-top-lg">
                            <Tooltip
                                content={
                                    <div>
                                        When a <b>Delete</b> operation is replicated and the consumer server finds that the entry to be
                                        deleted has child entries, the conflict resolution procedure creates a "<i>glue entry</i>" to
                                        avoid having orphaned entries in the database.  In the same way, when an <b>Add</b> operation is
                                        replicated and the consumer server cannot find the parent entry, the conflict resolution
                                        procedure creates a "<i>glue entry</i>", representing the "parent entry", so that the new entry is
                                        not an orphaned entry.  You can choose to convert the glue entry, or remove the glue entry and
                                        all its child entries.
                                    </div>
                                }
                            >
                                <a className="ds-indent ds-font-size-sm">What Is A Replication Glue Entry?</a>
                            </Tooltip>
                            <GlueTable
                                glues={glueEntries}
                                convertGlue={this.confirmConvertGlue}
                                deleteGlue={this.confirmDeleteGlue}
                                key={glueEntries}
                            />
                        </div>
                    </Tab>
                </Tabs>
                <ConflictCompareModal
                    showModal={this.state.showCompareModal}
                    conflictEntry={this.state.cmpConflictEntry}
                    validEntry={this.state.cmpValidEntry}
                    swapConflictRadio={this.state.swapConflictRadio}
                    convertConflictRadio={this.state.convertConflictRadio}
                    deleteConflictRadio={this.state.deleteConflictRadio}
                    newRDN={this.state.convertRDN}
                    closeHandler={this.closeCompareModal}
                    saveHandler={this.handleConflictConversion}
                    handleChange={this.handleChange}
                    handleRadioChange={this.handleRadioChange}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteGlue}
                    closeHandler={this.closeConfirmDeleteGlue}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.deleteGlue}
                    spinning={this.state.modalSpinning}
                    item={this.state.glueEntry}
                    checked={this.state.modalChecked}
                    mTitle="Delete Glue Entry"
                    mMsg="Are you really sure you want to delete this glue entry and its child entries?"
                    mSpinningMsg="Deleting Glue Entry ..."
                    mBtnName="Delete Glue"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmConvertGlue}
                    closeHandler={this.closeConfirmConvertGlue}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.convertGlue}
                    spinning={this.state.modalSpinning}
                    item={this.state.glueEntry}
                    checked={this.state.modalChecked}
                    mTitle="Convert Glue Entry"
                    mMsg="Are you really sure you want to convert this glue entry to a regular entry?"
                    mSpinningMsg="Converting Glue Entry ..."
                    mBtnName="Convert Glue"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmConvertConflict}
                    closeHandler={this.closeConfirmConvertConflict}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.convertConflict}
                    spinning={this.state.modalSpinning}
                    item={this.state.conflictEntry}
                    checked={this.state.modalChecked}
                    mTitle="Convert Conflict Entry Into New Entry"
                    mMsg="Are you really sure you want to convert this conflict entry?"
                    mSpinningMsg="Converting Conflict Entry ..."
                    mBtnName="Convert Conflict"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmSwapConflict}
                    closeHandler={this.closeConfirmSwapConflict}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.swapConflict}
                    spinning={this.state.modalSpinning}
                    item={this.state.conflictEntry}
                    checked={this.state.modalChecked}
                    mTitle="Swap Conflict Entry"
                    mMsg="Are you really sure you want to swap this conflict entry with the valid entry?"
                    mSpinningMsg="Swapping Conflict Entry ..."
                    mBtnName="Swap Conflict"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteConflict}
                    closeHandler={this.closeConfirmDeleteConflict}
                    handleChange={this.handleFieldChange}
                    actionHandler={this.deleteConflict}
                    spinning={this.state.modalSpinning}
                    item={this.state.conflictEntry}
                    checked={this.state.modalChecked}
                    mTitle="Delete Replication Conflict Entry"
                    mMsg="Are you really sure you want to delete this conflict entry?"
                    mSpinningMsg="Deleting Conflict Entry ..."
                    mBtnName="Delete Conflict"
                />
            </div>
        );
    }
}

// Props and defaultProps

ReplMonConflict.propTypes = {
    data: PropTypes.object,
    suffix: PropTypes.string,
    serverId: PropTypes.string,
    addNotification: PropTypes.func,
    enableTree: PropTypes.func,
};

ReplMonConflict.defaultProps = {
    data: {},
    suffix: "",
    serverId: "",
};

export default ReplMonConflict;
