import cockpit from "cockpit";
import React from "react";
import { log_cmd, valid_dn, getApiErrorMessage } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import { PwpTable } from "./databaseTables.jsx";
import {
	Alert,
	Button,
	Checkbox,
	Divider,
	Form,
	FormAlert,
	FormHelperText,
	FormSelect,
	FormSelectOption,
	Grid,
	GridItem,
	Modal,
	ModalVariant,
	Spinner,
	Tab,
	Tabs,
	TabTitleText,
	TextInput,
	Text,
	TextContent,
	TextVariants,
	ValidatedOptions
} from '@patternfly/react-core';
import {
    hasInvalidField,
    renderValidationError,
    updateFieldValidation,
} from "./pwpValidation.jsx";
import TypeaheadSelect from "../../dsBasicComponents.jsx";
import { DsNumberInput } from "../dsNumberInput.jsx";
import { SyncAltIcon } from "@patternfly/react-icons";
import PropTypes from "prop-types";

const _ = cockpit.gettext;

const general_attrs = [
    "passwordstoragescheme",
    "passwordtrackupdatetime",
    "passwordchange",
    "passwordmustchange",
    "passwordhistory",
    "passwordinhistory",
    "passwordminage",
    "passwordadmindn",
    "passwordadminskipinfoupdate",
];

const exp_attrs = [
    "passwordexp",
    "passwordgracelimit",
    "passwordsendexpiringtime",
    "passwordmaxage",
    "passwordwarning",
];

const lockout_attrs = [
    "passwordlockout",
    "passwordunlock",
    "passwordlockoutduration",
    "passwordmaxfailure",
    "passwordresetfailurecount",
];

const syntax_attrs = [
    "passwordchecksyntax",
    "passwordminlength",
    "passwordmindigits",
    "passwordminalphas",
    "passwordminuppers",
    "passwordminlowers",
    "passwordminspecials",
    "passwordmin8bit",
    "passwordmaxrepeats",
    "passwordpalindrome",
    "passwordmaxsequence",
    "passwordmaxseqsets",
    "passwordmaxclasschars",
    "passwordmincategories",
    "passwordmintokenlength",
    "passwordbadwords",
    "passworduserattributes",
    "passworddictcheck",
];

const tpr_attrs = [
    "passwordtprmaxuse",
    "passwordtprdelayexpireat",
    "passwordtprdelayvalidfrom",
];

function getDefaultCreatePolicyState(defaultStorageScheme = "") {
    return {
        create_passwordchange: false,
        create_passwordmustchange: false,
        create_passwordhistory: false,
        create_passwordtrackupdatetime: false,
        create_passwordexp: false,
        create_passwordsendexpiringtime: false,
        create_passwordlockout: false,
        create_passwordunlock: false,
        create_passwordchecksyntax: false,
        create_passwordpalindrome: false,
        create_passworddictcheck: false,
        create_passwordstoragescheme: defaultStorageScheme,
        create_passwordinhistory: "0",
        create_passwordwarning: "0",
        create_passwordmaxage: "0",
        create_passwordminage: "0",
        create_passwordgracelimit: "0",
        create_passwordlockoutduration: "0",
        create_passwordmaxfailure: "0",
        create_passwordresetfailurecount: "0",
        create_passwordminlength: "0",
        create_passwordmindigits: "0",
        create_passwordminalphas: "0",
        create_passwordminuppers: "0",
        create_passwordminlowers: "0",
        create_passwordminspecials: "0",
        create_passwordmin8bit: "0",
        create_passwordmaxrepeats: "0",
        create_passwordmaxsequence: "0",
        create_passwordmaxseqsets: "0",
        create_passwordmaxclasschars: "0",
        create_passwordmincategories: "0",
        create_passwordmintokenlength: "0",
        create_passwordbadwords: "",
        create_passworduserattributes: [],
        create_passwordtprmaxuse: "-1",
        create_passwordtprdelayexpireat: "-1",
        create_passwordtprdelayvalidfrom: "-1",
        create_passwordadmindn: "",
        create_passwordadminskipinfoupdate: false,
        _create_passwordchange: false,
        _create_passwordmustchange: false,
        _create_passwordhistory: false,
        _create_passwordtrackupdatetime: false,
        _create_passwordexp: false,
        _create_passwordsendexpiringtime: false,
        _create_passwordlockout: false,
        _create_passwordunlock: false,
        _create_passwordchecksyntax: false,
        _create_passwordpalindrome: false,
        _create_passworddictcheck: false,
        _create_passwordstoragescheme: defaultStorageScheme,
        _create_passwordinhistory: "0",
        _create_passwordwarning: "0",
        _create_passwordmaxage: "0",
        _create_passwordminage: "0",
        _create_passwordgracelimit: "0",
        _create_passwordlockoutduration: "0",
        _create_passwordmaxfailure: "0",
        _create_passwordresetfailurecount: "0",
        _create_passwordminlength: "0",
        _create_passwordmindigits: "0",
        _create_passwordminalphas: "0",
        _create_passwordminuppers: "0",
        _create_passwordminlowers: "0",
        _create_passwordminspecials: "0",
        _create_passwordmin8bit: "0",
        _create_passwordmaxrepeats: "0",
        _create_passwordmaxsequence: "0",
        _create_passwordmaxseqsets: "0",
        _create_passwordmaxclasschars: "0",
        _create_passwordmincategories: "0",
        _create_passwordmintokenlength: "0",
        _create_passwordbadwords: "",
        _create_passworduserattributes: [],
        _create_passwordtprmaxuse: "-1",
        _create_passwordtprdelayexpireat: "-1",
        _create_passwordtprdelayvalidfrom: "-1",
        _create_passwordadmindn: "",
        _create_passwordadminskipinfoupdate: false,
    };
}

const edit_policy_attrs = general_attrs.concat(
    exp_attrs,
    lockout_attrs,
    syntax_attrs,
    tpr_attrs
);

function attrValuesDiffer(attr, orig, cur) {
    if (attr === "passworduserattributes") {
        const orig_val = Array.isArray(orig) ? orig.join(" ") : (orig || "");
        const new_val = Array.isArray(cur) ? cur.join(" ") : (cur || "");
        return orig_val !== new_val;
    }
    return orig !== cur;
}

function computeSaveEditDisabled(state) {
    const hasChanges = edit_policy_attrs.some((attr) =>
        attrValuesDiffer(attr, state["_" + attr], state[attr])
    );
    if (!hasChanges) {
        return true;
    }
    return hasInvalidField(edit_policy_attrs, state.invalidFields);
}

function formatAttrValueForCmd(attr, val) {
    if (attr === "passworduserattributes") {
        return Array.isArray(val) ? val.join(" ") : val;
    }
    if (typeof val === "boolean") {
        return val ? "on" : "off";
    }
    return val;
}

class CreatePolicy extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            createActiveTabKey: 0,
        };
        this.handleCreateNavSelect = (_event, tabIndex) => {
            this.setState({ createActiveTabKey: tabIndex });
        };
    }

    render() {
        let helper_text = _("Required field");
        if (this.props.invalid_dn) {
            helper_text = _("Invalid DN");
        }

        return (
            <>
                <Form className="ds-margin-left" isHorizontal autoComplete="off">
                    <Grid className="ds-margin-top-lg">
                        <GridItem className="ds-label" span={3}>
                            {_("Password Policy Type")}
                        </GridItem>
                        <GridItem span={7}>
                            <FormSelect value={this.props.createPolicyType} onChange={this.props.handleSelectChange} id="createPolicyType" aria-label={_("FormSelect Input")}>
                                <FormSelectOption key={1} value="Subtree Policy" label={_("Subtree Policy")} />
                                <FormSelectOption key={2} value="User Policy" label={_("User Policy")} />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid title={_("The DN of the entry to apply this password policy to.")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Target DN")}
                        </GridItem>
                        <GridItem span={7}>
                            <TextInput
                                type="text"
                                id="policyDN"
                                aria-describedby="horizontal-form-name-helper"
                                name="policyDN"
                                value={this.props.policyDN}
                                onChange={(e, str) => {
                                    this.props.handleChange(e);
                                }}
                                validated={this.props.invalid_dn ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText  >
                                {helper_text}
                            </FormHelperText>
                        </GridItem>
                    </Grid>
                </Form>
                <Tabs className="ds-margin-top-lg" activeKey={this.state.createActiveTabKey} onSelect={this.handleCreateNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>{_("General Settings")}</TabTitleText>}>
                        <Form className="ds-margin-left-sm ds-margin-top-lg" isHorizontal autoComplete="off">
                            <Grid className="ds-margin-top" title={_("Set the password storage scheme (passwordstoragescheme).")}>
                                <GridItem className="ds-label" span={3}>
                                    {_("Password Storage Scheme")}
                                </GridItem>
                                <GridItem span={7}>
                                    <FormSelect
                                        id="create_passwordstoragescheme"
                                        value={this.props.create_passwordstoragescheme}
                                        onChange={(event, value) => {
                                            this.props.handleChange(event);
                                        }}
                                        aria-label="FormSelect Input"
                                    >
                                        {this.props.pwdStorageSchemes.map((option, index) => (
                                            <FormSelectOption
                                                key={index}
                                                value={option}
                                                label={option}
                                            />
                                        ))}
                                    </FormSelect>
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Indicates the number of seconds that must pass before a user can change their password again. (passwordMinAge).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Password Minimum Age")}
                                </GridItem>
                                <GridItem span={7}>
                                    <DsNumberInput
                                        id="create_passwordminage"
                                        value={this.props.create_passwordminage}
                                        fieldName="create_passwordminage"
                                        invalidFields={this.props.invalidCreateFields}
                                        onChange={(e) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                                {renderValidationError("create_passwordminage", this.props.invalidCreateFields)}
                            </Grid>
                            <Grid
                                title={_("The DN for a password administrator or administrator group (passwordAdminDN).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Password Administrator")}
                                </GridItem>
                                <GridItem span={7}>
                                    <TextInput
                                        value={this.props.create_passwordadmindn}
                                        type="text"
                                        id="create_passwordadmindn"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="create_passwordadmindn"
                                        onChange={(e, checked) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid hasGutter>
                                <GridItem
                                    span={5}
                                    title={_("Record a separate timestamp specifically for the last time that the password for an entry was changed. If this is enabled, then it adds the pwdUpdateTime operational attribute to the user account entry (passwordTrackUpdateTime).")}
                                >
                                    <Checkbox
                                        id="create_passwordtrackupdatetime"
                                        isChecked={this.props.create_passwordtrackupdatetime}
                                        onChange={(e, checked) => {
                                            this.props.handleChange(e);
                                        }}
                                        label={_("Track Password Update Time")}
                                    />
                                </GridItem>
                                <GridItem span={5} title={_("Allow user's to change their passwords (passwordChange).")}>
                                    <Checkbox
                                        id="create_passwordchange"
                                        isChecked={this.props.create_passwordchange}
                                        onChange={(e, checked) => {
                                            this.props.handleChange(e);
                                        }}
                                        label={_("Allow Users To Change Their Passwords")}
                                    />
                                </GridItem>
                                <GridItem span={5} title={_("User must change its password after its been reset by an administrator (passwordMustChange).")}>
                                    <Checkbox
                                        id="create_passwordmustchange"
                                        isChecked={this.props.create_passwordmustchange}
                                        onChange={(e, checked) => {
                                            this.props.handleChange(e);
                                        }}
                                        label={_("User Must Change Password After Reset")}
                                    />
                                </GridItem>
                                <GridItem
                                    span={5}
                                    title={_("Disable updating password state attributes like passwordExpirationtime, passwordHistory, etc, when setting a user's password as a Password Administrator (passwordAdminSkipInfoUpdate).")}
                                >
                                    <Checkbox
                                        id="create_passwordadminskipinfoupdate"
                                        isChecked={this.props.create_passwordadminskipinfoupdate}
                                        onChange={(e, checked) => {
                                            this.props.handleChange(e);
                                        }}
                                        label={_("Do not update target entry's password state attributes")}
                                    />
                                </GridItem>
                                <GridItem span={5} title={_("Maintain a password history for each user (passwordHistory).")}>
                                    <div className="ds-inline">
                                        <Checkbox
                                            id="create_passwordhistory"
                                            isChecked={this.props.create_passwordhistory}
                                            onChange={(e, checked) => {
                                                this.props.handleChange(e);
                                            }}
                                            label={_("Keep Password History")}
                                        />
                                    </div>
                                    <div className="ds-inline ds-left-margin ds-raise-field-md ds-width-sm">
                                        <DsNumberInput
                                            id="create_passwordinhistory"
                                            value={this.props.create_passwordinhistory}
                                            fieldName="create_passwordinhistory"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordinhistory", this.props.invalidCreateFields)}
                                    </div>
                                </GridItem>
                            </Grid>
                        </Form>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>{_("Expiration")}</TabTitleText>}>
                        <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                            <Grid className="ds-margin-top" title={_("Enable a password expiration policy (passwordExp).")}>
                                <GridItem span={10}>
                                    <Checkbox
                                        id="create_passwordexp"
                                        isChecked={this.props.create_passwordexp}
                                        onChange={(e, checked) => {
                                            this.props.handleChange(e);
                                        }}
                                        label={_("Enforce Password Expiration")}
                                    />
                                </GridItem>
                            </Grid>
                            <div className="ds-left-indent">
                                <Grid
                                    title={_("The maximum age of a password in seconds before it expires (passwordMaxAge).")}
                                >
                                    <GridItem className="ds-label" span={4}>
                                        {_("Password Expiration Time")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordmaxage"
                                            value={this.props.create_passwordmaxage}
                                            isDisabled={!this.props.create_passwordexp}
                                            fieldName="create_passwordmaxage"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                    </GridItem>
                                    {renderValidationError("create_passwordmaxage", this.props.invalidCreateFields)}
                                </Grid>
                                <Grid className="ds-margin-top" title={_("The number of logins that are allowed after the password has expired (passwordGraceLimit).")}>
                                    <GridItem className="ds-label" span={4}>
                                        {_("Allowed Logins After Password Expires")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordgracelimit"
                                            value={this.props.create_passwordgracelimit}
                                            isDisabled={!this.props.create_passwordexp}
                                            fieldName="create_passwordgracelimit"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                    </GridItem>
                                    {renderValidationError("create_passwordgracelimit", this.props.invalidCreateFields)}
                                </Grid>
                                <Grid className="ds-margin-top" title={_("Set the time (in seconds), before a password is about to expire, to send a warning. (passwordWarning).")}>
                                    <GridItem className="ds-label" span={4}>
                                        {_("Send Password Expiring Warning")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordwarning"
                                            value={this.props.create_passwordwarning}
                                            isDisabled={!this.props.create_passwordexp}
                                            fieldName="create_passwordwarning"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                    </GridItem>
                                    {renderValidationError("create_passwordwarning", this.props.invalidCreateFields)}
                                </Grid>
                                <Grid className="ds-margin-top" title={_("Always return a password expiring control when requested (passwordSendExpiringTime).")}>
                                    <GridItem span={4}>
                                        <Checkbox
                                            id="create_passwordsendexpiringtime"
                                            isChecked={this.props.create_passwordsendexpiringtime}
                                            onChange={(e, checked) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.create_passwordexp}
                                            label={_("Always send Password Expiring Control")}
                                            className="ds-lower-field"
                                        />
                                    </GridItem>
                                </Grid>
                            </div>
                        </Form>
                    </Tab>
                    <Tab eventKey={2} title={<TabTitleText>{_("Account Lockout")}</TabTitleText>}>
                        <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                            <Grid className="ds-margin-top" title={_("Enable account lockout (passwordLockout).")}>
                                <GridItem span={10}>
                                    <Checkbox
                                        id="create_passwordlockout"
                                        isChecked={this.props.create_passwordlockout}
                                        onChange={(e, checked) => {
                                            this.props.handleChange(e);
                                        }}
                                        label={_("Enable Account Lockout")}
                                    />
                                </GridItem>
                            </Grid>
                            <div className="ds-left-indent">
                                <Grid title={_("The maximum number of failed logins before account gets locked (passwordMaxFailure).")}>
                                    <GridItem className="ds-label" span={4}>
                                        {_("Number of Failed Logins That Locks out Account")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <DsNumberInput
                                            id="create_passwordmaxfailure"
                                            value={this.props.create_passwordmaxfailure}
                                            isDisabled={!this.props.create_passwordlockout}
                                            fieldName="create_passwordmaxfailure"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                    </GridItem>
                                    {renderValidationError("create_passwordmaxfailure", this.props.invalidCreateFields)}
                                </Grid>
                                <Grid className="ds-margin-top" title={_("The number of seconds until an accounts failure count is reset (passwordResetFailureCount).")}>
                                    <GridItem className="ds-label" span={4}>
                                        {_("Time Until Failure Count Resets")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <DsNumberInput
                                            id="create_passwordresetfailurecount"
                                            value={this.props.create_passwordresetfailurecount}
                                            isDisabled={!this.props.create_passwordlockout}
                                            fieldName="create_passwordresetfailurecount"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                    </GridItem>
                                    {renderValidationError("create_passwordresetfailurecount", this.props.invalidCreateFields)}
                                </Grid>
                                <Grid className="ds-margin-top" title={_("The number of seconds, duration, before the account gets unlocked (passwordLockoutDuration).")}>
                                    <GridItem className="ds-label" span={4}>
                                        {_("Time Until Account Unlocked")}
                                    </GridItem>
                                    <GridItem span={2}>
                                        <DsNumberInput
                                            id="create_passwordlockoutduration"
                                            value={this.props.create_passwordlockoutduration}
                                            isDisabled={!this.props.create_passwordlockout}
                                            fieldName="create_passwordlockoutduration"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                    </GridItem>
                                    {renderValidationError("create_passwordlockoutduration", this.props.invalidCreateFields)}
                                </Grid>
                                <Grid className="ds-margin-top" title={_("Do not lockout the user account forever, instead the account will unlock based on the lockout duration (passwordUnlock).")}>
                                    <GridItem span={6}>
                                        <Checkbox
                                            id="create_passwordunlock"
                                            isChecked={this.props.create_passwordunlock}
                                            onChange={(e, checked) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.create_passwordlockout}
                                            label={_("Do Not Lockout Account Forever")}
                                        />
                                    </GridItem>
                                </Grid>
                            </div>
                        </Form>
                    </Tab>
                    <Tab eventKey={3} title={<TabTitleText>{_("Syntax Checking")}</TabTitleText>}>
                        <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                            <Grid title={_("Enable password syntax checking (passwordCheckSyntax).")}>
                                <GridItem span={10}>
                                    <Checkbox
                                        id="create_passwordchecksyntax"
                                        isChecked={this.props.create_passwordchecksyntax}
                                        onChange={(e, checked) => {
                                            this.props.handleChange(e);
                                        }}
                                        label={_("Enable Password Syntax Checking")}
                                    />
                                </GridItem>
                            </Grid>
                            <div className="ds-left-indent">
                                <Grid>
                                    <GridItem className="ds-label" span={2}>
                                        {_("Minimum Length")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordminlength"
                                            value={this.props.create_passwordminlength}
                                            title={_("The minimum number of characters in the password (passwordMinLength).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordminlength"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordminlength", this.props.invalidCreateFields)}
                                    </GridItem>
                                    <GridItem className="ds-label" offset={6} span={2}>
                                        {_("Max Repeated Chars")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordmaxrepeats"
                                            value={this.props.create_passwordmaxrepeats}
                                            title={_("The maximum number of times the same character can sequentially appear in a password (passwordMaxRepeats).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordmaxrepeats"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordmaxrepeats", this.props.invalidCreateFields)}
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={2}>
                                        {_("Prohibited Words")}
                                    </GridItem>
                                    <GridItem span={8}>
                                        <TextInput
                                            title={_("A space-separated list of words that are not allowed to be contained in the new password (passwordBadWords).")}
                                            type="text"
                                            id="create_passwordbadwords"
                                            aria-describedby="create_passwordbadwords"
                                            name="create_passwordbadwords"
                                            value={this.props.create_passwordbadwords}
                                            onChange={(e, str) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem span={3} title={_("Check the password against the system's CrackLib dictionary (passwordDictCheck).")}>
                                        <Checkbox
                                            id="create_passworddictcheck"
                                            isChecked={this.props.create_passworddictcheck}
                                            onChange={(e, checked) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            label={_("Dictionary Check")}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem span={3} title={_("Check if the password is a palindrome (passwordPalindrome).")}>
                                        <Checkbox
                                            id="create_passwordpalindrome"
                                            isChecked={this.props.create_passwordpalindrome}
                                            className="ds-label"
                                            onChange={(e, checked) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            label={_("Reject Palindromes")}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid>
                                    <GridItem span={10}>
                                        <Divider />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={2}>
                                        {_("Minimum Alpha's")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordminalphas"
                                            value={this.props.create_passwordminalphas}
                                            title={_("Reject passwords with fewer than this many alpha characters (passwordMinAlphas).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordminalphas"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordminalphas", this.props.invalidCreateFields)}
                                    </GridItem>
                                    <GridItem className="ds-label" offset={6} span={2}>
                                        {_("Minimum Digits")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordmindigits"
                                            value={this.props.create_passwordmindigits}
                                            title={_("Reject passwords with fewer than this many digit characters (0-9) (passwordMinDigits).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordmindigits"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordmindigits", this.props.invalidCreateFields)}
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={2}>
                                        {_("Minimum Uppercase")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordminuppers"
                                            value={this.props.create_passwordminuppers}
                                            title={_("Reject passwords with fewer than this many uppercase characters (passwordMinUppers).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordminuppers"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordminuppers", this.props.invalidCreateFields)}
                                    </GridItem>
                                    <GridItem className="ds-label" offset={6} span={2}>
                                        {_("Minimum Lowercase")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordminlowers"
                                            value={this.props.create_passwordminlowers}
                                            title={_("Reject passwords with fewer than this many lowercase characters (passwordMinLowers).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordminlowers"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordminlowers", this.props.invalidCreateFields)}
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={2}>
                                        {_("Minimum Special")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordminspecials"
                                            value={this.props.create_passwordminspecials}
                                            title={_("Reject passwords with fewer than this many special non-alphanumeric characters (passwordMinSpecials).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordminspecials"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordminspecials", this.props.invalidCreateFields)}
                                    </GridItem>
                                    <GridItem className="ds-label" offset={6} span={2}>
                                        {_("Minimum 8-bit")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordmin8bit"
                                            value={this.props.create_passwordmin8bit}
                                            title={_("Reject passwords with fewer than this many 8-bit or multi-byte characters (passwordMin8Bit).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordmin8bit"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordmin8bit", this.props.invalidCreateFields)}
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={2}>
                                        {_("Minimum Categories")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordmincategories"
                                            value={this.props.create_passwordmincategories}
                                            title={_("The minimum number of character categories that a password must contain (categories are upper, lower, digit, special, and 8-bit) (passwordMinCategories).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordmincategories"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordmincategories", this.props.invalidCreateFields)}
                                    </GridItem>
                                </Grid>
                                <Grid>
                                    <GridItem span={10}>
                                        <Divider />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={2}>
                                        {_("Maximum Sequences")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordmaxsequence"
                                            value={this.props.create_passwordmaxsequence}
                                            title={_("The maximum number of allowed monotonic characters sequences (passwordMaxSequence).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordmaxsequence"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordmaxsequence", this.props.invalidCreateFields)}
                                    </GridItem>
                                    <GridItem className="ds-label" offset={6} span={2}>
                                        {_("Max Sequence Sets")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordmaxseqsets"
                                            value={this.props.create_passwordmaxseqsets}
                                            title={_("The maximum number of allowed monotonic characters sequences that can appear more than once (passwordMaxSeqSets).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordmaxseqsets"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordmaxseqsets", this.props.invalidCreateFields)}
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={2}>
                                        {_("Max Seq Per Class")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordmaxclasschars"
                                            value={this.props.create_passwordmaxclasschars}
                                            title={_("The maximum number of consecutive characters from the same character class/category (passwordMaxClassChars).")}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordmaxclasschars"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordmaxclasschars", this.props.invalidCreateFields)}
                                    </GridItem>
                                </Grid>
                                <Grid>
                                    <GridItem span={10}>
                                        <Divider />
                                    </GridItem>
                                </Grid>
                                <Grid title={_("A list of entry attributes to compare to the new password (passwordUserAttributes).")}>
                                    <GridItem className="ds-label" span={2}>
                                        {_("Check User Attributes")}
                                    </GridItem>
                                    <GridItem span={8}>
                                        <TypeaheadSelect
                                            selected={this.props.create_passworduserattributes}
                                            onSelect={this.props.handleChange}
                                            onClear={this.props.onUserAttrsCreateClear}
                                            options={this.props.attrs}
                                            isOpen={this.props.isUserAttrsCreateOpen}
                                            onToggle={this.props.onUserAttrsCreateToggle}
                                            placeholder={_("Type attributes to check...")}
                                            noResultsText={_("There are no matching entries")}
                                            ariaLabel="Type an attribute to check"
                                            isMulti={true}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title={_("The smallest attribute value used when checking if the password contains any of the user's attributes that are set via passwordUserAttributes (passwordMinTokenLength).")}>
                                    <GridItem className="ds-label" span={2}>
                                        {_("Minimum Token Length")}
                                    </GridItem>
                                    <GridItem span={1}>
                                        <DsNumberInput
                                            id="create_passwordmintokenlength"
                                            value={this.props.create_passwordmintokenlength}
                                            isDisabled={!this.props.create_passwordchecksyntax}
                                            fieldName="create_passwordmintokenlength"
                                            invalidFields={this.props.invalidCreateFields}
                                            onChange={(e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                        {renderValidationError("create_passwordmintokenlength", this.props.invalidCreateFields)}
                                    </GridItem>
                                </Grid>
                            </div>
                        </Form>
                    </Tab>
                    <Tab eventKey={4} title={<TabTitleText>{_("Temporary Password Rules")}</TabTitleText>}>
                        <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                            {this.props.create_passwordmustchange === false && (
                                <FormAlert className="ds-margin-top">
                                    <Alert
                                        variant="info"
                                        title={_("\"User Must Change Password After Reset\" must be enabled in General Settings to activate TPR.")}
                                        aria-live="polite"
                                        isInline
                                    />
                                </FormAlert>
                            )}
                            <Grid
                                title={_("Number of times the temporary password can be used to authenticate (passwordTPRMaxUse).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Password Max Use")}
                                </GridItem>
                                <GridItem span={7}>
                                    <DsNumberInput
                                        id="create_passwordtprmaxuse"
                                        fieldName="create_passwordtprmaxuse"
                                        value={this.props.create_passwordtprmaxuse}
                                        invalidFields={this.props.invalidCreateFields}
                                        isDisabled={!this.props.create_passwordmustchange}
                                        onChange={(e) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Number of seconds before the temporary password expires (passwordTPRDelayExpireAt).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Password Expires In")}
                                </GridItem>
                                <GridItem span={9}>
                                    <DsNumberInput
                                        id="create_passwordtprdelayexpireat"
                                        fieldName="create_passwordtprdelayexpireat"
                                        value={this.props.create_passwordtprdelayexpireat}
                                        invalidFields={this.props.invalidCreateFields}
                                        isDisabled={!this.props.create_passwordmustchange}
                                        onChange={(e) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title={_("Number of seconds after which temporary password starts to be valid for authentication (passwordTPRDelayValidFrom).")}
                            >
                                <GridItem className="ds-label" span={3}>
                                    {_("Password Valid From")}
                                </GridItem>
                                <GridItem span={9}>
                                    <DsNumberInput
                                        id="create_passwordtprdelayvalidfrom"
                                        fieldName="create_passwordtprdelayvalidfrom"
                                        value={this.props.create_passwordtprdelayvalidfrom}
                                        invalidFields={this.props.invalidCreateFields}
                                        isDisabled={!this.props.create_passwordmustchange}
                                        onChange={(e) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                        </Form>
                    </Tab>
                </Tabs>
            </>
        );
    }
}


export class LocalPwPolicy extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            loading: true,
            loaded: false,
            activeTabKey: 0,
            showCreateModal: false,
            showEditModal: false,
            modalChecked: false,
            editPolicy: false,
            tableLoading: false,
            // Create policy
            policyType: "",
            policyDN: "",
            policyName: "",
            deleteName: "",
            createDisabled: true,
            creating: false,
            createPolicyType: "Subtree Policy",
            // Lists of all the attributes for each tab/section.
            // We use the exact attribute name for the ID of
            // each field, so we can loop over them to efficently
            // check for changes, and updating/saving the config.
            rows: [],
            saveEditDisabled: true,
            showDeletePolicy: false,
            // Edit policy
            passwordchange: false,
            passwordmustchange: false,
            passwordhistory: false,
            passwordtrackupdatetime: false,
            passwordexp: false,
            passwordsendexpiringtime: false,
            passwordlockout: false,
            passwordunlock: "0",
            passwordchecksyntax: false,
            passwordpalindrome: false,
            passworddictcheck: false,
            passwordstoragescheme: "",
            passwordinhistory: "0",
            passwordwarning: "0",
            passwordmaxage: "0",
            passwordminage: "0",
            passwordgracelimit: "0",
            passwordlockoutduration: "0",
            passwordmaxfailure: "0",
            passwordresetfailurecount: "0",
            passwordminlength: "0",
            passwordmindigits: "0",
            passwordminalphas: "0",
            passwordminuppers: "0",
            passwordminlowers: "0",
            passwordminspecials: "0",
            passwordmin8bit: "0",
            passwordmaxrepeats: "0",
            passwordmaxsequence: "0",
            passwordmaxseqsets: "0",
            passwordmaxclasschars: "0",
            passwordmincategories: "0",
            passwordmintokenlength: "0",
            passwordbadwords: "",
            passworduserattributes: [],
            passwordtprmaxuse: "-1",
            passwordtprdelayexpireat:  "-1",
            passwordtprdelayvalidfrom:  "-1",
            passwordadmindn: "",
            passwordadminskipinfoupdate: false,
            _passwordchange: false,
            _passwordmustchange: false,
            _passwordhistory: false,
            _passwordtrackupdatetime: false,
            _passwordexp: false,
            _passwordsendexpiringtime: false,
            _passwordlockout: false,
            _passwordunlock: "0",
            _passwordchecksyntax: false,
            _passwordpalindrome: false,
            _passworddictcheck: false,
            _passwordstoragescheme: "",
            _passwordinhistory: "0",
            _passwordwarning: "0",
            _passwordmaxage: "0",
            _passwordminage: "0",
            _passwordgracelimit: "0",
            _passwordlockoutduration: "0",
            _passwordmaxfailure: "0",
            _passwordresetfailurecount: "0",
            _passwordminlength: "0",
            _passwordmindigits: "0",
            _passwordminalphas: "0",
            _passwordminuppers: "0",
            _passwordminlowers: "0",
            _passwordminspecials: "0",
            _passwordmin8bit: "0",
            _passwordmaxrepeats: "0",
            _passwordmaxsequence: "0",
            _passwordmaxseqsets: "0",
            _passwordmaxclasschars: "0",
            _passwordmincategories: "0",
            _passwordmintokenlength: "0",
            _passwordbadwords: "",
            _passworduserattributes: [],
            _passwordtprmaxuse: "-1",
            _passwordtprdelayexpireat:  "-1",
            _passwordtprdelayvalidfrom:  "-1",
            _passwordadmindn: "",
            _passwordadminskipinfoupdate: false,
            // Create policy
            ...getDefaultCreatePolicyState(),
            // Validation
            invalidFields: {},
            invalidCreateFields: {},
            // Select typeahead
            isUserAttrsCreateOpen: false,
            isUserAttrsEditOpen: false,

            attrMap: {
                passwordstoragescheme: "--pwdscheme",
                passwordtrackupdatetime: "--pwdtrack",
                passwordchange: "--pwdchange",
                passwordmustchange: "--pwdmustchange",
                passwordhistory: "--pwdhistory",
                passwordinhistory: "--pwdhistorycount",
                passwordminage: "--pwdminage",
                passwordexp: "--pwdexpire",
                passwordgracelimit: "--pwdgracelimit",
                passwordsendexpiringtime: "--pwdsendexpiring",
                passwordmaxage: "--pwdmaxage",
                passwordwarning: "--pwdwarning",
                passwordlockout: "--pwdlockout",
                passwordunlock: "--pwdunlock",
                passwordlockoutduration: "--pwdlockoutduration",
                passwordmaxfailure: "--pwdmaxfailures",
                passwordresetfailurecount: "--pwdresetfailcount",
                passwordchecksyntax: "--pwdchecksyntax",
                passwordminlength: "--pwdminlen",
                passwordmindigits: "--pwdmindigits",
                passwordminalphas: "--pwdminalphas",
                passwordminuppers: "--pwdminuppers",
                passwordminlowers: "--pwdminlowers",
                passwordminspecials: "--pwdminspecials",
                passwordmin8bit: "--pwdmin8bits",
                passwordmaxrepeats: "--pwdmaxrepeats",
                passwordpalindrome: "--pwdpalindrome",
                passwordmaxsequence: "--pwdmaxseq",
                passwordmaxseqsets: "--pwdmaxseqsets",
                passwordmaxclasschars: "--pwdmaxclasschars",
                passwordmincategories: "--pwdmincatagories",
                passwordmintokenlength: "--pwdmintokenlen",
                passwordbadwords: "--pwdbadwords",
                passworduserattributes: "--pwduserattrs",
                passworddictcheck: "--pwddictcheck",
                passwordadmindn: "--pwdadmin",
                passwordadminskipinfoupdate: "--pwdadminskipupdates",
                passwordtprmaxuse: "--pwptprmaxuse",
                passwordtprdelayexpireat:  "--pwptprdelayexpireat",
                passwordtprdelayvalidfrom:  "--pwptprdelayvalidfrom",
            },
        };

        this.unknownPolicyType = "Unknown policy type";

        // Check User Attributes Create
        this.handleUserAttrsCreateToggle = (_event, isUserAttrsCreateOpen) => {
            this.setState({
                isUserAttrsCreateOpen
            });
        };
        this.handleUserAttrsCreateClear = () => {
            this.setState({
                create_passworduserattributes: [],
                isUserAttrsCreateOpen: false
            });
        };

        // Check User Attributes Edit
        this.onUserAttrsEditToggle = isUserAttrsEditOpen => {
            this.setState({
                isUserAttrsEditOpen
            });
        };
        this.onUserAttrsEditClear = () => {
            this.setState({
                passworduserattributes: [],
                isUserAttrsEditOpen: false
            });
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        this.openCreateModal = this.openCreateModal.bind(this);
        this.closeCreateModal = this.closeCreateModal.bind(this);
        this.closeEditModal = this.closeEditModal.bind(this);
        this.getCreatePolicyProps = this.getCreatePolicyProps.bind(this);

        this.handleSelectToggle = (_event, isSelectOpen) => {
            this.setState({
                isSelectOpen
            });
        };

        this.handleSelectClear = () => {
            this.setState({
                passworduserattributes: [],
            });
        };

        this.createPolicy = this.createPolicy.bind(this);
        this.closeDeletePolicy = this.closeDeletePolicy.bind(this);
        this.deletePolicy = this.deletePolicy.bind(this);
        this.onCreateChange = this.onCreateChange.bind(this);
        this.onCreateSelectChange = this.onCreateSelectChange.bind(this);
        this.handleExpChange = this.handleExpChange.bind(this);
        this.handleGeneralChange = this.handleGeneralChange.bind(this);
        this.handleLockoutChange = this.handleLockoutChange.bind(this);
        this.onModalChange = this.onModalChange.bind(this);
        this.handleSyntaxChange = this.handleSyntaxChange.bind(this);
        this.handleTPRChange = this.handleTPRChange.bind(this);
        this.loadLocal = this.loadLocal.bind(this);
        this.handleLoadPolicies = this.handleLoadPolicies.bind(this);
        this.handleSavePolicy = this.handleSavePolicy.bind(this);
        this.showDeletePolicy = this.showDeletePolicy.bind(this);
    }

    componentDidMount() {
        // Loading config
        if (!this.state.loaded) {
            this.handleLoadPolicies();
        } else {
            this.props.enableTree();
        }
    }

    showDeletePolicy(name) {
        this.setState({
            showDeletePolicy: true,
            modalChecked: false,
            deleteName: name
        });
    }

    closeDeletePolicy() {
        this.setState({
            showDeletePolicy: false,
            deleteName: "",
        });
    }

    openCreateModal() {
        const defaultStorageScheme = this.state._create_passwordstoragescheme || "";
        this.setState({
            showCreateModal: true,
            createDisabled: true,
            creating: false,
            policyDN: "",
            invalid_dn: false,
            invalidCreateFields: {},
            createPolicyType: "Subtree Policy",
            activeTabKey: 0,
            isUserAttrsCreateOpen: false,
            ...getDefaultCreatePolicyState(defaultStorageScheme),
        });
    }

    closeCreateModal() {
        this.setState({
            showCreateModal: false,
            creating: false,
        });
    }

    closeEditModal() {
        this.setState({
            showEditModal: false,
            editPolicy: false,
        });
    }

    getCreatePolicyProps() {
        const props = {
            handleChange: this.onCreateChange,
            handleSelectChange: this.onCreateSelectChange,
            attrs: this.props.attrs,
            passwordexp: this.state.create_passwordexp,
            passwordchecksyntax: this.state.create_passwordchecksyntax,
            passwordlockout: this.state.create_passwordlockout,
            createDisabled: this.state.createDisabled,
            passworduserattributes: this.state.create_passworduserattributes,
            invalid_dn: this.state.invalid_dn,
            policyDN: this.state.policyDN,
            createPolicyType: this.state.createPolicyType,
            pwdStorageSchemes: this.props.pwdStorageSchemes,
            invalidCreateFields: this.state.invalidCreateFields,
            onUserAttrsCreateToggle: this.handleUserAttrsCreateToggle,
            onUserAttrsCreateClear: this.handleUserAttrsCreateClear,
            isUserAttrsCreateOpen: this.state.isUserAttrsCreateOpen,
        };
        const all_attrs = general_attrs.concat(exp_attrs, lockout_attrs, syntax_attrs, tpr_attrs);
        for (const attr of all_attrs) {
            props['create_' + attr] = this.state['create_' + attr];
        }
        return props;
    }

    onModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;

        this.setState({
            [attr]: value,
        });
    }

    onCreateSelectChange(_event, value) {
        this.setState({
            createPolicyType: value
        });
    }

    onCreateChange(e, selection) {
        let attr;
        let value;
        let disableSaveBtn = true;
        let invalid_dn = false;
        const all_attrs = general_attrs.concat(exp_attrs, lockout_attrs, syntax_attrs, tpr_attrs);

        if (selection) {
            attr = "create_passworduserattributes";
            value = selection;
        } else {
            value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
            attr = e.target.id;
        }

        // Check if a setting was changed, if so enable the save button
        for (const all_attr of all_attrs) {
            if (all_attr === 'passworduserattributes' && attr === 'create_passworduserattributes') {
                const orig_val = this.state._create_passworduserattributes.join(' ');
                const new_val = Array.isArray(value) ? value.join(' ') : value;
                if (orig_val !== new_val) {
                    value = selection; // restore value
                    disableSaveBtn = false;
                    break;
                }
                value = selection; // restore value
            } else if (attr === "create_" + all_attr && this.state['_create_' + all_attr] !== value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const all_attr of all_attrs) {
            if (all_attr === 'passworduserattributes' && attr !== 'create_passworduserattributes') {
                // Typeahead attribute needs special care
                const orig_val = this.state._create_passworduserattributes.join(' ');
                const new_val = this.state.create_passworduserattributes.join(' ');
                if (orig_val !== new_val) {
                    disableSaveBtn = false;
                    break;
                }
            } else if (attr !== "create_" + all_attr && this.state['_create_' + all_attr] !== this.state["create_" + all_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        // Lastly check the target DN is valid
        if (attr === "policyDN") {
            if (valid_dn(value)) {
                disableSaveBtn = false;
            } else {
                if (value !== "") {
                    invalid_dn = true;
                }
                disableSaveBtn = true;
            }
        } else {
            if (this.state.policyDN === "") {
                disableSaveBtn = true;
            } else {
                disableSaveBtn = false;
            }
        }

        // Validate constrained fields for create
        const newInvalidCreateFields = updateFieldValidation(this.state.invalidCreateFields, attr, value);
        if (newInvalidCreateFields[attr]) {
            disableSaveBtn = true;
        }

        // Select Typeahead
        if (selection) {
            this.setState({
                [attr]: Array.isArray(selection) ? selection : [],
                createDisabled: disableSaveBtn,
                invalid_dn,
                invalidCreateFields: newInvalidCreateFields,
                isUserAttrsCreateOpen: false
            });
        } else { // Checkbox
            this.setState({
                [attr]: value,
                createDisabled: disableSaveBtn,
                invalid_dn,
                invalidCreateFields: newInvalidCreateFields,
            });
        }
    }

    createPolicy() {
        const all_attrs = general_attrs.concat(exp_attrs, lockout_attrs, syntax_attrs, tpr_attrs);
        let action = "adduser";

        this.setState({
            creating: true,
        });

        if (this.state.createPolicyType === "Subtree Policy") {
            action = "addsubtree";
        }
        const cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'localpwp', action, this.state.policyDN
        ];

        for (const attr of all_attrs) {
            let old_val = this.state['_create_' + attr];
            let new_val = this.state['create_' + attr];
            if (new_val !== old_val) {
                if (typeof new_val === "boolean") {
                    if (new_val) {
                        new_val = "on";
                    } else {
                        new_val = "off";
                    }
                } else if (attr === 'passworduserattributes') {
                    old_val = this.state._create_passworduserattributes.join(' ');
                    new_val = this.state.create_passworduserattributes.join(' ');
                    if (old_val === new_val) {
                        continue;
                    }
                }
                cmd.push(this.state.attrMap[attr] + "=" + new_val);
            }
        }

        log_cmd("createPolicy", "Create a local password policy", cmd);
        cockpit
                .spawn(cmd, { superuser: "require", err: "message" })
                .done(content => {
                    this.setState({
                        creating: false,
                        showCreateModal: false,
                    });
                    this.handleLoadPolicies();
                    let message;
                    let type = "success";

                    const response = JSON.parse(content);
                    if (response) {
                        switch (response.ensure_status) {
                            case "UNCHANGED":
                                message = _("Password policy is already up to date");
                                break;
                            case "UPDATED":
                                message = _("Successfully updated password policy");
                                break;
                            case "ADDED":
                                message = _("Successfully created new password policy");
                                break;
                            default:
                                message = _("Unknown password policy operation");
                                type = "error";
                                break;
                        }
                    }
                    this.props.addNotification(type, message);
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.handleLoadPolicies();
                    this.setState({
                        creating: false,
                    });
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error creating password policy - $0"), errMsg)
                    );
                });
    }

    updateEditPolicyField(attr, value, extraState = {}) {
        const invalidFields = updateFieldValidation(
            this.state.invalidFields,
            attr,
            value
        );
        const nextState = {
            ...this.state,
            ...extraState,
            [attr]: value,
            invalidFields,
        };
        this.setState({
            ...extraState,
            [attr]: value,
            invalidFields,
            saveEditDisabled: computeSaveEditDisabled(nextState),
        });
    }

    handleGeneralChange(e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        this.updateEditPolicyField(e.target.id, value);
    }

    handleExpChange(e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        this.updateEditPolicyField(e.target.id, value);
    }

    handleLockoutChange(e) {
        const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
        this.updateEditPolicyField(e.target.id, value);
    }

    handleSyntaxChange(e, selection) {
        if (selection) {
            const attr = "passworduserattributes";
            const value = Array.isArray(selection) ? selection : [];
            this.updateEditPolicyField(attr, value, { isUserAttrsEditOpen: false });
        } else {
            const value = e.target.type === "checkbox" ? e.target.checked : e.target.value;
            this.updateEditPolicyField(e.target.id, value, { isUserAttrsEditOpen: false });
        }
    }

    handleTPRChange(e) {
        this.updateEditPolicyField(e.target.id, e.target.value);
    }

    handleSavePolicy() {
        this.setState({
            saving: true,
        });

        const cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "localpwp",
            "set",
            this.state.policyName,
        ];

        for (const attr of edit_policy_attrs) {
            if (attrValuesDiffer(attr, this.state["_" + attr], this.state[attr])) {
                const val = formatAttrValueForCmd(attr, this.state[attr]);
                cmd.push(this.state.attrMap[attr] + "=" + val);
            }
        }

        log_cmd("handleSavePolicy", "Saving local password policy settings", cmd);
        cockpit
            .spawn(cmd, { superuser: "require", err: "message" })
            .done(() => {
                this.setState({
                    saving: false,
                    showEditModal: false,
                    editPolicy: false,
                });
                this.handleLoadPolicies();
                this.props.addNotification(
                    "success",
                    _("Successfully updated password policy configuration")
                );
            })
            .fail((err) => {
                const errMsg = getApiErrorMessage(err);
                this.loadLocal(this.state.policyName);
                this.setState({
                    saving: false,
                });
                this.props.addNotification(
                    "error",
                    cockpit.format(
                        _("Error updating password policy configuration - $0"),
                        errMsg
                    )
                );
            });
    }

    deletePolicy() {
        this.setState({
            loading: true,
            editPolicy: false,
            showEditModal: false,
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "localpwp", "remove", this.state.deleteName
        ];
        log_cmd("deletePolicy", "delete policy", cmd);
        cockpit
                .spawn(cmd, { superuser: "require", err: "message" })
                .done(content => {
                    this.handleLoadPolicies();
                    this.props.addNotification(
                        "success",
                        "Successfully deleted password policy"
                    )
                })

                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.handleLoadPolicies();
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error deleting local password policy - $0"), errMsg)
                    );
                });
    }

    handleLoadPolicies() {
        this.setState({
            loading: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "localpwp", "list"
        ];
        log_cmd("handleLoadPolicies", "Load all the local password policies for the table", cmd);
        cockpit
                .spawn(cmd, { superuser: "require", err: "message" })
                .done(content => {
                    const policy_obj = JSON.parse(content);
                    const pwpRows = [];
                    for (const row of policy_obj.items) {
                        let {basedn} = row;
                        if (row.pwp_type === this.unknownPolicyType) {
                            basedn = row.dn;
                        }
                        pwpRows.push([row.targetdn, row.pwp_type, basedn]);
                    }
                    this.setState({
                        activeTabKey: 0,
                        rows: pwpRows,
                        loaded: true,
                        loading: false,
                        editPolicy: false,
                        showCreateModal: false,
                        showEditModal: false,
                        policyDN: "",
                        createPolicyType: "Subtree Policy",
                        policyName: "",
                        deleteName: "",
                        showDeletePolicy: false,
                        invalidFields: {},
                        invalidCreateFields: {},
                        // Reset edit and create tab
                        saveEditDisabled: true,
                        // Edit policy
                        passwordchange: false,
                        passwordmustchange: false,
                        passwordhistory: false,
                        passwordtrackupdatetime: false,
                        passwordexp: false,
                        passwordsendexpiringtime: false,
                        passwordlockout: false,
                        passwordunlock: "0",
                        passwordchecksyntax: false,
                        passwordpalindrome: false,
                        passworddictcheck: false,
                        passwordstoragescheme: "",
                        passwordinhistory: "0",
                        passwordwarning: "0",
                        passwordmaxage: "0",
                        passwordminage: "0",
                        passwordgracelimit: "0",
                        passwordlockoutduration: "0",
                        passwordmaxfailure: "0",
                        passwordresetfailurecount: "0",
                        passwordminlength: "0",
                        passwordmindigits: "0",
                        passwordminalphas: "0",
                        passwordminuppers: "0",
                        passwordminlowers: "0",
                        passwordminspecials: "0",
                        passwordmin8bit: "0",
                        passwordmaxrepeats: "0",
                        passwordmaxsequence: "0",
                        passwordmaxseqsets: "0",
                        passwordmaxclasschars: "0",
                        passwordmincategories: "0",
                        passwordmintokenlength: "0",
                        passwordbadwords: "",
                        passworduserattributes: [],
                        passwordadmindn: "",
                        passwordadminskipinfoupdate: false,
                        _passwordchange: false,
                        _passwordmustchange: false,
                        _passwordhistory: false,
                        _passwordtrackupdatetime: false,
                        _passwordexp: false,
                        _passwordsendexpiringtime: false,
                        _passwordlockout: false,
                        _passwordunlock: "0",
                        _passwordchecksyntax: false,
                        _passwordpalindrome: false,
                        _passworddictcheck: false,
                        _passwordstoragescheme: "",
                        _passwordinhistory: "0",
                        _passwordwarning: "0",
                        _passwordmaxage: "0",
                        _passwordminage: "0",
                        _passwordgracelimit: "0",
                        _passwordlockoutduration: "0",
                        _passwordmaxfailure: "0",
                        _passwordresetfailurecount: "0",
                        _passwordminlength: "0",
                        _passwordmindigits: "0",
                        _passwordminalphas: "0",
                        _passwordminuppers: "0",
                        _passwordminlowers: "0",
                        _passwordminspecials: "0",
                        _passwordmin8bit: "0",
                        _passwordmaxrepeats: "0",
                        _passwordmaxsequence: "0",
                        _passwordmaxseqsets: "0",
                        _passwordmaxclasschars: "0",
                        _passwordmincategories: "0",
                        _passwordmintokenlength: "0",
                        _passwordbadwords: "",
                        _passworduserattributes: [],
                        _passwordadmindn: "",
                        _passwordadminskipinfoupdate: false,
                        // Create policy
                        ...getDefaultCreatePolicyState(),
                    }, () => {
                        const gcmd = [
                            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                            "config", "get", "passwordstoragescheme"
                        ];
                        log_cmd("handleLoadPolicies", "Load global password policy password scheme", gcmd);
                        cockpit
                                .spawn(gcmd, { superuser: "require", err: "message" })
                                .done(content => {
                                    const config = JSON.parse(content);
                                    const attrs = config.attrs;
                                    const defaultStorageScheme = attrs.passwordstoragescheme[0];
                                    this.setState({
                                        create_passwordstoragescheme: defaultStorageScheme,
                                        _create_passwordstoragescheme: defaultStorageScheme,
                                    }, this.props.enableTree);
                                })
                                .fail(err => {
                                    const errMsg = getApiErrorMessage(err);
                                    this.setState({
                                        loaded: true,
                                        loading: false,
                                    });
                                    this.props.addNotification(
                                        "error",
                                        cockpit.format(_("Error loading global password storage scheme - $0"), errMsg)
                                    );
                                });
                    });
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.setState({
                        loaded: true,
                        loading: false,
                    }, this.props.enableTree);
                    console.log(
                        `Error loading local password policies - ${errMsg}`
                    );
                });
    }

    loadLocal(name) {
        this.setState({
            loading: true,
            showEditModal: true,
            editPolicy: false,
            policyName: name,
            activeTabKey: 0,
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "localpwp", "get", name
        ];
        log_cmd("loadLocal", "Load a local password policy", cmd);
        cockpit
                .spawn(cmd, { superuser: "require", err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    const attrs = config.attrs;
                    // Handle the checkbox values
                    let pwChange = false;
                    let pwMustChange = false;
                    let pwHistory = false;
                    let pwTrackUpdate = false;
                    let pwExpire = false;
                    let pwSendExpire = false;
                    let pwLockout = false;
                    let pwUnlock = false;
                    let pwCheckSyntax = false;
                    let pwPalindrome = false;
                    let pwDictCheck = false;
                    let pwUserAttrs = [];
                    let pwInHistory = "0";
                    let pwBadWords = "";
                    let pwScheme = this.state.create_passwordstoragescheme; // Default
                    let pwWarning = "0";
                    let pwMaxAge = "0";
                    let pwMinAge = "0";
                    let pwGraceLimit = "0";
                    let pwLockoutDur = "0";
                    let pwMaxFailure = "0";
                    let pwFailCount = "0";
                    let pwMinLen = "0";
                    let pwMinDigits = "0";
                    let pwMinAlphas = "0";
                    let pwMinUppers = "0";
                    let pwMinLowers = "0";
                    let pwMinSpecials = "0";
                    let pwMin8bit = "0";
                    let pwMaxRepeats = "0";
                    let pwMaxSeq = "0";
                    let pwMaxSeqSets = "0";
                    let pwMaxClassChars = "0";
                    let pwMinCat = "0";
                    let pwMinTokenLen = "0";
                    let pwTPRMaxUse = "-1";
                    let pwTPRDelayExpireAt = "-1";
                    let pwTPRDelayValidFrom = "-1";
                    let pwAdminDN = "";
                    let pwAdminSkipUpdates = false;

                    if ('passwordmintokenlength' in attrs) {
                        pwMinTokenLen = attrs.passwordmintokenlength[0];
                    }
                    if ('passwordmincategories' in attrs) {
                        pwMinCat = attrs.passwordmincategories[0];
                    }
                    if ('passwordmaxclasschars' in attrs) {
                        pwMaxClassChars = attrs.passwordmaxclasschars[0];
                    }
                    if ('passwordmaxseqsets' in attrs) {
                        pwMaxSeqSets = attrs.passwordmaxseqsets[0];
                    }
                    if ('passwordmaxsequence' in attrs) {
                        pwMaxSeq = attrs.passwordmaxsequence[0];
                    }
                    if ('passwordmaxrepeats' in attrs) {
                        pwMaxRepeats = attrs.passwordmaxrepeats[0];
                    }
                    if ('passwordmin8bit' in attrs) {
                        pwMin8bit = attrs.passwordmin8bit[0];
                    }
                    if ('passwordminspecials' in attrs) {
                        pwMinSpecials = attrs.passwordminspecials[0];
                    }
                    if ('passwordminlowers' in attrs) {
                        pwMinLowers = attrs.passwordminlowers[0];
                    }
                    if ('passwordminuppers' in attrs) {
                        pwMinUppers = attrs.passwordminuppers[0];
                    }
                    if ('passwordminalphas' in attrs) {
                        pwMinAlphas = attrs.passwordminalphas[0];
                    }
                    if ('passwordmindigits' in attrs) {
                        pwMinDigits = attrs.passwordmindigits[0];
                    }
                    if ('passwordminlength' in attrs) {
                        pwMinLen = attrs.passwordminlength[0];
                    }
                    if ('passwordresetfailurecount' in attrs) {
                        pwFailCount = attrs.passwordresetfailurecount[0];
                    }
                    if ('passwordmaxfailure' in attrs) {
                        pwMaxFailure = attrs.passwordmaxfailure[0];
                    }
                    if ('passwordlockoutduration' in attrs) {
                        pwLockoutDur = attrs.passwordlockoutduration[0];
                    }
                    if ('passwordgracelimit' in attrs) {
                        pwGraceLimit = attrs.passwordgracelimit[0];
                    }
                    if ('passwordmaxage' in attrs) {
                        pwMaxAge = attrs.passwordmaxage[0];
                    }
                    if ('passwordminage' in attrs) {
                        pwMinAge = attrs.passwordminage[0];
                    }
                    if ('passwordwarning' in attrs) {
                        pwWarning = attrs.passwordwarning[0];
                    }
                    if ('passwordstoragescheme' in attrs) {
                        pwScheme = attrs.passwordstoragescheme[0];
                    }
                    if ('passwordinhistory' in attrs) {
                        pwInHistory = attrs.passwordinhistory[0];
                    }
                    if ('passwordchange' in attrs && attrs.passwordchange[0] === "on") {
                        pwChange = true;
                    }
                    if ('passwordmustchange' in attrs && attrs.passwordmustchange[0] === "on") {
                        pwMustChange = true;
                    }
                    if ('passwordhistory' in attrs && attrs.passwordhistory[0] === "on") {
                        pwHistory = true;
                    }
                    if ('passwordtrackupdatetime' in attrs && attrs.passwordtrackupdatetime[0] === "on") {
                        pwTrackUpdate = true;
                    }
                    if ('passwordsendexpiringtime' in attrs && attrs.passwordsendexpiringtime[0] === "on") {
                        pwSendExpire = true;
                    }
                    if ('passwordlockout' in attrs && attrs.passwordlockout[0] === "on") {
                        pwLockout = true;
                    }
                    if ('passwordunlock' in attrs && attrs.passwordunlock[0] === "on") {
                        pwUnlock = true;
                    }
                    if ('passwordexp' in attrs && attrs.passwordexp[0] === "on") {
                        pwExpire = true;
                    }
                    if ('passwordchecksyntax' in attrs && attrs.passwordchecksyntax[0] === "on") {
                        pwCheckSyntax = true;
                    }
                    if ('passwordpalindrome' in attrs && attrs.passwordpalindrome[0] === "on") {
                        pwPalindrome = true;
                    }
                    if ('passworddictcheck' in attrs && attrs.passworddictcheck[0] === "on") {
                        pwDictCheck = true;
                    }
                    if ('passwordbadwords' in attrs && attrs.passwordbadwords[0] !== "") {
                        // Hack until this is fixed: https://github.com/389ds/389-ds-base/issues/3928
                        if (attrs.passwordbadwords.length > 1) {
                            pwBadWords = attrs.passwordbadwords.join(' ');
                        } else {
                            pwBadWords = attrs.passwordbadwords[0];
                        }
                    }
                    if ('passworduserattributes' in attrs && attrs.passworduserattributes[0] !== "") {
                        if (attrs.passworduserattributes.length > 1) {
                            // Hack until this is fixed: https://github.com/389ds/389-ds-base/issues/3928
                            attrs.passworduserattributes[0] = attrs.passworduserattributes.join(' ');
                        }
                        // Could be space or comma separated list
                        if (attrs.passworduserattributes[0].indexOf(',') > -1) {
                            pwUserAttrs = attrs.passworduserattributes[0].trim();
                            pwUserAttrs = pwUserAttrs.split(',');
                        } else {
                            pwUserAttrs = attrs.passworduserattributes[0].split(' ');
                        }
                    }
                    if ('passwordTPRMaxUse' in attrs) {
                        pwTPRMaxUse = attrs.passwordTPRMaxUse[0];
                    }
                    if ('passwordTPRDelayExpireAt' in attrs) {
                        pwTPRDelayExpireAt = attrs.passwordTPRDelayExpireAt[0];
                    }
                    if ('passwordTPRDelayValidFrom' in attrs) {
                        pwTPRDelayValidFrom = attrs.passwordTPRDelayValidFrom[0];
                    }
                    if ('passwordadmindn' in attrs) {
                        pwAdminDN = attrs.passwordadmindn[0];
                    }
                    if ('passwordadminskipinfoupdate' in attrs && attrs.passwordadminskipinfoupdate[0] === "on") {
                        pwAdminSkipUpdates = true;
                    }

                    this.setState({
                        editPolicy: true,
                        loading: false,
                        showEditModal: true,
                        activeTabKey: 0,
                        policyName: name,
                        policyType: config.pwp_type,
                        saveEditDisabled: true,
                        // Settings
                        passwordchange: pwChange,
                        passwordmustchange: pwMustChange,
                        passwordhistory: pwHistory,
                        passwordtrackupdatetime: pwTrackUpdate,
                        passwordexp: pwExpire,
                        passwordsendexpiringtime: pwSendExpire,
                        passwordlockout: pwLockout,
                        passwordunlock: pwUnlock,
                        passwordchecksyntax: pwCheckSyntax,
                        passwordpalindrome: pwPalindrome,
                        passworddictcheck: pwDictCheck,
                        passwordstoragescheme: pwScheme,
                        passwordinhistory: pwInHistory,
                        passwordwarning: pwWarning,
                        passwordmaxage: pwMaxAge,
                        passwordminage: pwMinAge,
                        passwordgracelimit: pwGraceLimit,
                        passwordlockoutduration: pwLockoutDur,
                        passwordmaxfailure: pwMaxFailure,
                        passwordresetfailurecount: pwFailCount,
                        passwordminlength: pwMinLen,
                        passwordmindigits: pwMinDigits,
                        passwordminalphas: pwMinAlphas,
                        passwordminuppers: pwMinUppers,
                        passwordminlowers: pwMinLowers,
                        passwordminspecials: pwMinSpecials,
                        passwordmin8bit: pwMin8bit,
                        passwordmaxrepeats: pwMaxRepeats,
                        passwordmaxsequence: pwMaxSeq,
                        passwordmaxseqsets: pwMaxSeqSets,
                        passwordmaxclasschars: pwMaxClassChars,
                        passwordmincategories: pwMinCat,
                        passwordmintokenlength: pwMinTokenLen,
                        passwordbadwords: pwBadWords,
                        passworduserattributes: pwUserAttrs,
                        passwordtprmaxuse: pwTPRMaxUse,
                        passwordtprdelayexpireat: pwTPRDelayExpireAt,
                        passwordtprdelayvalidfrom: pwTPRDelayValidFrom,
                        passwordadmindn: pwAdminDN,
                        passwordadminskipinfoupdate: pwAdminSkipUpdates,
                        // Record original values
                        _passwordchange: pwChange,
                        _passwordmustchange: pwMustChange,
                        _passwordhistory: pwHistory,
                        _passwordtrackupdatetime: pwTrackUpdate,
                        _passwordexp: pwExpire,
                        _passwordsendexpiringtime: pwSendExpire,
                        _passwordlockout: pwLockout,
                        _passwordunlock: pwUnlock,
                        _passwordchecksyntax: pwCheckSyntax,
                        _passwordpalindrome: pwPalindrome,
                        _passworddictcheck: pwDictCheck,
                        _passwordstoragescheme: pwScheme,
                        _passwordinhistory: pwInHistory,
                        _passwordwarning: pwWarning,
                        _passwordmaxage: pwMaxAge,
                        _passwordminage: pwMinAge,
                        _passwordgracelimit: pwGraceLimit,
                        _passwordlockoutduration: pwLockoutDur,
                        _passwordmaxfailure: pwMaxFailure,
                        _passwordresetfailurecount: pwFailCount,
                        _passwordminlength: pwMinLen,
                        _passwordmindigits: pwMinDigits,
                        _passwordminalphas: pwMinAlphas,
                        _passwordminuppers: pwMinUppers,
                        _passwordminlowers: pwMinLowers,
                        _passwordminspecials: pwMinSpecials,
                        _passwordmin8bit: pwMin8bit,
                        _passwordmaxrepeats: pwMaxRepeats,
                        _passwordmaxsequence: pwMaxSeq,
                        _passwordmaxseqsets: pwMaxSeqSets,
                        _passwordmaxclasschars: pwMaxClassChars,
                        _passwordmincategories: pwMinCat,
                        _passwordmintokenlength: pwMinTokenLen,
                        _passwordbadwords: pwBadWords,
                        _passworduserattributes: pwUserAttrs,
                        _passwordtprmaxuse: pwTPRMaxUse,
                        _passwordtprdelayexpireat: pwTPRDelayExpireAt,
                        _passwordtprdelayvalidfrom: pwTPRDelayValidFrom,
                        _passwordadmindn: pwAdminDN,
                        _passwordadminskipinfoupdate: pwAdminSkipUpdates,
                    });
                })
                .fail(err => {
                    const errMsg = getApiErrorMessage(err);
                    this.setState({
                        loaded: true,
                        loading: false,
                        showEditModal: false,
                        editPolicy: false,
                    });
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error loading local password policy - $0"), errMsg)
                    );
                });
    }

    render() {
        let editPolicyTabs = null;
        let pwExpirationRows = "";
        let pwLockoutRows = "";
        let pwSyntaxRows = "";

        let editSaveBtnName = _("Save");
        const editSaveExtraPrimaryProps = {};
        if (this.state.saving) {
            editSaveBtnName = _("Saving ...");
            editSaveExtraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        pwSyntaxRows = (
            <>
                <Grid>
                    <GridItem className="ds-label" span={2}>
                        {_("Minimum Length")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordminlength"
                            value={this.state.passwordminlength}
                            title={_("The minimum number of characters in the password (passwordMinLength).")}
                            fieldName="passwordminlength"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                        {renderValidationError("passwordminlength", this.state.invalidFields)}
                    </GridItem>

                    <GridItem className="ds-label" offset={6} span={2}>
                        {_("Max Repeated Chars")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordmaxrepeats"
                            value={this.state.passwordmaxrepeats}
                            title={_("The maximum number of times the same character can sequentially appear in a password (passwordMaxRepeats).")}
                            fieldName="passwordmaxrepeats"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                        {renderValidationError("passwordmaxrepeats", this.state.invalidFields)}
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={2}>
                        {_("Prohibited Words")}
                    </GridItem>
                    <GridItem span={8}>
                        <TextInput
                            title={_("A space-separated list of words that are not allowed to be contained in the new password (passwordBadWords).")}
                            value={this.state.passwordbadwords}
                            type="text"
                            id="passwordbadwords"
                            aria-describedby="horizontal-form-name-helper"
                            name="passwordbadwords"
                            onChange={(e, checked) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem span={3} title={_("Check the password against the system's CrackLib dictionary (passwordDictCheck).")}>
                        <Checkbox
                            id="passworddictcheck"
                            isChecked={this.state.passworddictcheck}
                            onChange={(e, checked) => {
                                this.handleSyntaxChange(e);
                            }}
                            label={_("Dictionary Check")}
                        />
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem span={3} title={_("Check if the password is a palindrome (passwordPalindrome).")}>
                        <Checkbox
                            id="passwordpalindrome"
                            isChecked={this.state.passwordpalindrome}
                            className="ds-label"
                            onChange={(e, checked) => {
                                this.handleSyntaxChange(e);
                            }}
                            label={_("Reject Palindromes")}
                        />
                    </GridItem>
                </Grid>
                <Grid>
                    <GridItem span={10}>
                        <Divider />
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={2}>
                        {_("Minimum Alpha's")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordminalphas"
                            value={this.state.passwordminalphas}
                            title={_("Reject passwords with fewer than this many alpha characters (passwordMinAlphas).")}
                            fieldName="passwordminalphas"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                        {renderValidationError("passwordminalphas", this.state.invalidFields)}
                    </GridItem>
                    <GridItem className="ds-label" offset={6} span={2}>
                        {_("Minimum Digits")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordmindigits"
                            value={this.state.passwordmindigits}
                            title={_("Reject passwords with fewer than this many digit characters (0-9) (passwordMinDigits).")}
                            fieldName="passwordmindigits"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                        {renderValidationError("passwordmindigits", this.state.invalidFields)}
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={2}>
                        {_("Minimum Uppercase")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordminuppers"
                            value={this.state.passwordminuppers}
                            title={_("Reject passwords with fewer than this many uppercase characters (passwordMinUppers).")}
                            fieldName="passwordminuppers"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                        {renderValidationError("passwordminuppers", this.state.invalidFields)}
                    </GridItem>
                    <GridItem className="ds-label" offset={6} span={2}>
                        {_("Minimum Lowercase")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordminlowers"
                            value={this.state.passwordminlowers}
                            title={_("Reject passwords with fewer than this many lowercase characters (passwordMinLowers).")}
                            fieldName="passwordminlowers"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                        {renderValidationError("passwordminlowers", this.state.invalidFields)}
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={2}>
                        {_("Minimum Special")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordminspecials"
                            value={this.state.passwordminspecials}
                            title={_("Reject passwords with fewer than this many special non-alphanumeric characters (passwordMinSpecials).")}
                            fieldName="passwordminspecials"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                        {renderValidationError("passwordminspecials", this.state.invalidFields)}
                    </GridItem>
                    <GridItem className="ds-label" offset={6} span={2}>
                        {_("Minimum 8-bit")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordmin8bit"
                            value={this.state.passwordmin8bit}
                            title={_("Reject passwords with fewer than this many 8-bit or multi-byte characters (passwordMin8Bit).")}
                            fieldName="passwordmin8bit"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                        {renderValidationError("passwordmin8bit", this.state.invalidFields)}
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={2}>
                        {_("Minimum Categories")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordmincategories"
                            value={this.state.passwordmincategories}
                            title={_("The minimum number of character categories that a password must contain (categories are upper, lower, digit, special, and 8-bit) (passwordMinCategories).")}
                            fieldName="passwordmincategories"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                        {renderValidationError("passwordmincategories", this.state.invalidFields)}
                    </GridItem>
                </Grid>
                <Grid>
                    <GridItem span={10}>
                        <Divider />
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={2}>
                        {_("Maximum Sequences")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordmaxsequence"
                            value={this.state.passwordmaxsequence}
                            title={_("The maximum number of allowed monotonic characters sequences (passwordMaxSequence).")}
                            fieldName="passwordmaxsequence"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordmaxsequence", this.state.invalidFields)}
                    <GridItem className="ds-label" offset={6} span={2}>
                        {_("Max Sequence Sets")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordmaxseqsets"
                            value={this.state.passwordmaxseqsets}
                            title={_("The maximum number of allowed monotonic characters sequences that can appear more than once (passwordMaxSeqSets).")}
                            fieldName="passwordmaxseqsets"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordmaxseqsets", this.state.invalidFields)}
                </Grid>
                <Grid className="ds-margin-top">
                    <GridItem className="ds-label" span={2}>
                        {_("Max Seq Per Class")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordmaxclasschars"
                            value={this.state.passwordmaxclasschars}
                            title={_("The maximum number of consecutive characters from the same character class/category (passwordMaxClassChars).")}
                            fieldName="passwordmaxclasschars"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordmaxclasschars", this.state.invalidFields)}
                </Grid>
                <Grid>
                    <GridItem span={10}>
                        <Divider />
                    </GridItem>
                </Grid>
                <Grid title={_("A list of entry attributes to compare to the new password (passwordUserAttributes).")}>
                    <GridItem className="ds-label" span={2}>
                        {_("Check User Attributes")}
                    </GridItem>
                    <GridItem span={8}>
                        <TypeaheadSelect
                            selected={this.state.passworduserattributes}
                            onSelect={this.handleSyntaxChange}
                            onClear={this.handleSelectClear}
                            options={this.props.attrs}
                            isOpen={this.state.isSelectOpen}
                            onToggle={this.handleSelectToggle}
                            placeholder={_("Type attributes to check...")}
                            noResultsText={_("There are no matching entries")}
                            ariaLabel="Type an attribute to check"
                            isMulti={true}
                        />
                    </GridItem>
                </Grid>
                <Grid className="ds-margin-top" title={_("The smallest attribute value used when checking if the password contains any of the user's attributes that are set via passwordUserAttributes (passwordMinTokenLength).")}>
                    <GridItem className="ds-label" span={2}>
                        {_("Minimum Token Length")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordmintokenlength"
                            value={this.state.passwordmintokenlength}
                            fieldName="passwordmintokenlength"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleSyntaxChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordmintokenlength", this.state.invalidFields)}
                </Grid>
            </>
        );

        pwLockoutRows = (
            <>
                <Grid title={_("The maximum number of failed logins before account gets locked (passwordMaxFailure).")}>
                    <GridItem className="ds-label" span={4}>
                        {_("Number of Failed Logins That Locks out Account")}
                    </GridItem>
                    <GridItem span={2}>
                        <DsNumberInput
                            id="passwordmaxfailure"
                            value={this.state.passwordmaxfailure}
                            fieldName="passwordmaxfailure"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleLockoutChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordmaxfailure", this.state.invalidFields)}
                </Grid>
                <Grid className="ds-margin-top" title={_("The number of seconds until an accounts failure count is reset (passwordResetFailureCount).")}>
                    <GridItem className="ds-label" span={4}>
                        {_("Time Until <i>Failure Count</i> Resets")}
                    </GridItem>
                    <GridItem span={2}>
                        <DsNumberInput
                            id="passwordresetfailurecount"
                            value={this.state.passwordresetfailurecount}
                            fieldName="passwordresetfailurecount"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleLockoutChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordresetfailurecount", this.state.invalidFields)}
                </Grid>
                <Grid className="ds-margin-top" title={_("The number of seconds, duration, before the account gets unlocked (passwordLockoutDuration).")}>
                    <GridItem className="ds-label" span={4}>
                        {_("Time Until Account Unlocked")}
                    </GridItem>
                    <GridItem span={2}>
                        <DsNumberInput
                            id="passwordlockoutduration"
                            value={this.state.passwordlockoutduration}
                            fieldName="passwordlockoutduration"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleLockoutChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordlockoutduration", this.state.invalidFields)}
                </Grid>
                <Grid className="ds-margin-top" title={_("Do not lockout the user account forever, instead the account will unlock based on the lockout duration (passwordUnlock).")}>
                    <GridItem span={6}>
                        <Checkbox
                            id="passwordunlock"
                            isChecked={this.state.passwordunlock}
                            onChange={(e, checked) => {
                                this.handleLockoutChange(e);
                            }}
                            label={_("Do Not Lockout Account Forever")}
                        />
                    </GridItem>
                </Grid>
            </>
        );

        pwExpirationRows = (
            <>
                <Grid title={_("The maximum age of a password in seconds before it expires (passwordMaxAge).")}>
                    <GridItem className="ds-label" span={4}>
                        {_("Password Expiration Time")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordmaxage"
                            value={this.state.passwordmaxage}
                            fieldName="passwordmaxage"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleExpChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordmaxage", this.state.invalidFields)}
                </Grid>
                <Grid className="ds-margin-top" title={_("The number of logins that are allowed after the password has expired (passwordGraceLimit).")}>
                    <GridItem className="ds-label" span={4}>
                        {_("Allowed Logins After Password Expires")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordgracelimit"
                            value={this.state.passwordgracelimit}
                            fieldName="passwordgracelimit"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleExpChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordgracelimit", this.state.invalidFields)}
                </Grid>
                <Grid className="ds-margin-top" title={_("Set the time (in seconds), before a password is about to expire, to send a warning. (passwordWarning).")}>
                    <GridItem className="ds-label" span={4}>
                        {_("Send Password Expiring Warning")}
                    </GridItem>
                    <GridItem span={1}>
                        <DsNumberInput
                            id="passwordwarning"
                            value={this.state.passwordwarning}
                            fieldName="passwordwarning"
                            invalidFields={this.state.invalidFields}
                            onChange={(e) => {
                                this.handleExpChange(e);
                            }}
                        />
                    </GridItem>
                    {renderValidationError("passwordwarning", this.state.invalidFields)}
                </Grid>
                <Grid className="ds-margin-top" title={_("Always return a password expiring control when requested (passwordSendExpiringTime).")}>
                    <GridItem span={4}>
                        <Checkbox
                            id="passwordsendexpiringtime"
                            isChecked={this.state.passwordsendexpiringtime}
                            onChange={(e, checked) => {
                                this.handleExpChange(e);
                            }}
                            label={_("Always send Password Expiring Control")}
                            className="ds-lower-field"
                        />
                    </GridItem>
                </Grid>
            </>
        );

        if (this.state.showEditModal && this.state.loading && !this.state.editPolicy) {
            editPolicyTabs = (
                <div className="ds-center ds-margin-top-xlg">
                    <Spinner size="xl" />
                </div>
            );
        } else if (this.state.editPolicy) {
            editPolicyTabs = (
                <div className="ds-left-margin">
                    <TextContent>
                        <Text className="ds-margin-top" component={TextVariants.h4}>
                            <b>{this.state.policyName}</b> <font size="2">({this.state.policyType})</font>
                        </Text>
                    </TextContent>
                    <Tabs className="ds-margin-top-lg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={0} title={<TabTitleText>{_("General Settings")}</TabTitleText>}>
                            <Form className="ds-margin-left-sm ds-margin-top-lg" isHorizontal autoComplete="off">
                                <Grid hasGutter>
                                    <GridItem
                                        span={5}
                                        title={_("Record a separate timestamp specifically for the last time that the password for an entry was changed. If this is enabled, then it adds the pwdUpdateTime operational attribute to the user account entry (passwordTrackUpdateTime).")}
                                    >
                                        <Checkbox
                                            id="passwordtrackupdatetime"
                                            isChecked={this.state.passwordtrackupdatetime}
                                            onChange={(e, checked) => {
                                                this.handleGeneralChange(e);
                                            }}
                                            label={_("Track Password Update Time")}
                                        />
                                    </GridItem>
                                    <GridItem span={5} title={_("Allow user's to change their passwords (passwordChange).")}>
                                        <Checkbox
                                            id="passwordchange"
                                            isChecked={this.state.passwordchange}
                                            onChange={(e, checked) => {
                                                this.handleGeneralChange(e);
                                            }}
                                            label={_("Allow Users To Change Their Passwords")}
                                        />
                                    </GridItem>
                                    <GridItem span={5} title={_("User must change its password after its been reset by an administrator (passwordMustChange).")}>
                                        <Checkbox
                                            id="passwordmustchange"
                                            isChecked={this.state.passwordmustchange}
                                            onChange={(e, checked) => {
                                                this.handleGeneralChange(e);
                                            }}
                                            label={_("User Must Change Password After Reset")}
                                        />
                                    </GridItem>
                                    <GridItem
                                        span={5}
                                        title={_("Disable updating password state attributes like passwordExpirationtime, passwordHistory, etc, when setting a user's password as a Password Administrator (passwordAdminSkipInfoUpdate).")}
                                    >
                                        <Checkbox
                                            id="passwordadminskipinfoupdate"
                                            isChecked={this.state.passwordadminskipinfoupdate}
                                            onChange={(e, checked) => {
                                                this.handleGeneralChange(e);
                                            }}
                                            label={_("Do not update target entry's password state attributes")}
                                        />
                                    </GridItem>
                                    <GridItem span={5} title={_("Maintain a password history for each user (passwordHistory).")}>
                                        <div className="ds-inline">
                                            <Checkbox
                                                id="passwordhistory"
                                                isChecked={this.state.passwordhistory}
                                                onChange={(e, checked) => {
                                                    this.handleGeneralChange(e);
                                                }}
                                                label={_("Keep Password History")}
                                            />
                                        </div>
                                        <div className="ds-inline ds-left-margin ds-raise-field-md ds-width-sm">
                                            <DsNumberInput
                                                id="passwordinhistory"
                                                value={this.state.passwordinhistory}
                                                fieldName="passwordinhistory"
                                                invalidFields={this.state.invalidFields}
                                                onChange={(e) => {
                                                    this.handleGeneralChange(e);
                                                }}
                                            />
                                            {renderValidationError("passwordinhistory", this.state.invalidFields)}
                                        </div>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title={_("Set the password storage scheme (passwordstoragescheme).")}>
                                    <GridItem className="ds-label" span={3}>
                                        {_("Password Storage Scheme")}
                                    </GridItem>
                                    <GridItem span={7}>
                                        <FormSelect
                                            id="passwordstoragescheme"
                                            value={this.state.passwordstoragescheme}
                                            onChange={(event, value) => {
                                                this.handleGeneralChange(event);
                                            }}
                                            aria-label="FormSelect Input"
                                        >
                                            {this.props.pwdStorageSchemes.map((option, index) => (
                                                <FormSelectOption
                                                    key={index}
                                                    value={option}
                                                    label={option}
                                                />
                                            ))}
                                        </FormSelect>
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title={_("Indicates the number of seconds that must pass before a user can change their password again. (passwordMinAge).")}
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Password Minimum Age")}
                                    </GridItem>
                                    <GridItem span={7}>
                                        <DsNumberInput
                                            id="passwordminage"
                                            value={this.state.passwordminage}
                                            fieldName="passwordminage"
                                            invalidFields={this.state.invalidFields}
                                            onChange={(e) => {
                                                this.handleGeneralChange(e);
                                            }}
                                        />
                                    </GridItem>
                                    {renderValidationError("passwordminage", this.state.invalidFields)}
                                </Grid>
                                <Grid
                                    title={_("The DN for a password administrator or administrator group (passwordAdminDN).")}
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Password Administrator")}
                                    </GridItem>
                                    <GridItem span={7}>
                                        <TextInput
                                            value={this.state.passwordadmindn}
                                            type="text"
                                            id="passwordadmindn"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="passwordadmindn"
                                            onChange={(e, checked) => {
                                                this.handleGeneralChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                        </Tab>
                        <Tab eventKey={1} title={<TabTitleText>{_("Expiration")}</TabTitleText>}>
                            <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                                <Grid className="ds-margin-top" title={_("Enable a password expiration policy (passwordExp).")}>
                                    <GridItem span={10}>
                                        <Checkbox
                                            id="passwordexp"
                                            isChecked={this.state.passwordexp}
                                            onChange={(e, checked) => {
                                                this.handleExpChange(e);
                                            }}
                                            label={_("Enforce Password Expiration")}
                                        />
                                    </GridItem>
                                </Grid>
                                <div className="ds-left-indent">
                                    {pwExpirationRows}
                                </div>
                            </Form>
                        </Tab>
                        <Tab eventKey={2} title={<TabTitleText>{_("Account Lockout")}</TabTitleText>}>
                            <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                                <Grid className="ds-margin-top" title={_("Enable account lockout (passwordLockout).")}>
                                    <GridItem span={10}>
                                        <Checkbox
                                            id="passwordlockout"
                                            isChecked={this.state.passwordlockout}
                                            onChange={(e, checked) => {
                                                this.handleLockoutChange(e);
                                            }}
                                            label={_("Enable Account Lockout")}
                                        />
                                    </GridItem>
                                </Grid>
                                <div className="ds-left-indent">
                                    {pwLockoutRows}
                                </div>
                            </Form>
                        </Tab>
                        <Tab eventKey={3} title={<TabTitleText>{_("Syntax Checking")}</TabTitleText>}>
                            <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                                <Grid title={_("Enable password syntax checking (passwordCheckSyntax).")}>
                                    <GridItem span={10}>
                                        <Checkbox
                                            id="passwordchecksyntax"
                                            isChecked={this.state.passwordchecksyntax}
                                            onChange={(e, checked) => {
                                                this.handleSyntaxChange(e);
                                            }}
                                            label={_("Enable Password Syntax Checking")}
                                        />
                                    </GridItem>
                                </Grid>
                                <div className="ds-left-indent">
                                    {pwSyntaxRows}
                                </div>
                            </Form>
                        </Tab>
                        <Tab eventKey={4} title={<TabTitleText>{_("Temporary Password Rules")}</TabTitleText>}>
                            <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                                {this.state.passwordmustchange === false && (
                                    <FormAlert className="ds-margin-top">
                                        <Alert
                                        variant="info"
                                        title={_("\"User Must Change Password After Reset\" must be enabled in General Settings to activate TPR.")}
                                        aria-live="polite"
                                        isInline
                                        />
                                    </FormAlert>
                                )}
                                <Grid
                                    title={_("Number of times the temporary password can be used to authenticate (passwordTPRMaxUse).")}
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Password Max Use")}
                                    </GridItem>
                                    <GridItem span={7}>
                                        <DsNumberInput
                                            id="passwordtprmaxuse"
                                            fieldName="passwordtprmaxuse"
                                            value={this.state.passwordtprmaxuse}
                                            invalidFields={this.state.invalidFields}
                                            isDisabled={!this.state.passwordmustchange}
                                            onChange={(e) => {
                                                this.handleTPRChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                            <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                                <Grid
                                    title={_("Number of seconds before the temporary password expires (passwordTPRDelayExpireAt).")}
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Password Expires In")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <DsNumberInput
                                            id="passwordtprdelayexpireat"
                                            fieldName="passwordtprdelayexpireat"
                                            value={this.state.passwordtprdelayexpireat}
                                            invalidFields={this.state.invalidFields}
                                            isDisabled={!this.state.passwordmustchange}
                                            onChange={(e) => {
                                                this.handleTPRChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                            <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                                <Grid
                                    title={_("Number of seconds after which temporary password starts to be valid for authentication (passwordTPRDelayValidFrom).")}
                                >
                                    <GridItem className="ds-label" span={3}>
                                        {_("Password Valid From")}
                                    </GridItem>
                                    <GridItem span={9}>
                                        <DsNumberInput
                                            id="passwordtprdelayvalidfrom"
                                            fieldName="passwordtprdelayvalidfrom"
                                            value={this.state.passwordtprdelayvalidfrom}
                                            invalidFields={this.state.invalidFields}
                                            isDisabled={!this.state.passwordmustchange}
                                            onChange={(e) => {
                                                this.handleTPRChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                        </Tab>
                    </Tabs>
                </div>
            );
        }

        let createBtnName = _("Create New Policy");
        const createExtraPrimaryProps = {};
        if (this.state.creating) {
            createBtnName = _("Creating ...");
            createExtraPrimaryProps.spinnerAriaValueText = _("Creating");
        }

        let body = (
            <div className="ds-margin-top-xlg">
                <PwpTable
                    key={this.state.rows}
                    rows={this.state.rows}
                    editPolicy={this.loadLocal}
                    deletePolicy={this.showDeletePolicy}
                />
                <Button
                    variant="primary"
                    className="ds-margin-top-lg"
                    onClick={this.openCreateModal}
                >
                    {_("Create New Local Policy")}
                </Button>
            </div>
        );

        if (!this.state.loaded || (this.state.loading && !this.state.showCreateModal && !this.state.showEditModal)) {
            body = (
                <div className="ds-margin-top-xlg ds-center">
                    <Spinner  size="xl" />
                </div>
            );
        }

        return (
            <div className={this.state.saving ? "ds-disabled" : ""}>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h2}>
                                {_("Local Password Policies")}
                                <Button
                                    variant="plain"
                                    aria-label={_("Refresh the local password policies")}
                                    onClick={this.handleLoadPolicies}
                                >
                                    <SyncAltIcon />
                                </Button>
                            </Text>
                        </TextContent>
                    </GridItem>
                </Grid>
                {body}
                <Modal
                    variant={ModalVariant.large}
                    title={_("Create Local Password Policy")}
                    isOpen={this.state.showCreateModal}
                    onClose={this.closeCreateModal}
                    actions={[
                        <Button
                            key="create"
                            variant="primary"
                            onClick={this.createPolicy}
                            isDisabled={this.state.createDisabled || this.state.creating}
                            isLoading={this.state.creating}
                            spinnerAriaValueText={this.state.creating ? _("Creating") : undefined}
                            {...createExtraPrimaryProps}
                        >
                            {createBtnName}
                        </Button>,
                        <Button key="cancel" variant="link" onClick={this.closeCreateModal}>
                            {_("Cancel")}
                        </Button>,
                    ]}
                >
                    <CreatePolicy
                        key={this.state.showCreateModal ? "create-open" : "create-closed"}
                        {...this.getCreatePolicyProps()}
                    />
                </Modal>
                <Modal
                    variant={ModalVariant.large}
                    title={this.state.editPolicy
                        ? cockpit.format(_("Edit Local Password Policy - $0"), this.state.policyName)
                        : _("Edit Local Password Policy")}
                    isOpen={this.state.showEditModal}
                    onClose={this.closeEditModal}
                    actions={[
                        <Button
                            key="save"
                            variant="primary"
                            onClick={this.handleSavePolicy}
                            isDisabled={this.state.saveEditDisabled || this.state.saving}
                            isLoading={this.state.saving}
                            spinnerAriaValueText={this.state.saving ? _("Saving") : undefined}
                            {...editSaveExtraPrimaryProps}
                        >
                            {editSaveBtnName}
                        </Button>,
                        <Button key="close" variant="link" onClick={this.closeEditModal}>
                            {_("Close")}
                        </Button>,
                    ]}
                >
                    {editPolicyTabs}
                </Modal>
                <DoubleConfirmModal
                    showModal={this.state.showDeletePolicy}
                    closeHandler={this.closeDeletePolicy}
                    handleChange={this.onModalChange}
                    actionHandler={this.deletePolicy}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    spinning={this.state.loading}
                    mTitle={_("Delete Local Password Policy")}
                    mMsg={_("Are you sure you want to delete this local password policy?")}
                    mSpinningMsg={_("Deleting local password policy ...")}
                    mBtnName={_("Delete Policy")}
                />
            </div>
        );
    }
}

LocalPwPolicy.propTypes = {
    attrs: PropTypes.array,
    pwdStorageSchemes: PropTypes.array,
};

LocalPwPolicy.defaultProps = {
    attrs: [],
    pwdStorageSchemes: [],
};
