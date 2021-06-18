import React from "react";
import {
    Row,
    Col,
    ControlLabel,
    Form,
    FormControl,
    Radio,
} from "patternfly-react";
import {
    Button,
    Checkbox,
    ExpandableSection,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    Select,
    SelectVariant,
    SelectOption,
    Spinner,
    // TextInput,
    noop
} from "@patternfly/react-core";
import PropTypes from "prop-types";

export class WinsyncAgmtModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            isExpanded: false,
        };

        this.onToggle = (isExpanded) => {
            this.setState({
                isExpanded
            });
        };
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    render() {
        const {
            showModal,
            closeHandler,
            saveHandler,
            handleChange,
            handleFracChange,
            onSelectToggle,
            onSelectClear,
            spinning,
            agmtName,
            agmtHost,
            agmtPort,
            agmtProtocol,
            agmtBindDN,
            agmtBindPW,
            agmtBindPWConfirm,
            agmtFracAttrs,
            agmtSync,
            agmtSyncMon,
            agmtSyncTue,
            agmtSyncWed,
            agmtSyncThu,
            agmtSyncFri,
            agmtSyncSat,
            agmtSyncSun,
            agmtStartTime,
            agmtEndTime,
            agmtSyncGroups,
            agmtSyncUsers,
            agmtWinDomain,
            agmtWinSubtree,
            agmtDSSubtree,
            agmtOneWaySync, // "both", "toWindows", "fromWindows"
            agmtSyncInterval,
            availAttrs,
            error,
            errorMsg,
            errorScheduleMsg,
            isExcludeAttrOpen,
        } = this.props;
        let spinner = "";
        let saveDisabled = !this.props.saveOK;
        let title = "Create";
        let initRow = "";
        let errMsgClass = "ds-center ds-modal-error";
        let errMsg = errorMsg;
        let name = "agmt-modal";

        if (this.props.edit) {
            title = "Edit";
            name = "agmt-modal-edit";
        } else {
            initRow =
                <Row className="ds-margin-top">
                    <Col componentClass={ControlLabel} sm={4}>
                        Consumer Initialization
                    </Col>
                    <Col sm={8}>
                        <select className="btn btn-default dropdown" id="agmtInit" name={name} onChange={handleChange}>
                            <option value="noinit">Do Not Initialize</option>
                            <option value="online-init">Do Online Initialization</option>
                        </select>
                    </Col>
                </Row>;
        }

        if (errMsg == "") {
            // To keep the modal nice and stable during input validation
            // We need text that is invisible to keep the modal input from
            // jumping around
            errMsgClass = "ds-center ds-clear-text";
            errMsg = "No errors";
        }

        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="md" />Creating winsync agreement ...
                    </div>
                </Row>;
        }

        let scheduleRow =
            <div className="ds-left-indent-md">
                <Row className="ds-margin-top-lg">
                    <Col sm={12}>
                        <i>Custom Synchronization Schedule</i>
                    </Col>
                </Row>
                <hr />
                <div className="ds-indent">
                    <Row>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncMon"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                title="Monday"
                                isChecked={agmtSyncMon}
                                label="Mon"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncWed"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Wednesday"
                                name={name}
                                isChecked={agmtSyncWed}
                                label="Wed"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncFri"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Friday"
                                name={name}
                                isChecked={agmtSyncFri}
                                label="Fri"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncSun"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Sunday"
                                name={name}
                                isChecked={agmtSyncSun}
                                label="Sun"
                            />
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncTue"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Tuesday"
                                name={name}
                                isChecked={agmtSyncTue}
                                label="Tue"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncThu"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Thursday"
                                name={name}
                                isChecked={agmtSyncThu}
                                label="Thu"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncSat"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Saturday"
                                name={name}
                                isChecked={agmtSyncSat}
                                label="Sat"
                            />
                        </Col>
                    </Row>
                </div>
                <Row className="ds-margin-top">
                    <Col sm={10}>
                        <p className="ds-modal-error">{errorScheduleMsg}</p>
                    </Col>
                </Row>
                <Row className="ds-margin-top" title="Time to start initiating replication sessions">
                    <Col componentClass={ControlLabel} sm={4}>
                        Replication Start Time
                    </Col>
                    <Col sm={8}>
                        <FormControl
                            id="agmtStartTime"
                            type="time"
                            name={name}
                            className={error.agmtStartTime ? "ds-input-bad" : ""}
                            onChange={handleChange}
                            defaultValue={agmtStartTime}
                        />
                    </Col>
                </Row>
                <Row className="ds-margin-top" title="Time to initiating replication sessions">
                    <Col componentClass={ControlLabel} sm={4}>
                        Replication End Time
                    </Col>
                    <Col sm={8}>
                        <FormControl
                            id="agmtEndTime"
                            type="time"
                            name={name}
                            className={error.agmtEndTime ? "ds-input-bad" : ""}
                            onChange={handleChange}
                            defaultValue={agmtEndTime}
                        />
                    </Col>
                </Row>
            </div>;

        if (agmtSync) {
            scheduleRow = "";
        }

        title = title + " Winsync Agreement";

        return (
            <Modal
                variant={ModalVariant.medium}
                aria-labelledby="ds-modal"
                title={title}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" isDisabled={saveDisabled} onClick={saveHandler}>
                        Save Agreement
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <Row className="ds-margin-top">
                        <Col sm={10}>
                            <p className={errMsgClass}>{errMsg}</p>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Agreement Name
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtName"
                                type="text"
                                name={name}
                                className={error.agmtName ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                defaultValue={agmtName}
                                disabled={this.props.edit}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Windows AD Host
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtHost"
                                type="text"
                                name={name}
                                className={error.agmtHost ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                defaultValue={agmtHost}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Windows AD Port
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtPort"
                                type="text"
                                name={name}
                                className={error.agmtPort ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                defaultValue={agmtPort}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Bind DN
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtBindDN"
                                type="text"
                                name={name}
                                className={error.agmtBindDN ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                autoComplete="false"
                                defaultValue={agmtBindDN}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Bind Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtBindPW"
                                type="password"
                                name={name}
                                className={error.agmtBindPW ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                autoComplete="new-password"
                                defaultValue={agmtBindPW}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Confirm Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtBindPWConfirm"
                                type="password"
                                name={name}
                                className={error.agmtBindPWConfirm ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                autoComplete="new-password"
                                defaultValue={agmtBindPWConfirm}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Windows Domain Name
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="agmtWinDomain"
                                name={name}
                                className={error.agmtWinDomain ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                defaultValue={agmtWinDomain}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The Active Directory subtree to synchronize">
                        <Col componentClass={ControlLabel} sm={4}>
                            Windows Subtree
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="agmtWinSubtree"
                                name={name}
                                className={error.agmtWinSubtree ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                defaultValue={agmtWinSubtree}
                                placeholder="e.g. cn=Users,dc=domain,dc=com"
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Directory Server subtree to synchronize">
                        <Col componentClass={ControlLabel} sm={4}>
                            DS Subtree
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="agmtDSSubtree"
                                name={name}
                                className={error.agmtDSSubtree ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                defaultValue={agmtDSSubtree}
                                placeholder="e.g. ou=People,dc=domain,dc=com"
                            />
                        </Col>
                    </Row>
                    {initRow}
                    <ExpandableSection
                        className="ds-margin-top-xlg"
                        toggleText={this.state.isExpanded ? 'Hide Advanced Settings' : 'Show Advanced Settings'}
                        onToggle={this.onToggle}
                        isExpanded={this.state.isExpanded}
                    >
                        <div className="ds-margin-left">
                            <Row className="ds-margin-top">
                                <Col componentClass={ControlLabel} sm={4}>
                                    Connection Protocol
                                </Col>
                                <Col sm={8}>
                                    <select className="btn btn-default dropdown" id="agmtProtocol" defaultValue={agmtProtocol} name={name} onChange={handleChange}>
                                        <option>LDAPS</option>
                                        <option title="Currently not recommended">StartTLS</option>
                                    </select>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col componentClass={ControlLabel} sm={4}>
                                    Synchronization Direction
                                </Col>
                                <Col sm={8}>
                                    <select className="btn btn-default dropdown" defaultValue={agmtOneWaySync} id="agmtOneWaySync" name={name} onChange={handleChange}>
                                        <option title="Synchronization in both directions (default behavior).">both</option>
                                        <option title="Only synchronize Directory Server updates to Windows.">toWindows</option>
                                        <option title="Only synchronize Windows updates to Directory Server.">fromWindows</option>
                                    </select>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The interval to check for updates on Windows.  Default is 300 seconds">
                                <Col componentClass={ControlLabel} sm={4}>
                                    Synchronization Interval
                                </Col>
                                <Col sm={8}>
                                    <FormControl
                                        type="text"
                                        id="agmtSyncInterval"
                                        name={name}
                                        className={error.agmtSyncInterval ? "ds-input-bad" : ""}
                                        onChange={handleChange}
                                        defaultValue={agmtSyncInterval}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Attribute to exclude from replication">
                                <Col componentClass={ControlLabel} sm={4}>
                                    Exclude Attributes
                                </Col>
                                <Col sm={8}>
                                    <Select
                                        variant={SelectVariant.typeaheadMulti}
                                        typeAheadAriaLabel="Type an attribute"
                                        onToggle={onSelectToggle}
                                        onSelect={(e, selection) => { handleFracChange(selection) }}
                                        onClear={onSelectClear}
                                        selections={agmtFracAttrs}
                                        isOpen={isExcludeAttrOpen}
                                        aria-labelledby="typeAhead-exclude-attrs"
                                        placeholderText="Start typing an attribute..."
                                        noResultsFoundText="There are no matching entries"
                                        >
                                        {availAttrs.map((attr, index) => (
                                            <SelectOption
                                                key={index}
                                                value={attr}
                                            />
                                            ))}
                                    </Select>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top-med">
                                <Col>
                                    <Checkbox
                                        id="agmtSyncGroups"
                                        onChange={(checked, e) => {
                                            handleChange(e);
                                        }}
                                        name={name}
                                        isChecked={agmtSyncGroups}
                                        label="Synchronize New Windows Groups"
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col>
                                    <Checkbox
                                        id="agmtSyncUsers"
                                        onChange={(checked, e) => {
                                            handleChange(e);
                                        }}
                                        name={name}
                                        isChecked={agmtSyncUsers}
                                        label="Synchronize New Windows Users"
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col>
                                    <Checkbox
                                        id="agmtSync"
                                        isChecked={agmtSync}
                                        onChange={(checked, e) => {
                                            handleChange(e);
                                        }}
                                        name={name}
                                        title="Always keep replication in synchronization, or use a specific schedule by unchecking the box."
                                        label="Keep Replication In Constant Synchronization"
                                    />
                                </Col>
                            </Row>
                            {scheduleRow}
                        </div>
                    </ExpandableSection>
                    {spinner}
                </Form>
            </Modal>
        );
    }
}

export class ReplAgmtModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            isExpanded: false,
        };

        this.onToggle = (isExpanded) => {
            this.setState({
                isExpanded
            });
        };
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    render() {
        const {
            showModal,
            closeHandler,
            saveHandler,
            handleChange,
            handleStripChange,
            handleFracChange,
            handleFracInitChange,
            onExcludeAttrsToggle,
            onExcludeAttrsClear,
            onExcludeAttrsInitToggle,
            onExcludeAttrsInitClear,
            onStripAttrsToggle,
            onStripAttrsClear,
            isExcludeAttrsOpen,
            isExcludeInitAttrsOpen,
            isStripAttrsOpen,
            spinning,
            agmtName,
            agmtHost,
            agmtPort,
            agmtProtocol,
            agmtBindMethod,
            agmtBindDN,
            agmtBindPW,
            agmtBindPWConfirm,
            agmtBootstrap,
            agmtBootstrapBindDN,
            agmtBootstrapBindPW,
            agmtBootstrapBindPWConfirm,
            agmtBootstrapProtocol,
            agmtBootstrapBindMethod,
            agmtStripAttrs,
            agmtFracAttrs,
            agmtFracInitAttrs,
            agmtSync,
            agmtSyncMon,
            agmtSyncTue,
            agmtSyncWed,
            agmtSyncThu,
            agmtSyncFri,
            agmtSyncSat,
            agmtSyncSun,
            agmtStartTime,
            agmtEndTime,
            availAttrs,
            error,
            errorMsg,
            errorScheduleMsg,
        } = this.props;
        let spinner = "";
        let saveDisabled = !this.props.saveOK;
        let title = "Create";
        let initRow = "";
        let errMsgClass = "ds-center ds-modal-error";
        let errMsg = errorMsg;
        let name = "agmt-modal";
        let bootstrapTitle = "If you are using Bind Group's on the consumer " +
            "replica you can configure bootstrap credentials that can be used " +
            "to do online initializations, or bootstrap a session if the bind " +
            "groups get out of synchronization";

        if (this.props.edit) {
            title = "Edit";
            name = "agmt-modal-edit";
        } else {
            initRow =
                <Row className="ds-margin-top">
                    <Col componentClass={ControlLabel} sm={4}>
                        Consumer Initialization
                    </Col>
                    <Col sm={8}>
                        <select className="btn btn-default dropdown" id="agmtInit" name={name} onChange={handleChange}>
                            <option value="noinit">Do Not Initialize</option>
                            <option value="online-init">Do Online Initialization</option>
                        </select>
                    </Col>
                </Row>;
        }

        if (errMsg == "") {
            // To keep the modal nice and stable during input validation
            // We need text that is invisible to keep the modal input from
            // jumping around
            errMsgClass = "ds-center ds-clear-text";
            errMsg = "No errors";
        }

        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="md" />Creating replication agreement ...
                    </div>
                </Row>;
        }

        let bootstrapRow =
            <div className="ds-left-indent-md">
                <Row className="ds-margin-top-lg" title="The Bind DN the agreement can use to bootstrap initialization">
                    <Col componentClass={ControlLabel} sm={4}>
                        Bind DN
                    </Col>
                    <Col sm={8}>
                        <FormControl
                            id="agmtBootstrapBindDN"
                            type="text"
                            name={name}
                            className={error.agmtBootstrapBindDN ? "ds-input-bad" : ""}
                            onChange={handleChange}
                            autoComplete="false"
                            defaultValue={agmtBootstrapBindDN}
                        />
                    </Col>
                </Row>
                <Row className="ds-margin-top">
                    <Col componentClass={ControlLabel} sm={4} title="The Bind DN password for bootstrap initialization">
                        Password
                    </Col>
                    <Col sm={8}>
                        <FormControl
                            id="agmtBootstrapBindPW"
                            type="password"
                            name={name}
                            className={error.agmtBootstrapBindPW ? "ds-input-bad" : ""}
                            onChange={handleChange}
                            autoComplete="new-password"
                            defaultValue={agmtBootstrapBindPW}
                        />
                    </Col>
                </Row>
                <Row className="ds-margin-top">
                    <Col componentClass={ControlLabel} sm={4} title="Confirm the Bind DN password for bootstrap initialization">
                        Confirm Password
                    </Col>
                    <Col sm={8}>
                        <FormControl
                            id="agmtBootstrapBindPWConfirm"
                            type="password"
                            name={name}
                            className={error.agmtBootstrapBindPWConfirm ? "ds-input-bad" : ""}
                            onChange={handleChange}
                            autoComplete="new-password"
                            defaultValue={agmtBootstrapBindPWConfirm}
                        />
                    </Col>
                </Row>
                <Row className="ds-margin-top">
                    <Col componentClass={ControlLabel} sm={4} title="The connection protocol for bootstrap initialization">
                        Connection Protocol
                    </Col>
                    <Col sm={8}>
                        <select className={error.agmtBootstrapProtocol ? "btn btn-default dropdown ds-input-bad" : "btn btn-default dropdown"}
                            id="agmtBootstrapProtocol"
                            defaultValue={agmtBootstrapProtocol}
                            name={name}
                            onChange={handleChange}
                        >
                            <option>LDAP</option>
                            <option>LDAPS</option>
                            <option title="Currently not recommended">StartTLS</option>
                        </select>
                    </Col>
                </Row>
                <Row className="ds-margin-top">
                    <Col componentClass={ControlLabel} sm={4} title="The authentication method for bootstrap initialization">
                        Authentication Method
                    </Col>
                    <Col sm={8}>
                        <select className={error.agmtBootstrapBindMethod ? "btn btn-default dropdown ds-input-bad" : "btn btn-default dropdown"}
                            defaultValue={agmtBootstrapBindMethod}
                            id="agmtBootstrapBindMethod"
                            name={name}
                            onChange={handleChange}
                        >
                            <option title="Use a bind DN and password">SIMPLE</option>
                            <option title="Use a SSL/TLS Client Certificate">SSLCLIENTAUTH</option>
                        </select>
                    </Col>
                </Row>
            </div>;

        let scheduleRow =
            <div className="ds-left-indent-md">
                <Row className="ds-margin-top-lg">
                    <Col sm={12}>
                        <i>Custom Synchronization Schedule</i>
                    </Col>
                </Row>
                <hr />
                <div className="ds-indent">
                    <Row>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncMon"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                title="Monday"
                                isChecked={agmtSyncMon}
                                label="Mon"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncWed"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Wednesday"
                                name={name}
                                isChecked={agmtSyncWed}
                                label="Wed"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncFri"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Friday"
                                name={name}
                                isChecked={agmtSyncFri}
                                label="Fri"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncSun"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Sunday"
                                name={name}
                                isChecked={agmtSyncSun}
                                label="Sun"
                            />
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncTue"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Tuesday"
                                name={name}
                                isChecked={agmtSyncTue}
                                label="Tue"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncThu"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Thursday"
                                name={name}
                                isChecked={agmtSyncThu}
                                label="Thu"
                            />
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncSat"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                title="Saturday"
                                name={name}
                                isChecked={agmtSyncSat}
                                label="Sat"
                            />
                        </Col>
                    </Row>
                </div>
                <Row className="ds-margin-top">
                    <Col sm={10}>
                        <p className="ds-modal-error">{errorScheduleMsg}</p>
                    </Col>
                </Row>
                <Row className="ds-margin-top" title="Time to start initiating replication sessions">
                    <Col componentClass={ControlLabel} sm={4}>
                        Replication Start Time
                    </Col>
                    <Col sm={8}>
                        <FormControl
                            id="agmtStartTime"
                            type="time"
                            name={name}
                            className={error.agmtStartTime ? "ds-input-bad" : ""}
                            onChange={handleChange}
                            defaultValue={agmtStartTime}
                        />
                    </Col>
                </Row>
                <Row className="ds-margin-top" title="Time to initiating replication sessions">
                    <Col componentClass={ControlLabel} sm={4}>
                        Replication End Time
                    </Col>
                    <Col sm={8}>
                        <FormControl
                            id="agmtEndTime"
                            type="time"
                            name={name}
                            className={error.agmtEndTime ? "ds-input-bad" : ""}
                            onChange={handleChange}
                            defaultValue={agmtEndTime}
                        />
                    </Col>
                </Row>
            </div>;

        if (agmtSync) {
            scheduleRow = "";
        }
        if (!agmtBootstrap) {
            bootstrapRow = "";
        }

        title = title + " Replication Agreement";
        return (
            <Modal
                variant={ModalVariant.medium}
                title={title}
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        isDisabled={saveDisabled}
                        onClick={saveHandler}
                    >
                        Save Agreement
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <Row className="ds-margin-top">
                        <Col sm={10}>
                            <p className={errMsgClass}>{errMsg}</p>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Agreement Name
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtName"
                                type="text"
                                name={name}
                                className={error.agmtName ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                defaultValue={agmtName}
                                disabled={this.props.edit}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Consumer Host
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtHost"
                                type="text"
                                name={name}
                                className={error.agmtHost ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                defaultValue={agmtHost}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Consumer Port
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtPort"
                                type="text"
                                name={name}
                                className={error.agmtPort ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                defaultValue={agmtPort}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Bind DN
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtBindDN"
                                type="text"
                                name={name}
                                className={error.agmtBindDN ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                autoComplete="false"
                                defaultValue={agmtBindDN}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Bind Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtBindPW"
                                type="password"
                                name={name}
                                className={error.agmtBindPW ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                autoComplete="new-password"
                                defaultValue={agmtBindPW}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Confirm Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="agmtBindPWConfirm"
                                type="password"
                                name={name}
                                className={error.agmtBindPWConfirm ? "ds-input-bad" : ""}
                                onChange={handleChange}
                                autoComplete="new-password"
                                defaultValue={agmtBindPWConfirm}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Connection Protocol
                        </Col>
                        <Col sm={8}>
                            <select className={error.agmtProtocol ? "btn btn-default dropdown ds-input-bad" : "btn btn-default dropdown"}
                                id="agmtProtocol"
                                defaultValue={agmtProtocol}
                                name={name}
                                onChange={handleChange}
                            >
                                <option>LDAP</option>
                                <option>LDAPS</option>
                                <option title="Currently not recommended">StartTLS</option>
                            </select>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={4}>
                            Authentication Method
                        </Col>
                        <Col sm={8}>
                            <select className={error.agmtBindMethod ? "btn btn-default dropdown ds-input-bad" : "btn btn-default dropdown"}
                                defaultValue={agmtBindMethod}
                                id="agmtBindMethod"
                                name={name}
                                onChange={handleChange}
                            >
                                <option title="Use bind DN and password">SIMPLE</option>
                                <option title="Use SSL Client Certificate">SSLCLIENTAUTH</option>
                                <option title="Use SASL Digest-MD5">SASL/DIGEST-MD5</option>
                                <option title="Use SASL GSSAPI">SASL/GSSAPI</option>
                            </select>
                        </Col>
                    </Row>
                    {initRow}
                    <ExpandableSection
                        className="ds-margin-top-lg"
                        toggleText={this.state.isExpanded ? 'Hide Advanced Settings' : 'Show Advanced Settings'}
                        onToggle={this.onToggle}
                        isExpanded={this.state.isExpanded}
                    >
                        <div className="ds-margin-left">
                            <Row className="ds-margin-top-lg" title="Attribute to exclude from replication">
                                <Col componentClass={ControlLabel} sm={4}>
                                    Exclude Attributes
                                </Col>
                                <Col sm={8}>
                                    <Select
                                        variant={SelectVariant.typeaheadMulti}
                                        typeAheadAriaLabel="Type an attribute"
                                        onToggle={onExcludeAttrsToggle}
                                        onSelect={(e, selection) => { handleFracChange(selection) }}
                                        onClear={onExcludeAttrsClear}
                                        selections={agmtFracAttrs}
                                        isOpen={isExcludeAttrsOpen}
                                        aria-labelledby="typeAhead-exclude-attrs"
                                        placeholderText="Start typing an attribute..."
                                        >
                                        {availAttrs.map((attr, index) => (
                                            <SelectOption
                                                key={index}
                                                value={attr}
                                            />
                                            ))}
                                    </Select>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Attribute to exclude from replica Initializations">
                                <Col componentClass={ControlLabel} sm={4}>
                                    Exclude Init Attributes
                                </Col>
                                <Col sm={8}>
                                    <Select
                                        variant={SelectVariant.typeaheadMulti}
                                        typeAheadAriaLabel="Type an attribute"
                                        onToggle={onExcludeAttrsInitToggle}
                                        onSelect={(e, selection) => { handleFracInitChange(selection) }}
                                        onClear={onExcludeAttrsInitClear}
                                        selections={agmtFracInitAttrs}
                                        isOpen={isExcludeInitAttrsOpen}
                                        aria-labelledby="typeAhead-exclude-init-attrs"
                                        placeholderText="Start typing an attribute..."
                                        noResultsFoundText="There are no matching entries"
                                        >
                                        {availAttrs.map((attr, index) => (
                                            <SelectOption
                                                key={index}
                                                value={attr}
                                            />
                                            ))}
                                    </Select>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Attributes to strip from a replicatio<Selectn update">
                                <Col componentClass={ControlLabel} sm={4}>
                                    Strip Attributes
                                </Col>
                                <Col sm={8}>
                                    <Select
                                        variant={SelectVariant.typeaheadMulti}
                                        typeAheadAriaLabel="Type an attribute"
                                        onToggle={onStripAttrsToggle}
                                        onSelect={(e, selection) => { handleStripChange(selection) }}
                                        onClear={onStripAttrsClear}
                                        selections={agmtStripAttrs}
                                        isOpen={isStripAttrsOpen}
                                        aria-labelledby="typeAhead-strip-attrs"
                                        placeholderText="Start typing an attribute..."
                                        noResultsFoundText="There are no matching entries"
                                        >
                                        {availAttrs.map((attr, index) => (
                                            <SelectOption
                                                key={index}
                                                value={attr}
                                            />
                                            ))}
                                    </Select>
                                </Col>
                            </Row>
                            <hr />
                            <Row className="ds-margin-top-med">
                                <Col sm={8}>
                                    <Checkbox
                                        id="agmtBootstrap"
                                        isChecked={agmtBootstrap}
                                        onChange={(checked, e) => {
                                            handleChange(e);
                                        }}
                                        name={name}
                                        title={bootstrapTitle}
                                        label="Configure Bootstrap Settings"
                                    />
                                </Col>
                            </Row>
                            {bootstrapRow}
                            <hr />
                            <Row className="ds-margin-top-med">
                                <Col sm={8}>
                                    <Checkbox
                                        id="agmtSync"
                                        isChecked={agmtSync}
                                        onChange={(checked, e) => {
                                            handleChange(e);
                                        }}
                                        name={name}
                                        title="Always keep replication in synchronization, or use a specific schedule by unchecking the box."
                                        label="Keep Replication In Constant Synchronization"
                                    />
                                </Col>
                            </Row>
                            {scheduleRow}
                        </div>
                    </ExpandableSection>
                    {spinner}
                </Form>
            </Modal>
        );
    }
}

export class ChangeReplRoleModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            showConfirmPromote: false,
            showConfirmDemote: false,
        };
    }

    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            role,
            spinning,
            checked,
        } = this.props;
        let spinner = "";
        let changeType = "";
        let roleOptions = [];
        let ridRow = "";
        let newRole = this.props.newRole;
        let saveDisabled = !checked;

        // Set the change type
        if (role == "Supplier") {
            changeType = "Demoting";
            roleOptions = ["Hub", "Consumer"];
        } else if (role == "Consumer") {
            changeType = "Promoting";
            roleOptions = ["Supplier", "Hub"];
        } else {
            // Hub
            if (newRole == "Supplier") {
                changeType = "Promoting";
            } else {
                changeType = "Demoting";
            }
            roleOptions = ["Supplier", "Consumer"];
        }
        if (newRole == "Supplier") {
            ridRow =
                <Row className="ds-margin-top-lg" title="Supplier Replica Identifier.  This must be unique across all the Supplier replicas in your environment">
                    <Col componentClass={ControlLabel} sm={2}>
                        Replica ID
                    </Col>
                    <Col sm={4}>
                        <input id="newRID" type="number" min="1" max="65534"
                            onChange={handleChange} defaultValue="1" size="10"
                        />
                    </Col>
                </Row>;
        }

        let selectOptions = roleOptions.map((role) =>
            <option key={role} value={role}>{role}</option>
        );

        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="md" />{changeType} replica ...
                    </div>
                </Row>;
            saveDisabled = true;
        }

        return (
            <Modal
                variant={ModalVariant.small}
                title="Change Replica Role"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="change"
                        variant="primary"
                        onClick={() => {
                            saveHandler(changeType);
                        }}
                        isDisabled={saveDisabled}
                    >
                        Change Role
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <h4>Please choose the new replication role you would like for this suffix</h4>
                    <Row className="ds-margin-top-lg">
                        <Col componentClass={ControlLabel} sm={3}>
                            New Role
                        </Col>
                        <Col sm={4}>
                            <select id="newRole" onChange={handleChange}>
                                {selectOptions}
                            </select>
                        </Col>
                    </Row>
                    {ridRow}
                    <Row className="ds-margin-top-xlg">
                        <Col sm={12} className="ds-center">
                            <Checkbox
                                id="modalChecked"
                                isChecked={checked}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                label={<><b>Yes</b>&nbsp;,I am sure.</>}
                            />
                        </Col>
                    </Row>
                    {spinner}
                </Form>
            </Modal>
        );
    }
}

export class AddManagerModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            spinning,
            error
        } = this.props;
        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="md" />Adding Replication Manager...
                    </div>
                </Row>;
        }

        return (
            <Modal
                variant={ModalVariant.small}
                title="Add Replication Manager"
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={saveHandler}>
                        Add Replication Manager
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <h5>
                        Create a Replication Manager entry, and add it to the replication configuration
                        for this suffix.  If the entry already exists it will be overwritten with
                        the new credentials.
                    </h5>
                    <Row className="ds-margin-top-lg" title="The DN of the replication manager">
                        <Col componentClass={ControlLabel} sm={4}>
                            Replication Manager DN
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="manager"
                                defaultValue="cn=replication manager,cn=config"
                                className={error.manager ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Replication Manager password">
                        <Col componentClass={ControlLabel} sm={4}>
                            Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="password"
                                id="manager_passwd"
                                className={error.manager_passwd ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Replication Manager password">
                        <Col componentClass={ControlLabel} sm={4}>
                            Confirm Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="password"
                                id="manager_passwd_confirm"
                                className={error.manager_passwd_confirm ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    {spinner}
                </Form>
            </Modal>
        );
    }
}

export class EnableReplModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            spinning,
            role,
            error
        } = this.props;
        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="md" />Enabling Replication ...
                    </div>
                </Row>;
        }

        let replicaIDRow = "";
        if (role == "Supplier") {
            replicaIDRow =
                <Row className="ds-margin-top">
                    <Col sm={3} componentClass={ControlLabel}>
                        Replica ID
                    </Col>
                    <Col sm={9}>
                        <input id="enableRID" type="number" min="1" max="65534"
                            onChange={handleChange} defaultValue="1" size="10"
                        />
                    </Col>
                </Row>;
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Enable Replication"
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="enable"
                        variant="primary"
                        onClick={saveHandler}
                        isDisabled={this.props.disabled}
                    >
                        Enable Replication
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <h5>
                        Choose the replication role for this suffix.  If it
                        is a Supplier replica then you must pick a unique ID
                        to identify it among the other Supplier replicas in your
                        environment.  The replication changelog will also
                        automatically be created if it does not exist.
                    </h5>
                    <hr />
                    <Row className="ds-margin-top-lg">
                        <Col sm={3} componentClass={ControlLabel}>
                            Replication Role
                        </Col>
                        <Col sm={9}>
                            <select className="btn btn-default dropdown" id="enableRole" defaultValue="Supplier" onChange={handleChange}>
                                <option>Supplier</option>
                                <option>Hub</option>
                                <option>Consumer</option>
                            </select>
                        </Col>
                    </Row>
                    {replicaIDRow}
                    <h5 className="ds-margin-top-xxlg">
                        You can optionally define the authentication information
                        for this replicated suffix.  Either a Manager DN and Password,
                        a Bind Group DN, or both, can be provided.  The Manager DN should
                        be an entry under "cn=config" and if it does not exist it will
                        be created, while the Bind Group DN is usually an existing
                        group located in the database suffix.  Typically, just the
                        Manager DN and Password are used when enabling replication
                        for a suffix.
                    </h5>
                    <hr />
                    <Row className="ds-margin-top-lg" title="The DN of the replication manager.  If you supply a password the entry will be created in the server (it will also overwrite the entry is it already exists).">
                        <Col componentClass={ControlLabel} sm={4}>
                            Replication Manager DN
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="enableBindDN"
                                defaultValue="cn=replication manager,cn=config"
                                className={error.enableBindDN ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Replication Manager password">
                        <Col componentClass={ControlLabel} sm={4}>
                            Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="password"
                                id="enableBindPW"
                                className={error.enableBindPW ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Confirm the Replication Manager password">
                        <Col componentClass={ControlLabel} sm={4}>
                            Confirm Password
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="password"
                                id="enableBindPWConfirm"
                                className={error.enableBindPWConfirm ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    <hr />
                    <Row className="ds-margin-top" title="The DN of a group that contains users that can perform replication updates">
                        <Col componentClass={ControlLabel} sm={4}>
                            Bind Group DN
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="enableBindGroupDN"
                                className={error.enableBindGroupDN ? "ds-input-auto-bad" : "ds-input-auto"}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    {spinner}
                </Form>
            </Modal>
        );
    }
}

export class ExportModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            spinning,
            saveOK
        } = this.props;
        let spinner = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="lg" />Exporting database... <font size="2">(You can safely close this window)</font>
                    </div>
                </Row>;
        }

        return (
            <Modal
                variant={ModalVariant.small}
                title="Create Replication Initialization LDIF File"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="export"
                        variant="primary"
                        onClick={saveHandler}
                        isDisabled={!saveOK}
                    >
                        Export Replica
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <Row>
                        <Col sm={11} className="ds-left-indent-md">
                            <p>Enter the name of the LDIF file, do not use a path as the file will only be written to the server's LDIF directory</p>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top-lg" title="Name of the exported LDIF file">
                        <Col componentClass={ControlLabel} sm={3}>
                            <b>LDIF Name</b>
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                type="text"
                                id="ldifLocation"
                                className={saveOK ? "" : "ds-input-bad"}
                                onChange={handleChange}
                            />
                        </Col>
                    </Row>
                    {spinner}
                </Form>
            </Modal>
        );
    }
}

export class ExportCLModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            default: true,
            debug: false,
        };
    }

    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            handleLDIFChange,
            handleRadioChange,
            saveHandler,
            spinning,
            defaultCL,
            debugCL,
            decodeCL,
            exportCSN,
            ldifFile,
            saveOK
        } = this.props;
        let spinner = "";
        let page = "";
        if (spinning) {
            spinner =
                <Row>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="lg" />Exporting Replication Change Log... <font size="2">(You can safely close this window)</font>
                    </div>
                </Row>;
        }

        if (defaultCL) {
            page =
                <h5>
                    This will export the changelog to the server's LDIF directory.  This
                    is the only LDIF file that can be imported into the server for enabling
                    changelog encryption.  Do not edit or rename the file.
                </h5>;
        } else {
            page =
                <div className="ds-margin-left">
                    <Row className="ds-margin-top-lg">
                        <Col sm={12}>
                            <h5>
                                The LDIF file that is generated should <b>not</b> be used
                                to initialize the Replication Changelog.  It is only
                                meant for debugging/investigative purposes.
                            </h5>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top-xlg">
                        <Col componentClass={ControlLabel} sm={2}>
                            LDIF File
                        </Col>
                        <Col sm={10}>
                            <FormControl
                                id="ldifFile"
                                value={ldifFile}
                                onChange={handleLDIFChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top-xlg ds-margin-left">
                        <Checkbox
                            id="decodeCL"
                            isChecked={decodeCL}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                            label="Decode base64 changes"
                        />
                    </Row>
                    <Row className="ds-margin-top ds-margin-left">
                        <Checkbox
                            id="exportCSN"
                            isChecked={exportCSN}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                            label="Only Export CSN's"
                        />
                    </Row>
                </div>;
        }

        return (
            <Modal
                variant={ModalVariant.small}
                title="Create Replication Change Log LDIF File"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="export"
                        variant="primary"
                        onClick={saveHandler}
                        isDisabled={!saveOK}
                    >
                        Export Changelog
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <Row className="ds-indent">
                        <Radio
                            name="radioGroup"
                            id="defaultCL"
                            onChange={handleRadioChange}
                            checked={defaultCL} inline
                        >
                            Export to LDIF For Reinitializing The Changelog
                        </Radio>
                    </Row>
                    <Row className="ds-indent">
                        <Radio
                            name="radioGroup"
                            id="debugCL"
                            onChange={handleRadioChange}
                            checked={debugCL} inline
                        >
                            Export to LDIF For Debugging
                        </Radio>
                    </Row>
                    <hr />
                    {page}
                    {spinner}
                </Form>
            </Modal>
        );
    }
}

EnableReplModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    disabled: PropTypes.bool,
    error: PropTypes.object,
};

EnableReplModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    spinning: false,
    disabled: false,
    error: {},
};

AddManagerModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    error: PropTypes.object,
};

AddManagerModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    spinning: false,
    error: {},
};

ChangeReplRoleModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    role: PropTypes.string,
    newRole: PropTypes.string,
};

ChangeReplRoleModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    spinning: false,
    role: "",
    newRole: "",
};

ReplAgmtModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    handleStripChange: PropTypes.func,
    handleFracChange: PropTypes.func,
    handleFracInitChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    availAttrs: PropTypes.array,
    agmtName: PropTypes.string,
    agmtHost: PropTypes.string,
    agmtPort: PropTypes.string,
    agmtProtocol: PropTypes.string,
    agmtBindMethod: PropTypes.string,
    agmtBindDN: PropTypes.string,
    agmtBindPW: PropTypes.string,
    agmtBindPWConfirm: PropTypes.string,
    agmtBootstrap: PropTypes.bool,
    agmtBootstrapProtocol: PropTypes.string,
    agmtBootstrapBindMethod: PropTypes.string,
    agmtBootstrapBindDN: PropTypes.string,
    agmtBootstrapBindPW: PropTypes.string,
    agmtBootstrapBindPWConfirm: PropTypes.string,
    agmtStripAttrs: PropTypes.array,
    agmtFracAttrs: PropTypes.array,
    agmtFracInitAttrs: PropTypes.array,
    agmtSync: PropTypes.bool,
    agmtSyncMon: PropTypes.bool,
    agmtSyncTue: PropTypes.bool,
    agmtSyncWed: PropTypes.bool,
    agmtSyncThu: PropTypes.bool,
    agmtSyncFri: PropTypes.bool,
    agmtSyncSat: PropTypes.bool,
    agmtSyncSun: PropTypes.bool,
    agmtStartTime: PropTypes.string,
    agmtEndTime: PropTypes.string,
    saveOK: PropTypes.bool,
    error: PropTypes.object,
    errorMsg: PropTypes.string,
    edit: PropTypes.bool,
};

ReplAgmtModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    handleStripChange: noop,
    handleFracChange: noop,
    handleFracInitChange: noop,
    saveHandler: noop,
    spinning: false,
    availAttrs: [],
    agmtName: "",
    agmtHost: "",
    agmtPort: "",
    agmtProtocol: "LDAP",
    agmtBindMethod: "SIMPLE",
    agmtBindDN: "",
    agmtBindPW: "",
    agmtBindPWConfirm: "",
    agmtBootstrap: false,
    agmtBootstrapProtocol: "LDAP",
    agmtBootstrapBindMethod: "SIMPLE",
    agmtBootstrapBindDN: "",
    agmtBootstrapBindPW: "",
    agmtBootstrapBindPWConfirm: "",
    agmtStripAttrs: [],
    agmtFracAttrs: [],
    agmtFracInitAttrs: [],
    agmtSync: true,
    agmtSyncMon: false,
    agmtSyncTue: false,
    agmtSyncWed: false,
    agmtSyncThu: false,
    agmtSyncFri: false,
    agmtSyncSat: false,
    agmtSyncSun: false,
    agmtStartTime: "00:00",
    agmtEndTime: "23:59",
    saveOK: false,
    error: {},
    errorMsg: "",
    edit: false,
};

WinsyncAgmtModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    handleFracChange: PropTypes.func,
    saveHandler: PropTypes.func,
    spinning: PropTypes.bool,
    availAttrs: PropTypes.array,
    agmtName: PropTypes.string,
    agmtHost: PropTypes.string,
    agmtPort: PropTypes.string,
    agmtProtocol: PropTypes.string,
    agmtBindDN: PropTypes.string,
    agmtBindPW: PropTypes.string,
    agmtBindPWConfirm: PropTypes.string,
    agmtFracAttrs: PropTypes.array,
    agmtSync: PropTypes.bool,
    agmtSyncMon: PropTypes.bool,
    agmtSyncTue: PropTypes.bool,
    agmtSyncWed: PropTypes.bool,
    agmtSyncThu: PropTypes.bool,
    agmtSyncFri: PropTypes.bool,
    agmtSyncSat: PropTypes.bool,
    agmtSyncSun: PropTypes.bool,
    agmtStartTime: PropTypes.string,
    agmtEndTime: PropTypes.string,
    agmtSyncGroups: PropTypes.bool,
    agmtSyncUsers: PropTypes.bool,
    agmtWinDomain: PropTypes.string,
    agmtWinSubtree: PropTypes.string,
    agmtDSSubtree: PropTypes.string,
    agmtOneWaySync: PropTypes.string,
    agmtSyncInterval: PropTypes.string,
    saveOK: PropTypes.bool,
    error: PropTypes.object,
    errorMsg: PropTypes.string,
    edit: PropTypes.bool,
};

WinsyncAgmtModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    handleFracChange: noop,
    saveHandler: noop,
    spinning: false,
    availAttrs: [],
    agmtName: "",
    agmtHost: "",
    agmtPort: "",
    agmtProtocol: "LDAPS",
    agmtBindDN: "",
    agmtBindPW: "",
    agmtBindPWConfirm: "",
    agmtFracAttrs: [],
    agmtSync: true,
    agmtSyncMon: false,
    agmtSyncTue: false,
    agmtSyncWed: false,
    agmtSyncThu: false,
    agmtSyncFri: false,
    agmtSyncSat: false,
    agmtSyncSun: false,
    agmtStartTime: "00:00",
    agmtEndTime: "23:59",
    agmtSyncGroups: false,
    agmtSyncUsers: false,
    agmtWinDomain: "",
    agmtWinSubtree: "",
    agmtDSSubtree: "",
    agmtOneWaySync: "both", // "both", "toWindows", "fromWindows"
    agmtSyncInterval: "",
    saveOK: false,
    error: {},
    errorMsg: "",
    edit: false,
};

ExportModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    saveOK: PropTypes.bool,
    spinning: PropTypes.bool
};

ExportModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    saveOK: false,
    spinning: false
};
