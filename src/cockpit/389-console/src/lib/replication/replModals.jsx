import React from "react";
import {
    Button,
    Row,
    Checkbox,
    Col,
    ControlLabel,
    Form,
    FormControl,
    Icon,
    Modal,
    noop,
    Spinner,
} from "patternfly-react";
import PropTypes from "prop-types";
import CustomCollapse from "../customCollapse.jsx";
import { Typeahead } from "react-bootstrap-typeahead";
import "../../css/ds.css";

export class WinsyncAgmtModal extends React.Component {
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
                        <Spinner loading inline size="md" />Creating winsync agreement ...
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
                                onChange={handleChange}
                                name={name}
                                title="Monday"
                                defaultChecked={agmtSyncMon}
                            >
                                Mon
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncWed"
                                onChange={handleChange}
                                title="Wednesday"
                                name={name}
                                defaultChecked={agmtSyncWed}
                            >
                                Wed
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncFri"
                                onChange={handleChange}
                                title="Friday"
                                name={name}
                                defaultChecked={agmtSyncFri}
                            >
                                Fri
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncSun"
                                onChange={handleChange}
                                title="Sunday"
                                name={name}
                                defaultChecked={agmtSyncSun}
                            >
                                Sun
                            </Checkbox>
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncTue"
                                onChange={handleChange}
                                title="Tuesday"
                                name={name}
                                defaultChecked={agmtSyncTue}
                            >
                                Tue
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncThu"
                                onChange={handleChange}
                                title="Thursday"
                                name={name}
                                defaultChecked={agmtSyncThu}
                            >
                                Thu
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncSat"
                                onChange={handleChange}
                                title="Saturday"
                                name={name}
                                defaultChecked={agmtSyncSat}
                            >
                                Sat
                            </Checkbox>
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
        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            {title} Winsync Agreement
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
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
                            <CustomCollapse
                                className="ds-margin-top"
                                textOpened="Hide Advanced Settings"
                                textClosed="Show Advanced Setting"
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
                                            <Typeahead
                                                multiple
                                                onChange={handleFracChange}
                                                selected={agmtFracAttrs}
                                                options={availAttrs}
                                                name={name}
                                                newSelectionPrefix="Add a attribute: "
                                                placeholder="Start typing an attribute..."
                                                id="agmtFracAttrs"
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top-med">
                                        <Col>
                                            <Checkbox
                                                id="agmtSyncGroups"
                                                onChange={handleChange}
                                                name={name}
                                                defaultChecked={agmtSyncGroups}
                                            >
                                                Synchronize New Windows Groups
                                            </Checkbox>
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col>
                                            <Checkbox
                                                id="agmtSyncUsers"
                                                onChange={handleChange}
                                                name={name}
                                                defaultChecked={agmtSyncUsers}
                                            >
                                                Synchronize New Windows Users
                                            </Checkbox>
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top">
                                        <Col>
                                            <Checkbox
                                                id="agmtSync"
                                                defaultChecked={agmtSync}
                                                onChange={handleChange}
                                                name={name}
                                                title="Always keep replication in synchronization, or use a specific schedule by unchecking the box."
                                            >
                                                Keep Replication In Constant Synchronization
                                            </Checkbox>
                                        </Col>
                                    </Row>
                                    {scheduleRow}
                                </div>
                            </CustomCollapse>
                            {spinner}
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                            disabled={saveDisabled}
                        >
                            Save Agreement
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

export class ReplAgmtModal extends React.Component {
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
            spinning,
            agmtName,
            agmtHost,
            agmtPort,
            agmtProtocol,
            agmtBindMethod,
            agmtBindDN,
            agmtBindPW,
            agmtBindPWConfirm,
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
                        <Spinner loading inline size="md" />Creating replication agreement ...
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
                                onChange={handleChange}
                                name={name}
                                title="Monday"
                                defaultChecked={agmtSyncMon}
                            >
                                Mon
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncWed"
                                onChange={handleChange}
                                title="Wednesday"
                                name={name}
                                defaultChecked={agmtSyncWed}
                            >
                                Wed
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncFri"
                                onChange={handleChange}
                                title="Friday"
                                name={name}
                                defaultChecked={agmtSyncFri}
                            >
                                Fri
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncSun"
                                onChange={handleChange}
                                title="Sunday"
                                name={name}
                                defaultChecked={agmtSyncSun}
                            >
                                Sun
                            </Checkbox>
                        </Col>
                    </Row>
                    <Row>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncTue"
                                onChange={handleChange}
                                title="Tuesday"
                                name={name}
                                defaultChecked={agmtSyncTue}
                            >
                                Tue
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncThu"
                                onChange={handleChange}
                                title="Thursday"
                                name={name}
                                defaultChecked={agmtSyncThu}
                            >
                                Thu
                            </Checkbox>
                        </Col>
                        <Col sm={3}>
                            <Checkbox
                                id="agmtSyncSat"
                                onChange={handleChange}
                                title="Saturday"
                                name={name}
                                defaultChecked={agmtSyncSat}
                            >
                                Sat
                            </Checkbox>
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
                        />                    </Col>
                </Row>
            </div>;

        if (agmtSync) {
            scheduleRow = "";
        }
        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            {title} Replication Agreement
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
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
                                    <select className="btn btn-default dropdown" id="agmtProtocol" defaultValue={agmtProtocol} name={name} onChange={handleChange}>
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
                                    <select className="btn btn-default dropdown" defaultValue={agmtBindMethod} id="agmtBindMethod" name={name} onChange={handleChange}>
                                        <option title="Use bind DN and password">SIMPLE</option>
                                        <option title="Use SSL Client Certificate">SSLCLIENTAUTH</option>
                                        <option title="Use SASL Digest-MD5">SASL/DIGEST-MD5</option>
                                        <option title="Use SASL GSSAPI">SASL/GSSAPI</option>
                                    </select>
                                </Col>
                            </Row>
                            {initRow}
                            <CustomCollapse
                                className="ds-margin-top"
                                textOpened="Hide Advanced Settings"
                                textClosed="Show Advanced Settings"
                            >
                                <div className="ds-margin-left">
                                    <Row className="ds-margin-top" title="Attribute to exclude from replication">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Exclude Attributes
                                        </Col>
                                        <Col sm={8}>
                                            <Typeahead
                                                multiple
                                                onChange={handleFracChange}
                                                selected={agmtFracAttrs}
                                                options={availAttrs}
                                                name={name}
                                                newSelectionPrefix="Add a attribute: "
                                                placeholder="Start typing an attribute..."
                                                id="agmtFracAttrs"
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="Attribute to exclude from replica Initializations">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Exclude Init Attributes
                                        </Col>
                                        <Col sm={8}>
                                            <Typeahead
                                                multiple
                                                onChange={handleFracInitChange}
                                                selected={agmtFracInitAttrs}
                                                options={availAttrs}
                                                name={name}
                                                newSelectionPrefix="Add a attribute: "
                                                placeholder="Start typing an attribute..."
                                                id="agmtFracInitAttrs"
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top" title="Attributes to strip from a replication update">
                                        <Col componentClass={ControlLabel} sm={4}>
                                            Strip Attributes
                                        </Col>
                                        <Col sm={8}>
                                            <Typeahead
                                                multiple
                                                onChange={handleStripChange}
                                                selected={agmtStripAttrs}
                                                options={availAttrs}
                                                name={name}
                                                newSelectionPrefix="Add a attribute: "
                                                placeholder="Start typing an attribute..."
                                                id="agmtStripAttrs"
                                            />
                                        </Col>
                                    </Row>
                                    <Row className="ds-margin-top-med">
                                        <Col sm={8}>
                                            <Checkbox
                                                id="agmtSync"
                                                defaultChecked={agmtSync}
                                                onChange={handleChange}
                                                name={name}
                                                title="Always keep replication in synchronization, or use a specific schedule by unchecking the box."
                                            >
                                                Keep Replication In Constant Synchronization
                                            </Checkbox>
                                        </Col>
                                    </Row>
                                    {scheduleRow}
                                </div>
                            </CustomCollapse>
                            {spinner}
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                            disabled={saveDisabled}
                        >
                            Save Agreement
                        </Button>
                    </Modal.Footer>
                </div>
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
        if (role == "Master") {
            changeType = "Demoting";
            roleOptions = ["Hub", "Consumer"];
        } else if (role == "Consumer") {
            changeType = "Promoting";
            roleOptions = ["Master", "Hub"];
        } else {
            // Hub
            if (newRole == "Master") {
                changeType = "Promoting";
            } else {
                changeType = "Demoting";
            }
            roleOptions = ["Master", "Consumer"];
        }
        if (newRole == "Master") {
            ridRow =
                <Row className="ds-margin-top-lg" title="Master Replica Identifier.  This must be unique across all the Master replicas in your environment">
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
                        <Spinner loading inline size="md" />{changeType} replica ...
                    </div>
                </Row>;
            saveDisabled = true;
        }

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Change Replica Role
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <h4>Please choose the new replication role you would like for this suffix</h4>
                            <Row className="ds-margin-top-lg">
                                <Col componentClass={ControlLabel} sm={2}>
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
                                        defaultChecked={checked}
                                        onChange={handleChange}
                                    >
                                        <b>Yes</b>, I am sure.
                                    </Checkbox>
                                </Col>
                            </Row>
                            {spinner}
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={() => {
                                saveHandler(changeType);
                            }}
                            disabled={saveDisabled}
                        >
                            Change Role
                        </Button>
                    </Modal.Footer>
                </div>
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
                        <Spinner loading inline size="md" />Adding Replication Manager...
                    </div>
                </Row>;
        }

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Add Replication Manager
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <p>Create a Replication Manager entry, and add it to the replication configuration for this suffix.  If the entry already exists it will be overwritten with the new credentials.</p>
                            <Row className="ds-margin-top" title="The DN of the replication manager">
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
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Add Replication Manager
                        </Button>
                    </Modal.Footer>
                </div>
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
                        <Spinner loading inline size="md" />Enabling Replication ...
                    </div>
                </Row>;
        }

        let replicaIDRow = "";
        if (role == "Master") {
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
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Enable Replication
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <p>
                                Choose the replication role for this suffix.  If it
                                is a Master replica then you must pick a unique ID
                                to identify it among the other Master replicas in your
                                environment.  The replication changelog will also
                                automatically be created if it does not exist.
                            </p>

                            <hr />
                            <Row className="ds-margin-top-lg">
                                <Col sm={3} componentClass={ControlLabel}>
                                    Replication Role
                                </Col>
                                <Col sm={9}>
                                    <select className="btn btn-default dropdown" id="enableRole" defaultValue="Master" onChange={handleChange}>
                                        <option>Master</option>
                                        <option>Hub</option>
                                        <option>Consumer</option>
                                    </select>
                                </Col>
                            </Row>
                            {replicaIDRow}
                            <p className="ds-margin-top-xxlg">
                                You can optionally define the authentication information
                                for this replicated suffix.  Either a Manager DN and Password,
                                a Bind Group DN, or both, can be provideed.  The Manager DN should
                                be an entry under "cn=config" and if it does not exist it will
                                be created, while the Bind Group DN is usually an existing
                                group located in the database suffix.  Typically, just the
                                Manager DN and Password are used when enabling replication
                                for a suffix.
                            </p>
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
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                            disabled={this.props.disabled}
                        >
                            Enable Replication
                        </Button>
                    </Modal.Footer>
                </div>
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
                        <Spinner loading inline size="lg" />Exporting database... <font size="1">(You can safely close this window)</font>
                    </div>
                </Row>;
        }

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Create Replication Initialization LDIF File
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <Row>
                                <Col sm={11} className="ds-left-indent-md">
                                    <p>Enter the name of the LDIF file, do not use a path as the file will only be written to the server's LDIF directory</p>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top-lg" title="Name of the exported LDIF file">
                                <Col sm={3}>
                                    <b>LDIF Name</b>
                                </Col>
                                <Col sm={9}>
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
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                            disabled={!saveOK}
                        >
                            Export Replica
                        </Button>
                    </Modal.Footer>
                </div>
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
