import cockpit from "cockpit";
import React from "react";
import { log_cmd, valid_dn } from "../tools.jsx";
import CustomCollapse from "../customCollapse.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
import { PwpTable } from "./databaseTables.jsx";
import {
    Button,
    Checkbox,
    Col,
    ControlLabel,
    Form,
    FormControl,
    Icon,
    Nav,
    NavItem,
    Row,
    TabContainer,
    TabContent,
    TabPane,
    Spinner,
} from "patternfly-react";
import { Typeahead } from "react-bootstrap-typeahead";
import PropTypes from "prop-types";

const general_attrs = [
    "passwordstoragescheme",
    "passwordtrackupdatetime",
    "passwordchange",
    "passwordmustchange",
    "passwordhistory",
    "passwordinhistory",
    "passwordminage",
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

class CreatePolicy extends React.Component {
    render() {
        return (
            <div>
                <Form className="ds-margin-left" horizontal>
                    <Row>
                        <h4>Create A New Local Password Policy</h4>
                    </Row>
                    <Row className="ds-margin-top-lg">
                        <Col componentClass={ControlLabel} sm={3}>
                            Password Policy Type
                        </Col>
                        <Col sm={8}>
                            <select
                              className="btn btn-default dropdown ds-margin-left-sm" id="createPolicyType"
                              onChange={this.props.handleChange}>
                                <option>Subtree Policy</option>
                                <option>User Policy</option>
                            </select>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top ds-config-header" title="The DN of the entry to apply this password policy to.">
                        <Col componentClass={ControlLabel} sm={3}>
                            Target DN
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="policyDN"
                                type="text"
                                onChange={this.props.handleChange}
                                className={this.props.invalid_dn ? "ds-margin-left-sm ds-input-auto ds-input-bad" : "ds-margin-left-sm ds-input-auto"}
                            />
                        </Col>
                    </Row>

                    <CustomCollapse textClosed="Show General Settings" textOpened="Hide General Settings" className="ds-margin-right ds-margin-top-lg">
                        <div className="ds-margin-left">
                            <Row title="Set the password storage scheme (passwordstoragescheme)." className="ds-margin-top">
                                <Col sm={3}>
                                    <ControlLabel>
                                        Password Storage Scheme
                                    </ControlLabel>
                                </Col>
                                <Col sm={2}>
                                    <select
                                      className="btn btn-default dropdown" id="create_passwordstoragescheme"
                                      onChange={this.props.handleChange}>
                                        <option>PBKDF2_SHA256</option>
                                        <option>SSHA512</option>
                                        <option>SSHA384</option>
                                        <option>SSHA256</option>
                                        <option>SSHA</option>
                                        <option>MD5</option>
                                        <option>SMD5</option>
                                        <option>CRYPT-MD5</option>
                                        <option>CRYPT-SHA512</option>
                                        <option>CRYPT-SHA256</option>
                                        <option>CRYPT</option>
                                        <option>GOST_YESCRYPT</option>
                                        <option>CLEAR</option>
                                    </select>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Indicates the number of seconds that must pass before a user can change their password again. (passwordMinAge)">
                                <Col sm={3}>
                                    <ControlLabel>
                                        Password Minimum Age
                                    </ControlLabel>
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordminage"
                                        type="number"
                                        min="0"
                                        max="2147483647"
                                        onChange={this.props.handleChange}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Record a separate timestamp specifically for the last time that the password for an entry was changed. If this is enabled, then it adds the pwdUpdateTime operational attribute to the user account entry (passwordTrackUpdateTime).">
                                <Col sm={11}>
                                    <Checkbox
                                        id="create_passwordtrackupdatetime"
                                        onChange={this.props.handleChange}
                                    >
                                        Track Password Update Time
                                    </Checkbox>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Allow user's to change their passwords (passwordChange).">
                                <Col sm={11}>
                                    <Checkbox
                                        id="create_passwordchange"
                                        onChange={this.props.handleChange}
                                    >
                                        Allow Users To Change Their Passwords
                                    </Checkbox>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="User must change its password after its been reset by an administrator (passwordMustChange).">
                                <Col sm={11}>
                                    <Checkbox
                                        id="create_passwordmustchange"
                                        onChange={this.props.handleChange}
                                    >
                                        User Must Change Password After Reset
                                    </Checkbox>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Maintain a password history for each user (passwordHistory).">
                                <Col sm={11}>
                                    <Checkbox
                                        id="create_passwordhistory"
                                        onChange={this.props.handleChange}
                                    >
                                        Keep Password History
                                    </Checkbox>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top ds-margin-bottom" title="The number of passwords to remember for each user (passwordInHistory).">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Passwords In History
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordinhistory"
                                        type="number"
                                        min="0"
                                        max="24"
                                        onChange={this.props.handleChange}
                                    />
                                </Col>
                            </Row>
                        </div>
                    </CustomCollapse>

                    <CustomCollapse textClosed="Show Expiration Settings" textOpened="Hide Expiration Settings" className="ds-margin-right">
                        <div className="ds-margin-left">
                            <Row className="ds-margin-top" title="Enable a password expiration policy (passwordExp).">
                                <Col sm={11}>
                                    <Checkbox
                                        id="create_passwordexp"
                                        onChange={this.props.handleChange}
                                    >
                                        Enforce Password Expiration
                                    </Checkbox>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The maxiumum age of a password in seconds before it expires (passwordMaxAge).">
                                <Col componentClass={ControlLabel} sm={6}>
                                    Password Expiration Time
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordmaxage"
                                        type="number"
                                        min="1"
                                        max="2147483647"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordexp}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The number of logins that are allowed after the password has expired (passwordGraceLimit).">
                                <Col componentClass={ControlLabel} sm={6}>
                                    Allowed Logins After Password Expires
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordgracelimit"
                                        type="number"
                                        min="0"
                                        max="128"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordexp}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Set the time (in seconds), before a password is about to expire, to send a warning. (passwordWarning).">
                                <Col componentClass={ControlLabel} sm={6}>
                                    Send Password Expiring Warning
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordwarning"
                                        type="number"
                                        min="1"
                                        max="2147483647"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordexp}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Always return a password expiring control when requested (passwordSendExpiringTime).">
                                <Col componentClass={ControlLabel} sm={6}>
                                    <Checkbox
                                        id="create_passwordsendexpiringtime"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordexp}
                                    >
                                        Always Send <i>Password Expiring</i> Control
                                    </Checkbox>
                                </Col>
                            </Row>
                        </div>
                    </CustomCollapse>

                    <CustomCollapse textClosed="Show Lockout Settings" textOpened="Hide Lockout Settings" className="ds-margin-right">
                        <div className="ds-margin-left">
                            <Row className="ds-margin-top" title="Enable account lockout (passwordLockout).">
                                <Col sm={11}>
                                    <Checkbox
                                        id="create_passwordlockout"
                                        onChange={this.props.handleChange}
                                    >
                                        Enable Account Lockout
                                    </Checkbox>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The maximum number of failed logins before account gets locked (passwordMaxFailure).">
                                <Col componentClass={ControlLabel} sm={6}>
                                    Number of Failed Logins That Locks out Account
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordmaxfailure"
                                        type="number"
                                        min="1"
                                        max="100"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordlockout}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The number of seconds until an accounts failure count is reset (passwordResetFailureCount).">
                                <Col componentClass={ControlLabel} sm={6}>
                                    Time Until <i>Failure Count</i> Resets
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordresetfailurecount"
                                        type="number"
                                        min="1"
                                        max="2147483647"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordlockout}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The number of seconds, duration, before the account gets unlocked (passwordLockoutDuration).">
                                <Col componentClass={ControlLabel} sm={6}>
                                    Time Until Account Unlocked
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordlockoutduration"
                                        type="number"
                                        min="1"
                                        max="2147483647"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordlockout}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Do not lockout the user account forever, instead the account will unlock based on the lockout duration (passwordUnlock)">
                                <Col componentClass={ControlLabel} sm={6}>
                                    <Checkbox
                                        id="create_passwordunlock"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordlockout}
                                    >
                                        Do Not Lockout Account Forever
                                    </Checkbox>
                                </Col>
                            </Row>
                        </div>
                    </CustomCollapse>

                    <CustomCollapse textClosed="Show Syntax Settings" textOpened="Hide Syntax Settings" className="ds-margin-right">
                        <div className="ds-margin-left">
                            <Row className="ds-margin-top" title="Enable password syntax checking (passwordCheckSyntax).">
                                <Col sm={11}>
                                    <Checkbox
                                        id="create_passwordchecksyntax"
                                        onChange={this.props.handleChange}
                                    >
                                        Enable Password Syntax Checking
                                    </Checkbox>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The minimum number of characters in the password (passwordMinLength).">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Min Length
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordminlength"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                                <Col componentClass={ControlLabel} sm={3} title="Reject passwords with fewer than this many alpha characters (passwordMinAlphas).">
                                    Min Alpha's
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordminalphas"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Reject passwords with fewer than this many digit characters (0-9) (passwordMinDigits).">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Min Digits
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordmindigits"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                                <Col componentClass={ControlLabel} sm={3}>
                                    Min Special
                                </Col>
                                <Col sm={2} title="Reject passwords with fewer than this many special non-alphanumeric characters (passwordMinSpecials).">
                                    <FormControl
                                        id="create_passwordminspecials"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Min Uppercase
                                </Col>
                                <Col sm={2} title="Reject passwords with fewer than this many uppercase characters (passwordMinUppers).">
                                    <FormControl
                                        id="create_passwordminuppers"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                                <Col componentClass={ControlLabel} sm={3}>
                                    Min Lowercase
                                </Col>
                                <Col sm={2} title="Reject passwords with fewer than this many lowercase characters (passwordMinLowers).">
                                    <FormControl
                                        id="create_passwordminlowers"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Reject passwords with fewer than this many 8-bit or multi-byte characters (passwordMin8Bit).">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Min 8-bit
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordmin8bit"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                                <Col componentClass={ControlLabel} sm={3}>
                                    Min Categories
                                </Col>
                                <Col sm={2} title="The minimum number of character categories that a password must contain (categories are upper, lower, digit, special, and 8-bit) (passwordMinCategories).">
                                    <FormControl
                                        id="create_passwordmincategories"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The smallest attribute value used when checking if the password contains any of the user's account information (passwordMinTokenLength).">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Min Token Length
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordmintokenlength"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                                <Col componentClass={ControlLabel} sm={3}>
                                    Max Repeated Chars
                                </Col>
                                <Col sm={2} title="The maximum number of times the same character can sequentially appear in a password (passwordMaxRepeats).">
                                    <FormControl
                                        id="create_passwordmaxrepeats"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The maximum number of allowed monotonic characters sequences (passwordMaxSequence).">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Max Sequences
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordmaxsequence"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                                <Col componentClass={ControlLabel} sm={3}>
                                    Max Sequence Sets
                                </Col>
                                <Col sm={2} title="The maximum number of allowed monotonic characters sequences that can appear more than once (passwordMaxSeqSets).">
                                    <FormControl
                                        id="create_passwordmaxseqsets"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="The maximum number of consecutive characters from the same character class/category (passwordMaxClassChars).">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Max Seq Per Class
                                </Col>
                                <Col sm={2}>
                                    <FormControl
                                        id="create_passwordmaxclasschars"
                                        type="number"
                                        min="0"
                                        max="1000"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                            </Row>
                            <Row title="A space-separated list of words that are not allowed to be contained in the new password (passwordBadWords)." className="ds-margin-top">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Prohibited Words
                                </Col>
                                <Col sm={8}>
                                    <FormControl
                                        id="create_passwordbadwords"
                                        type="text"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                            </Row>
                            <Row title="A space-separated list of entry attributes to compare to the new password (passwordUserAttributes)." className="ds-margin-top">
                                <Col componentClass={ControlLabel} sm={3}>
                                    Check User Attributes
                                </Col>
                                <Col sm={8}>
                                    <Typeahead
                                        onChange={values => {
                                            this.props.handleChange(values);
                                        }}
                                        multiple
                                        selected={this.props.passworduserattributes}
                                        options={this.props.attrs}
                                        placeholder="Type attributes to check..."
                                        disabled={!this.props.passwordchecksyntax}
                                    />
                                </Col>
                            </Row>
                            <Row className="ds-margin-top-lg" title="Check the password against the system's CrackLib dictionary (passwordDictCheck).">
                                <Col componentClass={ControlLabel} sm={3}>
                                    <Checkbox
                                        id="create_passworddictcheck"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    >
                                        Dictionary Check
                                    </Checkbox>
                                </Col>
                            </Row>
                            <Row className="ds-margin-top" title="Reject a password if it is a palindrome (passwordPalindrome).">
                                <Col componentClass={ControlLabel} sm={3}>
                                    <Checkbox
                                        id="create_passwordpalindrome"
                                        onChange={this.props.handleChange}
                                        disabled={!this.props.passwordchecksyntax}
                                    >
                                        Reject Palindromes
                                    </Checkbox>
                                </Col>
                            </Row>
                        </div>
                    </CustomCollapse>
                </Form>
                <Button
                    disabled={this.props.createDisabled}
                    bsStyle="primary"
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
            activeKey: 1,
            localActiveKey: 1,
            modalChecked: false,
            editPolicy: false,
            tableLoading: false,
            // Create policy
            policyType: "Subtree Policy",
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
            showDeletePolicy: false,
            attrMap: {
                "passwordstoragescheme": "--pwdscheme",
                "passwordtrackupdatetime": "--pwdtrack",
                "passwordchange": "--pwdchange",
                "passwordmustchange": "--pwdmustchange",
                "passwordhistory": "--pwdhistory",
                "passwordinhistory": "--pwdhistorycount",
                "passwordminage": "--pwdminage",
                "passwordexp": "--pwdexpire",
                "passwordgracelimit": "--pwdgracelimit",
                "passwordsendexpiringtime": "--pwdsendexpiring",
                "passwordmaxage": "--pwdmaxage",
                "passwordwarning": "--pwdwarning",
                "passwordlockout": "--pwdlockout",
                "passwordunlock": "--pwdunlock",
                "passwordlockoutduration": "--pwdlockoutduration",
                "passwordmaxfailure": "--pwdmaxfailures",
                "passwordresetfailurecount": "--pwdresetfailcount",
                "passwordchecksyntax": "--pwdchecksyntax",
                "passwordminlength": "--pwdminlen",
                "passwordmindigits": "--pwdmindigits",
                "passwordminalphas": "--pwdminalphas",
                "passwordminuppers": "--pwdminuppers",
                "passwordminlowers": "--pwdminlowers",
                "passwordminspecials": "--pwdminspecials",
                "passwordmin8bit": "--pwdmin8bits",
                "passwordmaxrepeats": "--pwdmaxrepeats",
                "passwordpalindrome": "--pwdpalindrome",
                "passwordmaxsequence": "--pwdmaxseq",
                "passwordmaxseqsets": "--pwdmaxseqsets",
                "passwordmaxclasschars": "--pwdmaxclasschars",
                "passwordmincategories": "--pwdmincatagories",
                "passwordmintokenlength": "--pwdmintokenlen",
                "passwordbadwords": "--pwdbadwords",
                "passworduserattributes": "--pwduserattrs",
                "passworddictcheck": "--pwddictcheck",
            },
        };

        this.createPolicy = this.createPolicy.bind(this);
        this.closeDeletePolicy = this.closeDeletePolicy.bind(this);
        this.deletePolicy = this.deletePolicy.bind(this);
        this.handleCreateChange = this.handleCreateChange.bind(this);
        this.handleExpChange = this.handleExpChange.bind(this);
        this.handleGeneralChange = this.handleGeneralChange.bind(this);
        this.handleLocalNavSelect = this.handleLocalNavSelect.bind(this);
        this.handleLockoutChange = this.handleLockoutChange.bind(this);
        this.handleModalChange = this.handleModalChange.bind(this);
        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleSyntaxChange = this.handleSyntaxChange.bind(this);
        this.loadLocal = this.loadLocal.bind(this);
        this.loadPolicies = this.loadPolicies.bind(this);
        this.resetTab = this.resetTab.bind(this);
        this.saveExp = this.saveExp.bind(this);
        this.saveGeneral = this.saveGeneral.bind(this);
        this.saveLockout = this.saveLockout.bind(this);
        this.saveSyntax = this.saveSyntax.bind(this);
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
        this.setState({ localActiveKey: 1 });
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
    }

    handleLocalNavSelect(key) {
        this.setState({ localActiveKey: key });
    }

    handleModalChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;

        this.setState({
            [attr]: value,
        });
    }

    handleCreateChange(e) {
        let attr;
        let value;
        let disableSaveBtn = true;
        let invalid_dn = false;
        let all_attrs = general_attrs.concat(exp_attrs, lockout_attrs, syntax_attrs);

        if (Array.isArray(e)) {
            // Typeahead - convert array to string
            attr = "create_passworduserattributes";
            value = e.join(' ');
        } else {
            value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
            attr = e.target.id;
        }

        // Check if a setting was changed, if so enable the save button
        for (let all_attr of all_attrs) {
            if (all_attr == 'passworduserattributes' && attr == 'create_passworduserattributes') {
                let orig_val = this.state['_' + all_attr].join(' ');
                if (orig_val != value) {
                    value = e; // restore value
                    disableSaveBtn = false;
                    break;
                }
                value = e; // restore value
            } else if (attr == "create_" + all_attr && this.state['_create_' + all_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }
        if (attr == "policyDN" && value != "") {
            if (valid_dn(value)) {
                disableSaveBtn = false;
            } else {
                invalid_dn = true;
            }
        }
        // Now check for differences in values that we did not touch
        for (let all_attr of all_attrs) {
            if (all_attr == 'passworduserattributes' && attr != 'create_passworduserattributes') {
                // Typeahead attribute needs special care
                let orig_val = this.state['_' + all_attr].join(' ');
                let new_val = this.state[all_attr].join(' ');
                if (orig_val != new_val) {
                    disableSaveBtn = false;
                    break;
                }
            } else if (attr != "create_" + all_attr && this.state['_create_' + all_attr] != this.state["create_" + all_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        if (this.state.policyDN == "" || (attr == "policyDN" && value == "")) {
            disableSaveBtn = true;
        }

        this.setState({
            [attr]: value,
            createDisabled: disableSaveBtn,
            invalid_dn: invalid_dn,
        });
    }

    createPolicy() {
        let all_attrs = general_attrs.concat(exp_attrs, lockout_attrs, syntax_attrs);
        let action = "adduser";

        this.setState({
            loading: true
        });

        if (this.state.policyType == "Subtree Policy") {
            action = "addsubtree";
        }
        let cmd = [
            'dsconf', '-j', this.props.serverId, 'localpwp', action, this.state.policyDN
        ];

        for (let attr of all_attrs) {
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
                    if (old_val.join(' ') == new_val.join(' ')) {
                        continue;
                    }
                }
                cmd.push(this.state.attrMap[attr] + "=" + new_val);
            }
        }

        log_cmd("createPolicy", "Create a local password policy", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
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
                    let errMsg = JSON.parse(err);
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
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (let general_attr of general_attrs) {
            if (attr == general_attr && this.state['_' + general_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let general_attr of general_attrs) {
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
            'dsconf', '-j', this.props.serverId, 'localpwp', 'set', this.state.policyName
        ];

        for (let attr of general_attrs) {
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
                .spawn(cmd, {superuser: true, "err": "message"})
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
                    let errMsg = JSON.parse(err);
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
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (let exp_attr of exp_attrs) {
            if (attr == exp_attr && this.state['_' + exp_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let exp_attr of exp_attrs) {
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
            'dsconf', '-j', this.props.serverId, 'localpwp', 'set', this.state.policyName
        ];

        for (let attr of exp_attrs) {
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
                .spawn(cmd, {superuser: true, "err": "message"})
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
                    let errMsg = JSON.parse(err);
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
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (let lockout_attr of lockout_attrs) {
            if (attr == lockout_attr && this.state['_' + lockout_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let lockout_attr of lockout_attrs) {
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
            'dsconf', '-j', this.props.serverId, 'localpwp', 'set', this.state.policyName
        ];

        for (let attr of lockout_attrs) {
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
                .spawn(cmd, {superuser: true, "err": "message"})
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
                    let errMsg = JSON.parse(err);
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

    handleSyntaxChange(e) {
        // Could be a typeahead change, check if "e" is an Array
        let attr;
        let value;
        if (Array.isArray(e)) {
            // Typeahead - convert array to string
            attr = "passworduserattributes";
            value = e.join(' ');
        } else {
            value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
            attr = e.target.id;
        }
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (let syntax_attr of syntax_attrs) {
            if (syntax_attr == 'passworduserattributes' && attr == 'passworduserattributes') {
                let orig_val = this.state['_' + syntax_attr].join(' ');
                if (orig_val != value) {
                    value = e; // restore value
                    disableSaveBtn = false;
                    break;
                }
                value = e; // restore value
            } else if (attr == syntax_attr && this.state['_' + syntax_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let syntax_attr of syntax_attrs) {
            if (syntax_attr == 'passworduserattributes' && attr != 'passworduserattributes') {
                // Typeahead attribute needs special care
                let orig_val = this.state['_' + syntax_attr].join(' ');
                let new_val = this.state[syntax_attr].join(' ');
                if (orig_val != new_val) {
                    disableSaveBtn = false;
                    break;
                }
            } else if (attr != syntax_attr && this.state['_' + syntax_attr] != this.state[syntax_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [attr]: value,
            saveSyntaxDisabled: disableSaveBtn,
        });
    }

    saveSyntax() {
        this.setState({
            loading: true
        });

        let cmd = [
            'dsconf', '-j', this.props.serverId, 'localpwp', 'set', this.state.policyName
        ];

        for (let attr of syntax_attrs) {
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
                .spawn(cmd, {superuser: true, "err": "message"})
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
                    let errMsg = JSON.parse(err);
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

    deletePolicy() {
        // Start spinning
        let new_rows = this.state.rows;
        for (let row of new_rows) {
            if (row.targetdn == this.state.deleteName) {
                row.pwp_type = [<Spinner className="ds-lower-field" key={row.pwp_type} loading size="sm" />];
                row.basedn = [<Spinner className="ds-lower-field" key={row.basedn} loading size="sm" />];
                row.actions = [<Spinner className="ds-lower-field" key={row.targetdn} loading size="sm" />];
            }
        }
        this.setState({
            rows: new_rows,
            editPolicy: false,
        });

        let cmd = [
            "dsconf", "-j", this.props.serverId, "localpwp", "remove", this.state.deleteName
        ];
        log_cmd("deletePolicy", "delete policy", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.loadPolicies();
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.loadPolicies();
                    this.props.addNotification(
                        "error",
                        `Error deleting local password policy - ${errMsg.desc}`
                    );
                });
    }

    loadPolicies() {
        let cmd = [
            "dsconf", "-j", this.props.serverId, "localpwp", "list"
        ];
        log_cmd("loadPolicies", "Load all the local password policies for the table", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let policy_obj = JSON.parse(content);
                    this.setState({
                        localActiveKey: 1,
                        activeKey: 1,
                        rows: policy_obj.items,
                        loaded: true,
                        loading: false,
                        editPolicy: false,
                        policyDN: "",
                        policyType: "Subtree Policy",
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
                        // Create policy
                        create_passwordchange: false,
                        create_passwordmustchange: false,
                        create_passwordhistory: false,
                        create_passwordtrackupdatetime: false,
                        create_passwordexp: false,
                        create_passwordsendexpiringtime: false,
                        create_passwordlockout: false,
                        create_passwordunlock: "0",
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
                        _create_passwordchange: false,
                        _create_passwordmustchange: false,
                        _create_passwordhistory: false,
                        _create_passwordtrackupdatetime: false,
                        _create_passwordexp: false,
                        _create_passwordsendexpiringtime: false,
                        _create_passwordlockout: false,
                        _create_passwordunlock: "0",
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
                    }, this.props.enableTree);
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.setState({
                        loaded: true,
                        loading: false,
                    });
                    this.props.addNotification(
                        "error",
                        `Error loading local password policies - ${errMsg.desc}`
                    );
                });
    }

    loadLocal(name) {
        let cmd = [
            "dsconf", "-j", this.props.serverId, "localpwp", "get", name
        ];
        log_cmd("loadLocal", "Load a local password policy", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
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
                    let pwScheme = "";
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

                    if ('passwordmintokenlength' in attrs) {
                        pwMinTokenLen = attrs['passwordmintokenlength'][0];
                    }
                    if ('passwordmincategories' in attrs) {
                        pwMinCat = attrs['passwordmincategories'][0];
                    }
                    if ('passwordmaxclasschars' in attrs) {
                        pwMaxClassChars = attrs['passwordmaxclasschars'][0];
                    }
                    if ('passwordmaxseqsets' in attrs) {
                        pwMaxSeqSets = attrs['passwordmaxseqsets'][0];
                    }
                    if ('passwordmaxsequence' in attrs) {
                        pwMaxSeq = attrs['passwordmaxsequence'][0];
                    }
                    if ('passwordmaxrepeats' in attrs) {
                        pwMaxRepeats = attrs['passwordmaxrepeats'][0];
                    }
                    if ('passwordmin8bit' in attrs) {
                        pwMin8bit = attrs['passwordmin8bit'][0];
                    }
                    if ('passwordminspecials' in attrs) {
                        pwMinSpecials = attrs['passwordminspecials'][0];
                    }
                    if ('passwordminlowers' in attrs) {
                        pwMinLowers = attrs['passwordminlowers'][0];
                    }
                    if ('passwordminuppers' in attrs) {
                        pwMinUppers = attrs['passwordminuppers'][0];
                    }
                    if ('passwordminalphas' in attrs) {
                        pwMinAlphas = attrs['passwordminalphas'][0];
                    }
                    if ('passwordmindigits' in attrs) {
                        pwMinDigits = attrs['passwordmindigits'][0];
                    }
                    if ('passwordminlength' in attrs) {
                        pwMinLen = attrs['passwordminlength'][0];
                    }
                    if ('passwordresetfailurecount' in attrs) {
                        pwFailCount = attrs['passwordresetfailurecount'][0];
                    }
                    if ('passwordmaxfailure' in attrs) {
                        pwMaxFailure = attrs['passwordmaxfailure'][0];
                    }
                    if ('passwordlockoutduration' in attrs) {
                        pwLockoutDur = attrs['passwordlockoutduration'][0];
                    }
                    if ('passwordgracelimit' in attrs) {
                        pwGraceLimit = attrs['passwordgracelimit'][0];
                    }
                    if ('passwordmaxage' in attrs) {
                        pwMaxAge = attrs['passwordmaxage'][0];
                    }
                    if ('passwordminage' in attrs) {
                        pwMinAge = attrs['passwordminage'][0];
                    }
                    if ('passwordwarning' in attrs) {
                        pwWarning = attrs['passwordwarning'][0];
                    }
                    if ('passwordstoragescheme' in attrs) {
                        pwScheme = attrs['passwordstoragescheme'][0];
                    }
                    if ('passwordinhistory' in attrs) {
                        pwInHistory = attrs['passwordinhistory'][0];
                    }
                    if ('passwordchange' in attrs && attrs['passwordchange'][0] == "on") {
                        pwChange = true;
                    }
                    if ('passwordmustchange' in attrs && attrs['passwordmustchange'][0] == "on") {
                        pwMustChange = true;
                    }
                    if ('passwordhistory' in attrs && attrs['passwordhistory'][0] == "on") {
                        pwHistory = true;
                    }
                    if ('passwordtrackupdatetime' in attrs && attrs['passwordtrackupdatetime'][0] == "on") {
                        pwTrackUpdate = true;
                    }
                    if ('passwordsendexpiringtime' in attrs && attrs['passwordsendexpiringtime'][0] == "on") {
                        pwSendExpire = true;
                    }
                    if ('passwordlockout' in attrs && attrs['passwordlockout'][0] == "on") {
                        pwLockout = true;
                    }
                    if ('passwordunlock' in attrs && attrs['passwordunlock'][0] == "on") {
                        pwUnlock = true;
                    }
                    if ('passwordexp' in attrs && attrs['passwordexp'][0] == "on") {
                        pwExpire = true;
                    }
                    if ('passwordchecksyntax' in attrs && attrs['passwordchecksyntax'][0] == "on") {
                        pwCheckSyntax = true;
                    }
                    if ('passwordpalindrome' in attrs && attrs['passwordpalindrome'][0] == "on") {
                        pwPalindrome = true;
                    }
                    if ('passworddictcheck' in attrs && attrs['passworddictcheck'][0] == "on") {
                        pwDictCheck = true;
                    }
                    if ('passwordbadwords' in attrs && attrs['passwordbadwords'][0] != "") {
                        // Hack until this is fixed: https://github.com/389ds/389-ds-base/issues/3928
                        if (attrs['passwordbadwords'].length > 1) {
                            pwBadWords = attrs['passwordbadwords'].join(' ');
                        } else {
                            pwBadWords = attrs['passwordbadwords'][0];
                        }
                    }
                    if ('passworduserattributes' in attrs && attrs['passworduserattributes'][0] != "") {
                        if (attrs['passworduserattributes'].length > 1) {
                            // Hack until this is fixed: https://github.com/389ds/389-ds-base/issues/3928
                            attrs['passworduserattributes'][0] = attrs['passworduserattributes'].join(' ');
                        }
                        // Could be space or comma separated list
                        if (attrs['passworduserattributes'][0].indexOf(',') > -1) {
                            pwUserAttrs = attrs['passworduserattributes'][0].trim();
                            pwUserAttrs = pwUserAttrs.split(',');
                        } else {
                            pwUserAttrs = attrs['passworduserattributes'][0].split();
                        }
                    }

                    this.setState(() => (
                        {
                            editPolicy: true,
                            loading: false,
                            localActiveKey: 2,
                            activeKey: 1,
                            policyName: name,
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
                        })
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
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

        if (this.state.passwordchecksyntax) {
            pwSyntaxRows =
                <div className="ds-margin-left">
                    <Row className="ds-margin-top" title="The minimum number of characters in the password (passwordMinLength).">
                        <Col componentClass={ControlLabel} sm={3}>
                            Min Length
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordminlength"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordminlength}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3} title="Reject passwords with fewer than this many alpha characters (passwordMinAlphas).">
                            Min Alpha's
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordminalphas"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordminalphas}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Reject passwords with fewer than this many digit characters (0-9) (passwordMinDigits).">
                        <Col componentClass={ControlLabel} sm={3}>
                            Min Digits
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordmindigits"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordmindigits}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Min Special
                        </Col>
                        <Col sm={2} title="Reject passwords with fewer than this many special non-alphanumeric characters (passwordMinSpecials).">
                            <FormControl
                                id="passwordminspecials"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordminspecials}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Min Uppercase
                        </Col>
                        <Col sm={2} title="Reject passwords with fewer than this many uppercase characters (passwordMinUppers).">
                            <FormControl
                                id="passwordminuppers"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordminuppers}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Min Lowercase
                        </Col>
                        <Col sm={2} title="Reject passwords with fewer than this many lowercase characters (passwordMinLowers).">
                            <FormControl
                                id="passwordminlowers"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordminlowers}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Reject passwords with fewer than this many 8-bit or multi-byte characters (passwordMin8Bit).">
                        <Col componentClass={ControlLabel} sm={3}>
                            Min 8-bit
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordmin8bit"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordmin8bit}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Min Categories
                        </Col>
                        <Col sm={2} title="The minimum number of character categories that a password must contain (categories are upper, lower, digit, special, and 8-bit) (passwordMinCategories).">
                            <FormControl
                                id="passwordmincategories"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordmincategories}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The smallest attribute value used when checking if the password contains any of the user's account information (passwordMinTokenLength).">
                        <Col componentClass={ControlLabel} sm={3}>
                            Min Token Length
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordmintokenlength"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordmintokenlength}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Max Repeated Chars
                        </Col>
                        <Col sm={2} title="The maximum number of times the same character can sequentially appear in a password (passwordMaxRepeats).">
                            <FormControl
                                id="passwordmaxrepeats"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordmaxrepeats}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The maximum number of allowed monotonic characters sequences (passwordMaxSequence).">
                        <Col componentClass={ControlLabel} sm={3}>
                            Max Sequences
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordmaxsequence"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordmaxsequence}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                        <Col componentClass={ControlLabel} sm={3}>
                            Max Sequence Sets
                        </Col>
                        <Col sm={2} title="The maximum number of allowed monotonic characters sequences that can appear more than once (passwordMaxSeqSets).">
                            <FormControl
                                id="passwordmaxseqsets"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordmaxseqsets}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The maximum number of consecutive characters from the same character class/category (passwordMaxClassChars).">
                        <Col componentClass={ControlLabel} sm={3}>
                            Max Seq Per Class
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordmaxclasschars"
                                type="number"
                                min="0"
                                max="1000"
                                value={this.state.passwordmaxclasschars}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                    </Row>
                    <Row title="A space-separated list of words that are not allowed to be contained in the new password (passwordBadWords)." className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Prohibited Words
                        </Col>
                        <Col sm={8}>
                            <FormControl
                                id="passwordbadwords"
                                type="text"
                                value={this.state.passwordbadwords}
                                onChange={this.handleSyntaxChange}
                            />
                        </Col>
                    </Row>
                    <Row title="A space-separated list of entry attributes to compare to the new password (passwordUserAttributes)." className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Check User Attributes
                        </Col>
                        <Col sm={8}>
                            <Typeahead
                                onChange={values => {
                                    this.handleSyntaxChange(values);
                                }}
                                multiple
                                selected={this.state.passworduserattributes}
                                options={this.props.attrs}
                                placeholder="Type attributes to check..."
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top-lg" title="Check the password against the system's CrackLib dictionary (passwordDictCheck).">
                        <Col componentClass={ControlLabel} sm={3}>
                            <Checkbox
                                id="passworddictcheck"
                                defaultChecked={this.state.passworddictcheck}
                                onChange={this.handleSyntaxChange}
                            >
                                Dictionary Check
                            </Checkbox>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Reject a password if it is a palindrome (passwordPalindrome).">
                        <Col componentClass={ControlLabel} sm={3}>
                            <Checkbox
                                id="passwordpalindrome"
                                defaultChecked={this.state.passwordpalindrome}
                                onChange={this.handleSyntaxChange}
                            >
                                Reject Palindromes
                            </Checkbox>
                        </Col>
                    </Row>
                </div>;
        }

        if (this.state.passwordlockout) {
            pwLockoutRows =
                <div className="ds-margin-left">
                    <Row className="ds-margin-top" title="The maximum number of failed logins before account gets locked (passwordMaxFailure).">
                        <Col componentClass={ControlLabel} sm={5}>
                            Number of Failed Logins That Locks out Account
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordmaxfailure"
                                type="number"
                                min="1"
                                max="100"
                                value={this.state.passwordmaxfailure}
                                onChange={this.handleLockoutChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The number of seconds until an accounts failure count is reset (passwordResetFailureCount).">
                        <Col componentClass={ControlLabel} sm={5}>
                            Time Until <i>Failure Count</i> Resets
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordresetfailurecount"
                                type="number"
                                min="1"
                                max="2147483647"
                                value={this.state.passwordresetfailurecount}
                                onChange={this.handleLockoutChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The number of seconds, duration, before the account gets unlocked (passwordLockoutDuration).">
                        <Col componentClass={ControlLabel} sm={5}>
                            Time Until Account Unlocked
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordlockoutduration"
                                type="number"
                                min="1"
                                max="2147483647"
                                value={this.state.passwordlockoutduration}
                                onChange={this.handleLockoutChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Do not lockout the user account forever, instead the account will unlock based on the lockout duration (passwordUnlock)">
                        <Col sm={5}>
                            <Checkbox
                                id="passwordunlock"
                                defaultChecked={this.state.passwordunlock}
                                onChange={this.handleLockoutChange}
                            >
                                Do Not Lockout Account Forever
                            </Checkbox>
                        </Col>
                    </Row>
                </div>;
        }

        if (this.state.passwordexp) {
            pwExpirationRows =
                <div className="ds-margin-left">
                    <Row className="ds-margin-top" title="The maxiumum age of a password in seconds before it expires (passwordMaxAge).">
                        <Col componentClass={ControlLabel} sm={5}>
                            Password Expiration Time
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordmaxage"
                                type="number"
                                min="1"
                                max="2147483647"
                                value={this.state.passwordmaxage}
                                onChange={this.handleExpChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="The number of logins that are allowed after the password has expired (passwordGraceLimit).">
                        <Col componentClass={ControlLabel} sm={5}>
                            Allowed Logins After Password Expires
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordgracelimit"
                                type="number"
                                min="0"
                                max="128"
                                value={this.state.passwordgracelimit}
                                onChange={this.handleExpChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Set the time (in seconds), before a password is about to expire, to send a warning. (passwordWarning).">
                        <Col componentClass={ControlLabel} sm={5}>
                            Send Password Expiring Warning
                        </Col>
                        <Col sm={2}>
                            <FormControl
                                id="passwordwarning"
                                type="number"
                                min="1"
                                max="2147483647"
                                value={this.state.passwordwarning}
                                onChange={this.handleExpChange}
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Always return a password expiring control when requested (passwordSendExpiringTime).">
                        <Col componentClass={ControlLabel} sm={5}>
                            <Checkbox
                                id="passwordsendexpiringtime"
                                defaultChecked={this.state.passwordsendexpiringtime}
                                onChange={this.handleExpChange}
                            >
                                Always Send <i>Password Expiring</i> Control
                            </Checkbox>
                        </Col>
                    </Row>
                </div>;
        }

        if (!this.state.editPolicy) {
            edit_tab =
                <div className="ds-margin-top-xlg ds-center">
                    <h4>Please choose a policy from the <a onClick={this.resetTab}>Local Policy Table</a>.</h4>
                </div>;
        } else {
            edit_tab =
                <div className="ds-margin-left">
                    <h4 className="ds-margin-top-xlg">{this.state.policyName}</h4>
                    <TabContainer id="server-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                        <div className="ds-margin-top-xlg">
                            <Nav bsClass="nav nav-tabs nav-tabs-pf">
                                <NavItem eventKey={1}>
                                    <div dangerouslySetInnerHTML={{__html: 'General Settings'}} />
                                </NavItem>
                                <NavItem eventKey={2}>
                                    <div dangerouslySetInnerHTML={{__html: 'Expiration'}} />
                                </NavItem>
                                <NavItem eventKey={3}>
                                    <div dangerouslySetInnerHTML={{__html: 'Account Lockout'}} />
                                </NavItem>
                                <NavItem eventKey={4}>
                                    <div dangerouslySetInnerHTML={{__html: 'Syntax Checking'}} />
                                </NavItem>
                            </Nav>
                            <TabContent className="ds-margin-top-lg">
                                <TabPane eventKey={1}>
                                    <Form className="ds-margin-top-lg ds-margin-left" horizontal>
                                        <Row title="Set the password storage scheme (passwordstoragescheme)." className="ds-margin-top">
                                            <Col sm={8}>
                                                <ControlLabel>
                                                    Password Storage Scheme
                                                </ControlLabel>
                                                <select
                                                  className="btn btn-default dropdown ds-margin-left-sm" id="passwordstoragescheme"
                                                  onChange={this.handleGeneralChange} value={this.state.passwordstoragescheme}>
                                                    <option>PBKDF2_SHA256</option>
                                                    <option>SSHA512</option>
                                                    <option>SSHA384</option>
                                                    <option>SSHA256</option>
                                                    <option>SSHA</option>
                                                    <option>MD5</option>
                                                    <option>SMD5</option>
                                                    <option>CRYPT-MD5</option>
                                                    <option>CRYPT-SHA512</option>
                                                    <option>CRYPT-SHA256</option>
                                                    <option>CRYPT</option>
                                                    <option>GOST_YESCRYPT</option>
                                                    <option>CLEAR</option>
                                                </select>
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top" title="Record a separate timestamp specifically for the last time that the password for an entry was changed. If this is enabled, then it adds the pwdUpdateTime operational attribute to the user account entry (passwordTrackUpdateTime).">
                                            <Col sm={11}>
                                                <Checkbox
                                                    id="passwordtrackupdatetime"
                                                    defaultChecked={this.state.passwordtrackupdatetime}
                                                    onChange={this.handleGeneralChange}
                                                >
                                                    Track Password Update Time
                                                </Checkbox>
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top" title="Allow user's to change their passwords (passwordChange).">
                                            <Col sm={11}>
                                                <Checkbox
                                                    id="passwordchange"
                                                    defaultChecked={this.state.passwordchange}
                                                    onChange={this.handleGeneralChange}
                                                >
                                                    Allow Users To Change Their Passwords
                                                </Checkbox>
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top" title="User must change its password after its been reset by an administrator (passwordMustChange).">
                                            <Col sm={11}>
                                                <Checkbox
                                                    id="passwordmustchange"
                                                    defaultChecked={this.state.passwordmustchange}
                                                    onChange={this.handleGeneralChange}
                                                >
                                                    User Must Change Password After Reset
                                                </Checkbox>
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top" title="Maintain a password history for each user (passwordHistory).">
                                            <Col sm={11}>
                                                <div className="ds-inline">
                                                    <Checkbox
                                                        id="passwordhistory"
                                                        defaultChecked={this.state.passwordhistory}
                                                        onChange={this.handleUserChange}
                                                    >
                                                        Keep Password History
                                                    </Checkbox>
                                                </div>
                                                <div className="ds-inline ds-left-margin ds-raise-field ds-width-sm">
                                                    <FormControl
                                                        id="passwordinhistory"
                                                        type="number"
                                                        min="0"
                                                        max="24"
                                                        value={this.state.passwordinhistory}
                                                        onChange={this.handleGeneralChange}
                                                        disabled={!this.state.passwordhistory}
                                                    />
                                                </div>
                                            </Col>
                                        </Row>
                                        <Row className="ds-margin-top" title="Indicates the number of seconds that must pass before a user can change their password again. (passwordMinAge)">
                                            <Col sm={12}>
                                                <div className="ds-inline">
                                                    <ControlLabel>
                                                        Password Minimum Age
                                                    </ControlLabel>
                                                </div>
                                                <div className="ds-inline ds-left-margin ds-raise-field">
                                                    <FormControl
                                                        id="passwordminage"
                                                        type="number"
                                                        min="0"
                                                        max="2147483647"
                                                        value={this.state.passwordminage}
                                                        onChange={this.handleGeneralChange}
                                                    />
                                                </div>
                                            </Col>
                                        </Row>
                                        <Button
                                            disabled={this.state.saveGeneralDisabled}
                                            bsStyle="primary"
                                            className="ds-margin-top-lg"
                                            onClick={this.saveGeneral}
                                            title="Save the General Settings"
                                        >
                                            Save
                                        </Button>
                                    </Form>
                                </TabPane>

                                <TabPane eventKey={2}>
                                    <Form className="ds-margin-top-lg ds-margin-left" horizontal>
                                        <Row className="ds-margin-top" title="Enable a password expiration policy (passwordExp).">
                                            <Col sm={11}>
                                                <Checkbox
                                                    id="passwordexp"
                                                    defaultChecked={this.state.passwordexp}
                                                    onChange={this.handleExpChange}
                                                >
                                                    Enforce Password Expiration
                                                </Checkbox>
                                            </Col>
                                        </Row>
                                        {pwExpirationRows}
                                        <Button
                                            disabled={this.state.saveExpDisabled}
                                            bsStyle="primary"
                                            className="ds-margin-top-lg ds-margin-left"
                                            onClick={this.saveExp}
                                            title="Save the Expiration Settings"
                                        >
                                            Save
                                        </Button>
                                    </Form>
                                </TabPane>

                                <TabPane eventKey={3}>
                                    <Form className="ds-margin-top-lg ds-margin-left" horizontal>
                                        <Row className="ds-margin-top" title="Enable account lockout (passwordLockout).">
                                            <Col sm={11}>
                                                <Checkbox
                                                    id="passwordlockout"
                                                    defaultChecked={this.state.passwordlockout}
                                                    onChange={this.handleLockoutChange}
                                                >
                                                    Enable Account Lockout
                                                </Checkbox>
                                            </Col>
                                        </Row>
                                        {pwLockoutRows}
                                        <Button
                                            disabled={this.state.saveLockoutDisabled}
                                            bsStyle="primary"
                                            className="ds-margin-top-lg ds-margin-left"
                                            onClick={this.saveLockout}
                                            title="Save the Lockout Settings"
                                        >
                                            Save
                                        </Button>
                                    </Form>
                                </TabPane>

                                <TabPane eventKey={4}>
                                    <Form className="ds-margin-top-lg ds-margin-left" horizontal>
                                        <Row className="ds-margin-top" title="Enable password syntax checking (passwordCheckSyntax).">
                                            <Col sm={11}>
                                                <Checkbox
                                                    id="passwordchecksyntax"
                                                    checked={this.state.passwordchecksyntax}
                                                    onChange={this.handleSyntaxChange}
                                                >
                                                    Enable Password Syntax Checking
                                                </Checkbox>
                                            </Col>
                                        </Row>
                                        {pwSyntaxRows}
                                        <Button
                                            disabled={this.state.saveSyntaxDisabled}
                                            bsStyle="primary"
                                            className="ds-margin-top-lg ds-margin-left"
                                            onClick={this.saveSyntax}
                                            title="Save the Syntax Settings"
                                        >
                                            Save
                                        </Button>
                                    </Form>
                                </TabPane>
                            </TabContent>
                        </div>
                    </TabContainer>
                </div>;
        }

        let body =
            <div className="ds-margin-top-lg">
                <TabContainer id="local-passwords" onSelect={this.handleLocalNavSelect} activeKey={this.state.localActiveKey}>
                    <div className="ds-margin-top">
                        <Nav bsClass="nav nav-tabs nav-tabs-pf">
                            <NavItem eventKey={1}>
                                <div dangerouslySetInnerHTML={{__html: 'Local Policy Table'}} />
                            </NavItem>
                            <NavItem eventKey={2}>
                                <div dangerouslySetInnerHTML={{__html: 'Edit Policy'}} />
                            </NavItem>
                            <NavItem eventKey={3}>
                                <div dangerouslySetInnerHTML={{__html: 'Create Policy'}} />
                            </NavItem>
                        </Nav>

                        <TabContent className="ds-margin-top-lg">
                            <TabPane eventKey={1}>
                                <div className="ds-margin-top-xlg">
                                    <PwpTable
                                        rows={this.state.rows}
                                        editPolicy={this.loadLocal}
                                        deletePolicy={this.showDeletePolicy}
                                    />
                                </div>
                            </TabPane>
                            <TabPane eventKey={2}>
                                {edit_tab}
                            </TabPane>
                            <TabPane eventKey={3}>
                                <CreatePolicy
                                    handleChange={this.handleCreateChange}
                                    attrs={this.props.attrs}
                                    passwordexp={this.state.create_passwordexp}
                                    passwordchecksyntax={this.state.create_passwordchecksyntax}
                                    passwordlockout={this.state.create_passwordlockout}
                                    createDisabled={this.state.createDisabled}
                                    passworduserattributes={this.state.create_passworduserattributes}
                                    createPolicy={this.createPolicy}
                                    invalid_dn={this.state.invalid_dn}
                                    key={this.state.rows}
                                />
                            </TabPane>
                        </TabContent>
                    </div>
                </TabContainer>
            </div>;

        if (this.state.loading || !this.state.loaded) {
            body = <Spinner loading size="md" />;
        }

        return (
            <div>
                <Row>
                    <Col sm={5}>
                        <ControlLabel className="ds-suffix-header ds-margin-top-lg">
                            Local Password Policies
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh the local password policies"
                                onClick={this.reloadConfig}
                                disabled={this.state.loading}
                            />
                        </ControlLabel>
                    </Col>
                </Row>
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
};

LocalPwPolicy.defaultProps = {
    attrs: [],
};
