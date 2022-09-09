import React from "react";
import {
    Button,
    Checkbox,
    Form,
    FormHelperText,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    NumberInput,
    Radio,
    Select,
    SelectVariant,
    SelectOption,
    Spinner,
    Tab,
    Tabs,
    TabTitleIcon,
    TabTitleText,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    TimePicker,
    ValidatedOptions,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import ExclamationTriangleIcon from '@patternfly/react-icons/dist/js/icons/exclamation-triangle-icon';

export class WinsyncAgmtModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            activeTabKey: 0,
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            event.preventDefault();
            this.setState({
                activeTabKey: tabIndex
            });
        };
    }

    hasMainErrors(errors) {
        const attrs = [
            'agmtName', 'agmtHost', 'agmtPort', 'agmtBindDN', 'agmtBindPW', 'agmtBindPWConfirm'
        ];
        for (const attr of attrs) {
            if (attr in errors && errors[attr]) {
                return true;
            }
        }
        return false;
    }

    hasDomainErrors(errors) {
        const attrs = [
            'agmtWinDomain', 'agmtWinSubtree', 'agmtDSSubtree',
        ];
        for (const attr of attrs) {
            if (attr in errors && errors[attr]) {
                return true;
            }
        }
        return false;
    }

    hasScheduleErrors(errors) {
        const attrs = [
            'agmtStartTime', 'agmtEndTime',
        ];
        for (const attr of attrs) {
            if (attr in errors && errors[attr]) {
                return true;
            }
        }
        return false;
    }

    render() {
        const {
            showModal,
            closeHandler,
            saveHandler,
            handleChange,
            handleTimeChange,
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
            agmtInit,
            availAttrs,
            error,
            isExcludeAttrOpen,
        } = this.props;
        const saveDisabled = !this.props.saveOK;
        let title = "Create";
        let initRow = "";
        let name = "agmt-modal";
        const startHour = agmtStartTime.substring(0, 2);
        const startMin = agmtStartTime.substring(2, 4);
        const startTime = startHour + ":" + startMin;
        const endHour = agmtEndTime.substring(0, 2);
        const endMin = agmtEndTime.substring(2, 4);
        const endTime = endHour + ":" + endMin;
        let saveBtnName = "Save Agreement";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Saving Agreement ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }
        let mainSettingsError = "";
        let scheduleError = "";
        let domainError = "";
        if (this.hasMainErrors(error)) {
            mainSettingsError = <TabTitleIcon className="ds-warning-icon"><ExclamationTriangleIcon /></TabTitleIcon>;
        }
        if (this.hasDomainErrors(error)) {
            domainError = <TabTitleIcon className="ds-warning-icon"><ExclamationTriangleIcon /></TabTitleIcon>;
        }
        if (this.hasScheduleErrors(error)) {
            scheduleError = <TabTitleIcon className="ds-warning-icon"><ExclamationTriangleIcon /></TabTitleIcon>;
        }

        if (this.props.edit) {
            title = "Edit";
            name = "agmt-modal-edit";
        } else {
            initRow =
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={3}>
                        Consumer Initialization
                    </GridItem>
                    <GridItem span={9}>
                        <FormSelect
                            value={agmtInit}
                            id="agmtInit"
                            onChange={(str, e) => {
                                handleChange(e);
                            }}
                            aria-label="FormSelect Input"
                        >
                            <FormSelectOption key={0} value="noinit" label="Do Not Initialize" />
                            <FormSelectOption key={1} value="online-init" label="Do Online Initialization" />
                        </FormSelect>
                    </GridItem>
                </Grid>;
        }

        let scheduleRow =
            <div className="ds-left-indent-md">
                <Grid className="ds-margin-top-lg">
                    <GridItem className="ds-label" span={12}>
                        Days To Send Synchronization Updates
                    </GridItem>
                </Grid>
                <div className="ds-indent ds-margin-top">
                    <Grid>
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncMon"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncMon}
                                label="Monday"
                                isValid={!error.agmtSyncMon}
                            />
                        </GridItem>
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncFri"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncFri}
                                label="Friday"
                                isValid={!error.agmtSyncFri}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncTue"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncTue}
                                isValid={!error.agmtSyncTue}
                                label="Tuesday"
                            />
                        </GridItem>
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncSat"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncSat}
                                isValid={!error.agmtSyncSat}
                                label="Saturday"
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncWed"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncWed}
                                label="Wednesday"
                                isValid={!error.agmtSyncWed}
                            />
                        </GridItem>
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncSun"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncSun}
                                isValid={!error.agmtSyncSun}
                                label="Sunday"
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncThu"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncThu}
                                isValid={!error.agmtSyncThu}
                                label="Thursday"
                            />
                        </GridItem>
                    </Grid>
                </div>
                <Grid className="ds-margin-top-lg" title="Time to start initiating replication sessions">
                    <GridItem className="ds-label" span={3}>
                        Replication Start Time
                    </GridItem>
                    <GridItem span={9}>
                        <TimePicker
                            time={startTime}
                            id="agmtStartTime"
                            onChange={(val) => {
                                handleTimeChange(this.props.edit ? "edit" : "create", "agmtStartTime", val);
                            }}
                            stepMinutes={5}
                            direction="up"
                            is24Hour
                        />
                        <FormHelperText isError isHidden={!error.agmtStartTime}>
                            Start time must be before the End time
                        </FormHelperText>
                    </GridItem>
                </Grid>
                <Grid title="Time to initiating replication sessions">
                    <GridItem className="ds-label" span={3}>
                        Replication End Time
                    </GridItem>
                    <GridItem span={9}>
                        <TimePicker
                            time={endTime}
                            id="agmtEndTime"
                            onChange={(val) => {
                                handleTimeChange(this.props.edit ? "edit" : "create", "agmtEndTime", val);
                            }}
                            stepMinutes={5}
                            direction="up"
                            is24Hour
                        />
                        <FormHelperText isError isHidden={!error.agmtEndTime}>
                            End time must be after the Start time
                        </FormHelperText>
                    </GridItem>
                </Grid>
            </div>;

        if (!agmtSync) {
            scheduleRow = "";
        }

        title = title + " Winsync Agreement";

        return (
            <Modal
                variant={ModalVariant.medium}
                className="ds-modal-winsync-agmt"
                aria-labelledby="ds-modal"
                title={title}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        isDisabled={saveDisabled || spinning}
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <div className={spinning ? "ds-disabled" : ""}>
                    <Form isHorizontal autoComplete="off">
                        <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                            <Tab eventKey={0} title={<>{mainSettingsError}<TabTitleText>Main Settings</TabTitleText></>}>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Agreement Name
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtName}
                                            type="text"
                                            id="agmtName"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtName"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            isDisabled={this.props.edit}
                                            validated={error.agmtName ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top-lg">
                                    <GridItem className="ds-label" span={3}>
                                        Windows AD Host
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtHost}
                                            type="text"
                                            id="agmtHost"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtHost"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtHost ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top-lg">
                                    <GridItem className="ds-label" span={3}>
                                        Windows AD Port
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtPort}
                                            type="number"
                                            id="agmtPort"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtPort"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtPort ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top-lg">
                                    <GridItem className="ds-label" span={3}>
                                        Bind DN
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtBindDN}
                                            type="text"
                                            id="agmtBindDN"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtBindDN"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtBindDN ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtBindDN || agmtBindDN == ""}>
                                            Value must be a valid DN
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Bind Password
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtBindPW}
                                            type="password"
                                            id="agmtBindPW"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtBindPW"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtBindPW ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtBindPW || agmtBindPW == "" || agmtBindPWConfirm == ""}>
                                            Passwords must match
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Confirm Password
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtBindPWConfirm}
                                            type="password"
                                            id="agmtBindPWConfirm"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtBindPWConfirm"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtBindPWConfirm ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtBindPWConfirm || agmtBindPWConfirm == ""}>
                                            Passwords must match
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                {initRow}
                            </Tab>
                            <Tab eventKey={1} title={<>{domainError}<TabTitleText>Domain & Content</TabTitleText></>}>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Windows Domain Name
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtWinDomain}
                                            type="text"
                                            id="agmtWinDomain"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtWinDomain"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtWinDomain ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top-lg" title="The Active Directory subtree to synchronize">
                                    <GridItem className="ds-label" span={3}>
                                        Windows Subtree
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtWinSubtree}
                                            type="text"
                                            id="agmtWinSubtree"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtWinSubtree"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            placeholder="e.g. cn=Users,dc=domain,dc=com"
                                            validated={error.agmtWinSubtree ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtWinSubtree || agmtWinSubtree == ""}>
                                            Value must be a valid DN
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Directory Server subtree to synchronize">
                                    <GridItem className="ds-label" span={3}>
                                        DS Subtree
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtDSSubtree}
                                            type="text"
                                            id="agmtDSSubtree"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtDSSubtree"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            placeholder="e.g. ou=People,dc=domain,dc=com"
                                            validated={error.agmtDSSubtree ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtDSSubtree || agmtDSSubtree == ""}>
                                            Value must be a valid DN
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                            </Tab>
                            <Tab eventKey={2} title={<TabTitleText>Advanced Settings</TabTitleText>}>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Connection Protocol
                                    </GridItem>
                                    <GridItem span={9}>
                                        <FormSelect
                                            value={agmtProtocol}
                                            id="agmtProtocol"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            aria-label="FormSelect Input"
                                        >
                                            <FormSelectOption key={0} value="LDAPS" label="LDAPS" />
                                            <FormSelectOption key={1} value="StartTLS" label="StartTLS" />
                                        </FormSelect>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Synchronization Direction
                                    </GridItem>
                                    <GridItem span={9}>
                                        <FormSelect
                                            value={agmtOneWaySync}
                                            id="agmtOneWaySync"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            aria-label="FormSelect Input"
                                        >
                                            <FormSelectOption title="Synchronization in both directions (default behavior)." key={0} value="both" label="both" />
                                            <FormSelectOption title="Only synchronize Directory Server updates to Windows." key={1} value="toWindows" label="toWindows" />
                                            <FormSelectOption title="Only synchronize Windows updates to Directory Server." key={2} value="fromWindows" label="fromWindows" />
                                        </FormSelect>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="The interval to check for updates on Windows.  Default is 300 seconds">
                                    <GridItem className="ds-label" span={3}>
                                        Synchronization Interval
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtSyncInterval}
                                            type="number"
                                            id="agmtSyncInterval"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtSyncInterval"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtSyncInterval ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Attribute to exclude from replication">
                                    <GridItem className="ds-label" span={3}>
                                        Exclude Attributes
                                    </GridItem>
                                    <GridItem span={9}>
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
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top-med">
                                    <GridItem>
                                        <Checkbox
                                            id="agmtSyncGroups"
                                            onChange={(checked, e) => {
                                                handleChange(e);
                                            }}
                                            name={name}
                                            isChecked={agmtSyncGroups}
                                            label="Synchronize New Windows Groups"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem>
                                        <Checkbox
                                            id="agmtSyncUsers"
                                            onChange={(checked, e) => {
                                                handleChange(e);
                                            }}
                                            name={name}
                                            isChecked={agmtSyncUsers}
                                            label="Synchronize New Windows Users"
                                        />
                                    </GridItem>
                                </Grid>

                            </Tab>
                            <Tab eventKey={3} title={<>{scheduleError}<TabTitleText>Scheduling</TabTitleText></>}>
                                <Grid className="ds-margin-top">
                                    <GridItem span={12}>
                                        <TextContent>
                                            <Text component={TextVariants.h5}>
                                                By default replication updates are sent to the replica as soon as possible, but
                                                if there is a need for replication updates to only be sent on certains days and
                                                within certain windows of time then you can setup a custom replication schedule.
                                            </Text>
                                        </TextContent>
                                    </GridItem>
                                    <GridItem className="ds-margin-top-lg" span={12}>
                                        <Checkbox
                                            id="agmtSync"
                                            isChecked={agmtSync}
                                            onChange={(checked, e) => {
                                                handleChange(e);
                                            }}
                                            name={name}
                                            label="Use A Custom Schedule"
                                        />
                                    </GridItem>
                                </Grid>
                                {scheduleRow}
                            </Tab>
                        </Tabs>
                    </Form>
                </div>
            </Modal>
        );
    }
}

export class ReplAgmtModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            activeTabKey: 0,
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            event.preventDefault();
            this.setState({
                activeTabKey: tabIndex
            });
        };
    }

    hasMainErrors(errors) {
        const attrs = [
            'agmtName', 'agmtHost', 'agmtPort', 'agmtBindDN', 'agmtBindPW', 'agmtBindPWConfirm'
        ];
        for (const attr of attrs) {
            if (attr in errors && errors[attr]) {
                return true;
            }
        }
        return false;
    }

    hasBootErrors(errors) {
        const attrs = [
            'agmtBootstrapBindDN', 'agmtBootstrapBindPW', 'agmtBootstrapBindPWConfirm'
        ];
        for (const attr of attrs) {
            if (attr in errors && errors[attr]) {
                return true;
            }
        }
        return false;
    }

    hasScheduleErrors(errors) {
        const attrs = [
            'agmtStartTime', 'agmtEndTime'
        ];
        for (const attr of attrs) {
            if (attr in errors && errors[attr]) {
                return true;
            }
        }
        return false;
    }

    render() {
        const {
            showModal,
            closeHandler,
            saveHandler,
            handleChange,
            handleTimeChange,
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
            agmtBindMethodOptions,
            agmtBindDN,
            agmtBindPW,
            agmtBindPWConfirm,
            agmtBootstrap,
            agmtBootstrapBindDN,
            agmtBootstrapBindPW,
            agmtBootstrapBindPWConfirm,
            agmtBootstrapProtocol,
            agmtBootstrapBindMethod,
            agmtBootstrapBindMethodOptions,
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
            agmtInit,
            availAttrs,
            error,
        } = this.props;
        const saveDisabled = !this.props.saveOK;
        let title = "Create";
        let initRow = "";
        let name = "agmt-modal";
        const bootstrapTitle = "If you are using Bind Group's on the consumer " +
            "replica you can configure bootstrap credentials that can be used " +
            "to do online initializations, or bootstrap a session if the bind " +
            "groups get out of synchronization";
        const startHour = agmtStartTime.substring(0, 2);
        const startMin = agmtStartTime.substring(2, 4);
        const startTime = startHour + ":" + startMin;
        const endHour = agmtEndTime.substring(0, 2);
        const endMin = agmtEndTime.substring(2, 4);
        const endTime = endHour + ":" + endMin;
        let saveBtnName = "Save Agreement";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Saving Agreement ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        let mainSettingsError = "";
        let bootSettingsError = "";
        let scheduleSettingsError = "";
        if (this.hasMainErrors(error)) {
            mainSettingsError = <TabTitleIcon className="ds-warning-icon"><ExclamationTriangleIcon /></TabTitleIcon>;
        }
        if (this.hasBootErrors(error)) {
            bootSettingsError = <TabTitleIcon className="ds-warning-icon"><ExclamationTriangleIcon /></TabTitleIcon>;
        }
        if (this.hasScheduleErrors(error)) {
            scheduleSettingsError = <TabTitleIcon className="ds-warning-icon"><ExclamationTriangleIcon /></TabTitleIcon>;
        }

        if (this.props.edit) {
            title = "Edit";
            name = "agmt-modal-edit";
        } else {
            initRow =
                <Grid className="ds-margin-top-lg">
                    <GridItem className="ds-label" span={3}>
                        Consumer Initialization
                    </GridItem>
                    <GridItem span={9}>
                        <FormSelect
                            value={agmtInit}
                            id="agmtInit"
                            onChange={(str, e) => {
                                handleChange(e);
                            }}
                            aria-label="FormSelect Input"
                        >
                            <FormSelectOption key={0} value="noinit" label="Do Not Initialize" />
                            <FormSelectOption key={1} value="online-init" label="Do Online Initialization" />
                        </FormSelect>
                    </GridItem>
                </Grid>;
        }

        let bootstrapRow =
            <div className="ds-left-indent-md">
                <Grid className="ds-margin-top-lg" title="The Bind DN the agreement can use to bootstrap initialization">
                    <GridItem className="ds-label" span={3}>
                        Bind DN
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={agmtBootstrapBindDN}
                            type="text"
                            id="agmtBootstrapBindDN"
                            aria-describedby="horizontal-form-name-helper"
                            name="agmtBootstrapBindDN"
                            onChange={(str, e) => {
                                handleChange(e);
                            }}
                            validated={error.agmtBootstrapBindDN ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                        <FormHelperText isError isHidden={!error.agmtBootstrapBindDN || agmtBootstrapBindDN == ""}>
                            Value must be a valid DN
                        </FormHelperText>
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={3} title="The Bind DN password for bootstrap initialization">
                        Password
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={agmtBootstrapBindPW}
                            type="password"
                            id="agmtBootstrapBindPW"
                            aria-describedby="horizontal-form-name-helper"
                            name="agmtBootstrapBindPW"
                            onChange={(str, e) => {
                                handleChange(e);
                            }}
                            validated={error.agmtBootstrapBindPW ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                        <FormHelperText isError isHidden={!error.agmtBootstrapBindPW || agmtBootstrapBindPW == "" || error.agmtBootstrapBindPWConfirm == ""}>
                            Password must match
                        </FormHelperText>
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={3} title="Confirm the Bind DN password for bootstrap initialization">
                        Confirm Password
                    </GridItem>
                    <GridItem span={9}>
                        <TextInput
                            value={agmtBootstrapBindPWConfirm}
                            type="password"
                            id="agmtBootstrapBindPWConfirm"
                            aria-describedby="horizontal-form-name-helper"
                            name="agmtBootstrapBindPWConfirm"
                            onChange={(str, e) => {
                                handleChange(e);
                            }}
                            validated={error.agmtBootstrapBindPWConfirm ? ValidatedOptions.error : ValidatedOptions.default}
                        />
                        <FormHelperText isError isHidden={!error.agmtBootstrapBindPWConfirm || agmtBootstrapBindPWConfirm == ""}>
                            Passwords must match
                        </FormHelperText>
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={3} title="The connection protocol for bootstrap initialization">
                        Connection Protocol
                    </GridItem>
                    <GridItem span={9}>
                        <FormSelect
                            value={agmtBootstrapProtocol}
                            id="agmtBootstrapProtocol"
                            onChange={(str, e) => {
                                handleChange(e);
                            }}
                            aria-label="FormSelect Input"
                            validated={error.agmtBootstrapProtocol ? ValidatedOptions.error : ValidatedOptions.default}
                        >
                            <FormSelectOption key={0} value="LDAP" label="LDAP" />
                            <FormSelectOption key={1} value="LDAPS" label="LDAPS" />
                            <FormSelectOption key={2} value="STARTTLS" label="STARTTLS" />
                        </FormSelect>
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top-lg">
                    <GridItem className="ds-label" span={3} title="The authentication method for bootstrap initialization">
                        Authentication Method
                    </GridItem>
                    <GridItem span={9}>
                        <FormSelect
                            value={agmtBootstrapBindMethod}
                            id="agmtBootstrapBindMethod"
                            onChange={(str, e) => {
                                handleChange(e);
                            }}
                            aria-label="FormSelect Input"
                            validated={error.agmtBootstrapBindMethod ? ValidatedOptions.error : ValidatedOptions.default}
                        >
                            {agmtBootstrapBindMethodOptions.map((option, index) => (
                                <FormSelectOption key={index} value={option} label={option} />
                            ))}
                        </FormSelect>
                    </GridItem>
                </Grid>
            </div>;

        let scheduleRow =
            <div className="ds-left-indent-md">
                <Grid className="ds-margin-top-lg">
                    <GridItem className="ds-label" span={12}>
                        Days To Send Replication Updates
                    </GridItem>
                </Grid>
                <div className="ds-indent ds-margin-top">
                    <Grid>
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncMon"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncMon}
                                label="Monday"
                                isValid={!error.agmtSyncMon}
                            />
                        </GridItem>
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncFri"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncFri}
                                label="Friday"
                                isValid={!error.agmtSyncFri}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncTue"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncTue}
                                isValid={!error.agmtSyncTue}
                                label="Tuesday"
                            />
                        </GridItem>
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncSat"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncSat}
                                isValid={!error.agmtSyncSat}
                                label="Saturday"
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncWed"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncWed}
                                label="Wednesday"
                                isValid={!error.agmtSyncWed}
                            />
                        </GridItem>
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncSun"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncSun}
                                isValid={!error.agmtSyncSun}
                                label="Sunday"
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <Checkbox
                                id="agmtSyncThu"
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                name={name}
                                isChecked={agmtSyncThu}
                                isValid={!error.agmtSyncThu}
                                label="Thursday"
                            />
                        </GridItem>
                    </Grid>
                </div>
                <Grid className="ds-margin-top-lg" title="Time to start initiating replication sessions">
                    <GridItem className="ds-label" span={3}>
                        Replication Start Time
                    </GridItem>
                    <GridItem span={9}>
                        <TimePicker
                            time={startTime}
                            id="agmtStartTime"
                            onChange={(val) => {
                                handleTimeChange(this.props.edit ? "edit" : "create", "agmtStartTime", val);
                            }}
                            stepMinutes={5}
                            is24Hour
                        />
                        <FormHelperText isError isHidden={!error.agmtStartTime}>
                            Start time must be before the End time
                        </FormHelperText>
                    </GridItem>
                </Grid>
                <Grid title="Time to initiating replication sessions">
                    <GridItem className="ds-label" span={3}>
                        Replication End Time
                    </GridItem>
                    <GridItem span={9}>
                        <TimePicker
                            time={endTime}
                            id="agmtEndTime"
                            onChange={(val) => {
                                handleTimeChange(this.props.edit ? "edit" : "create", "agmtEndTime", val);
                            }}
                            stepMinutes={5}
                            is24Hour
                        />
                        <FormHelperText isError isHidden={!error.agmtEndTime}>
                            End time must be after the Start time
                        </FormHelperText>
                    </GridItem>
                </Grid>
            </div>;

        if (!agmtSync) {
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
                className="ds-modal-repl-agmt"
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        isDisabled={saveDisabled || spinning}
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <div className={spinning ? "ds-disabled" : ""}>
                    <Form isHorizontal autoComplete="off">
                        <Tabs activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                            <Tab eventKey={0} title={<>{mainSettingsError}<TabTitleText>Main Settings</TabTitleText></>}>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Agreement Name
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtName}
                                            type="text"
                                            id="agmtName"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtName"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            isDisabled={this.props.edit}
                                            validated={error.agmtName ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtName || agmtName == ""}>
                                            Required field
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Consumer Host
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtHost}
                                            type="text"
                                            id="agmtHost"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtHost"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtHost ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtHost || agmtHost == ""}>
                                            Required field
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Consumer Port
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtPort}
                                            type="number"
                                            id="agmtPort"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtPort"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtPort ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtPort}>
                                            Port must be between 1 and 65535
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Bind DN
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtBindDN}
                                            type="text"
                                            id="agmtBindDN"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtBindDN"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtBindDN ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtBindDN}>
                                            Value must be a valid DN
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Bind Password
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtBindPW}
                                            type="password"
                                            id="agmtBindPW"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtBindPW"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtBindPW ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtBindPW || error.agmtBindPW == "" || error.agmtBindPWConfirm == ""}>
                                            Passwords must match
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Confirm Password
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={agmtBindPWConfirm}
                                            type="password"
                                            id="agmtBindPWConfirm"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="agmtBindPWConfirm"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            validated={error.agmtBindPWConfirm ? ValidatedOptions.error : ValidatedOptions.default}
                                        />
                                        <FormHelperText isError isHidden={!error.agmtBindPWConfirm || agmtBindPWConfirm == ""}>
                                            Passwords must match
                                        </FormHelperText>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Connection Protocol
                                    </GridItem>
                                    <GridItem span={9}>
                                        <FormSelect
                                            value={agmtProtocol}
                                            id="agmtProtocol"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            aria-label="FormSelect Input"
                                            validated={error.agmtProtocol ? ValidatedOptions.error : ValidatedOptions.default}
                                        >
                                            <FormSelectOption key={0} value="LDAP" label="LDAP" />
                                            <FormSelectOption key={1} value="LDAPS" label="LDAPS" />
                                            <FormSelectOption key={2} value="STARTTLS" label="STARTTLS" />
                                        </FormSelect>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top-lg">
                                    <GridItem className="ds-label" span={3}>
                                        Authentication Method
                                    </GridItem>
                                    <GridItem span={9}>
                                        <FormSelect
                                            value={agmtBindMethod}
                                            id="agmtBindMethod"
                                            onChange={(str, e) => {
                                                handleChange(e);
                                            }}
                                            aria-label="FormSelect Input"
                                            validated={error.agmtBindMethod ? ValidatedOptions.error : ValidatedOptions.default}
                                        >
                                            {agmtBindMethodOptions.map((option, index) => (
                                                <FormSelectOption key={index} value={option} label={option} />
                                            ))}
                                        </FormSelect>
                                    </GridItem>
                                </Grid>
                                {initRow}
                            </Tab>
                            <Tab eventKey={1} title={<TabTitleText>Fractional Settings</TabTitleText>}>
                                <Grid className="ds-margin-top-lg" title="Attribute to exclude from replication">
                                    <GridItem className="ds-label" span={3}>
                                        Exclude Attributes
                                    </GridItem>
                                    <GridItem span={9}>
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
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Attribute to exclude from replica Initializations">
                                    <GridItem className="ds-label" span={3}>
                                        Exclude Init Attributes
                                    </GridItem>
                                    <GridItem span={9}>
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
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Attributes to strip from a replicatio<Selectn update">
                                    <GridItem className="ds-label" span={3}>
                                        Strip Attributes
                                    </GridItem>
                                    <GridItem span={9}>
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
                                    </GridItem>
                                </Grid>
                            </Tab>
                            <Tab eventKey={2} title={<>{bootSettingsError}<TabTitleText>Bootstrap Settings</TabTitleText></>}>
                                <Grid className="ds-margin-top-med">
                                    <GridItem span={9}>
                                        <Checkbox
                                            id="agmtBootstrap"
                                            isChecked={agmtBootstrap}
                                            onChange={(checked, e) => {
                                                handleChange(e);
                                            }}
                                            name={name}
                                            title={bootstrapTitle}
                                            label="Enable Bootstrap Settings"
                                        />
                                    </GridItem>
                                </Grid>
                                {bootstrapRow}
                            </Tab>
                            <Tab eventKey={3} title={<>{scheduleSettingsError}<TabTitleText>Scheduling</TabTitleText></>}>
                                <Grid className="ds-margin-top-med">
                                    <GridItem span={12}>
                                        <TextContent>
                                            <Text component={TextVariants.h5}>
                                                By default replication updates are sent to the replica as soon as possible, but
                                                if there is a need for replication updates to only be sent on certains days and within certain
                                                windows of time then you can setup a custom replication schedule.
                                            </Text>
                                        </TextContent>
                                    </GridItem>
                                    <GridItem className="ds-margin-top-lg" span={12}>
                                        <Checkbox
                                            id="agmtSync"
                                            isChecked={agmtSync}
                                            onChange={(checked, e) => {
                                                handleChange(e);
                                            }}
                                            name={name}
                                            label="Use A Custom Schedule"
                                        />
                                    </GridItem>
                                </Grid>
                                {scheduleRow}
                            </Tab>
                        </Tabs>
                    </Form>
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
            onMinus,
            onNumberChange,
            onPlus,
            newRID,
        } = this.props;
        let spinner = "";
        let changeType = "";
        let roleOptions = [];
        let ridRow = "";
        const newRole = this.props.newRole;
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
                <Grid className="ds-margin-top-lg" title="Supplier Replica Identifier.  This must be unique across all the Supplier replicas in your environment">
                    <GridItem className="ds-label" span={3}>
                        Replica ID
                    </GridItem>
                    <GridItem span={2}>
                        <NumberInput
                            value={newRID}
                            min={1}
                            max={65534}
                            onMinus={onMinus}
                            onChange={onNumberChange}
                            onPlus={onPlus}
                            inputName="input"
                            inputAriaLabel="number input"
                            minusBtnAriaLabel="minus"
                            plusBtnAriaLabel="plus"
                            widthChars={8}
                        />
                    </GridItem>
                </Grid>;
        }

        if (spinning) {
            spinner =
                <Grid>
                    <div className="ds-margin-top ds-modal-spinner">
                        <Spinner size="md" />{changeType} replica ...
                    </div>
                </Grid>;
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
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Please choose the new replication role you would like for this suffix
                        </Text>
                    </TextContent>
                    <Grid className="ds-margin-top-lg">
                        <GridItem className="ds-label" span={3}>
                            New Role
                        </GridItem>
                        <GridItem span={3}>
                            <FormSelect
                                value={newRole}
                                id="newRole"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                aria-label="FormSelect Input"
                            >
                                {roleOptions.map((option, index) => (
                                    <FormSelectOption key={index} value={option} label={option} />
                                ))}
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    {ridRow}
                    <Grid className="ds-margin-top-xlg">
                        <GridItem span={12} className="ds-center">
                            <Checkbox
                                id="modalChecked"
                                isChecked={checked}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                label={<><b>Yes</b>, I am sure.</>}
                            />
                        </GridItem>
                    </Grid>
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
            manager,
            manager_passwd,
            manager_passwd_confirm,
            error
        } = this.props;
        let saveBtnName = "Add Replication Manager";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Adding Replication Manager ...";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Add Replication Manager"
                aria-labelledby="ds-modal"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={error.manager || error.manager_passwd || error.manager_passwd_confirm || spinning}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Create a Replication Manager entry, and add it to the replication configuration
                            for this suffix.  If the entry already exists it will be overwritten with
                            the new credentials.
                        </Text>
                    </TextContent>
                    <Grid className="ds-margin-top-lg" title="The DN of the replication manager">
                        <GridItem className="ds-label" span={3}>
                            Replication Manager DN
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={manager}
                                type="text"
                                id="manager"
                                aria-describedby="horizontal-form-name-helper"
                                name="manager"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.manager ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="Replication Manager password">
                        <GridItem className="ds-label" span={3}>
                            Password
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={manager_passwd}
                                type="password"
                                id="manager_passwd"
                                aria-describedby="horizontal-form-name-helper"
                                name="manager_passwd"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.manager_passwd ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="Replication Manager password">
                        <GridItem className="ds-label" span={3}>
                            Confirm Password
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={manager_passwd_confirm}
                                type="password"
                                id="manager_passwd_confirm"
                                aria-describedby="horizontal-form-name-helper"
                                name="manager_passwd_confirm"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.manager_passwd_confirm ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
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
            enableRole,
            enableRID,
            enableBindDN,
            enableBindPW,
            enableBindPWConfirm,
            enableBindGroupDN,
            error,
            onMinus,
            onPlus,
            onNumberChange
        } = this.props;
        let saveBtnName = "Enable Replication";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Enabling Replication ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }
        let replicaIDRow = "";
        if (enableRole == "Supplier") {
            replicaIDRow =
                <Grid>
                    <GridItem span={3} className="ds-label">
                        Replica ID
                    </GridItem>
                    <GridItem span={2}>
                        <NumberInput
                            value={enableRID}
                            min={1}
                            max={65534}
                            onMinus={onMinus}
                            onChange={onNumberChange}
                            onPlus={onPlus}
                            inputName="input"
                            inputAriaLabel="number input"
                            minusBtnAriaLabel="minus"
                            plusBtnAriaLabel="plus"
                            widthChars={6}
                        />
                    </GridItem>
                </Grid>;
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
                        isDisabled={this.props.disabled || spinning}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text component={TextVariants.h6}>
                            Choose the replication role for this suffix.  If it
                            is a Supplier replica then you must pick a unique ID
                            to identify it among the other Supplier replicas in your
                            environment.  The replication changelog will also
                            automatically be created for you.
                        </Text>
                    </TextContent>
                    <Grid>
                        <GridItem span={3} className="ds-label">
                            Replication Role
                        </GridItem>
                        <GridItem span={2}>
                            <FormSelect
                                id="enableRole"
                                value={enableRole}
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                aria-label="FormSelect Input"
                            >
                                <FormSelectOption key={0} value="Supplier" label="Supplier" />
                                <FormSelectOption key={1} value="Hub" label="Hub" />
                                <FormSelectOption key={2} value="Consumer" label="Consumer" />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    {replicaIDRow}
                    <hr />
                    <TextContent>
                        <Text component={TextVariants.h6}>
                            You can optionally define the authentication information
                            for this replicated suffix.  Either a Manager DN and Password,
                            a Bind Group DN, or both, can be provided.  The Manager DN should
                            be an entry under "cn=config" and if it does not exist it will
                            be created, while the Bind Group DN is usually an existing
                            group located in the database suffix.  Typically, just the
                            Manager DN and Password are used when enabling replication
                            for a suffix.
                        </Text>
                    </TextContent>
                    <Grid title="The DN of the replication manager.  If you supply a password the entry will be created in the server (it will also overwrite the entry is it already exists).">
                        <GridItem className="ds-label" span={3}>
                            Replication Manager DN
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={enableBindDN}
                                type="text"
                                id="enableBindDN"
                                aria-describedby="horizontal-form-name-helper"
                                name="enableBindDN"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.enableBindDN ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="Replication Manager password">
                        <GridItem className="ds-label" span={3}>
                            Password
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={enableBindPW}
                                type="password"
                                id="enableBindPW"
                                aria-describedby="horizontal-form-name-helper"
                                name="enableBindPW"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.enableBindPW ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="Confirm the Replication Manager password">
                        <GridItem className="ds-label" span={3}>
                            Confirm Password
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={enableBindPWConfirm}
                                type="password"
                                id="enableBindPWConfirm"
                                aria-describedby="horizontal-form-name-helper"
                                name="enableBindPWConfirm"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.enableBindPWConfirm ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="The DN of a group that contains users that can perform replication updates">
                        <GridItem className="ds-label" span={3}>
                            Bind Group DN
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={enableBindGroupDN}
                                type="text"
                                id="enableBindGroupDN"
                                aria-describedby="horizontal-form-name-helper"
                                name="enableBindGroupDN"
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                                validated={error.enableBindGroupDN ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                        </GridItem>
                    </Grid>
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
        let page = "";
        let saveBtnName = "Export Changelog";
        const extraPrimaryProps = {};
        if (spinning) {
            saveBtnName = "Exporting ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        if (defaultCL) {
            page =
                <TextContent>
                    <Text component={TextVariants.h4}>
                        This will export the changelog to the server's LDIF directory.  This
                        is the only LDIF file that can be imported into the server for enabling
                        changelog encryption.  Do not edit or rename the file.
                    </Text>
                </TextContent>;
        } else {
            page =
                <div>
                    <Grid>
                        <GridItem span={12}>
                            <TextContent>
                                <Text component={TextVariants.h4}>
                                    The LDIF file that is generated should <b>not</b> be used
                                    to initialize the Replication Changelog.  It is only
                                    meant for debugging/investigative purposes.
                                </Text>
                            </TextContent>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top-xlg">
                        <GridItem className="ds-label" span={2}>
                            LDIF File
                        </GridItem>
                        <GridItem span={10}>
                            <TextInput
                                value={ldifFile}
                                type="text"
                                id="ldifFile"
                                aria-describedby="horizontal-form-name-helper"
                                name="ldifFile"
                                onChange={(str, e) => {
                                    handleLDIFChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top-xlg ds-margin-left">
                        <Checkbox
                            id="decodeCL"
                            isChecked={decodeCL}
                            isDisabled={exportCSN}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                            label="Decode base64 changes"
                        />
                    </Grid>
                    <Grid className="ds-margin-top ds-margin-left">
                        <Checkbox
                            id="exportCSN"
                            isChecked={exportCSN}
                            isDisabled={decodeCL}
                            onChange={(checked, e) => {
                                handleChange(e);
                            }}
                            label="Only Export CSN's"
                        />
                    </Grid>
                </div>;
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                className="ds-modal-changelog-export"
                title="Create Replication Change Log LDIF File"
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="export"
                        variant="primary"
                        onClick={saveHandler}
                        isDisabled={!saveOK || spinning}
                        isLoading={spinning}
                        spinnerAriaValueText={spinning ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid className="ds-indent ds-margin-top">
                        <Radio
                            isChecked={defaultCL}
                            name="radioGroup"
                            onChange={handleRadioChange}
                            label="Export to LDIF For Reinitializing The Changelog"
                            id="defaultCL"
                        />
                    </Grid>
                    <Grid className="ds-indent">
                        <Radio
                            isChecked={debugCL}
                            name="radioGroup"
                            onChange={handleRadioChange}
                            label="Export to LDIF For Debugging"
                            id="debugCL"
                        />
                    </Grid>
                    <hr />
                    {page}
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
    edit: PropTypes.bool,
};

ReplAgmtModal.defaultProps = {
    showModal: false,
    spinning: false,
    availAttrs: [],
    agmtName: "",
    agmtHost: "",
    agmtPort: "636",
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
    edit: PropTypes.bool,
};

WinsyncAgmtModal.defaultProps = {
    showModal: false,
    spinning: false,
    availAttrs: [],
    agmtName: "",
    agmtHost: "",
    agmtPort: "636",
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
    edit: false,
};
