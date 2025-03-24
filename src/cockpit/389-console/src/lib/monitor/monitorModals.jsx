import cockpit from "cockpit";
import React from "react";
import {
    Button,
    Checkbox,
    EmptyState,
    EmptyStateIcon,
    EmptyStateBody,
    Grid,
    GridItem,
    Form,
    Modal,
    ModalVariant,
    NumberInput,
    Radio,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    TextArea,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    Title,
    Tooltip,
    Card,
    CardBody,
    Alert,
    CardTitle,
    DescriptionList,
    DescriptionListGroup,
    DescriptionListTerm,
    DescriptionListDescription,
    List,
    ListItem
} from "@patternfly/react-core";
import {
    CopyIcon,
    OutlinedQuestionCircleIcon,
    DownloadIcon,
} from '@patternfly/react-icons';
import PropTypes from "prop-types";
import { get_date_string } from "../tools.jsx";
import {
    ReportSingleTable,
    ReportConsumersTable,
    ExistingLagReportsTable
} from "./monitorTables.jsx";
import {
    ExclamationCircleIcon,
    InfoIcon
} from "@patternfly/react-icons";
import {
    Chart,
    ChartAxis,
    ChartGroup,
    ChartLine,
    ChartScatter,
    ChartThemeColor,
    ChartVoronoiContainer,
    ChartTooltip
} from "@patternfly/react-charts";
import { getResizeObserver } from "@patternfly/react-core";

const _ = cockpit.gettext;

class TaskLogModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            logData,
        } = this.props;

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Task Log")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Close")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextArea
                        resizeOrientation="vertical"
                        className="ds-logarea"
                        value={logData}
                        aria-label="text area example"
                    />
                </Form>
            </Modal>
        );
    }
}

class AgmtDetailsModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            agmt,
        } = this.props;

        // Format status dates from agmt
        const convertedDate = {};
        const dateAttrs = ['last-update-start', 'last-update-end',
            'last-init-start', 'last-init-end'];
        for (const attr of dateAttrs) {
            if (agmt[attr][0] === "19700101000000Z") {
                convertedDate[attr] = "Unavailable";
            } else {
                convertedDate[attr] = get_date_string(agmt[attr][0]);
            }
        }

        const btnList = [
            <Button key="cancel" variant="link" onClick={closeHandler}>
                {_("Cancel")}
            </Button>
        ];

        const title = _("Replication Agreement Details (") + agmt['agmt-name'] + ")";

        return (
            <Modal
                variant={ModalVariant.medium}
                title={title}
                isOpen={showModal}
                onClose={closeHandler}
                actions={btnList}
            >
                <Form isHorizontal>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Replica")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{agmt.replica}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Agreement Enabled")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{agmt['replica-enabled']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Last Init Started")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{convertedDate['last-init-start']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Last Init Ended")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{convertedDate['last-init-end']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Last Initialization Status")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <TextArea
                                resizeOrientation="vertical"
                                className="ds-textarea"
                                value={agmt['last-init-status']}
                                aria-label="text area example"
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Replication In Progress")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{agmt['update-in-progress']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Changes Sent")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{agmt['number-changes-sent']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Last Update Started")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{convertedDate['last-update-start']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Last Update Ended")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{convertedDate['last-update-end']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>{_("Last Update Status")}</b>
                        </GridItem>
                        <GridItem span={8}>
                            <TextArea
                                resizeOrientation="vertical"
                                className="ds-textarea"
                                value={agmt['last-update-status']}
                                aria-label="text area example"
                            />
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

class ConflictCompareModal extends React.Component {
    render() {
        const {
            showModal,
            conflictEntry,
            validEntry,
            closeHandler,
            saveHandler,
            handleChange,
            handleRadioChange,
            convertConflictRadio,
            deleteConflictRadio,
            swapConflictRadio,
            newRDN
        } = this.props;

        const ignoreAttrs = ['createtimestamp', 'creatorsname', 'modifytimestamp',
            'modifiersname', 'entryid', 'entrydn', 'parentid', 'numsubordinates'];
        let conflict = "dn: " + conflictEntry.dn + "\n";
        let valid = "dn: " + validEntry.dn + "\n";
        let conflictChildren = "0";
        let validChildren = "0";
        let orig_rdn = newRDN;
        if (newRDN === "") {
            // Create an example rdn value based off the conflict rdn
            orig_rdn = conflictEntry.dn.split('+');
            orig_rdn = orig_rdn[0] + "-MUST_CHANGE";
        }
        for (const key in conflictEntry.attrs) {
            if (key === "numsubordinates") {
                conflictChildren = conflictEntry.attrs[key];
            }
            if (!ignoreAttrs.includes(key)) {
                for (const attr of conflictEntry.attrs[key]) {
                    conflict += key + ": " + attr + "\n";
                }
            }
        }
        for (const key in validEntry.attrs) {
            if (key === "numsubordinates") {
                validChildren = <font color="red">{validEntry.attrs[key]}</font>;
            }
            if (!ignoreAttrs.includes(key)) {
                for (const attr of validEntry.attrs[key]) {
                    valid += key + ": " + attr + "\n";
                }
            }
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Resolve Replication Conflict")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={() => saveHandler(conflictEntry.dn)}>
                        {_("Resolve Conflict")}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid>
                        <GridItem span={6}>
                            <TextContent>
                                <Text component={TextVariants.h4}>
                                    {_("Valid Entry")}
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={6}>
                            <p className="ds-margin-top ds-right-align ds-font-size-sm">
                                {_("Child Entries: ")}<b>{validChildren}</b>
                            </p>
                        </GridItem>
                        <GridItem span={12}>
                            <TextArea id="conflictValid" resizeOrientation="vertical" className="ds-conflict" value={valid}  readOnlyVariant="default" />
                        </GridItem>
                        <GridItem className="ds-margin-top-lg" span={6}>
                            <TextContent>
                                <Text component={TextVariants.h4}>
                                    {_("Conflict Entry")}
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem className="ds-margin-top-lg" span={6}>
                            <p className="ds-margin-top ds-right-align ds-font-size-sm">
                                {_("Child Entries: ")}<b>{conflictChildren}</b>
                            </p>
                        </GridItem>
                        <GridItem span={12}>
                            <TextArea id="conflictConflict" resizeOrientation="vertical" className="ds-conflict" value={conflict}  readOnlyVariant="default" />
                        </GridItem>
                        <hr />
                        <div className="ds-container">
                            <Radio
                                  name="resolve-choice"
                                  onChange={handleRadioChange}
                                  label={_("Delete Conflict Entry")}
                                  id="deleteConflictRadio"
                                  isChecked={deleteConflictRadio}
                            />
                            <div className="ds-left-margin">
                                <Tooltip
                                    position="top"
                                    content={
                                        <div>
                                            {_("This will delete the conflict entry, and the \"valid\" entry will remain intact.")}
                                        </div>
                                    }
                                >
                                    <OutlinedQuestionCircleIcon />
                                </Tooltip>
                            </div>
                        </div>
                        <div className="ds-container ds-margin-top">
                            <Radio
                                  name="resolve-choice"
                                  onChange={handleRadioChange}
                                  label={_("Swap Conflict Entry With Valid Entry")}
                                  id="swapConflictRadio"
                                  isChecked={swapConflictRadio}
                            />
                            <div className="ds-left-margin">
                                <Tooltip
                                    className="ds-margin-left"
                                    position="top"
                                    content={
                                        <div>
                                            {_("This will replace the \"valid\" entry with the conflict entry, but keeping the valid entry DN intact.")}
                                        </div>
                                    }
                                >
                                    <OutlinedQuestionCircleIcon />
                                </Tooltip>
                            </div>
                        </div>
                        <div className="ds-container ds-margin-top">
                            <Radio
                                  name="resolve-choice"
                                  onChange={handleRadioChange}
                                  label={_("Convert Conflict Entry Into New Entry")}
                                  id="convertConflictRadio"
                                  isChecked={convertConflictRadio}
                            />
                            <div className="ds-left-margin">
                                <Tooltip
                                    position="top"
                                    content={
                                        <div>
                                            {_("The conflict entry uses a multi-valued RDN to specify the original DN and it's nsUniqueID.  To convert the conflict entry to a new entry you must provide a new RDN attribute/value for the new entry.  \"RDN_ATTRIBUTE=VALUE\".  For example: cn=my_new_entry")}
                                        </div>
                                    }
                                >
                                    <OutlinedQuestionCircleIcon />
                                </Tooltip>
                            </div>
                        </div>
                        <div className="ds-margin-top ds-margin-left-sm">
                            <TextInput
                                placeholder={_("Enter new RDN here")}
                                type="text"
                                onChange={handleChange}
                                aria-label="new rdn label"
                                id="convertRDN"
                                value={orig_rdn}
                                isDisabled={!convertConflictRadio}
                            />
                        </div>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

class ReportCredentialsModal extends React.Component {
    render() {
        const {
            handleFieldChange,
            showModal,
            closeHandler,
            newEntry,
            hostname,
            port,
            binddn,
            pwInputInterractive,
            bindpw,
            addConfig,
            editConfig,
            onMinusConfig,
            onPlusConfig,
            onConfigChange,
        } = this.props;

        const value = (newEntry ? _("Add") : _("Edit"));
        const title = cockpit.format(_("$0 Report Credentials"), value);

        return (
            <Modal
                variant={ModalVariant.medium}
                title={title}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="save"
                        variant="primary"
                        onClick={newEntry ? addConfig : editConfig}
                        isDisabled={
                            hostname === "" || binddn === "" ||
                            (bindpw === "" && !pwInputInterractive)
                        }
                    >
                        {_("Save")}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Grid>
                    <GridItem span={12}>
                        <Form isHorizontal autoComplete="off">
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    {_("Hostname")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={hostname}
                                        type="text"
                                        id="credsHostname"
                                        aria-describedby="cachememsize"
                                        name="credsHostname"
                                        onChange={(e, str) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    {_("Port")}
                                </GridItem>
                                <GridItem span={9}>
                                    <NumberInput
                                        value={port}
                                        min={1}
                                        max={65534}
                                        onMinus={() => { onMinusConfig("credsPort") }}
                                        onChange={(e) => { onConfigChange(e, "credsPort", 1) }}
                                        onPlus={() => { onPlusConfig("credsPort") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={8}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("Bind DN for the specified instances")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Bind DN")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={binddn}
                                        type="text"
                                        id="credsBinddn"
                                        aria-describedby="cachememsize"
                                        name="credsBinddn"
                                        onChange={(e, str) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("Bind password for the specified instances")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Password")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={bindpw}
                                        type="password"
                                        id="credsBindpw"
                                        aria-describedby="cachememsize"
                                        name="credsBindpw"
                                        isDisabled={pwInputInterractive}
                                        onChange={(e, str) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("Input the password interactively")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Interractive Input")}
                                </GridItem>
                                <GridItem span={9}>
                                    <Checkbox
                                        isChecked={pwInputInterractive}
                                        id="pwInputInterractive"
                                        onChange={(e, checked) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                        </Form>
                    </GridItem>
                </Grid>
            </Modal>
        );
    }
}

class ReportConnectionModal extends React.Component {
    render() {
        const {
            handleFieldChange,
            showModal,
            closeHandler,
            name,
            hostname,
            port,
            binddn,
            pwInputInterractive,
            bindpw,
            addConn,
            onMinusConfig,
            onPlusConfig,
            onConfigChange,
        } = this.props;

        return (
            <Modal
                variant={ModalVariant.medium}
                title={_("Add Replica Connection")}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="save"
                        variant="primary"
                        onClick={addConn}
                        isDisabled={
                            name === "" || hostname === "" || port === "" ||
                            binddn === "" || (bindpw === "" && !pwInputInterractive)
                        }
                    >
                        {_("Save")}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Grid>
                    <GridItem span={12}>
                        <Form isHorizontal autoComplete="off">
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    {_("Connection Name")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={name}
                                        type="text"
                                        id="connName"
                                        aria-describedby="connName"
                                        name="connName"
                                        onChange={(e, str) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    {_("Hostname")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={hostname}
                                        type="text"
                                        id="connHostname"
                                        aria-describedby="connHostname"
                                        name="connHostname"
                                        onChange={(e, str) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    {_("Port")}
                                </GridItem>
                                <GridItem span={9}>
                                    <NumberInput
                                        value={port}
                                        min={1}
                                        max={65534}
                                        onMinus={() => { onMinusConfig("connPort") }}
                                        onChange={(e) => { onConfigChange(e, "connPort", 1) }}
                                        onPlus={() => { onPlusConfig("connPort") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={8}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("Bind DN for the specified instances")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Bind DN")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={binddn}
                                        type="text"
                                        id="connBindDN"
                                        aria-describedby="connBindDN"
                                        name="connBindDN"
                                        onChange={(e, str) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("Bind password for the specified instance.  You can also specify a password file but the filename needs to be inside of brackets [/PATH/FILE]")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Password")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={bindpw}
                                        type="password"
                                        id="connCred"
                                        aria-describedby="connCred"
                                        name="connCred"
                                        isDisabled={pwInputInterractive}
                                        onChange={(e, str) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("Input the password interactively, stores '*' as the password value in .dsrc")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Interractive Input")}
                                </GridItem>
                                <GridItem span={9}>
                                    <Checkbox
                                        isChecked={pwInputInterractive}
                                        id="pwInputInterractive"
                                        onChange={(e, checked) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                        </Form>
                    </GridItem>
                </Grid>
            </Modal>
        );
    }
}

class ReportAliasesModal extends React.Component {
    render() {
        const {
            handleFieldChange,
            showModal,
            closeHandler,
            newEntry,
            hostname,
            port,
            alias,
            addConfig,
            editConfig,
            onMinusConfig,
            onPlusConfig,
            onConfigChange
        } = this.props;

        const value = (newEntry ? _("Add") : _("Edit"));
        const title = cockpit.format(_("$0 Report Alias"), value);

        return (
            <Modal
                variant={ModalVariant.medium}
                title={title}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={newEntry ? addConfig : editConfig}
                        isDisabled={alias === "" || hostname === ""}
                    >
                        {_("Save")}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Grid>
                    <GridItem span={12}>
                        <Form isHorizontal autoComplete="off">
                            <Grid title={_("Alias name for the instance")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Alias")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={alias}
                                        type="text"
                                        id="aliasName"
                                        aria-describedby="aliasName"
                                        name="aliasName"
                                        onChange={(e, str) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("An instance hostname")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Hostname")}
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={hostname}
                                        type="text"
                                        id="aliasHostname"
                                        aria-describedby="aliasHostname"
                                        name="aliasHostname"
                                        onChange={(e, str) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title={_("An instance port")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Port")}
                                </GridItem>
                                <GridItem span={9}>
                                    <NumberInput
                                        value={port}
                                        min={1}
                                        max={65534}
                                        onMinus={() => { onMinusConfig("aliasPort") }}
                                        onChange={(e) => { onConfigChange(e, "aliasPort", 1) }}
                                        onPlus={() => { onPlusConfig("aliasPort") }}
                                        inputName="input"
                                        inputAriaLabel="number input"
                                        minusBtnAriaLabel="minus"
                                        plusBtnAriaLabel="plus"
                                        widthChars={8}
                                    />
                                </GridItem>
                            </Grid>
                        </Form>
                    </GridItem>
                </Grid>
            </Modal>
        );
    }
}

class ReportLoginModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            processCredsInput,
            instanceName,
            disableBinddn,
            loginBinddn,
            loginBindpw
        } = this.props;

        const title = cockpit.format(_("Replication Login Credentials for $0"), instanceName);

        return (
            <Modal
                variant={ModalVariant.medium}
                title={title}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        isDisabled={loginBinddn === "" || loginBindpw === ""}
                        onClick={processCredsInput}
                    >
                        {_("Confirm Credentials Input")}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        {_("Cancel")}
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text component={TextVariants.h5}>
                            {_("In order to get the replication agreement lag times and state, the authentication credentials to the remote replicas must be provided.")}
                        </Text>
                    </TextContent>
                    <hr />
                    <TextContent>
                        <Text component={TextVariants.h5}>
                            {_("Bind DN was acquired from <b>Replica Credentials</b> table. If you want to bind as another user, change or remove the Bind DN there.")}
                        </Text>
                    </TextContent>
                    <Grid className="ds-margin-top-lg" title={_("Bind DN for the instance")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Bind DN")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={loginBinddn}
                                type="text"
                                id="loginBinddn"
                                aria-describedby="loginBinddn"
                                name="loginBinddn"
                                isDisabled={disableBinddn}
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("Password for the Bind DN")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Password")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={loginBindpw}
                                type="password"
                                id="loginBindpw"
                                aria-describedby="loginBindpw"
                                name="loginBindpw"
                                onChange={(e, str) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                </Form>
            </Modal>
        );
    }
}

class FullReportContent extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            oneTableReport: false,
            showDisabledAgreements: false
        };

        this.handleSwitchChange = this.handleSwitchChange.bind(this);
    }

    handleSwitchChange(e) {
        if (typeof e === "boolean") {
            // Handle Switch object
            this.setState({
                oneTableReport: e
            });
        } else {
            this.setState({
                [e.target.id]: e.target.checked
            });
        }
    }

    render() {
        const {
            reportData,
            handleRefresh,
            reportRefreshing,
            reportLoading
        } = this.props;

        let suppliers = [];
        let supplierName;
        let supplierData;
        let resultGrids = [];
        let spinner = "";
        if (reportLoading) {
            const value = reportRefreshing ? _("Refreshing") : _("Loading");
            spinner = (
                <div title={_("Do the refresh every few seconds")}>
                    {cockpit.format(_("$0 the report..."), value)}
                    <Spinner className="ds-left-margin" size="lg" />
                </div>
            );
        }
        let reportHeader = (
            <TextContent>
                <Text className="ds-margin-top-xlg" component={TextVariants.h4}>
                    {_("There is no report, you must first generate the report in the <b>Prepare Report</b> tab.")}
                </Text>
            </TextContent>
        );
        if (reportData.length > 0) {
            reportHeader = (
                <div>
                    <Form isHorizontal autoComplete="off">
                        <Grid>
                            <GridItem span={8}>
                                <Checkbox
                                    title={_("Display all agreements including the disabled ones and the ones we failed to connect to")}
                                    isChecked={this.state.showDisabledAgreements}
                                    id="showDisabledAgreements"
                                    onChange={(e, checked) => {
                                        this.handleSwitchChange(e);
                                    }}
                                    label={_("Show Disabled Agreements")}
                                />
                            </GridItem>
                            <GridItem span={4}>
                                <Button
                                    key="refresh"
                                    variant="secondary"
                                    onClick={handleRefresh}
                                    className="ds-float-right"
                                >
                                    {_("Refresh Report")}
                                </Button>
                            </GridItem>
                            <GridItem span={12}>
                                <Checkbox
                                    isChecked={this.state.oneTableReport}
                                    onChange={(e, checked) => {
                                        this.handleSwitchChange(e);
                                    }}
                                    id="oneTableReport"
                                    title={_("Show all data in one table (it makes it easier to check lag times)")}
                                    label={_("Table View")}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                </div>
            );
        }
        if (this.state.oneTableReport) {
            for (const supplier of reportData) {
                for (const replica of supplier.data) {
                    let idx = replica.agmts_status.length;
                    const agmts = JSON.parse(JSON.stringify(replica.agmts_status));
                    while (idx--) {
                        if (!this.state.showDisabledAgreements &&
                            'replica-enabled' in agmts[idx] &&
                            agmts[idx]['replica-enabled'][0] === "off") {
                            // remove disabled agmt
                            agmts.splice(idx, 1);
                        }
                    }
                    resultGrids = resultGrids.concat(agmts);
                }
            }
            suppliers = [(
                <div key="supp1">
                    <ReportSingleTable
                        key={resultGrids}
                        rows={resultGrids}
                        viewAgmt={this.props.viewAgmt}
                    />
                </div>
            )];
        } else {
            for (const supplier of reportData) {
                const s_data = supplier.data;
                if (s_data.length === 1 &&
                    (s_data[0].replica_status.startsWith("Unavailable") ||
                     s_data[0].replica_status.startsWith("Unreachable"))) {
                    supplierData = (
                        <div>
                            <TextContent>
                                <Text className="ds-margin-top-xlg" component={TextVariants.h4}>
                                    <b>{_("Can not get replication information from Replica:")}</b>&nbsp;&nbsp;{s_data[0].replica_status}
                                </Text>
                            </TextContent>
                        </div>
                    );
                } else {
                    // Create deep copy of supplier data, so we can filter it
                    // without changing the original data
                    const supData = JSON.parse(JSON.stringify(supplier.data));
                    for (const replica of supData) {
                        let idx = replica.agmts_status.length;
                        while (idx--) {
                            if (!this.state.showDisabledAgreements &&
                                'replica-enabled' in replica.agmts_status[idx] &&
                                replica.agmts_status[idx]['replica-enabled'][0] === "off") {
                                // remove disabled agmt
                                replica.agmts_status.splice(idx, 1);
                            }
                        }
                    }

                    supplierData = supData.map(replica => (
                        <Grid
                            key={replica.replica_root + replica.replica_id}
                            className="ds-margin-top-lg"
                        >
                            <GridItem span={2}>
                                {_("Replica Root")}
                            </GridItem>
                            <GridItem span={10}>
                                <b>{replica.replica_root}</b>
                            </GridItem>
                            <GridItem span={2}>
                                {_("Replica ID")}
                            </GridItem>
                            <GridItem span={10}>
                                <b>{replica.replica_id}</b>
                            </GridItem>
                            <GridItem span={2}>
                                {_("Max CSN")}
                            </GridItem>
                            <GridItem span={10}>
                                <b>{replica.maxcsn}</b>
                            </GridItem>
                            <GridItem span={2}>
                                {_("Replica Status")}
                            </GridItem>
                            <GridItem span={10}>
                                <b>{replica.replica_status}</b>
                            </GridItem>

                            {"agmts_status" in replica &&
                            replica.agmts_status.length > 0 &&
                            "agmt-name" in replica.agmts_status[0]
                                ? (
                                    <ReportConsumersTable
                                    key={replica.agmts_status}
                                    rows={replica.agmts_status}
                                    viewAgmt={this.props.viewAgmt}
                                    />
                                )
                                : (
                                    <TextContent>
                                        <Text component={TextVariants.h4}>
                                            <b><i>{_("No Agreements Were Found")}</i></b>
                                        </Text>
                                    </TextContent>
                                )}
                        </Grid>
                    ));
                }
                supplierName = (
                    <div className="ds-margin-top-xlg" key={supplier.name}>
                        <TextContent title={_("Supplier host:port (and alias if applicable)")}>
                            <Text component={TextVariants.h2}>
                                <CopyIcon />&nbsp;&nbsp;<b>{_("Supplier:")}</b>&nbsp;&nbsp;{supplier.name}
                            </Text>
                        </TextContent>
                        <hr />
                        {supplierData}
                    </div>
                );
                suppliers.push(supplierName);
            }
        }

        let report = suppliers.map(supplier => (
            <div key={supplier.key}>
                {supplier}
            </div>
        ));
        if (reportLoading) {
            report = (
                <GridItem span={12} className="ds-center ds-margin-top">
                    {spinner}
                </GridItem>
            );
        }

        return (
            <div>
                {reportHeader}
                {report}
                <hr />
            </div>
        );
    }
}

class ScatterLineChart extends React.Component {
    constructor(props) {
        super(props);
        this.containerRef = React.createRef();
        this.observer = () => {};
        this.state = {
            width: 0
        };
        this.handleResize = () => {
            if (this.containerRef.current && this.containerRef.current.clientWidth) {
                this.setState({ width: this.containerRef.current.clientWidth });
            }
        };
    }

    componentDidMount() {
        this.observer = getResizeObserver(this.containerRef.current, this.handleResize);
        this.handleResize();
    }

    componentWillUnmount() {
        this.observer();
    }

    render() {
        const { width } = this.state;
        const { chartData, title, yAxisLabel, xAxisLabel, maxY, minY } = this.props;

        // If no data is available, show a message
        if (!chartData || !chartData.series || chartData.series.length === 0) {
            return (
                <div className="pf-v5-c-empty-state">
                    <div className="pf-v5-c-empty-state__content">
                        <i className="pf-v5-c-empty-state__icon pf-v5-pficon pf-v5-pficon-info" />
                        <h2 className="pf-v5-c-title pf-m-lg">{_("No data available")}</h2>
                        <div className="pf-v5-c-empty-state__body">
                            {_("There is no data available for this chart.")}
                        </div>
                    </div>
                </div>
            );
        }

        // Don't render the chart until we have a valid width
        if (!width || width <= 0) {
            return (
                <div ref={this.containerRef} style={{ height: '400px' }}>
                    <div className="ds-center">
                        <Spinner size="lg" />
                        <p className="ds-margin-top">{_("Preparing chart...")}</p>
                    </div>
                </div>
            );
        }

        // Process dates to ensure proper formatting for the chart
        const series = chartData.series.map(s => {
            // Ensure data points have proper format, including date conversion
            const processedDatapoints = s.datapoints.map(dp => ({
                ...dp,
                // If x is an ISO date string, convert it to a Date object for the chart
                x: new Date(dp.x),
                name: dp.name
            }));

            return {
                ...s,
                datapoints: processedDatapoints
            };
        });

        // Calculate Y-axis domain if not provided
        let calculatedMaxY = maxY;
        let calculatedMinY = minY;

        if (!calculatedMaxY || !calculatedMinY) {
            // Find min and max Y values across all series
            let allYValues = [];
            series.forEach(s => {
                s.datapoints.forEach(dp => {
                    allYValues.push(dp.y);
                });
            });

            if (allYValues.length > 0) {
                const dataMin = Math.min(...allYValues);
                const dataMax = Math.max(...allYValues);

                // Set min to 0 or slightly below the minimum value (never negative)
                calculatedMinY = minY !== undefined ? minY : Math.max(0, dataMin * 0.9);

                // Set max to slightly above the maximum value
                calculatedMaxY = maxY !== undefined ? maxY : dataMax * 1.1;
            } else {
                // Default values if no data points
                calculatedMinY = 0;
                calculatedMaxY = 10;
            }
        }

        // Process tooltip HTML tags
        const formatTooltip = (datum) => {
            if (datum.childName && datum.childName.includes('line-')) {
                // Replace <br> tags with actual line breaks for tooltip display
                if (datum.hoverInfo) {
                    // Split the hoverInfo by <br> tags and join with newlines
                    return datum.hoverInfo.split(/<br\s*\/?>/i).join('\n');
                }
                return `${datum.name}: ${datum.y.toFixed(3)}s`;
            }
            return null;
        };

        // Ensure we never have negative width or height to avoid SVG rendering errors
        const safeWidth = Math.max(width, 10);
        const safeHeight = 400;

        return (
            <div ref={this.containerRef}>
                <div style={{ height: safeHeight + 'px' }}>
                    <Chart
                        ariaDesc={title || "Replication data chart"}
                        ariaTitle={title || "Replication data chart"}
                        containerComponent={
                            <ChartVoronoiContainer
                                labels={({ datum }) => formatTooltip(datum)}
                                constrainToVisibleArea
                                labelComponent={
                                    <ChartTooltip
                                        style={{
                                            fontSize: "12px",
                                            padding: 10,
                                            whiteSpace: "pre-line" // Important for newlines
                                        }}
                                    />
                                }
                            />
                        }
                        legendData={series.map(s => s.legendItem)}
                        legendPosition="bottom"
                        legendOrientation="horizontal"
                        height={safeHeight}
                        maxDomain={{ y: calculatedMaxY }}
                        minDomain={{ y: calculatedMinY }}
                        padding={{
                            bottom: 100,
                            left: 80,
                            right: 50,
                            top: 50
                        }}
                        themeColor={ChartThemeColor.blue}
                        width={safeWidth}
                        scale={{ x: 'time' }}
                        style={{
                            background: { fill: "var(--pf-v5-global--BackgroundColor--100, #f9f9f9)" },
                            axis: {
                                grid: { stroke: "var(--pf-v5-global--BorderColor--100, #e8e8e8)", strokeWidth: 1 },
                                ticks: { stroke: "var(--pf-v5-global--Color--200, #999)", size: 5 }
                            },
                            legend: {
                                labels: { fill: "var(--pf-v5-global--Color--100)" }
                            }
                        }}
                    >
                        <ChartAxis
                            label={xAxisLabel || ""}
                            tickFormat={(t) => {
                                // Format time as HH:MM:SS
                                const date = new Date(t);
                                return date.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit' });
                            }}
                            style={{
                                axisLabel: {
                                    fontSize: 14,
                                    padding: 40,
                                    fill: "var(--pf-v5-global--Color--100)"
                                },
                                tickLabels: {
                                    fontSize: 12,
                                    padding: 5,
                                    fill: "var(--pf-v5-global--Color--100)"
                                }
                            }}
                        />
                        <ChartAxis
                            dependentAxis
                            showGrid
                            label={yAxisLabel || "Value"}
                            tickFormat={(t) => `${t.toFixed(2)}s`}
                            style={{
                                axisLabel: {
                                    fontSize: 14,
                                    padding: 55,
                                    fill: "var(--pf-v5-global--Color--100)"
                                },
                                tickLabels: {
                                    fontSize: 12,
                                    padding: 5,
                                    fill: "var(--pf-v5-global--Color--100)"
                                },
                                grid: {
                                    stroke: "var(--pf-v5-global--BorderColor--100)"
                                }
                            }}
                        />
                        <ChartGroup>
                            {series.map((s, idx) => (
                                <ChartScatter
                                    key={`scatter-${idx}`}
                                    name={`scatter-${idx}`}
                                    data={s.datapoints}
                                    style={{
                                        data: {
                                            fill: s.color
                                        }
                                    }}
                                />
                            ))}
                        </ChartGroup>
                        <ChartGroup>
                            {series.map((s, idx) => (
                                <ChartLine
                                    key={`line-${idx}`}
                                    name={`line-${idx}`}
                                    data={s.datapoints}
                                    style={{
                                        data: {
                                            stroke: s.color,
                                            strokeWidth: 2,
                                            strokeDasharray: s.style?.data?.strokeDasharray
                                        }
                                    }}
                                />
                            ))}
                        </ChartGroup>
                    </Chart>
                </div>
            </div>
        );
    }
}

ScatterLineChart.propTypes = {
    chartData: PropTypes.shape({
        title: PropTypes.string,
        xAxisLabel: PropTypes.string,
        yAxisLabel: PropTypes.string,
        series: PropTypes.arrayOf(
            PropTypes.shape({
                datapoints: PropTypes.arrayOf(
                    PropTypes.shape({
                        name: PropTypes.string,
                        x: PropTypes.oneOfType([PropTypes.string, PropTypes.instanceOf(Date)]),
                        y: PropTypes.number,
                        hoverInfo: PropTypes.string
                    })
                ),
                legendItem: PropTypes.shape({
                    name: PropTypes.string
                }),
                color: PropTypes.string,
                style: PropTypes.object
            })
        )
    }),
    title: PropTypes.string,
    xAxisLabel: PropTypes.string,
    yAxisLabel: PropTypes.string,
    maxY: PropTypes.number,
    minY: PropTypes.number
};

class LagReportModal extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            activeTabKey: 0, // 0 = Summary, 1 = Charts, 2 = PNG Report, 3 = CSV Report, 4 = Report Files
            csvPreview: null,
            jsonSummary: null,
            jsonData: null, // PatternFly chart data
            loading: false,
            error: null,
            pngDataUrl: null,
            pngError: false,
            summary: null,
            suffixStats: {}
        };

        this.handleTabClick = this.handleTabClick.bind(this);
        this.loadData = this.loadData.bind(this);
        this.downloadFile = this.downloadFile.bind(this);
        this.fallbackDownload = this.fallbackDownload.bind(this);
        this.openInNewWindow = this.openInNewWindow.bind(this);
        this.loadPngAsDataUrl = this.loadPngAsDataUrl.bind(this);
    }

    componentDidMount() {
        this.loadData();
    }

    componentDidUpdate(prevProps) {
        if (this.props.reportUrls !== prevProps.reportUrls) {
            this.loadData();
        }
    }

    componentWillUnmount() {
        // Clean up any data URLs to prevent memory leaks
        if (this.state.pngDataUrl && this.state.pngDataUrl.startsWith('blob:')) {
            URL.revokeObjectURL(this.state.pngDataUrl);
        }
    }

    handleTabClick(event, tabIndex) {
        this.setState({ activeTabKey: tabIndex });
    }

    loadData() {
        this.setState({ loading: true });

        // Load PatternFly chart JSON data first if available (priority for rendering charts)
        if (this.props.reportUrls && this.props.reportUrls.json) {
            cockpit.file(this.props.reportUrls.json)
                .read()
                .then(content => {
                    try {
                        const data = JSON.parse(content);
                        this.setState({ jsonData: data, loading: false });
                        console.log("Successfully loaded PatternFly chart data");
                    } catch (e) {
                        console.error("Error parsing JSON data for charts:", e);
                        this.setState({ loading: false });
                    }
                })
                .catch(err => {
                    console.error("Error loading JSON data for charts:", err);
                    this.setState({ loading: false });
                });
        } else {
            this.setState({ loading: false });
        }

        // Load PNG data if available - as a secondary display option
        if (this.props.reportUrls && this.props.reportUrls.png) {
            this.loadPngAsDataUrl(this.props.reportUrls.png);
        } else {
            // Make sure pngDataUrl is null if no PNG is available
            this.setState({ pngDataUrl: null });
        }

        // Load CSV preview if available - as a secondary data option
        if (this.props.reportUrls && this.props.reportUrls.csv) {
            cockpit.file(this.props.reportUrls.csv)
                .read()
                .then(content => {
                    // Just show first few lines
                    const lines = content.split('\n').slice(0, 20);
                    this.setState({ csvPreview: lines.join('\n') });
                })
                .catch(err => {
                    console.error("Error loading CSV:", err);
                });
        }

        // Load summary JSON if available
        if (this.props.reportUrls && this.props.reportUrls.summary) {
            cockpit.file(this.props.reportUrls.summary)
                .read()
                .then(content => {
                    try {
                        const data = JSON.parse(content);
                        this.setState({
                            summary: data.analysis_summary,
                            suffixStats: data.suffix_statistics || {},
                            activeTabKey: 0
                        });
                    } catch (e) {
                        console.error("Error parsing JSON summary:", e);
                    }
                })
                .catch(err => {
                    console.error("Error loading JSON summary:", err);
                });
        }
    }

    loadPngAsDataUrl(pngPath) {
        if (!pngPath) return;

        // Use the base64 command line tool to encode the file
        cockpit.spawn(["base64", pngPath])
            .then(base64Output => {
                // Create a data URL with the base64 content
                const dataUrl = `data:image/png;base64,${base64Output.trim()}`;
                this.setState({ pngDataUrl: dataUrl });
            })
            .catch(err => {
                console.error("Error encoding PNG file to base64:", err);

                // Fallback to reading the file and using FileReader
                console.log("Trying fallback method...");
                cockpit.file(pngPath).read()
                    .then(content => {
                        if (content) {
                            try {
                                const blob = new Blob([content], { type: 'image/png' });
                                const reader = new FileReader();
                                reader.onload = () => {
                                    const dataUrl = reader.result;
                                    this.setState({ pngDataUrl: dataUrl });
                                };
                                reader.onerror = () => {
                                    console.error("Error reading blob as data URL");
                                    this.setState({ pngError: true });
                                };
                                reader.readAsDataURL(blob);
                            } catch (error) {
                                console.error("Error creating data URL:", error);
                                this.setState({ pngError: true });
                            }
                        } else {
                            console.error("No content returned from file");
                            this.setState({ pngError: true });
                        }
                    })
                    .catch(fileErr => {
                        console.error("Both methods failed to load PNG:", fileErr);
                        this.setState({ pngError: true });
                    });
            });
    }

    downloadFile(url, filename) {
        if (!url) return;

        console.log("Downloading file:", url, "as", filename);

        // Special handling for PNG files
        if (filename.endsWith('.png')) {
            if (this.state.pngDataUrl) {
                try {
                    // If we have the data URL, use it directly
                    const a = document.createElement('a');
                    a.href = this.state.pngDataUrl;
                    a.download = filename;
                    document.body.appendChild(a);
                    a.click();
                    setTimeout(() => document.body.removeChild(a), 100);
                    return;
                } catch (error) {
                    console.error("Error downloading PNG from data URL:", error);
                }
            }

            // If we don't have a data URL or it failed, use base64 encoding
            cockpit.spawn(["base64", url], { err: "message" })
                .then(base64Output => {
                    const dataUrl = `data:image/png;base64,${base64Output.trim()}`;
                    const a = document.createElement('a');
                    a.href = dataUrl;
                    a.download = filename;
                    document.body.appendChild(a);
                    a.click();
                    setTimeout(() => document.body.removeChild(a), 100);
                })
                .catch(error => {
                    console.error("Error reading PNG file with base64:", error);
                    this.fallbackDownload(url, filename);
                });
            return;
        }

        // Text-based files (JSON, CSV, HTML)
        let readBinary = false;
        if (filename.endsWith('.csv')) {
            readBinary = false;
        } else if (filename.endsWith('.json') || filename.endsWith('.html')) {
            readBinary = false;
        }

        // Use cockpit.file() to read the file contents
        cockpit.file(url, { binary: readBinary }).read()
            .then(content => {
                if (!content) {
                    console.error("No content read from file:", url);
                    this.fallbackDownload(url, filename);
                    return;
                }

                // Determine the correct MIME type
                let mimeType = 'application/octet-stream';
                if (filename.endsWith('.json')) {
                    mimeType = 'application/json';
                } else if (filename.endsWith('.csv')) {
                    mimeType = 'text/csv';
                } else if (filename.endsWith('.html')) {
                    mimeType = 'text/html';
                }

                // Create a blob and download it
                const blob = new Blob([content], { type: mimeType });
                const blobUrl = URL.createObjectURL(blob);

                const a = document.createElement('a');
                a.href = blobUrl;
                a.download = filename;
                document.body.appendChild(a);
                a.click();

                setTimeout(() => {
                    document.body.removeChild(a);
                    URL.revokeObjectURL(blobUrl);
                }, 100);
            })
            .catch(error => {
                console.error("Error reading file with cockpit.file():", error);
                this.fallbackDownload(url, filename);
            });
    }

    // Fallback download method as a last resort
    fallbackDownload(url, filename) {
        console.log("Using fallback download method for:", url);

        // Try using cockpit.spawn to read the file with cat
        cockpit.spawn(["cat", url], { err: "message" })
            .then(content => {
                if (!content) {
                    console.error("No content read from file with cat:", url);
                    return;
                }

                // Determine MIME type
                let mimeType = 'application/octet-stream';
                if (filename.endsWith('.json')) {
                    mimeType = 'application/json';
                } else if (filename.endsWith('.csv')) {
                    mimeType = 'text/csv';
                } else if (filename.endsWith('.html')) {
                    mimeType = 'text/html';
                }

                // Create blob and download
                const blob = new Blob([content], { type: mimeType });
                const blobUrl = URL.createObjectURL(blob);

                const a = document.createElement('a');
                a.href = blobUrl;
                a.download = filename;
                document.body.appendChild(a);
                a.click();

                setTimeout(() => {
                    document.body.removeChild(a);
                    URL.revokeObjectURL(blobUrl);
                }, 100);
            })
            .catch(error => {
                console.error("Error reading file with cat:", error);

                // Final fallback: try direct link
                try {
                    const a = document.createElement('a');
                    a.href = url + '?t=' + new Date().getTime();
                    a.download = filename;
                    a.target = '_blank';
                    document.body.appendChild(a);
                    a.click();
                    document.body.removeChild(a);
                } catch (directError) {
                    console.error("All download methods failed:", directError);

                    // If we have a saveHandler from props, use it as a final fallback
                    if (this.props.saveHandler) {
                        this.props.saveHandler(url);
                    }
                }
            });
    }

    openInNewWindow(url) {
        if (!url) return;
        window.open(url, '_blank');
    }

    renderSummaryTab() {
        const { summary, suffixStats, loading } = this.state;
        const { reportUrls } = this.props;

        if (loading) {
            return (
                <div className="ds-center">
                    <Spinner size="lg" />
                    <p className="ds-margin-top">{_("Loading summary data...")}</p>
                </div>
            );
        }

        if (!summary) {
            return (
                <EmptyState>
                    <EmptyStateIcon icon={InfoIcon} />
                    <Title headingLevel="h2">{_("No Summary Available")}</Title>
                    <EmptyStateBody>
                        {_("No report summary could be loaded.")}
                    </EmptyStateBody>
                </EmptyState>
            );
        }

        return (
            <div className="ds-margin-top-lg">
                <Card>
                    <CardTitle>
                        <Title headingLevel="h2">{_("Replication Analysis Summary")}</Title>
                    </CardTitle>
                    <CardBody>
                        <Grid hasGutter>
                            <GridItem span={6}>
                                <Card isFlat>
                                    <CardTitle>{_("Analysis Overview")}</CardTitle>
                                    <CardBody>
                                        <DescriptionList isHorizontal>
                                            <DescriptionListGroup>
                                                <DescriptionListTerm>{_("Total Servers")}</DescriptionListTerm>
                                                <DescriptionListDescription>
                                                    {summary.total_servers || 0}
                                                </DescriptionListDescription>
                                            </DescriptionListGroup>
                                            <DescriptionListGroup>
                                                <DescriptionListTerm>{_("Analyzed Log Events")}</DescriptionListTerm>
                                                <DescriptionListDescription>
                                                    {summary.analyzed_logs || 0}
                                                </DescriptionListDescription>
                                            </DescriptionListGroup>
                                            <DescriptionListGroup>
                                                <DescriptionListTerm>{_("Total Updates")}</DescriptionListTerm>
                                                <DescriptionListDescription>
                                                    {summary.total_updates || 0}
                                                </DescriptionListDescription>
                                            </DescriptionListGroup>
                                        </DescriptionList>
                                    </CardBody>
                                </Card>
                            </GridItem>

                            <GridItem span={6}>
                                <Card isFlat>
                                    <CardTitle>{_("Replication Lag Statistics")}</CardTitle>
                                    <CardBody>
                                        <DescriptionList isHorizontal>
                                            <DescriptionListGroup>
                                                <DescriptionListTerm>{_("Minimum Lag")}</DescriptionListTerm>
                                                <DescriptionListDescription>
                                                    {summary.minimum_lag != null ? `${summary.minimum_lag.toFixed(3)} seconds` : _("N/A")}
                                                </DescriptionListDescription>
                                            </DescriptionListGroup>
                                            <DescriptionListGroup>
                                                <DescriptionListTerm>{_("Maximum Lag")}</DescriptionListTerm>
                                                <DescriptionListDescription>
                                                    {summary.maximum_lag != null ? `${summary.maximum_lag.toFixed(3)} seconds` : _("N/A")}
                                                </DescriptionListDescription>
                                            </DescriptionListGroup>
                                            <DescriptionListGroup>
                                                <DescriptionListTerm>{_("Average Lag")}</DescriptionListTerm>
                                                <DescriptionListDescription>
                                                    {summary.average_lag != null ? `${summary.average_lag.toFixed(3)} seconds` : _("N/A")}
                                                </DescriptionListDescription>
                                            </DescriptionListGroup>
                                        </DescriptionList>
                                    </CardBody>
                                </Card>
                            </GridItem>

                            {summary.updates_by_suffix && Object.keys(summary.updates_by_suffix).length > 0 && (
                                <GridItem span={12}>
                                    <Card isFlat>
                                        <CardTitle>{_("Updates by Suffix")}</CardTitle>
                                        <CardBody>
                                            <List>
                                                {Object.entries(summary.updates_by_suffix).map(([suffix, count]) => (
                                                    <ListItem key={suffix}>
                                                        <strong>{suffix}:</strong> {count} {_("updates")}
                                                    </ListItem>
                                                ))}
                                            </List>
                                        </CardBody>
                                    </Card>
                                </GridItem>
                            )}

                            {summary.time_range && (
                                <GridItem span={12}>
                                    <Card isFlat>
                                        <CardTitle>{_("Time Range")}</CardTitle>
                                        <CardBody>
                                            <DescriptionList isHorizontal>
                                                <DescriptionListGroup>
                                                    <DescriptionListTerm>{_("Start Time")}</DescriptionListTerm>
                                                    <DescriptionListDescription>
                                                        {summary.time_range.start || _("From beginning")}
                                                    </DescriptionListDescription>
                                                </DescriptionListGroup>
                                                <DescriptionListGroup>
                                                    <DescriptionListTerm>{_("End Time")}</DescriptionListTerm>
                                                    <DescriptionListDescription>
                                                        {summary.time_range.end || _("To current")}
                                                    </DescriptionListDescription>
                                                </DescriptionListGroup>
                                            </DescriptionList>
                                        </CardBody>
                                    </Card>
                                </GridItem>
                            )}
                        </Grid>
                        {reportUrls && reportUrls.summary && (
                            <div className="ds-margin-top">
                                <Button
                                    variant="secondary"
                                    onClick={() => this.downloadFile(reportUrls.summary, "replication_analysis_summary.json")}
                                >
                                    <DownloadIcon className="ds-right-margin-sm" />
                                    {_("Download Summary JSON")}
                                </Button>
                            </div>
                        )}
                    </CardBody>
                </Card>
            </div>
        );
    }

    renderChartsTab() {
        const { reportUrls } = this.props;
        const { loading, jsonData } = this.state;

        if (loading) {
            return (
                <div className="ds-center">
                    <Spinner size="lg" />
                    <p className="ds-margin-top">{_("Loading chart data...")}</p>
                </div>
            );
        }

        if (!reportUrls || !reportUrls.json) {
            return (
                <EmptyState>
                    <EmptyStateIcon icon={InfoIcon} />
                    <Title headingLevel="h2">{_("No Chart Data Available")}</Title>
                    <EmptyStateBody>
                        {_("No JSON chart data was found or could be loaded.")}
                    </EmptyStateBody>
                </EmptyState>
            );
        }

        // Safely extract replication lag chart data
        const hasReplicationLags = jsonData && jsonData.replicationLags &&
                                  jsonData.replicationLags.series &&
                                  jsonData.replicationLags.series.length > 0;

        const hasHopLags = jsonData && jsonData.hopLags &&
                          jsonData.hopLags.series &&
                          jsonData.hopLags.series.length > 0;

        if (!jsonData || (!hasReplicationLags && !hasHopLags)) {
            return (
                <EmptyState>
                    <EmptyStateIcon icon={InfoIcon} />
                    <Title headingLevel="h2">{_("No Chart Data Available")}</Title>
                    <EmptyStateBody>
                        {_("Chart data was found but contains no visualization data.")}
                    </EmptyStateBody>
                    <Button
                        variant="secondary"
                        onClick={() => this.downloadFile(reportUrls.json, "replication_analysis.json")}
                    >
                        <DownloadIcon className="ds-right-margin-sm" />
                        {_("Download JSON Data")}
                    </Button>
                </EmptyState>
            );
        }

        return (
            <div className="ds-margin-top">
                <Grid hasGutter>
                    <GridItem span={12}>
                        <Card>
                            <CardTitle>{_("Interactive Charts")}</CardTitle>
                            <CardBody>
                                {hasReplicationLags && (
                                    <div className="ds-margin-bottom">
                                        <Title headingLevel="h3">
                                            {jsonData.replicationLags.title || _("Global Replication Lag Over Time")}
                                        </Title>
                                        <ScatterLineChart
                                            chartData={jsonData.replicationLags}
                                            title={jsonData.replicationLags.title || _("Global Replication Lag Over Time")}
                                            xAxisLabel={(jsonData.replicationLags.xAxisLabel || "").replace(/\s*Time\s*/g, "")}
                                            yAxisLabel={jsonData.replicationLags.yAxisLabel || _("Lag Time (seconds)")}
                                        />
                                    </div>
                                )}
                                {hasHopLags && (
                                    <div className="ds-margin-top-lg">
                                        <Title headingLevel="h3">
                                            {jsonData.hopLags.title || _("Per-Hop Replication Lags")}
                                        </Title>
                                        <ScatterLineChart
                                            chartData={jsonData.hopLags}
                                            title={jsonData.hopLags.title || _("Per-Hop Replication Lags")}
                                            xAxisLabel={(jsonData.hopLags.xAxisLabel || "").replace(/\s*Time\s*/g, "")}
                                            yAxisLabel={jsonData.hopLags.yAxisLabel || _("Hop Lag Time (seconds)")}
                                        />
                                    </div>
                                )}
                                <div className="ds-margin-top">
                                    <Button
                                        variant="secondary"
                                        onClick={() => this.downloadFile(reportUrls.json, "replication_analysis.json")}
                                    >
                                        <DownloadIcon className="ds-right-margin-sm" />
                                        {_("Download JSON Data")}
                                    </Button>
                                </div>
                            </CardBody>
                        </Card>
                    </GridItem>
                </Grid>
            </div>
        );
    }

    renderPngTab() {
        const { reportUrls } = this.props;
        const { loading, pngDataUrl, pngError } = this.state;

        if (loading) {
            return (
                <div className="ds-center">
                    <Spinner size="lg" />
                    <p className="ds-margin-top">{_("Loading PNG data...")}</p>
                </div>
            );
        }

        if (!reportUrls || !reportUrls.png) {
            return (
                <EmptyState>
                    <EmptyStateIcon icon={InfoIcon} />
                    <Title headingLevel="h2">{_("No PNG Report Available")}</Title>
                    <EmptyStateBody>
                        {_("No PNG report was generated. Make sure to select PNG format when generating the report.")}
                    </EmptyStateBody>
                </EmptyState>
            );
        }

        if (pngError) {
            return (
                <EmptyState>
                    <EmptyStateIcon icon={ExclamationCircleIcon} />
                    <Title headingLevel="h2">{_("Error Loading PNG")}</Title>
                    <EmptyStateBody>
                        {_("There was an error loading the PNG image. Try downloading it instead.")}
                    </EmptyStateBody>
                    <Button
                        variant="primary"
                        onClick={() => this.downloadFile(reportUrls.png, "replication_analysis.png")}
                    >
                        <DownloadIcon className="ds-right-margin-sm" />
                        {_("Download PNG")}
                    </Button>
                </EmptyState>
            );
        }

        return (
            <div className="ds-margin-top">
                <Grid hasGutter>
                    <GridItem span={12}>
                        <Card>
                            <CardTitle>{_("Static Image Report")}</CardTitle>
                            <CardBody>
                                {pngDataUrl ? (
                                    <div className="ds-margin-bottom ds-text-center">
                                        <img
                                            src={pngDataUrl}
                                            alt={_("Replication Analysis Chart")}
                                            style={{ maxWidth: '100%', height: 'auto' }}
                                        />
                                    </div>
                                ) : (
                                    <div className="ds-center">
                                        <Spinner size="lg" />
                                        <p className="ds-margin-top">{_("Loading image...")}</p>
                                    </div>
                                )}
                                <div className="ds-margin-top ds-text-center">
                                    <Button
                                        variant="primary"
                                        onClick={() => this.downloadFile(reportUrls.png, "replication_analysis.png")}
                                    >
                                        <DownloadIcon className="ds-right-margin-sm" />
                                        {_("Download PNG")}
                                    </Button>
                                </div>
                            </CardBody>
                        </Card>
                    </GridItem>
                </Grid>
            </div>
        );
    }

    renderCsvTab() {
        const { reportUrls } = this.props;
        const { loading, csvPreview } = this.state;

        if (loading) {
            return (
                <div className="ds-center">
                    <Spinner size="lg" />
                    <p className="ds-margin-top">{_("Loading CSV data...")}</p>
                </div>
            );
        }

        if (!reportUrls || !reportUrls.csv) {
            return (
                <EmptyState>
                    <EmptyStateIcon icon={InfoIcon} />
                    <Title headingLevel="h2">{_("No CSV Report Available")}</Title>
                    <EmptyStateBody>
                        {_("No CSV report was generated. Make sure to select CSV format when generating the report.")}
                    </EmptyStateBody>
                </EmptyState>
            );
        }

        return (
            <div className="ds-margin-top">
                <Grid hasGutter>
                    <GridItem span={12}>
                        <Card>
                            <CardTitle>{_("CSV Data Preview")}</CardTitle>
                            <CardBody>
                                <div className="ds-margin-bottom">
                                    {csvPreview ? (
                                        <TextContent>
                                            <Text component={TextVariants.h3}>{_("First 20 lines of CSV data:")}</Text>
                                            <pre className="ds-code-block ds-no-margin-bottom">{csvPreview}</pre>
                                        </TextContent>
                                    ) : (
                                        <EmptyState>
                                            <EmptyStateIcon icon={InfoIcon} />
                                            <Title headingLevel="h3">{_("CSV Preview Not Available")}</Title>
                                            <EmptyStateBody>
                                                {_("Preview could not be loaded. Try downloading the CSV file.")}
                                            </EmptyStateBody>
                                        </EmptyState>
                                    )}
                                </div>
                                <div className="ds-margin-top ds-text-center">
                                    <Button
                                        variant="primary"
                                        onClick={() => this.downloadFile(reportUrls.csv, "replication_analysis.csv")}
                                    >
                                        <DownloadIcon className="ds-right-margin-sm" />
                                        {_("Download CSV")}
                                    </Button>
                                </div>
                            </CardBody>
                        </Card>
                    </GridItem>
                </Grid>
            </div>
        );
    }

    renderReportFilesTab() {
        const { reportUrls } = this.props;
        const { loading } = this.state;

        if (loading) {
            return (
                <div className="ds-center">
                    <Spinner size="lg" />
                    <p className="ds-margin-top">{_("Loading file data...")}</p>
                </div>
            );
        }

        if (!reportUrls || Object.keys(reportUrls).length === 0) {
            return (
                <EmptyState>
                    <EmptyStateIcon icon={InfoIcon} />
                    <Title headingLevel="h2">{_("No Report Files Available")}</Title>
                    <EmptyStateBody>
                        {_("No report files were found or could be loaded.")}
                    </EmptyStateBody>
                </EmptyState>
            );
        }

        return (
            <div className="ds-margin-top">
                <Grid hasGutter>
                    <GridItem span={12}>
                        <Card>
                            <CardTitle>{_("Report Files")}</CardTitle>
                            <CardBody>
                                <TextContent>
                                    <Text component={TextVariants.h3}>{_("Download report files:")}</Text>
                                </TextContent>
                                <List className="ds-margin-top">
                                    {reportUrls.summary && (
                                        <ListItem>
                                            <Button
                                                variant="link"
                                                onClick={() => this.downloadFile(reportUrls.summary, "replication_analysis_summary.json")}
                                                icon={<DownloadIcon />}
                                            >
                                                {_("Summary JSON")}
                                            </Button>
                                            <Text component={TextVariants.small} className="ds-margin-left-sm">
                                                {_("Analysis summary data in JSON format")}
                                            </Text>
                                        </ListItem>
                                    )}
                                    {reportUrls.json && (
                                        <ListItem>
                                            <Button
                                                variant="link"
                                                onClick={() => this.downloadFile(reportUrls.json, "replication_analysis.json")}
                                                icon={<DownloadIcon />}
                                            >
                                                {_("Interactive Charts JSON")}
                                            </Button>
                                            <Text component={TextVariants.small} className="ds-margin-left-sm">
                                                {_("Chart data in JSON format")}
                                            </Text>
                                        </ListItem>
                                    )}
                                    {reportUrls.png && (
                                        <ListItem>
                                            <Button
                                                variant="link"
                                                onClick={() => this.downloadFile(reportUrls.png, "replication_analysis.png")}
                                                icon={<DownloadIcon />}
                                            >
                                                {_("PNG Image")}
                                            </Button>
                                            <Text component={TextVariants.small} className="ds-margin-left-sm">
                                                {_("Static chart image in PNG format")}
                                            </Text>
                                        </ListItem>
                                    )}
                                    {reportUrls.csv && (
                                        <ListItem>
                                            <Button
                                                variant="link"
                                                onClick={() => this.downloadFile(reportUrls.csv, "replication_analysis.csv")}
                                                icon={<DownloadIcon />}
                                            >
                                                {_("CSV Data")}
                                            </Button>
                                            <Text component={TextVariants.small} className="ds-margin-left-sm">
                                                {_("Tabular data in CSV format")}
                                            </Text>
                                        </ListItem>
                                    )}
                                    {reportUrls.html && (
                                        <ListItem>
                                            <Button
                                                variant="link"
                                                onClick={() => this.downloadFile(reportUrls.html, "replication_analysis.html")}
                                                icon={<DownloadIcon />}
                                            >
                                                {_("Standalone HTML Report")}
                                            </Button>
                                            <Text component={TextVariants.small} className="ds-margin-left-sm">
                                                {_("Self-contained HTML report with embedded charts")}
                                            </Text>
                                        </ListItem>
                                    )}
                                </List>
                            </CardBody>
                        </Card>
                    </GridItem>
                </Grid>
            </div>
        );
    }

    render() {
        const { showModal, closeHandler } = this.props;
        const { activeTabKey } = this.state;

        if (!showModal) {
            return null;
        }

        // Styles for the modal content
        const modalContentStyle = {
            display: 'flex',
            flexDirection: 'column',
            height: '100%',
            overflow: 'hidden' // Prevent outer scrollbar
        };

        // Styles for the tabs container
        const tabsContainerStyle = {
            flex: '0 0 auto',
            position: 'sticky',
            top: 0,
            zIndex: 100,
            backgroundColor: 'var(--pf-c-modal-box--BackgroundColor, #fff)'
        };

        // Styles for the tabs content area
        const tabContentStyle = {
            flex: '1 1 auto',
            overflowY: 'auto',
            paddingTop: '20px',
            maxHeight: 'calc(75vh - 125px)' // Limit height to prevent overlap
        };

        return (
            <Modal
                variant={ModalVariant.large}
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="close" variant="primary" onClick={closeHandler}>
                        {_("Close")}
                    </Button>
                ]}
                position="top"
                aria-label={_("Replication Report Modal")}
            >
                <div style={modalContentStyle}>
                    <div style={tabsContainerStyle}>
                        <Tabs
                            activeKey={activeTabKey}
                            onSelect={this.handleTabClick}
                            aria-label={_("Report tabs")}
                        >
                            <Tab
                                key={0}
                                eventKey={0}
                                title={<TabTitleText>{_("Summary")}</TabTitleText>}
                            />
                            {this.props.reportUrls && this.props.reportUrls.json && (
                                <Tab
                                    key={1}
                                    eventKey={1}
                                    title={<TabTitleText>{_("Charts")}</TabTitleText>}
                                />
                            )}
                            {this.props.reportUrls && this.props.reportUrls.png && (
                                <Tab
                                    key={2}
                                    eventKey={2}
                                    title={<TabTitleText>{_("PNG Report")}</TabTitleText>}
                                />
                            )}
                            {this.props.reportUrls && this.props.reportUrls.csv && (
                                <Tab
                                    key={3}
                                    eventKey={3}
                                    title={<TabTitleText>{_("CSV Report")}</TabTitleText>}
                                />
                            )}
                            <Tab
                                key={4}
                                eventKey={4}
                                title={<TabTitleText>{_("Report Files")}</TabTitleText>}
                            />
                        </Tabs>
                    </div>
                    <div style={tabContentStyle}>
                        {activeTabKey === 0 && this.renderSummaryTab()}
                        {activeTabKey === 1 && this.props.reportUrls && this.props.reportUrls.json && this.renderChartsTab()}
                        {activeTabKey === 2 && this.props.reportUrls && this.props.reportUrls.png && this.renderPngTab()}
                        {activeTabKey === 3 && this.props.reportUrls && this.props.reportUrls.csv && this.renderCsvTab()}
                        {activeTabKey === 4 && this.renderReportFilesTab()}
                    </div>
                </div>
            </Modal>
        );
    }
}

class ChooseLagReportModal extends React.Component {
    constructor(props) {
        super(props);

        this.state = {
            loading: true,
            reports: [],
            reportDirectory: props.reportDirectory || '/tmp',
            error: null,
            selectedReport: null,
            showLagReportModal: false,
            reportUrls: null
        };

        this.loadReports = this.loadReports.bind(this);
        this.handleSelectReport = this.handleSelectReport.bind(this);
        this.closeLagReportModal = this.closeLagReportModal.bind(this);
    }

    componentDidMount() {
        this.loadReports();
    }

    componentDidUpdate(prevProps) {
        if (prevProps.reportDirectory !== this.props.reportDirectory) {
            this.setState({
                reportDirectory: this.props.reportDirectory,
                loading: true
            }, this.loadReports);
        }
    }

    loadReports() {
        const { reportDirectory } = this.state;

        // Reset the state
        this.setState({
            loading: true,
            error: null,
            reports: []
        });

        // First get all directories in the specified path
        cockpit.spawn(["find", reportDirectory, "-maxdepth", "1", "-type", "d"])
            .then(output => {
                if (!output.trim()) {
                    this.setState({ loading: false });
                    return;
                }

                const directories = output.trim().split('\n');
                const promises = directories.map(dir => {
                    // Check which report files exist in this directory
                    return cockpit.spawn(["ls", "-la", dir])
                        .then(files => {
                            // Check if this directory contains any report files
                            const hasJson = files.includes('replication_analysis.json');
                            const hasHtml = files.includes('replication_analysis.html');
                            const hasCsv = files.includes('replication_analysis.csv');
                            const hasPng = files.includes('replication_analysis.png');
                            const hasSummary = files.includes('replication_analysis_summary.json');

                            // Skip directories that don't have any report files
                            if (!hasJson && !hasHtml && !hasCsv && !hasPng && !hasSummary) {
                                return null;
                            }

                            const reportName = dir.split('/').pop();
                            let creationTime = "";

                            // Try to get the directory creation time using stat
                            return cockpit.spawn(["stat", "-c", "%y", dir])
                                .then(statOutput => {
                                    // Format the creation time from stat output
                                    try {
                                        const timestamp = statOutput.trim();
                                        creationTime = new Date(timestamp).toLocaleString();
                                    } catch (e) {
                                        console.error("Error parsing date from stat:", e);
                                        creationTime = _("Unknown");
                                    }

                                    return {
                                        path: dir,
                                        name: reportName,
                                        creationTime,
                                        hasJson,
                                        hasHtml,
                                        hasCsv,
                                        hasPng,
                                        hasSummary
                                    };
                                })
                                .catch(statError => {
                                    console.error("Error getting directory stats:", statError);

                                    // If we can't get the creation time from stat, try to extract it from filename
                                    // or fall back to using the file listing
                                    if (reportName.startsWith('repl_report_')) {
                                        try {
                                            const timestamp = reportName.replace('repl_report_', '');
                                            // Check if the timestamp is a pure number (Unix timestamp)
                                            if (/^\d+$/.test(timestamp)) {
                                                creationTime = new Date(parseInt(timestamp) * 1000).toLocaleString();
                                            } else {
                                                creationTime = cockpit.format(_("Custom: $0"), timestamp);
                                            }
                                        } catch (e) {
                                            creationTime = _("Unknown");
                                        }
                                    } else {
                                        // Try to get file creation time from command output
                                        const dateMatch = files.match(/\w{3}\s+\d+\s+\d{2}:\d{2}/);
                                        if (dateMatch) {
                                            // Use file timestamp as a fallback
                                            creationTime = dateMatch[0];
                                        } else {
                                            creationTime = _("Unknown");
                                        }
                                    }

                                    return {
                                        path: dir,
                                        name: reportName,
                                        creationTime,
                                        hasJson,
                                        hasHtml,
                                        hasCsv,
                                        hasPng,
                                        hasSummary
                                    };
                                });
                        })
                        .catch(error => {
                            console.error("Error checking files in directory:", dir, error);
                            return null;
                        });
                });

                Promise.all(promises)
                    .then(results => {
                        // Filter out null results and directories without report files
                        const validReports = results.filter(report => report !== null);

                        // Sort by creation time, newest first
                        validReports.sort((a, b) => {
                            try {
                                return new Date(b.creationTime) - new Date(a.creationTime);
                            } catch (e) {
                                return 0;
                            }
                        });

                        this.setState({
                            reports: validReports,
                            loading: false
                        });
                    })
                    .catch(error => {
                        console.error("Error processing report directories:", error);
                        this.setState({
                            error: _("Error processing report directories: ") + error.message,
                            loading: false
                        });
                    });
            })
            .catch(error => {
                console.error("Error finding report directories:", error);
                this.setState({
                    error: _("Error finding report directories: ") + error.message,
                    loading: false
                });
            });
    }

    handleSelectReport(report) {
        // Construct the report URLs
        const reportUrls = {
            base: report.path,
            json: report.hasJson ? `${report.path}/replication_analysis.json` : null,
            html: report.hasHtml ? `${report.path}/replication_analysis.html` : null,
            csv: report.hasCsv ? `${report.path}/replication_analysis.csv` : null,
            png: report.hasPng ? `${report.path}/replication_analysis.png` : null
        };

        this.setState({
            selectedReport: report,
            showLagReportModal: true,
            reportUrls
        });
    }

    closeLagReportModal() {
        this.setState({
            showLagReportModal: false,
            selectedReport: null,
            reportUrls: null
        });
    }

    render() {
        const { showing, onClose } = this.props;
        const { loading, reports, error, showLagReportModal, reportUrls } = this.state;

        return (
            <>
                <Modal
                    variant="large"
                    title={_("Choose Existing Report")}
                    isOpen={showing}
                    onClose={onClose}
                    actions={[
                        <Button key="close" variant="primary" onClick={onClose}>
                            {_("Close")}
                        </Button>
                    ]}
                >
                    <div>
                        <Card isFullHeight>
                            <CardBody>
                                {error && (
                                    <Alert
                                        variant="danger"
                                        title={_("Error loading reports")}
                                        isInline
                                    >
                                        {error}
                                    </Alert>
                                )}

                                {loading ? (
                                    <div className="ds-center">
                                        <Spinner size="lg" />
                                        <p className="ds-margin-top">{_("Loading reports...")}</p>
                                    </div>
                                ) : (
                                    <ExistingLagReportsTable
                                        reports={reports}
                                        onSelectReport={this.handleSelectReport}
                                    />
                                )}
                            </CardBody>
                        </Card>
                    </div>
                </Modal>

                {showLagReportModal && (
                    <LagReportModal
                        showing={showLagReportModal}
                        reportName={_("Report")}
                        reportUrls={reportUrls}
                        onClose={this.closeLagReportModal}
                    />
                )}
            </>
        );
    }
}

// Prototypes and defaultProps
AgmtDetailsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    agmt: PropTypes.object,
};

AgmtDetailsModal.defaultProps = {
    showModal: false,
    agmt: {},
};

TaskLogModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    logData: PropTypes.string
};

TaskLogModal.defaultProps = {
    showModal: false,
    agreement: "",
};

ConflictCompareModal.propTypes = {
    showModal: PropTypes.bool,
    conflictEntry: PropTypes.object,
    validEntry: PropTypes.object,
    closeHandler: PropTypes.func,
};

ConflictCompareModal.defaultProps = {
    showModal: false,
    conflictEntry: { dn: "", attrs: [] },
    validEntry: {},
};

ReportCredentialsModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleFieldChange: PropTypes.func,
    hostname: PropTypes.string,
    port: PropTypes.number,
    binddn: PropTypes.string,
    bindpw: PropTypes.string,
    pwInputInterractive: PropTypes.bool,
    newEntry: PropTypes.bool,
    addConfig: PropTypes.func,
    editConfig: PropTypes.func
};

ReportCredentialsModal.defaultProps = {
    showModal: false,
    hostname: "",
    port: 389,
    binddn: "",
    bindpw: "",
    pwInputInterractive: false,
    newEntry: false,
};

ReportConnectionModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleFieldChange: PropTypes.func,
    name: PropTypes.string,
    hostname: PropTypes.string,
    port: PropTypes.number,
    binddn: PropTypes.string,
    bindpw: PropTypes.string,
    pwInputInterractive: PropTypes.bool,
    addConn: PropTypes.func,
};

ReportConnectionModal.defaultProps = {
    showModal: false,
    name: "",
    hostname: "",
    port: 636,
    binddn: "",
    bindpw: "",
    pwInputInterractive: false,
};

ReportAliasesModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleFieldChange: PropTypes.func,
    hostname: PropTypes.string,
    port: PropTypes.number,
    alias: PropTypes.string,
    newEntry: PropTypes.bool,
    addConfig: PropTypes.func,
    editConfig: PropTypes.func
};

ReportAliasesModal.defaultProps = {
    showModal: false,
    hostname: "",
    port: 389,
    alias: "",
    newEntry: false,
};

ReportLoginModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    processCredsInput: PropTypes.func,
    instanceName: PropTypes.string,
    disableBinddn: PropTypes.bool,
    loginBinddn: PropTypes.string,
    loginBindpw: PropTypes.string
};

ReportLoginModal.defaultProps = {
    showModal: false,
    instanceName: "",
    disableBinddn: false,
    loginBinddn: "",
    loginBindpw: ""
};

FullReportContent.propTypes = {
    reportData: PropTypes.array,
    handleRefresh: PropTypes.func,
    reportRefreshing: PropTypes.bool
};

FullReportContent.defaultProps = {
    reportData: [],
    reportRefreshTimeout: 5,
    reportRefreshing: false
};

LagReportModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    saveHandler: PropTypes.func,
    reportUrls: PropTypes.object
};

LagReportModal.defaultProps = {
    showModal: false,
    reportUrls: {}
};

ChooseLagReportModal.propTypes = {
    showing: PropTypes.bool,
    onClose: PropTypes.func,
    reportDirectory: PropTypes.string
};

ChooseLagReportModal.defaultProps = {
    showing: false,
    onClose: () => {},
    reportDirectory: '/tmp'
};

export {
    TaskLogModal,
    AgmtDetailsModal,
    ConflictCompareModal,
    ReportCredentialsModal,
    ReportConnectionModal,
    ReportAliasesModal,
    ReportLoginModal,
    FullReportContent,
    LagReportModal,
    ChooseLagReportModal
};
