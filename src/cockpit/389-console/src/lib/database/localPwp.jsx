import cockpit from "cockpit";
import React from "react";
import { log_cmd, valid_dn } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import { PwpTable } from "./databaseTables.jsx";
import {
    Alert,
    Button,
    Checkbox,
    ExpandableSection,
    Form,
    FormAlert,
    FormHelperText,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    Select,
    SelectOption,
    SelectVariant,
    Spinner,
    Tab,
    Tabs,
    TabTitleText,
    TextInput,
    Text,
    TextContent,
    TextVariants,
    ValidatedOptions
} from "@patternfly/react-core";
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';
import PropTypes from "prop-types";

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

class CreatePolicy extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            isExpiredExpanded: false,
            isGeneralExpanded: false,
            isLockoutExpanded: false,
            isSyntaxExpanded: false,
            isTPRExpanded: false,
        };

        this.onGeneralToggle = (isGeneralExpanded) => {
            this.setState({
                isGeneralExpanded
            });
        };
        this.onLockoutToggle = (isLockoutExpanded) => {
            this.setState({
                isLockoutExpanded
            });
        };
        this.onExpiredToggle = (isExpiredExpanded) => {
            this.setState({
                isExpiredExpanded
            });
        };
        this.onSyntaxToggle = (isSyntaxExpanded) => {
            this.setState({
                isSyntaxExpanded
            });
        };
        this.onTPRToggle = (isTPRExpanded) => {
            this.setState({
                isTPRExpanded
            });
        };
    }

    render() {
        let helper_text = "Required field";
        if (this.props.invalid_dn) {
            helper_text = "Invalid DN";
        }

        return (
            <div className="ds-margin-bottom-md">
                <Form className="ds-margin-left ds-margin-top-xlg" isHorizontal autoComplete="off">
                    <TextContent>
                        <Text className="ds-center" component={TextVariants.h3}>
                            Create A New Local Password Policy
                        </Text>
                    </TextContent>
                    <Grid className="ds-margin-top-lg">
                        <GridItem className="ds-label" span={3}>
                            Password Policy Type
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect value={this.props.createPolicyType} onChange={this.props.handleSelectChange} id="createPolicyType" aria-label="FormSelect Input">
                                <FormSelectOption key={1} value="Subtree Policy" label="Subtree Policy" />
                                <FormSelectOption key={2} value="User Policy" label="User Policy" />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid title="The DN of the entry to apply this password policy to.">
                        <GridItem className="ds-label" span={3}>
                            Target DN
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                type="text"
                                id="policyDN"
                                aria-describedby="horizontal-form-name-helper"
                                name="policyDN"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                                validated={this.props.invalid_dn ? ValidatedOptions.error : ValidatedOptions.default}
                            />
                            <FormHelperText isError isHidden={!this.props.invalid_dn && this.props.policyDN != ""}>
                                {helper_text}
                            </FormHelperText>
                        </GridItem>
                    </Grid>

                    <ExpandableSection
                        className="ds-margin-top-lg"
                        toggleText={this.state.isGeneralExpanded ? 'Hide General Settings' : 'Show General Settings'}
                        onToggle={this.onGeneralToggle}
                        isExpanded={this.state.isGeneralExpanded}
                    >
                        <div className="ds-margin-left">
                            <Grid className="ds-margin-top" title="Set the password storage scheme (passwordstoragescheme).">
                                <GridItem className="ds-label" span={3}>
                                    Password Storage Scheme
                                </GridItem>
                                <GridItem span={9}>
                                    <FormSelect
                                        id="create_passwordstoragescheme"
                                        value={this.props.create_passwordstoragescheme}
                                        onChange={(value, event) => {
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
                                title="Indicates the number of seconds that must pass before a user can change their password again. (passwordMinAge)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Password Minimum Age
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        type="number"
                                        id="create_passwordminage"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="create_passwordminage"
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                className="ds-margin-top"
                                title="The DN for a password administrator or administrator group (passwordAdminDN)."
                            >
                                <GridItem className="ds-label" span={3}>
                                    Password Administrator
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        value={this.props.passwordadmindn}
                                        type="text"
                                        id="create_passwordadmindn"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="create_passwordadmindn"
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                className="ds-margin-top"
                                title="Disable updating password state attributes like passwordExpirationtime, passwordHistory, etc, when setting a user's password as a Password Administrator (passwordAdminSkipInfoUpdate)."
                            >
                                <GridItem offset={3} span={9}>
                                    <Checkbox
                                        id="create_passwordadminskipinfoupdate"
                                        isChecked={this.props.create_passwordadminskipinfoupdate}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                        label="Do not update target entry's password state attributes"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid className="ds-margin-top" title="Record a separate timestamp specifically for the last time that the password for an entry was changed. If this is enabled, then it adds the pwdUpdateTime operational attribute to the user account entry (passwordTrackUpdateTime).">
                                <GridItem span={12}>
                                    <Checkbox
                                        id="create_passwordtrackupdatetime"
                                        isChecked={this.props.create_passwordtrackupdatetime}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                        label="Track Password Update Time"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid className="ds-margin-top" title="Allow user's to change their passwords (passwordChange).">
                                <GridItem span={12}>
                                    <Checkbox
                                        id="create_passwordchange"
                                        isChecked={this.props.create_passwordchange}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                        label="Allow Users To Change Their Passwords"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid className="ds-margin-top" title="User must change its password after its been reset by an administrator (passwordMustChange).">
                                <GridItem span={12}>
                                    <Checkbox
                                        id="create_passwordmustchange"
                                        isChecked={this.props.create_passwordmustchange}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                        label="User Must Change Password After Reset"
                                    />
                                </GridItem>
                            </Grid>
                            <Grid className="ds-margin-top" title="Maintain a password history for each user (passwordHistory).">
                                <GridItem span={12}>
                                    <div className="ds-inline">
                                        <Checkbox
                                            id="create_passwordhistory"
                                            isChecked={this.props.create_passwordhistory}
                                            onChange={(checked, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            label="Keep Password History"
                                        />
                                    </div>
                                    <div className="ds-inline ds-left-margin ds-raise-field-md ds-width-sm">
                                        <TextInput
                                            value={this.props.passwordinhistory}
                                            type="number"
                                            id="create_passwordinhistory"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="create_passwordinhistory"
                                            onChange={(checked, e) => {
                                                this.props.handleChange(e);
                                            }}
                                        />
                                    </div>
                                </GridItem>
                            </Grid>
                        </div>
                    </ExpandableSection>

                    <ExpandableSection
                        className="ds-margin-top-lg"
                        toggleText={this.state.isExpiredExpanded ? 'Hide Expiration Settings' : 'Show Expiration Settings'}
                        onToggle={this.onExpiredToggle}
                        isExpanded={this.state.isExpiredExpanded}
                    >
                        <div className="ds-margin-left">
                            <Grid className="ds-margin-top" title="Enable a password expiration policy (passwordExp).">
                                <GridItem span={12}>
                                    <Checkbox
                                        id="create_passwordexp"
                                        isChecked={this.props.create_passwordexp}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                        label="Enforce Password Expiration"
                                    />
                                </GridItem>
                            </Grid>
                            <div className="ds-left-indent">
                                <Grid
                                    title="The maxiumum age of a password in seconds before it expires (passwordMaxAge)."
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={4}>
                                        Password Expiration Time
                                    </GridItem>
                                    <GridItem span={1}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordmaxage"
                                            aria-describedby="create_passwordmaxage"
                                            name="create_passwordmaxage"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordexp}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="The number of logins that are allowed after the password has expired (passwordGraceLimit).">
                                    <GridItem className="ds-label" span={4}>
                                        Allowed Logins After Password Expires
                                    </GridItem>
                                    <GridItem span={1}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordgracelimit"
                                            aria-describedby="create_passwordgracelimit"
                                            name="create_passwordgracelimit"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordexp}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Set the time (in seconds), before a password is about to expire, to send a warning. (passwordWarning).">
                                    <GridItem className="ds-label" span={4}>
                                        Send Password Expiring Warning
                                    </GridItem>
                                    <GridItem span={1}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordwarning"
                                            aria-describedby="create_passwordwarning"
                                            name="create_passwordwarning"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordexp}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Always return a password expiring control when requested (passwordSendExpiringTime).">
                                    <GridItem className="ds-label" span={4}>
                                        Send Password Expiring Warning
                                    </GridItem>
                                    <GridItem span={4}>
                                        <Checkbox
                                            id="create_passwordsendexpiringtime"
                                            isChecked={this.props.create_passwordsendexpiringtime}
                                            onChange={(checked, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordexp}
                                            label={<>Always Send <i>Password Expiring</i> Control</>}
                                            className="ds-lower-field"
                                        />
                                    </GridItem>
                                </Grid>
                            </div>
                        </div>
                    </ExpandableSection>

                    <ExpandableSection
                        className="ds-margin-top-lg"
                        toggleText={this.state.isLockoutExpanded ? 'Hide Lockout Settings' : 'Show Lockout Settings'}
                        onToggle={this.onLockoutToggle}
                        isExpanded={this.state.isLockoutExpanded}
                    >
                        <div className="ds-margin-left">
                            <Grid className="ds-margin-top" title="Enable account lockout (passwordLockout).">
                                <GridItem span={12}>
                                    <Checkbox
                                        id="create_passwordlockout"
                                        isChecked={this.props.create_passwordlockout}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                        label="Enable Account Lockout"
                                    />
                                </GridItem>
                            </Grid>
                            <div className="ds-left-indent">
                                <Grid className="ds-margin-top" title="The maximum number of failed logins before account gets locked (passwordMaxFailure).">
                                    <GridItem className="ds-label" span={4}>
                                        Number of Failed Logins That Locks out Account
                                    </GridItem>
                                    <GridItem span={2}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordmaxfailure"
                                            aria-describedby="create_passwordmaxfailure"
                                            name="create_passwordmaxfailure"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordlockout}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="The number of seconds until an accounts failure count is reset (passwordResetFailureCount).">
                                    <GridItem className="ds-label" span={4}>
                                        Time Until <i>Failure Count</i> Resets
                                    </GridItem>
                                    <GridItem span={2}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordresetfailurecount"
                                            aria-describedby="create_passwordresetfailurecount"
                                            name="create_passwordresetfailurecount"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordlockout}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="The number of seconds, duration, before the account gets unlocked (passwordLockoutDuration).">
                                    <GridItem className="ds-label" span={4}>
                                        Time Until Account Unlocked
                                    </GridItem>
                                    <GridItem span={2}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordlockoutduration"
                                            aria-describedby="create_passwordlockoutduration"
                                            name="create_passwordlockoutduration"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordlockout}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Do not lockout the user account forever, instead the account will unlock based on the lockout duration (passwordUnlock).">
                                    <GridItem span={6}>
                                        <Checkbox
                                            id="create_passwordunlock"
                                            isChecked={this.props.create_passwordunlock}
                                            onChange={(checked, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordlockout}
                                            label="Do Not Lockout Account Forever"
                                        />
                                    </GridItem>
                                </Grid>
                            </div>
                        </div>
                    </ExpandableSection>

                    <ExpandableSection
                        className="ds-margin-top-lg"
                        toggleText={this.state.isSyntaxExpanded ? 'Hide Syntax Settings' : 'Show Syntax Settings'}
                        onToggle={this.onSyntaxToggle}
                        isExpanded={this.state.isSyntaxExpanded}
                    >
                        <div className="ds-margin-left">
                            <Grid title="Enable password syntax checking (passwordCheckSyntax).">
                                <GridItem span={12}>
                                    <Checkbox
                                        id="create_passwordchecksyntax"
                                        isChecked={this.props.create_passwordchecksyntax}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                        label="Enable Password Syntax Checking"
                                    />
                                </GridItem>
                            </Grid>
                            <div className="ds-left-indent">
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Minimum Length
                                    </GridItem>
                                    <GridItem span={1} title="The minimum number of characters in the password (passwordMinLength).">
                                        <TextInput
                                            type="number"
                                            id="create_passwordminlength"
                                            aria-describedby="create_passwordminlength"
                                            name="create_passwordminlength"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                    <GridItem className="ds-label" offset={5} span={3} title="Reject passwords with fewer than this many alpha characters (passwordMinAlphas).">
                                        Minimum Alpha's
                                    </GridItem>
                                    <GridItem span={1}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordminalphas"
                                            aria-describedby="create_passwordminalphas"
                                            name="create_passwordminalphas"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Minimum Digits
                                    </GridItem>
                                    <GridItem span={1} title="Reject passwords with fewer than this many digit characters (0-9) (passwordMinDigits).">
                                        <TextInput
                                            type="number"
                                            id="create_passwordmindigits"
                                            aria-describedby="create_passwordmindigits"
                                            name="create_passwordmindigits"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                    <GridItem className="ds-label" offset={5} span={3} title="Reject passwords with fewer than this many special non-alphanumeric characters (passwordMinSpecials).">
                                        Minimum Special
                                    </GridItem>
                                    <GridItem span={1}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordminspecials"
                                            aria-describedby="create_passwordminspecials"
                                            name="create_passwordminspecials"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Minimum Uppercase
                                    </GridItem>
                                    <GridItem span={1} title="Reject passwords with fewer than this many uppercase characters (passwordMinUppers).">
                                        <TextInput
                                            type="number"
                                            id="create_passwordminuppers"
                                            aria-describedby="create_passwordminuppers"
                                            name="create_passwordminuppers"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                    <GridItem className="ds-label" offset={5} span={3} title="Reject passwords with fewer than this many lowercase characters (passwordMinLowers).">
                                        Minimum Lowercase
                                    </GridItem>
                                    <GridItem span={1}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordminlowers"
                                            aria-describedby="create_passwordminlowers"
                                            name="create_passwordminlowers"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Minimum 8-bit
                                    </GridItem>
                                    <GridItem span={1} title="Reject passwords with fewer than this many 8-bit or multi-byte characters (passwordMin8Bit).">
                                        <TextInput
                                            type="number"
                                            id="create_passwordmin8bit"
                                            aria-describedby="create_passwordmin8bit"
                                            name="create_passwordmin8bit"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                    <GridItem className="ds-label" offset={5} span={3} title="The minimum number of character categories that a password must contain (categories are upper, lower, digit, special, and 8-bit) (passwordMinCategories).">
                                        Minimum Categories
                                    </GridItem>
                                    <GridItem span={1}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordmincategories"
                                            aria-describedby="create_passwordmincategories"
                                            name="create_passwordmincategories"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Minimum Token Length
                                    </GridItem>
                                    <GridItem span={1} title="The smallest attribute value used when checking if the password contains any of the user's account information (passwordMinTokenLength).">
                                        <TextInput
                                            type="number"
                                            id="create_passwordmintokenlength"
                                            aria-describedby="create_passwordmintokenlength"
                                            name="create_passwordmintokenlength"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                    <GridItem className="ds-label" offset={5} span={3} title="The maximum number of times the same character can sequentially appear in a password (passwordMaxRepeats).">
                                        Max Repeated Chars
                                    </GridItem>
                                    <GridItem span={1}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordmaxrepeats"
                                            aria-describedby="create_passwordmaxrepeats"
                                            name="create_passwordmaxrepeats"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Max Sequences
                                    </GridItem>
                                    <GridItem span={1} title="The maximum number of allowed monotonic characters sequences (passwordMaxSequence).">
                                        <TextInput
                                            type="number"
                                            id="create_passwordmaxsequence"
                                            aria-describedby="create_passwordmaxsequence"
                                            name="create_passwordmaxsequence"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                    <GridItem className="ds-label" offset={5} span={3} title="The maximum number of times the same character can sequentially appear in a password (passwordMaxRepeats).">
                                        Max Sequence Sets
                                    </GridItem>
                                    <GridItem span={1}>
                                        <TextInput
                                            type="number"
                                            id="create_passwordmaxseqsets"
                                            aria-describedby="create_passwordmaxseqsets"
                                            name="create_passwordmaxseqsets"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top">
                                    <GridItem className="ds-label" span={3}>
                                        Max Seq Per Class
                                    </GridItem>
                                    <GridItem span={1} title="The maximum number of consecutive characters from the same character class/category (passwordMaxClassChars)..">
                                        <TextInput
                                            type="number"
                                            id="create_passwordmaxclasschars"
                                            aria-describedby="create_passwordmaxclasschars"
                                            name="create_passwordmaxclasschars"
                                            onChange={(str, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="A space-separated list of words that are not allowed to be contained in the new password (passwordBadWords)."
                                    className="ds-margin-top"
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Prohibited Words
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            type="text"
                                            id="create_passwordbadwords"
                                            aria-describedby="create_passwordbadwords"
                                            name="create_passwordbadwords"
                                            onChange={(str, e) => {
                                                this.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="A list of entry attributes to compare to the new password (passwordUserAttributes).">
                                    <GridItem className="ds-label" span={3}>
                                        Check User Attributes
                                    </GridItem>
                                    <GridItem span={9}>
                                        <Select
                                            variant={SelectVariant.typeaheadMulti}
                                            typeAheadAriaLabel="Type a attribute name to check"
                                            onToggle={this.props.onUserAttrsCreateToggle}
                                            onSelect={this.props.handleChange}
                                            onClear={this.props.onUserAttrsCreateClear}
                                            selections={this.props.passworduserattributes}
                                            isOpen={this.props.isUserAttrsCreateOpen}
                                            aria-labelledby="typeAhead-user-attr-create"
                                            placeholderText="Type attributes to check..."
                                            noResultsFoundText="There are no matching entries"
                                            isDisabled={!this.props.passwordchecksyntax}
                                        >
                                            {this.props.attrs.map((attr, index) => (
                                                <SelectOption
                                                    key={index}
                                                    value={attr}
                                                />
                                            ))}
                                        </Select>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Check the password against the system's CrackLib dictionary (passwordDictCheck).">
                                    <GridItem span={12}>
                                        <Checkbox
                                            id="create_passworddictcheck"
                                            isChecked={this.props.create_passworddictcheck}
                                            onChange={(checked, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                            label="Dictionary Check"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Reject a password if it is a palindrome (passwordPalindrome).">
                                    <GridItem span={12}>
                                        <Checkbox
                                            id="create_passwordpalindrome"
                                            isChecked={this.props.create_passwordpalindrome}
                                            onChange={(checked, e) => {
                                                this.props.handleChange(e);
                                            }}
                                            isDisabled={!this.props.passwordchecksyntax}
                                            label="Reject Palindromes"
                                        />
                                    </GridItem>
                                </Grid>
                            </div>
                        </div>
                    </ExpandableSection>
                    <ExpandableSection
                        className="ds-margin-top-lg"
                        toggleText={this.state.isTPRExpanded ? 'Hide Temporary Password Settings' : 'Show Temporary Password Settings'}
                        onToggle={this.onTPRToggle}
                        isExpanded={this.state.isTPRExpanded}
                    >
                        <div className="ds-margin-left">
                                {this.props.create_passwordmustchange == false && (
                                <FormAlert>
                                    <Alert
                                        variant="info"
                                        title='"User Must Change Password After Reset" must be enabled in General Settings to activate TPR.'
                                        aria-live="polite"
                                        isInline
                                    />
                                </FormAlert>
                                 )}
                            <Grid
                                title="Number of times the temporary password can be used to authenticate (passwordTPRMaxUse)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Password Max Use
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        type="number"
                                        id="create_passwordtprmaxuse"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="create_passwordtprmaxuse"
                                        isDisabled={!this.props.create_passwordmustchange}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Number of seconds before the temporary password expires (passwordTPRDelayExpireAt)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Password Expires In
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        type="number"
                                        id="create_passwordtprdelayexpireat"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="create_passwordtprdelayexpireat"
                                        isDisabled={!this.props.create_passwordmustchange}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                            <Grid
                                title="Number of seconds after which temporary password starts to be valid for authentication (passwordTPRDelayValidFrom)."
                                className="ds-margin-top"
                            >
                                <GridItem className="ds-label" span={3}>
                                    Password Valid From
                                </GridItem>
                                <GridItem span={9}>
                                    <TextInput
                                        type="number"
                                        id="create_passwordtprdelayvalidfrom"
                                        aria-describedby="horizontal-form-name-helper"
                                        name="create_passwordtprdelayvalidfrom"
                                        isDisabled={!this.props.create_passwordmustchange}
                                        onChange={(checked, e) => {
                                            this.props.handleChange(e);
                                        }}
                                    />
                                </GridItem>
                            </Grid>
                        </div>
                    </ExpandableSection>
                </Form>
                <Button
                    isDisabled={this.props.createDisabled}
                    variant="primary"
                    className="ds-margin-top-lg ds-margin-left"
                    onClick={this.props.createPolicy}
                >
                    Create New Policy
                </Button>
            </div>
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
            localActiveTabKey: 0,
            modalChecked: false,
            editPolicy: false,
            tableLoading: false,
            // Create policy
            policyType: "",
            policyDN: "",
            policyName: "",
            deleteName: "",
            createDisabled: true,
            createPolicyType: "Subtree Policy",
            // Lists of all the attributes for each tab/section.
            // We use the exact attribute name for the ID of
            // each field, so we can loop over them to efficently
            // check for changes, and updating/saving the config.
            rows: [],
            saveGeneralDisabled: true,
            saveUserDisabled: true,
            saveExpDisabled: true,
            saveLockoutDisabled: true,
            saveSyntaxDisabled: true,
            saveTPRDisabled: true,
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
            create_passwordstoragescheme: "",
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
            create_passwordtprdelayexpireat:  "-1",
            create_passwordtprdelayvalidfrom:  "-1",
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
            _create_passwordstoragescheme: "",
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
            _create_passwordtprdelayexpireat:  "-1",
            _create_passwordtprdelayvalidfrom:  "-1",
            _create_passwordadmindn: "",
            _create_passwordadminskipinfoupdate: false,
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

        // Check User Attributes Create
        this.onUserAttrsCreateToggle = isUserAttrsCreateOpen => {
            this.setState({
                isUserAttrsCreateOpen
            });
        };
        this.onUserAttrsCreateClear = () => {
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

        this.handleLocalNavSelect = (event, tabIndex) => {
            this.setState({
                localActiveTabKey: tabIndex
            });
        };

        this.createPolicy = this.createPolicy.bind(this);
        this.closeDeletePolicy = this.closeDeletePolicy.bind(this);
        this.deletePolicy = this.deletePolicy.bind(this);
        this.handleCreateChange = this.handleCreateChange.bind(this);
        this.handleCreateSelectChange = this.handleCreateSelectChange.bind(this);
        this.handleExpChange = this.handleExpChange.bind(this);
        this.handleGeneralChange = this.handleGeneralChange.bind(this);
        this.handleLockoutChange = this.handleLockoutChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.handleSyntaxChange = this.handleSyntaxChange.bind(this);
        this.handleTPRChange = this.handleTPRChange.bind(this);
        this.loadLocal = this.loadLocal.bind(this);
        this.loadPolicies = this.loadPolicies.bind(this);
        this.resetTab = this.resetTab.bind(this);
        this.saveExp = this.saveExp.bind(this);
        this.saveGeneral = this.saveGeneral.bind(this);
        this.saveLockout = this.saveLockout.bind(this);
        this.saveSyntax = this.saveSyntax.bind(this);
        this.saveTPR = this.saveTPR.bind(this);
        this.showDeletePolicy = this.showDeletePolicy.bind(this);
    }

    componentDidMount() {
        // Loading config
        if (!this.state.loaded) {
            this.loadPolicies();
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

    resetTab() {
        // Reset to the table tab
        this.setState({ localActiveTabKey: 0 });
    }

    handleModalChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;

        this.setState({
            [attr]: value,
        });
    }

    handleCreateSelectChange(value) {
        this.setState({
            createPolicyType: value
        });
    }

    handleCreateChange(e, selection) {
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
            if (all_attr == 'passworduserattributes' && attr == 'create_passworduserattributes') {
                const orig_val = this.state['_' + all_attr].join(' ');
                if (orig_val != value) {
                    value = selection; // restore value
                    disableSaveBtn = false;
                    break;
                }
                value = selection; // restore value
            } else if (attr == "create_" + all_attr && this.state['_create_' + all_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const all_attr of all_attrs) {
            if (all_attr == 'passworduserattributes' && attr != 'create_passworduserattributes') {
                // Typeahead attribute needs special care
                const orig_val = this.state['_' + all_attr].join(' ');
                const new_val = this.state[all_attr].join(' ');
                if (orig_val != new_val) {
                    disableSaveBtn = false;
                    break;
                }
            } else if (attr != "create_" + all_attr && this.state['_create_' + all_attr] != this.state["create_" + all_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        // Lastly check the target DN is valid
        if (attr == "policyDN") {
            if (valid_dn(value)) {
                disableSaveBtn = false;
            } else {
                if (value != "") {
                    invalid_dn = true;
                }
                disableSaveBtn = true;
            }
        } else {
            if (this.state.policyDN == "") {
                disableSaveBtn = true;
            } else {
                disableSaveBtn = false;
            }
        }

        // Select Typeahead
        if (selection) {
            if (this.state[attr].includes(selection)) {
                this.setState(
                    (prevState) => ({
                        [attr]: prevState[attr].filter((item) => item !== selection),
                        createDisabled: disableSaveBtn,
                        invalid_dn: invalid_dn,
                        isUserAttrsCreateOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        [attr]: [...prevState[attr], selection],
                        createDisabled: disableSaveBtn,
                        invalid_dn: invalid_dn,
                        isUserAttrsCreateOpen: false
                    }),
                );
            }
        } else { // Checkbox
            this.setState({
                [attr]: value,
                createDisabled: disableSaveBtn,
                invalid_dn: invalid_dn
            });
        }
    }

    createPolicy() {
        const all_attrs = general_attrs.concat(exp_attrs, lockout_attrs, syntax_attrs, tpr_attrs);
        let action = "adduser";

        this.setState({
            loading: true
        });

        if (this.state.createPolicyType == "Subtree Policy") {
            action = "addsubtree";
        }
        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'localpwp', action, this.state.policyDN
        ];

        for (const attr of all_attrs) {
            let old_val = this.state['_create_' + attr];
            let new_val = this.state['create_' + attr];
            if (new_val != old_val) {
                if (typeof new_val === "boolean") {
                    if (new_val) {
                        new_val = "on";
                    } else {
                        new_val = "off";
                    }
                } else if (attr == 'passworduserattributes') {
                    old_val = this.state._create_passworduserattributes.join(' ');
                    new_val = this.state.create_passworduserattributes.join(' ');
                    if (old_val == new_val) {
                        continue;
                    }
                }
                cmd.push(this.state.attrMap[attr] + "=" + new_val);
            }
        }

        log_cmd("createPolicy", "Create a local password policy", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.loadPolicies();
                    this.setState({
                        loading: false,
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully created new password policy"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.loadPolicies();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error creating password policy - ${errMsg.desc}`
                    );
                });
    }

    handleGeneralChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (const general_attr of general_attrs) {
            if (attr == general_attr && this.state['_' + general_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const general_attr of general_attrs) {
            if (attr != general_attr && this.state['_' + general_attr] != this.state[general_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [attr]: value,
            saveGeneralDisabled: disableSaveBtn,
        });
    }

    saveGeneral() {
        this.setState({
            loading: true
        });

        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'localpwp', 'set', this.state.policyName
        ];

        for (const attr of general_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                let val = this.state[attr];
                if (typeof val === "boolean") {
                    if (val) {
                        val = "on";
                    } else {
                        val = "off";
                    }
                }
                cmd.push(this.state.attrMap[attr] + "=" + val);
            }
        }

        log_cmd("saveGeneral", "Saving general pwpolicy settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully updated password policy configuration"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error updating password policy configuration - ${errMsg.desc}`
                    );
                });
    }

    handleExpChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (const exp_attr of exp_attrs) {
            if (attr == exp_attr && this.state['_' + exp_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const exp_attr of exp_attrs) {
            if (attr != exp_attr && this.state['_' + exp_attr] != this.state[exp_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [attr]: value,
            saveExpDisabled: disableSaveBtn,
        });
    }

    saveExp() {
        this.setState({
            loading: true
        });

        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'localpwp', 'set', this.state.policyName
        ];

        for (const attr of exp_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                let val = this.state[attr];
                if (typeof val === "boolean") {
                    if (val) {
                        val = "on";
                    } else {
                        val = "off";
                    }
                }
                cmd.push(this.state.attrMap[attr] + "=" + val);
            }
        }

        log_cmd("saveExp", "Saving Expiration pwpolicy settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully updated password policy configuration"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error updating password policy configuration - ${errMsg.desc}`
                    );
                });
    }

    handleLockoutChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (const lockout_attr of lockout_attrs) {
            if (attr == lockout_attr && this.state['_' + lockout_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const lockout_attr of lockout_attrs) {
            if (attr != lockout_attr && this.state['_' + lockout_attr] != this.state[lockout_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [attr]: value,
            saveLockoutDisabled: disableSaveBtn,
        });
    }

    saveLockout() {
        this.setState({
            loading: true
        });

        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'localpwp', 'set', this.state.policyName
        ];

        for (const attr of lockout_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                let val = this.state[attr];
                if (typeof val === "boolean") {
                    if (val) {
                        val = "on";
                    } else {
                        val = "off";
                    }
                }
                cmd.push(this.state.attrMap[attr] + "=" + val);
            }
        }

        log_cmd("saveLockout", "Saving lockout pwpolicy settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully updated password policy configuration"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error updating password policy configuration - ${errMsg.desc}`
                    );
                });
    }

    handleSyntaxChange(e, selection) {
        let attr;
        let value;
        if (selection) {
            attr = "passworduserattributes";
            value = selection;
        } else {
            value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
            attr = e.target.id;
        }
        let disableSaveBtn = true;
        // Check if a setting was changed, if so enable the save button
        for (const syntax_attr of syntax_attrs) {
            if (syntax_attr == 'passworduserattributes' && attr == 'passworduserattributes') {
                const orig_val = this.state['_' + syntax_attr].join(' ');
                if (orig_val != value) {
                    value = selection; // restore value
                    disableSaveBtn = false;
                    break;
                }
                value = selection; // restore value
            } else if (attr == syntax_attr && this.state['_' + syntax_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const syntax_attr of syntax_attrs) {
            if (syntax_attr == 'passworduserattributes' && attr != 'passworduserattributes') {
                // Typeahead attribute needs special care
                const orig_val = this.state['_' + syntax_attr].join(' ');
                const new_val = this.state[syntax_attr].join(' ');
                if (orig_val != new_val) {
                    disableSaveBtn = false;
                    break;
                }
            } else if (attr != syntax_attr && this.state['_' + syntax_attr] != this.state[syntax_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        if (selection) {
            if (this.state[attr].includes(selection)) {
                this.setState(
                    (prevState) => ({
                        [attr]: prevState[attr].filter((item) => item !== selection),
                        isSelectOpen: false,
                        isUserAttrsEditOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        [attr]: [...prevState[attr], selection],
                        saveSyntaxDisabled: disableSaveBtn,
                        isUserAttrsEditOpen: false
                    }),
                );
            }
        } else {
            this.setState({
                [attr]: value,
                saveSyntaxDisabled: disableSaveBtn,
                isUserAttrsEditOpen: false
            });
        }
    }

    saveSyntax() {
        this.setState({
            loading: true
        });

        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'localpwp', 'set', this.state.policyName
        ];

        for (const attr of syntax_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                let val = this.state[attr];
                if (typeof val === "boolean") {
                    if (val) {
                        val = "on";
                    } else {
                        val = "off";
                    }
                }
                cmd.push(this.state.attrMap[attr] + "=" + val);
            }
        }

        log_cmd("saveSyntax", "Saving syntax checking pwpolicy settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully updated password policy configuration"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error updating password policy configuration - ${errMsg.desc}`
                    );
                });
    }

    handleTPRChange(e) {
        const value = e.target.value;
        const attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (const tpr_attr of tpr_attrs) {
            if (attr == tpr_attr && this.state['_' + tpr_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (const tpr_attr of tpr_attrs) {
            if (attr != tpr_attr && this.state['_' + tpr_attr] != this.state[tpr_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [attr]: value,
            saveTPRDisabled: disableSaveBtn,
        });
    }

    saveTPR() {
        this.setState({
            saving: true
        });

        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'localpwp', 'set', this.state.policyName
        ];

        for (const attr of tpr_attrs) {
            if (this.state['_' + attr] != this.state[attr]) {
                let val = this.state[attr];
                cmd.push(this.state.attrMap[attr] + "=" + val);
            }
        }

        log_cmd("saveTPR", "Saving TPR pwpolicy settings", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        saving: false
                    });
                    this.props.addNotification(
                        "success",
                        "Successfully updated password policy configuration"
                    );
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.loadLocal(this.state.policyName);
                    this.setState({
                        saving: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error updating password policy configuration - ${errMsg.desc}`
                    );
                });
    }

    deletePolicy() {
        this.setState({
            loading: true,
            editPolicy: false,
        });

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "localpwp", "remove", this.state.deleteName
        ];
        log_cmd("deletePolicy", "delete policy", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.loadPolicies();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.loadPolicies();
                    this.props.addNotification(
                        "error",
                        `Error deleting local password policy - ${errMsg.desc}`
                    );
                });
    }

    loadPolicies() {
        this.setState({
            loading: true,
        });
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "localpwp", "list"
        ];
        log_cmd("loadPolicies", "Load all the local password policies for the table", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const policy_obj = JSON.parse(content);
                    const pwpRows = [];
                    for (const row of policy_obj.items) {
                        pwpRows.push([row.targetdn, row.pwp_type, row.basedn]);
                    }
                    this.setState({
                        localActiveTabKey: 0,
                        activeKey: 0,
                        rows: pwpRows,
                        loaded: true,
                        loading: false,
                        editPolicy: false,
                        policyDN: "",
                        createPolicyType: "Subtree Policy",
                        policyName: "",
                        deleteName: "",
                        showDeletePolicy: false,
                        // Reset edit and create tab
                        saveGeneralDisabled: true,
                        saveUserDisabled: true,
                        saveExpDisabled: true,
                        saveLockoutDisabled: true,
                        saveSyntaxDisabled: true,
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
                        create_passwordstoragescheme: "",
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
                        _create_passwordstoragescheme: "",
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
                        _create_passwordadmindn: "",
                        _create_passwordadminskipinfoupdate: false,
                    }, () => {
                        const gcmd = [
                            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                            "config", "get", "passwordstoragescheme"
                        ];
                        log_cmd("loadPolicies", "Load global password policy password scheme", gcmd);
                        cockpit
                                .spawn(gcmd, { superuser: true, err: "message" })
                                .done(content => {
                                    const config = JSON.parse(content);
                                    const attrs = config.attrs;
                                    const defaultStorageScheme = attrs['passwordstoragescheme'][0];
                                    this.setState({
                                        create_passwordstoragescheme: defaultStorageScheme,
                                        _create_passwordstoragescheme: defaultStorageScheme,
                                    }, this.props.enableTree);
                                })
                                .fail(err => {
                                    const errMsg = JSON.parse(err);
                                    this.setState({
                                        loaded: true,
                                        loading: false,
                                    });
                                    this.props.addNotification(
                                        "error",
                                        `Error loading global password storage scheme - ${errMsg.desc}`
                                    );
                                });
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.setState({
                        loaded: true,
                        loading: false,
                    }, this.props.enableTree);
                    console.log(
                        `Error loading local password policies - ${errMsg.desc}`
                    );
                });
    }

    loadLocal(name) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "localpwp", "get", name
        ];
        log_cmd("loadLocal", "Load a local password policy", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
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
                    if ('passwordchange' in attrs && attrs.passwordchange[0] == "on") {
                        pwChange = true;
                    }
                    if ('passwordmustchange' in attrs && attrs.passwordmustchange[0] == "on") {
                        pwMustChange = true;
                    }
                    if ('passwordhistory' in attrs && attrs.passwordhistory[0] == "on") {
                        pwHistory = true;
                    }
                    if ('passwordtrackupdatetime' in attrs && attrs.passwordtrackupdatetime[0] == "on") {
                        pwTrackUpdate = true;
                    }
                    if ('passwordsendexpiringtime' in attrs && attrs.passwordsendexpiringtime[0] == "on") {
                        pwSendExpire = true;
                    }
                    if ('passwordlockout' in attrs && attrs.passwordlockout[0] == "on") {
                        pwLockout = true;
                    }
                    if ('passwordunlock' in attrs && attrs.passwordunlock[0] == "on") {
                        pwUnlock = true;
                    }
                    if ('passwordexp' in attrs && attrs.passwordexp[0] == "on") {
                        pwExpire = true;
                    }
                    if ('passwordchecksyntax' in attrs && attrs.passwordchecksyntax[0] == "on") {
                        pwCheckSyntax = true;
                    }
                    if ('passwordpalindrome' in attrs && attrs.passwordpalindrome[0] == "on") {
                        pwPalindrome = true;
                    }
                    if ('passworddictcheck' in attrs && attrs.passworddictcheck[0] == "on") {
                        pwDictCheck = true;
                    }
                    if ('passwordbadwords' in attrs && attrs.passwordbadwords[0] != "") {
                        // Hack until this is fixed: https://github.com/389ds/389-ds-base/issues/3928
                        if (attrs.passwordbadwords.length > 1) {
                            pwBadWords = attrs.passwordbadwords.join(' ');
                        } else {
                            pwBadWords = attrs.passwordbadwords[0];
                        }
                    }
                    if ('passworduserattributes' in attrs && attrs.passworduserattributes[0] != "") {
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
                    if ('passwordadminskipinfoupdate' in attrs && attrs.passwordadminskipinfoupdate[0] == "on") {
                        pwAdminSkipUpdates = true;
                    }

                    this.setState({
                        editPolicy: true,
                        loading: false,
                        localActiveTabKey: 1,
                        activeKey: 0,
                        policyName: name,
                        policyType: config.pwp_type,
                        saveGeneralDisabled: true,
                        saveUserDisabled: true,
                        saveExpDisabled: true,
                        saveLockoutDisabled: true,
                        saveSyntaxDisabled: true,
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
                    const errMsg = JSON.parse(err);
                    this.setState({
                        loaded: true,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error loading local password policy - ${errMsg.desc}`
                    );
                });
    }

    render() {
        let edit_tab = "";
        let pwExpirationRows = "";
        let pwLockoutRows = "";
        let pwSyntaxRows = "";

        let saveBtnName = "Save";
        const extraPrimaryProps = {};
        if (this.state.saving) {
            saveBtnName = "Saving ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        if (this.state.passwordchecksyntax) {
            pwSyntaxRows =
                <div className="ds-margin-left">
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            Minimum Length
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="The minimum number of characters in the password (passwordMinLength)."
                                value={this.state.passwordminlength}
                                type="number"
                                id="passwordminlength"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordminlength"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                        <GridItem className="ds-label" offset={6} span={3}>
                            Minimum Alpha's
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="Reject passwords with fewer than this many alpha characters (passwordMinAlphas)."
                                value={this.state.passwordminalphas}
                                type="number"
                                id="passwordminalphas"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordminalphas"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            Minimum Digits
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="Reject passwords with fewer than this many digit characters (0-9) (passwordMinDigits)."
                                value={this.state.passwordmindigits}
                                type="number"
                                id="passwordmindigits"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordmindigits"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                        <GridItem className="ds-label" offset={6} span={3}>
                            Minimum Special
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="Reject passwords with fewer than this many special non-alphanumeric characters (passwordMinSpecials)."
                                value={this.state.passwordminspecials}
                                type="number"
                                id="passwordminspecials"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordminspecials"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            Minimum Uppercase
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="Reject passwords with fewer than this many uppercase characters (passwordMinUppers)."
                                value={this.state.passwordminuppers}
                                type="number"
                                id="passwordminuppers"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordminuppers"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                        <GridItem className="ds-label" offset={6} span={3}>
                            Minimum Lowercase
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="Reject passwords with fewer than this many lowercase characters (passwordMinLowers)."
                                value={this.state.passwordminlowers}
                                type="number"
                                id="passwordminlowers"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordminlowers"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            Minimum 8-bit
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="Reject passwords with fewer than this many 8-bit or multi-byte characters (passwordMin8Bit)."
                                value={this.state.passwordmin8bit}
                                type="number"
                                id="passwordmin8bit"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordmin8bit"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                        <GridItem className="ds-label" offset={6} span={3}>
                            Minimum Categories
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="The minimum number of character categories that a password must contain (categories are upper, lower, digit, special, and 8-bit) (passwordMinCategories)."
                                value={this.state.passwordmincategories}
                                type="number"
                                id="passwordmincategories"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordmincategories"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            Maximum Sequences
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="The maximum number of allowed monotonic characters sequences (passwordMaxSequence)."
                                value={this.state.passwordmaxsequence}
                                type="number"
                                id="passwordmaxsequence"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordmaxsequence"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                        <GridItem className="ds-label" offset={6} span={3}>
                            Max Sequence Sets
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="The maximum number of allowed monotonic characters sequences that can appear more than once (passwordMaxSeqSets)."
                                value={this.state.passwordmaxseqsets}
                                type="number"
                                id="passwordmaxseqsets"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordmaxseqsets"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            Max Seq Per Class
                        </GridItem>
                        <GridItem span={1}>
                            <TextInput
                                title="The maximum number of consecutive characters from the same character class/category (passwordMaxClassChars)."
                                value={this.state.passwordmaxclasschars}
                                type="number"
                                id="passwordmaxclasschars"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordmaxclasschars"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem className="ds-label" span={3}>
                            Prohibited Words
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                title="A space-separated list of words that are not allowed to be contained in the new password (passwordBadWords)."
                                value={this.state.passwordbadwords}
                                type="text"
                                id="passwordbadwords"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordbadwords"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="A list of entry attributes to compare to the new password (passwordUserAttributes).">
                        <GridItem className="ds-label" span={3}>
                            Check User Attributes
                        </GridItem>
                        <GridItem span={9}>
                            <Select
                                variant={SelectVariant.typeaheadMulti}
                                typeAheadAriaLabel="Type an attribute to check"
                                onToggle={this.onSelectToggle}
                                onSelect={this.handleSyntaxChange}
                                onClear={this.onSelectClear}
                                selections={this.state.passworduserattributes}
                                isOpen={this.state.isSelectOpen}
                                aria-labelledby="typeAhead-user-attr"
                                placeholderText="Type attributes to check..."
                                noResultsFoundText="There are no matching entries"
                            >
                                {this.props.attrs.map((attr, index) => (
                                    <SelectOption
                                        key={index}
                                        value={attr}
                                    />
                                ))}
                            </Select>
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top-lg" title="Check the password against the system's CrackLib dictionary (passwordDictCheck).">
                        <GridItem span={12}>
                            <Checkbox
                                id="passworddictcheck"
                                isChecked={this.state.passworddictcheck}
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                                label="Dictionary Check"
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="Check if the password is a palindrome (passwordPalindrome).">
                        <GridItem span={12}>
                            <Checkbox
                                id="passwordpalindrome"
                                isChecked={this.state.passwordpalindrome}
                                className="ds-label"
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                                label="Reject Palindromes"
                            />
                        </GridItem>
                    </Grid>
                </div>;
        }

        if (this.state.passwordlockout) {
            pwLockoutRows =
                <div className="ds-margin-left">
                    <Grid className="ds-margin-top" title="The maximum number of failed logins before account gets locked (passwordMaxFailure).">
                        <GridItem className="ds-label" span={5}>
                            Number of Failed Logins That Locks out Account
                        </GridItem>
                        <GridItem span={2}>
                            <TextInput
                                value={this.state.passwordmaxfailure}
                                type="number"
                                id="passwordmaxfailure"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordmaxpasswordmaxfailureclasschars"
                                onChange={(checked, e) => {
                                    this.handleLockoutChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="The number of seconds until an accounts failure count is reset (passwordResetFailureCount).">
                        <GridItem className="ds-label" span={5}>
                            Time Until <i>Failure Count</i> Resets
                        </GridItem>
                        <GridItem span={2}>
                            <TextInput
                                value={this.state.passwordresetfailurecount}
                                type="number"
                                id="passwordresetfailurecount"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordresetfailurecount"
                                onChange={(checked, e) => {
                                    this.handleLockoutChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="The number of seconds, duration, before the account gets unlocked (passwordLockoutDuration).">
                        <GridItem className="ds-label" span={5}>
                            Time Until Account Unlocked
                        </GridItem>
                        <GridItem span={2}>
                            <TextInput
                                value={this.state.passwordlockoutduration}
                                type="number"
                                id="passwordlockoutduration"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordlockoutduration"
                                onChange={(checked, e) => {
                                    this.handleLockoutChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="Do not lockout the user account forever, instead the account will unlock based on the lockout duration (passwordUnlock).">
                        <GridItem className="ds-label" span={5}>
                            <Checkbox
                                id="passwordunlock"
                                isChecked={this.state.passwordunlock}
                                onChange={(checked, e) => {
                                    this.handleLockoutChange(e);
                                }}
                                label="Do Not Lockout Account Forever"
                            />
                        </GridItem>
                    </Grid>
                </div>;
        }

        if (this.state.passwordexp) {
            pwExpirationRows =
                <div className="ds-margin-left">
                    <Grid className="ds-margin-top" title="The maxiumum age of a password in seconds before it expires (passwordMaxAge).">
                        <GridItem className="ds-label" span={5}>
                            Password Expiration Time
                        </GridItem>
                        <GridItem span={2}>
                            <TextInput
                                value={this.state.passwordmaxage}
                                type="number"
                                id="passwordmaxage"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordmaxage"
                                onChange={(checked, e) => {
                                    this.handleExpChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="The number of logins that are allowed after the password has expired (passwordGraceLimit).">
                        <GridItem className="ds-label" span={5}>
                            Allowed Logins After Password Expires
                        </GridItem>
                        <GridItem span={2}>
                            <TextInput
                                value={this.state.passwordgracelimit}
                                type="number"
                                id="passwordgracelimit"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordgracelimit"
                                onChange={(checked, e) => {
                                    this.handleExpChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="Set the time (in seconds), before a password is about to expire, to send a warning. (passwordWarning).">
                        <GridItem className="ds-label" span={5}>
                            Send Password Expiring Warning
                        </GridItem>
                        <GridItem span={2}>
                            <TextInput
                                value={this.state.passwordwarning}
                                type="number"
                                id="passwordwarning"
                                aria-describedby="horizontal-form-name-helper"
                                name="passwordwarning"
                                onChange={(checked, e) => {
                                    this.handleExpChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top" title="Always return a password expiring control when requested (passwordSendExpiringTime).">
                        <GridItem className="ds-label" span={5}>
                            <Checkbox
                                id="passwordsendexpiringtime"
                                isChecked={this.state.passwordsendexpiringtime}
                                onChange={(checked, e) => {
                                    this.handleExpChange(e);
                                }}
                                label={<>Always Send <i>Password Expiring</i> Control</>}
                            />
                        </GridItem>
                    </Grid>
                </div>;
        }

        if (!this.state.editPolicy) {
            edit_tab =
                <div className="ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text className="ds-center" component={TextVariants.h3}>
                            Please choose a policy from the <a onClick={this.resetTab}>Local Policy Table</a>.
                        </Text>
                    </TextContent>
                </div>;
        } else {
            edit_tab =
                <div className={this.state.loading ? 'ds-fadeout ds-left-margin' : 'ds-fadein ds-left-margin'}>
                    <TextContent>
                        <Text className="ds-margin-top-xlg" component={TextVariants.h4}>
                            <b>{this.state.policyName}</b> <font size="2">({this.state.policyType})</font>
                        </Text>
                    </TextContent>
                    <Tabs className="ds-margin-top-lg" activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                        <Tab eventKey={0} title={<TabTitleText>General Settings</TabTitleText>}>
                            <Form className="ds-margin-left-sm ds-margin-top-lg" isHorizontal autoComplete="off">
                                <Grid title="Record a separate timestamp specifically for the last time that the password for an entry was changed. If this is enabled, then it adds the pwdUpdateTime operational attribute to the user account entry (passwordTrackUpdateTime).">
                                    <GridItem span={12}>
                                        <Checkbox
                                            id="passwordtrackupdatetime"
                                            isChecked={this.state.passwordtrackupdatetime}
                                            onChange={(checked, e) => {
                                                this.handleGeneralChange(e);
                                            }}
                                            label="Track Password Update Time"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title="Allow user's to change their passwords (passwordChange).">
                                    <GridItem span={12}>
                                        <Checkbox
                                            id="passwordchange"
                                            isChecked={this.state.passwordchange}
                                            onChange={(checked, e) => {
                                                this.handleGeneralChange(e);
                                            }}
                                            label="Allow Users To Change Their Passwords"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title="User must change its password after its been reset by an administrator (passwordMustChange).">
                                    <GridItem span={12}>
                                        <Checkbox
                                            id="passwordmustchange"
                                            isChecked={this.state.passwordmustchange}
                                            onChange={(checked, e) => {
                                                this.handleGeneralChange(e);
                                            }}
                                            label="User Must Change Password After Reset"
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid title="Maintain a password history for each user (passwordHistory).">
                                    <GridItem span={12}>
                                        <div className="ds-inline">
                                            <Checkbox
                                                id="passwordhistory"
                                                isChecked={this.state.passwordhistory}
                                                onChange={(checked, e) => {
                                                    this.handleGeneralChange(e);
                                                }}
                                                label="Keep Password History"
                                            />
                                        </div>
                                        <div className="ds-inline ds-left-margin ds-raise-field-md ds-width-sm">
                                            <TextInput
                                                value={this.state.passwordinhistory}
                                                type="number"
                                                id="passwordinhistory"
                                                aria-describedby="horizontal-form-name-helper"
                                                name="passwordinhistory"
                                                onChange={(checked, e) => {
                                                    this.handleGeneralChange(e);
                                                }}
                                            />
                                        </div>
                                    </GridItem>
                                </Grid>
                                <Grid className="ds-margin-top" title="Set the password storage scheme (passwordstoragescheme).">
                                    <GridItem span={3} className="ds-label">
                                        Password Storage Scheme
                                    </GridItem>
                                    <GridItem span={9}>
                                        <FormSelect
                                            id="passwordstoragescheme"
                                            value={this.state.passwordstoragescheme}
                                            onChange={(value, event) => {
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
                                    title="Indicates the number of seconds that must pass before a user can change their password again. (passwordMinAge)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Password Minimum Age
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state.passwordminage}
                                            type="number"
                                            id="passwordminage"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="passwordminage"
                                            onChange={(checked, e) => {
                                                this.handleGeneralChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="The DN for a password administrator or administrator group (passwordAdminDN)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Password Administrator
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state.passwordadmindn}
                                            type="text"
                                            id="passwordadmindn"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="passwordadmindn"
                                            onChange={(checked, e) => {
                                                this.handleGeneralChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                                <Grid
                                    title="Disable updating password state attributes like passwordExpirationtime, passwordHistory, etc, when setting a user's password as a Password Administrator (passwordAdminSkipInfoUpdate)."
                                >
                                    <GridItem offset={3} span={9}>
                                        <Checkbox
                                            id="passwordadminskipinfoupdate"
                                            isChecked={this.state.passwordadminskipinfoupdate}
                                            onChange={(checked, e) => {
                                                this.handleGeneralChange(e);
                                            }}
                                            label="Do not update target entry's password state attributes"
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                            <Button
                                isDisabled={this.state.saveGeneralDisabled || this.state.saving}
                                variant="primary"
                                className="ds-margin-top-xlg ds-margin-left-sm ds-margin-bottom-md"
                                onClick={this.saveGeneral}
                                isLoading={this.state.saving}
                                spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                                {...extraPrimaryProps}
                            >
                                {saveBtnName}
                            </Button>
                        </Tab>
                        <Tab eventKey={1} title={<TabTitleText>Expiration</TabTitleText>}>
                            <Form className="ds-margin-top-xlg ds-margin-left" isHorizontal autoComplete="off">
                                <Grid title="Enable a password expiration policy (passwordExp).">
                                    <GridItem span={12}>
                                        <Checkbox
                                            id="passwordexp"
                                            isChecked={this.state.passwordexp}
                                            onChange={(checked, e) => {
                                                this.handleExpChange(e);
                                            }}
                                            label="Enforce Password Expiration"
                                        />
                                    </GridItem>
                                </Grid>
                                {pwExpirationRows}
                            </Form>
                            <Button
                                isDisabled={this.state.saveExpDisabled || this.state.saving}
                                variant="primary"
                                className="ds-margin-top-lg ds-margin-left"
                                onClick={this.saveExp}
                                isLoading={this.state.saving}
                                spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                                {...extraPrimaryProps}
                            >
                                {saveBtnName}
                            </Button>
                        </Tab>
                        <Tab eventKey={2} title={<TabTitleText>Account Lockout</TabTitleText>}>
                            <Form className="ds-margin-top-xlg ds-margin-left" isHorizontal autoComplete="off">
                                <Grid title="Enable account lockout (passwordLockout).">
                                    <GridItem span={12}>
                                        <Checkbox
                                            id="passwordlockout"
                                            isChecked={this.state.passwordlockout}
                                            onChange={(checked, e) => {
                                                this.handleLockoutChange(e);
                                            }}
                                            label="Enable Account Lockout"
                                        />
                                    </GridItem>
                                </Grid>
                                {pwLockoutRows}
                            </Form>
                            <Button
                                isDisabled={this.state.saveLockoutDisabled || this.state.saving}
                                variant="primary"
                                className="ds-margin-top-lg ds-margin-left"
                                onClick={this.saveLockout}
                                isLoading={this.state.saving}
                                spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                                {...extraPrimaryProps}
                            >
                                {saveBtnName}
                            </Button>
                        </Tab>
                        <Tab eventKey={3} title={<TabTitleText>Syntax Checking</TabTitleText>}>
                            <Form className="ds-margin-top-xlg ds-margin-left" isHorizontal autoComplete="off">
                                <Grid title="Enable password syntax checking (passwordCheckSyntax).">
                                    <GridItem span={12}>
                                        <Checkbox
                                            id="passwordchecksyntax"
                                            isChecked={this.state.passwordchecksyntax}
                                            onChange={(checked, e) => {
                                                this.handleSyntaxChange(e);
                                            }}
                                            label="Enable Password Syntax Checking"
                                        />
                                    </GridItem>
                                </Grid>
                                {pwSyntaxRows}
                            </Form>
                            <Button
                                isDisabled={this.state.saveSyntaxDisabled || this.state.saving}
                                variant="primary"
                                className="ds-margin-top-xlg ds-margin-left ds-margin-bottom-md"
                                onClick={this.saveSyntax}
                                isLoading={this.state.saving}
                                spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                                {...extraPrimaryProps}
                            >
                                {saveBtnName}
                            </Button>
                        </Tab>
                        <Tab eventKey={4} title={<TabTitleText>Temporary Password Rules</TabTitleText>}>
                            <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                                {this.state.passwordmustchange == false && (
                                <FormAlert className="ds-margin-top">
                                    <Alert
                                        variant="info"
                                        title='"User Must Change Password After Reset" must be enabled in General Settings to activate TPR.'
                                        aria-live="polite"
                                        isInline
                                    />
                                </FormAlert>
                                 )}
                                <Grid
                                    title="Number of times the temporary password can be used to authenticate (passwordTPRMaxUse)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Password Max Use
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state.passwordtprmaxuse}
                                            type="number"
                                            id="passwordtprmaxuse"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="passwordtprmaxuse"
                                            isDisabled={!this.state.passwordmustchange}
                                            onChange={(checked, e) => {
                                                this.handleTPRChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                            <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                                <Grid
                                    title="Number of seconds before the temporary password expires (passwordTPRDelayExpireAt)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Password Expires In
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state.passwordtprdelayexpireat}
                                            type="number"
                                            id="passwordtprdelayexpireat"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="passwordtprdelayexpireat"
                                            isDisabled={!this.state.passwordmustchange}
                                            onChange={(checked, e) => {
                                                this.handleTPRChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                            <Form className="ds-margin-top ds-margin-left" isHorizontal autoComplete="off">
                                <Grid
                                    title="Number of seconds after which temporary password starts to be valid for authentication (passwordTPRDelayValidFrom)."
                                >
                                    <GridItem className="ds-label" span={3}>
                                        Password Valid From
                                    </GridItem>
                                    <GridItem span={9}>
                                        <TextInput
                                            value={this.state.passwordtprdelayvalidfrom}
                                            type="number"
                                            id="passwordtprdelayvalidfrom"
                                            aria-describedby="horizontal-form-name-helper"
                                            name="passwordtprdelayvalidfrom"
                                            isDisabled={!this.state.passwordmustchange}
                                            onChange={(checked, e) => {
                                                this.handleTPRChange(e);
                                            }}
                                        />
                                    </GridItem>
                                </Grid>
                            </Form>
                            <Button
                                isDisabled={this.state.saveTPRDisabled || this.state.saving}
                                variant="primary"
                                className="ds-margin-top-xlg ds-margin-left ds-margin-bottom-md"
                                onClick={this.saveTPR}
                                isLoading={this.state.saving}
                                spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                                {...extraPrimaryProps}
                            >
                                {saveBtnName}
                            </Button>
                        </Tab>
                    </Tabs>
                </div>;
        }

        let body =
            <div className="ds-margin-top-lg">
                <Tabs activeKey={this.state.localActiveTabKey} onSelect={this.handleLocalNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Local Policy Table</TabTitleText>}>
                        <div className="ds-margin-top-xlg">
                            <PwpTable
                                key={this.state.rows}
                                rows={this.state.rows}
                                editPolicy={this.loadLocal}
                                deletePolicy={this.showDeletePolicy}
                            />
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>Edit Policy</TabTitleText>}>
                        {edit_tab}
                    </Tab>
                    <Tab eventKey={2} title={<TabTitleText>Create A Policy</TabTitleText>}>
                        <CreatePolicy
                            handleChange={this.handleCreateChange}
                            handleSelectChange={this.handleCreateSelectChange}
                            attrs={this.props.attrs}
                            passwordexp={this.state.create_passwordexp}
                            passwordchecksyntax={this.state.create_passwordchecksyntax}
                            passwordlockout={this.state.create_passwordlockout}
                            createDisabled={this.state.createDisabled}
                            passworduserattributes={this.state.create_passworduserattributes}
                            createPolicy={this.createPolicy}
                            invalid_dn={this.state.invalid_dn}
                            key={this.state.rows}
                            policyDN={this.state.policyDN}
                            createPolicyType={this.state.createPolicyType}
                            create_passwordtrackupdatetime={this.state.create_passwordtrackupdatetime}
                            create_passwordchange={this.state.create_passwordchange}
                            create_passwordmustchange={this.state.create_passwordmustchange}
                            create_passwordhistory={this.state.create_passwordhistory}
                            create_passwordexp={this.state.create_passwordexp}
                            create_passwordsendexpiringtime={this.state.create_passwordsendexpiringtime}
                            create_passwordlockout={this.state.create_passwordlockout}
                            create_passwordunlock={this.state.create_passwordunlock}
                            create_passwordchecksyntax={this.state.create_passwordchecksyntax}
                            create_passworddictcheck={this.state.create_passworddictcheck}
                            create_passwordpalindrome={this.state.create_passwordpalindrome}
                            create_passwordstoragescheme={this.state.create_passwordstoragescheme}
                            create_passwordadmindn={this.state.create_passwordadmindn}
                            create_passwordadminskipinfoupdate={this.state.create_passwordadminskipinfoupdate}
                            onUserAttrsCreateToggle={this.onUserAttrsCreateToggle}
                            onUserAttrsCreateClear={this.onUserAttrsCreateClear}
                            isUserAttrsCreateOpen={this.state.isUserAttrsCreateOpen}
                            pwdStorageSchemes={this.props.pwdStorageSchemes}
                        />
                    </Tab>
                </Tabs>
            </div>;

        if (this.state.loading || !this.state.loaded) {
            body =
                <div className="ds-margin-top-xlg ds-center">
                    <Spinner isSVG size="xl" />
                </div>;
        }

        return (
            <div className={this.state.saving ? "ds-disabled" : ""}>
                <Grid>
                    <GridItem span={12}>
                        <TextContent>
                            <Text component={TextVariants.h3}>
                                Local Password Policies <FontAwesomeIcon
                                    size="lg"
                                    className="ds-left-margin ds-refresh"
                                    icon={faSyncAlt}
                                    title="Refresh the local password policies"
                                    onClick={this.loadPolicies}
                                />
                            </Text>
                        </TextContent>
                    </GridItem>
                </Grid>
                {body}
                <DoubleConfirmModal
                    showModal={this.state.showDeletePolicy}
                    closeHandler={this.closeDeletePolicy}
                    handleChange={this.handleModalChange}
                    actionHandler={this.deletePolicy}
                    item={this.state.deleteName}
                    checked={this.state.modalChecked}
                    spinning={this.state.tableLoading}
                    mTitle="Delete Local Password Policy"
                    mMsg="Are you sure you want to delete this local password policy?"
                    mSpinningMsg="Deleting local password policy ..."
                    mBtnName="Delete Policy"
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
