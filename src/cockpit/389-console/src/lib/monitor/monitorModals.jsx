import React from "react";
import {
    Button,
    Checkbox,
    Grid,
    GridItem,
    Form,
    Modal,
    ModalVariant,
    NumberInput,
    Radio,
    Spinner,
    TextArea,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    Tooltip,
} from "@patternfly/react-core";
import {
    CopyIcon,
} from '@patternfly/react-icons';
import faSyncAlt from '@fortawesome/free-solid-svg-icons';
import PropTypes from "prop-types";
import { get_date_string } from "../tools.jsx";
import { ReportSingleTable, ReportConsumersTable } from "./monitorTables.jsx";
import OutlinedQuestionCircleIcon from '@patternfly/react-icons/dist/js/icons/outlined-question-circle-icon';

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
                title="Task Log"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Close
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
            if (agmt[attr][0] == "19700101000000Z") {
                convertedDate[attr] = "Unavailable";
            } else {
                convertedDate[attr] = get_date_string(agmt[attr][0]);
            }
        }

        const btnList = [
            <Button key="cancel" variant="link" onClick={closeHandler}>
                Cancel
            </Button>
        ];

        const title = "Replication Agreement Details (" + agmt['agmt-name'] + ")";

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
                            <b>Replica</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{agmt.replica}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>Agreement Enabled</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{agmt['replica-enabled']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>Last Init Started</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{convertedDate['last-init-start']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>Last Init Ended</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{convertedDate['last-init-end']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>Last Initialization Status</b>
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
                            <b>Replication In Progress</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{agmt['update-in-progress']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>Changes Sent</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{agmt['number-changes-sent']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>Last Update Started</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{convertedDate['last-update-start']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>Last Update Ended</b>
                        </GridItem>
                        <GridItem span={8}>
                            <i>{convertedDate['last-update-end']}</i>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={3}>
                            <b>Last Update Status</b>
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
        if (newRDN == "") {
            // Create an example rdn value based off the conflict rdn
            orig_rdn = conflictEntry.dn.split('+');
            orig_rdn = orig_rdn[0] + "-MUST_CHANGE";
        }
        for (const key in conflictEntry.attrs) {
            if (key == "numsubordinates") {
                conflictChildren = conflictEntry.attrs[key];
            }
            if (!ignoreAttrs.includes(key)) {
                for (const attr of conflictEntry.attrs[key]) {
                    conflict += key + ": " + attr + "\n";
                }
            }
        }
        for (const key in validEntry.attrs) {
            if (key == "numsubordinates") {
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
                title="Resolve Replication Conflict"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={() => saveHandler(conflictEntry.dn)}>
                        Resolve Conflict
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <Grid>
                        <GridItem span={6}>
                            <TextContent>
                                <Text component={TextVariants.h4}>
                                    Valid Entry
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={6}>
                            <p className="ds-margin-top ds-right-align ds-font-size-sm">
                                Child Entries: <b>{validChildren}</b>
                            </p>
                        </GridItem>
                        <GridItem span={12}>
                            <TextArea id="conflictValid" resizeOrientation="vertical" className="ds-conflict" value={valid} isReadOnly />
                        </GridItem>
                        <GridItem className="ds-margin-top-lg" span={6}>
                            <TextContent>
                                <Text component={TextVariants.h4}>
                                    Conflict Entry
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem className="ds-margin-top-lg" span={6}>
                            <p className="ds-margin-top ds-right-align ds-font-size-sm">
                                Child Entries: <b>{conflictChildren}</b>
                            </p>
                        </GridItem>
                        <GridItem span={12}>
                            <TextArea id="conflictConflict" resizeOrientation="vertical" className="ds-conflict" value={conflict} isReadOnly />
                        </GridItem>
                        <hr />
                        <div className="ds-container">
                            <Radio
                                  name="resolve-choice"
                                  onChange={handleRadioChange}
                                  label="Delete Conflict Entry"
                                  id="deleteConflictRadio"
                                  isChecked={deleteConflictRadio}
                            />
                            <div className="ds-left-margin">
                                <Tooltip
                                    position="top"
                                    content={
                                        <div>
                                            This will delete the conflict entry,
                                            and the "valid" entry will remain
                                            intact.
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
                                  label="Swap Conflict Entry With Valid Entry"
                                  id="swapConflictRadio"
                                  isChecked={swapConflictRadio}
                            />
                            <div className="ds-left-margin">
                                <Tooltip
                                    className="ds-margin-left"
                                    position="top"
                                    content={
                                        <div>
                                            This will replace the "valid" entry
                                            with the conflict entry, but keeping
                                            the valid entry DN intact.
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
                                  label="Convert Conflict Entry Into New Entry"
                                  id="convertConflictRadio"
                                  isChecked={convertConflictRadio}
                            />
                            <div className="ds-left-margin">
                                <Tooltip
                                    position="top"
                                    content={
                                        <div>
                                            The conflict entry uses a
                                            multi-valued RDN to specify the
                                            original DN and it's nsUniqueID.  To
                                            convert the conflict entry to a new
                                            entry you must provide a new RDN
                                            attribute/value for the new entry.
                                            "RDN_ATTRIBUTE=VALUE".  For example:
                                            cn=my_new_entry
                                        </div>
                                    }
                                >
                                    <OutlinedQuestionCircleIcon />
                                </Tooltip>
                            </div>
                        </div>
                        <div className="ds-margin-top ds-margin-left-sm">
                            <TextInput
                                placeholder="Enter new RDN here"
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

        const title = (newEntry ? "Add" : "Edit") + " Report Credentials";

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
                        isDisabled={hostname === "" || binddn === "" || bindpw === ""}
                    >
                        Save
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Grid>
                    <GridItem span={12}>
                        <Form isHorizontal autoComplete="off">
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    Hostname
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={hostname}
                                        type="text"
                                        id="credsHostname"
                                        aria-describedby="cachememsize"
                                        name="credsHostname"
                                        onChange={(str, e) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    Port
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
                            <Grid title="Bind DN for the specified instances">
                                <GridItem className="ds-label" span={3}>
                                    Bind DN
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={binddn}
                                        type="text"
                                        id="credsBinddn"
                                        aria-describedby="cachememsize"
                                        name="credsBinddn"
                                        onChange={(str, e) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="Bind password for the specified instances">
                                <GridItem className="ds-label" span={3}>
                                    Password
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={bindpw}
                                        type="password"
                                        id="credsBindpw"
                                        aria-describedby="cachememsize"
                                        name="credsBindpw"
                                        isDisabled={pwInputInterractive}
                                        onChange={(str, e) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="Input the password interactively">
                                <GridItem className="ds-label" span={3}>
                                    Interractive Input
                                </GridItem>
                                <GridItem span={9}>
                                    <Checkbox
                                        isChecked={pwInputInterractive}
                                        id="pwInputInterractive"
                                        onChange={(checked, e) => {
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
                title="Add Replica Connection"
                isOpen={showModal}
                onClose={closeHandler}
                actions={[
                    <Button
                        key="save"
                        variant="primary"
                        onClick={addConn}
                        isDisabled={name ==="" || hostname === "" || port === "" || binddn === "" || bindpw === ""}
                    >
                        Save
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Grid>
                    <GridItem span={12}>
                        <Form isHorizontal autoComplete="off">
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    Connection Name
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={name}
                                        type="text"
                                        id="connName"
                                        aria-describedby="connName"
                                        name="connName"
                                        onChange={(str, e) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    Hostname
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={hostname}
                                        type="text"
                                        id="connHostname"
                                        aria-describedby="connHostname"
                                        name="connHostname"
                                        onChange={(str, e) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid>
                                <GridItem className="ds-label" span={3}>
                                    Port
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
                            <Grid title="Bind DN for the specified instances">
                                <GridItem className="ds-label" span={3}>
                                    Bind DN
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={binddn}
                                        type="text"
                                        id="connBindDN"
                                        aria-describedby="connBindDN"
                                        name="connBindDN"
                                        onChange={(str, e) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="Bind password for the specified instance.  You can also speciy a password file but the filename needs to be inside of brackets [/PATH/FILE]">
                                <GridItem className="ds-label" span={3}>
                                    Password
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={bindpw}
                                        type="password"
                                        id="connCred"
                                        aria-describedby="connCred"
                                        name="connCred"
                                        isDisabled={pwInputInterractive}
                                        onChange={(str, e) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="Input the password interactively, stores '*' as the password value in .dsrc">
                                <GridItem className="ds-label" span={3}>
                                    Interractive Input
                                </GridItem>
                                <GridItem span={9}>
                                    <Checkbox
                                        isChecked={pwInputInterractive}
                                        id="pwInputInterractive"
                                        onChange={(checked, e) => {
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

        const title = (newEntry ? "Add" : "Edit") + " Report Alias";

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
                        isDisabled={alias == "" || hostname == ""}
                    >
                        Save
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Grid>
                    <GridItem span={12}>
                        <Form isHorizontal autoComplete="off">
                            <Grid title="Alias name for the instance">
                                <GridItem className="ds-label" span={3}>
                                    Alias
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={alias}
                                        type="text"
                                        id="aliasName"
                                        aria-describedby="aliasName"
                                        name="aliasName"
                                        onChange={(str, e) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="An instance hostname">
                                <GridItem className="ds-label" span={3}>
                                    Hostname
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={hostname}
                                        type="text"
                                        id="aliasHostname"
                                        aria-describedby="aliasHostname"
                                        name="aliasHostname"
                                        onChange={(str, e) => {
                                            handleFieldChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid title="An instance port">
                                <GridItem className="ds-label" span={3}>
                                    Port
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

        const title = "Replication Login Credentials for " + instanceName;

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
                        isDisabled={loginBinddn == "" || loginBindpw == ""}
                        onClick={processCredsInput}
                    >
                        Confirm Credentials Input
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text component={TextVariants.h5}>
                            In order to get the replication agreement lag times and state, the
                            authentication credentials to the remote replicas must be provided.
                        </Text>
                    </TextContent>
                    <hr />
                    <TextContent>
                        <Text component={TextVariants.h5}>
                            Bind DN was acquired from <b>Replica Credentials</b> table. If you want
                            to bind as another user, change or remove the Bind DN there.
                        </Text>
                    </TextContent>
                    <Grid className="ds-margin-top-lg" title="Bind DN for the instance">
                        <GridItem className="ds-label" span={3}>
                            Bind DN
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={loginBinddn}
                                type="text"
                                id="loginBinddn"
                                aria-describedby="loginBinddn"
                                name="loginBinddn"
                                isDisabled={disableBinddn}
                                onChange={(str, e) => {
                                    handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title="Password for the Bind DN">
                        <GridItem className="ds-label" span={3}>
                            Password
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={loginBindpw}
                                type="password"
                                id="loginBindpw"
                                aria-describedby="loginBindpw"
                                name="loginBindpw"
                                onChange={(str, e) => {
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
            spinner = (
                <div title="Do the refresh every few seconds">
                    {reportRefreshing ? "Refreshing" : "Loading"} the report...
                    <Spinner className="ds-left-margin" size="lg" />
                </div>
            );
        }
        let reportHeader =
            <TextContent>
                <Text className="ds-margin-top-xlg" component={TextVariants.h4}>
                    There is no report, you must first generate the report in the <b>Prepare Report</b> tab.
                </Text>
            </TextContent>;
        if (reportData.length > 0) {
            reportHeader = (
                <div>
                    <Form isHorizontal autoComplete="off">
                        <Grid>
                            <GridItem span={8}>
                                <Checkbox
                                    title="Display all agreements including the disabled ones and the ones we failed to connect to"
                                    isChecked={this.state.showDisabledAgreements}
                                    id="showDisabledAgreements"
                                    onChange={(checked, e) => {
                                        this.handleSwitchChange(e);
                                    }}
                                    label="Show Disabled Agreements"
                                />
                            </GridItem>
                            <GridItem span={4}>
                                <Button
                                    key="refresh"
                                    variant="secondary"
                                    onClick={handleRefresh}
                                    className="ds-float-right"
                                >
                                    Refresh Report
                                </Button>
                            </GridItem>
                            <GridItem span={12}>
                                <Checkbox
                                    isChecked={this.state.oneTableReport}
                                    onChange={(checked, e) => {
                                        this.handleSwitchChange(e);
                                    }}
                                    id="oneTableReport"
                                    title="Show all data in one table (it makes it easier to check lag times)"
                                    label="Table View"
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
                            agmts[idx]['replica-enabled'][0] == "off") {
                            // remove disabled agmt
                            agmts.splice(idx, 1);
                        }
                    }
                    resultGrids = resultGrids.concat(agmts);
                }
            }
            suppliers = [(<div>
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
                                    <b>Can not get replication information from Replica:</b>&nbsp;&nbsp;{s_data[0].replica_status}
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
                                replica.agmts_status[idx]['replica-enabled'][0] == "off") {
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
                                Replica Root
                            </GridItem>
                            <GridItem span={10}>
                                <b>{replica.replica_root}</b>
                            </GridItem>
                            <GridItem span={2}>
                                Replica ID
                            </GridItem>
                            <GridItem span={10}>
                                <b>{replica.replica_id}</b>
                            </GridItem>
                            <GridItem span={2}>
                                Max CSN
                            </GridItem>
                            <GridItem span={10}>
                                <b>{replica.maxcsn}</b>
                            </GridItem>
                            <GridItem span={2}>
                                Replica Status
                            </GridItem>
                            <GridItem span={10}>
                                <b>{replica.replica_status}</b>
                            </GridItem>

                            {"agmts_status" in replica &&
                            replica.agmts_status.length > 0 &&
                            "agmt-name" in replica.agmts_status[0] ? (
                                <ReportConsumersTable
                                    key={replica.agmts_status}
                                    rows={replica.agmts_status}
                                    viewAgmt={this.props.viewAgmt}
                                    />
                                ) : (
                                    <TextContent>
                                        <Text component={TextVariants.h4}>
                                            <b><i>No Agreements Were Found</i></b>
                                        </Text>
                                    </TextContent>
                                )}
                        </Grid>
                    ));
                }
                supplierName = (
                    <div className="ds-margin-top-xlg" key={supplier.name}>
                        <TextContent title="Supplier host:port (and alias if applicable)">
                            <Text component={TextVariants.h2}>
                                <CopyIcon />&nbsp;&nbsp;<b>Supplier:</b>&nbsp;&nbsp;{supplier.name}
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
            report =
                <GridItem span={12} className="ds-center ds-margin-top">
                    {spinner}
                </GridItem>;
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

export {
    TaskLogModal,
    AgmtDetailsModal,
    ConflictCompareModal,
    ReportCredentialsModal,
    ReportConnectionModal,
    ReportAliasesModal,
    ReportLoginModal,
    FullReportContent
};
