import cockpit from "cockpit";
import React from "react";
import { log_cmd } from "../tools.jsx";
import {
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
} from "patternfly-react";
import {
    Spinner,
    Button,
    Checkbox,
    Select,
    SelectVariant,
    SelectOption
    // Form,
    // FormGroup,
    // Tab,
    // Tabs,
    // TabTitleText,
    // TextInput,
    // Grid,
    // GridItem,
} from "@patternfly/react-core";
import PropTypes from "prop-types";

const general_attrs = [
    "nsslapd-pwpolicy-local",
    "passwordstoragescheme",
    "passwordadmindn",
    "passwordtrackupdatetime",
    "nsslapd-allow-hashed-passwords",
    "nsslapd-pwpolicy-inherit-global",
    "passwordisglobalpolicy",
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

export class GlobalPwPolicy extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            loading: true,
            loaded: false,
            activeKey: 1,
            // Lists of all the attributes for each tab/section.
            // We use the exact attribute name for the ID of
            // each field, so we can loop over them to efficently
            // check for changes, and updating/saving the config.

            saveGeneralDisabled: true,
            saveExpDisabled: true,
            saveLockoutDisabled: true,
            saveSyntaxDisabled: true,
            isSelectOpen: false,
        };

        this.handleNavSelect = this.handleNavSelect.bind(this);
        this.handleGeneralChange = this.handleGeneralChange.bind(this);
        this.saveGeneral = this.saveGeneral.bind(this);
        this.handleExpChange = this.handleExpChange.bind(this);
        this.saveExp = this.saveExp.bind(this);
        this.handleLockoutChange = this.handleLockoutChange.bind(this);
        this.saveLockout = this.saveLockout.bind(this);
        this.handleSyntaxChange = this.handleSyntaxChange.bind(this);
        this.saveSyntax = this.saveSyntax.bind(this);
        this.loadGlobal = this.loadGlobal.bind(this);
        // Select Typeahead
        this.onSelectToggle = this.onSelectToggle.bind(this);
        this.onSelectClear = this.onSelectClear.bind(this);
    }

    componentDidMount() {
        // Loading config TODO
        if (!this.state.loaded) {
            this.loadGlobal();
        } else {
            this.props.enableTree();
        }
    }

    handleNavSelect(key) {
        this.setState({ activeKey: key });
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
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'config', 'replace'
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
                cmd.push(attr + "=" + val);
            }
        }

        log_cmd("saveGeneral", "Saving general pwpolicy settings", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadGlobal();
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
                    this.loadGlobal();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error updating password policy configuration - ${errMsg.desc}`
                    );
                });
    }

    handleUserChange(e) {
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let attr = e.target.id;
        let disableSaveBtn = true;

        // Check if a setting was changed, if so enable the save button
        for (let user_attr of this.state.user_attrs) {
            if (attr == user_attr && this.state['_' + user_attr] != value) {
                disableSaveBtn = false;
                break;
            }
        }

        // Now check for differences in values that we did not touch
        for (let user_attr of this.state.user_attrs) {
            if (attr != user_attr && this.state['_' + user_attr] != this.state[user_attr]) {
                disableSaveBtn = false;
                break;
            }
        }

        this.setState({
            [attr]: value,
            saveUserDisabled: disableSaveBtn,
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
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'config', 'replace'
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
                cmd.push(attr + "=" + val);
            }
        }

        log_cmd("saveExp", "Saving Expiration pwpolicy settings", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadGlobal();
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
                    this.loadGlobal();
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
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'config', 'replace'
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
                cmd.push(attr + "=" + val);
            }
        }

        log_cmd("saveLockout", "Saving lockout pwpolicy settings", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadGlobal();
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
                    this.loadGlobal();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error updating password policy configuration - ${errMsg.desc}`
                    );
                });
    }

    handleSyntaxChange = (e, selection, isPlaceholder) => {
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
        for (let syntax_attr of syntax_attrs) {
            if (syntax_attr == 'passworduserattributes' && attr == 'passworduserattributes') {
                let orig_val = this.state['_' + syntax_attr].join(' ');
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
        if (selection) {
            if (this.state[attr].includes(selection)) {
                this.setState(
                    (prevState) => ({
                        [attr]: prevState[attr].filter((item) => item !== selection),
                        isSelectOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        [attr]: [...prevState[attr], selection],
                        saveSyntaxDisabled: disableSaveBtn,
                        isSelectOpen: false

                    }),
                );
            }
        } else {
            this.setState({
                [attr]: value,
                saveSyntaxDisabled: disableSaveBtn,
                isSelectOpen: false
            });
        }
    }

    saveSyntax() {
        this.setState({
            loading: true
        });

        let cmd = [
            'dsconf', '-j', "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            'config', 'replace'
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
                cmd.push(attr + "=" + val);
            }
        }

        log_cmd("saveSyntax", "Saving syntax checking pwpolicy settings", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.loadGlobal();
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
                    this.loadGlobal();
                    this.setState({
                        loading: false
                    });
                    this.props.addNotification(
                        "error",
                        `Error updating password policy configuration - ${errMsg.desc}`
                    );
                });
    }

    loadGlobal() {
        this.setState({
            loading: true
        });
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "config", "get"
        ];
        log_cmd("loadGlobal", "Load global password policy", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    let config = JSON.parse(content);
                    let attrs = config.attrs;
                    // Handle the checkbox values
                    let pwpLocal = false;
                    let pwIsGlobal = false;
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
                    let pwAllowHashed = false;
                    let pwInheritGlobal = false;
                    let pwUserAttrs = [];

                    if (attrs['nsslapd-pwpolicy-local'][0] == "on") {
                        pwpLocal = true;
                    }
                    if (attrs['passwordchange'][0] == "on") {
                        pwChange = true;
                    }
                    if (attrs['passwordmustchange'][0] == "on") {
                        pwMustChange = true;
                    }
                    if (attrs['passwordhistory'][0] == "on") {
                        pwHistory = true;
                    }
                    if (attrs['passwordtrackupdatetime'][0] == "on") {
                        pwTrackUpdate = true;
                    }
                    if (attrs['passwordisglobalpolicy'][0] == "on") {
                        pwIsGlobal = true;
                    }
                    if (attrs['passwordsendexpiringtime'][0] == "on") {
                        pwSendExpire = true;
                    }
                    if (attrs['passwordlockout'][0] == "on") {
                        pwLockout = true;
                    }
                    if (attrs['passwordunlock'][0] == "on") {
                        pwUnlock = true;
                    }
                    if (attrs['passwordexp'][0] == "on") {
                        pwExpire = true;
                    }
                    if (attrs['passwordchecksyntax'][0] == "on") {
                        pwCheckSyntax = true;
                    }
                    if (attrs['passwordpalindrome'][0] == "on") {
                        pwPalindrome = true;
                    }
                    if (attrs['passworddictcheck'][0] == "on") {
                        pwExpire = true;
                    }
                    if (attrs['nsslapd-allow-hashed-passwords'][0] == "on") {
                        pwAllowHashed = true;
                    }
                    if (attrs['nsslapd-pwpolicy-inherit-global'][0] == "on") {
                        pwInheritGlobal = true;
                    }
                    if (attrs['passwordbadwords'][0] != "") {
                        // Hack until this is fixed: https://github.com/389ds/389-ds-base/issues/3928
                        if (attrs['passwordbadwords'].length > 1) {
                            attrs['passwordbadwords'][0] = attrs['passwordbadwords'].join(' ');
                        }
                    }
                    if (attrs['passworduserattributes'][0] != "") {
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
                            loaded: true,
                            loading: false,
                            saveGeneralDisabled: true,
                            saveUserDisabled: true,
                            saveExpDisabled: true,
                            saveLockoutDisabled: true,
                            saveSyntaxDisabled: true,
                            // Settings
                            'nsslapd-pwpolicy-local': pwpLocal,
                            passwordisglobalpolicy: pwIsGlobal,
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
                            'nsslapd-allow-hashed-passwords': pwAllowHashed,
                            'nsslapd-pwpolicy-inherit-global': pwInheritGlobal,
                            passwordstoragescheme: attrs['passwordstoragescheme'][0],
                            passwordinhistory: attrs['passwordinhistory'][0],
                            passwordwarning: attrs['passwordwarning'][0],
                            passwordmaxage: attrs['passwordmaxage'][0],
                            passwordminage: attrs['passwordminage'][0],
                            passwordgracelimit: attrs['passwordgracelimit'][0],
                            passwordlockoutduration: attrs['passwordlockoutduration'][0],
                            passwordmaxfailure: attrs['passwordmaxfailure'][0],
                            passwordresetfailurecount: attrs['passwordresetfailurecount'][0],
                            passwordminlength: attrs['passwordminlength'][0],
                            passwordmindigits: attrs['passwordmindigits'][0],
                            passwordminalphas: attrs['passwordminalphas'][0],
                            passwordminuppers: attrs['passwordminuppers'][0],
                            passwordminlowers: attrs['passwordminlowers'][0],
                            passwordminspecials: attrs['passwordminspecials'][0],
                            passwordmin8bit: attrs['passwordmin8bit'][0],
                            passwordmaxrepeats: attrs['passwordmaxrepeats'][0],
                            passwordmaxsequence: attrs['passwordmaxsequence'][0],
                            passwordmaxseqsets: attrs['passwordmaxseqsets'][0],
                            passwordmaxclasschars: attrs['passwordmaxclasschars'][0],
                            passwordmincategories: attrs['passwordmincategories'][0],
                            passwordmintokenlength: attrs['passwordmintokenlength'][0],
                            passwordbadwords: attrs['passwordbadwords'][0],
                            passworduserattributes: pwUserAttrs,
                            passwordadmindn: attrs['passwordadmindn'][0],
                            // Record original values
                            '_nsslapd-pwpolicy-local': pwpLocal,
                            _passwordisglobalpolicy: pwIsGlobal,
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
                            '_nsslapd-allow-hashed-passwords': pwAllowHashed,
                            '_nsslapd-pwpolicy-inherit-global': pwInheritGlobal,
                            _passwordstoragescheme: attrs['passwordstoragescheme'][0],
                            _passwordinhistory: attrs['passwordinhistory'][0],
                            _passwordwarning: attrs['passwordwarning'][0],
                            _passwordmaxage: attrs['passwordmaxage'][0],
                            _passwordminage: attrs['passwordminage'][0],
                            _passwordgracelimit: attrs['passwordgracelimit'][0],
                            _passwordlockoutduration: attrs['passwordlockoutduration'][0],
                            _passwordmaxfailure: attrs['passwordmaxfailure'][0],
                            _passwordresetfailurecount: attrs['passwordresetfailurecount'][0],
                            _passwordminlength: attrs['passwordminlength'][0],
                            _passwordmindigits: attrs['passwordmindigits'][0],
                            _passwordminalphas: attrs['passwordminalphas'][0],
                            _passwordminuppers: attrs['passwordminuppers'][0],
                            _passwordminlowers: attrs['passwordminlowers'][0],
                            _passwordminspecials: attrs['passwordminspecials'][0],
                            _passwordmin8bit: attrs['passwordmin8bit'][0],
                            _passwordmaxrepeats: attrs['passwordmaxrepeats'][0],
                            _passwordmaxsequence: attrs['passwordmaxsequence'][0],
                            _passwordmaxseqsets: attrs['passwordmaxseqsets'][0],
                            _passwordmaxclasschars: attrs['passwordmaxclasschars'][0],
                            _passwordmincategories: attrs['passwordmincategories'][0],
                            _passwordmintokenlength: attrs['passwordmintokenlength'][0],
                            _passwordbadwords: attrs['passwordbadwords'][0],
                            _passworduserattributes: pwUserAttrs,
                            _passwordadmindn: attrs['passwordadmindn'][0],
                        }), this.props.enableTree()
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
                        `Error loading global password policy - ${errMsg.desc}`
                    );
                });
    }

    onSelectToggle = isSelectOpen => {
        this.setState({
            isSelectOpen
        });
    }

    onSelectClear = () => {
        this.setState({
            passworduserattributes: [],
            isSelectOpen: false
        });
    }

    render() {
        let pwp_element = "";
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
                        </Col>
                    </Row>
                    <Row className="ds-margin-top-lg" title="Check the password against the system's CrackLib dictionary (passwordDictCheck).">
                        <Col componentClass={ControlLabel} sm={3}>
                            <Checkbox
                                id="passworddictcheck"
                                isChecked={this.state.passworddictcheck}
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                                label="Dictionary Check"
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top" title="Reject a password if it is a palindrome (passwordPalindrome).">
                        <Col componentClass={ControlLabel} sm={3}>
                            <Checkbox
                                id="passwordpalindrome"
                                isChecked={this.state.passwordpalindrome}
                                onChange={(checked, e) => {
                                    this.handleSyntaxChange(e);
                                }}
                                label="Reject Palindromes"
                            />
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
                        <Col componentClass={ControlLabel} sm={5}>
                            <Checkbox
                                id="passwordunlock"
                                isChecked={this.state.passwordunlock}
                                onChange={(checked, e) => {
                                    this.handleLockoutChange(e);
                                }}
                                label="Do Not Lockout Account Forever"
                            />
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
                                isChecked={this.state.passwordsendexpiringtime}
                                onChange={(checked, e) => {
                                    this.handleExpChange(e);
                                }}
                                label={<>Always Send <i>Password Expiring</i>&nbsp; Control</>}
                            />
                        </Col>
                    </Row>
                </div>;
        }

        if (this.state.loading || !this.state.loaded) {
            pwp_element =
                <div className="ds-margin-top-xlg ds-center">
                    <Spinner isSVG size="xl" />
                </div>;
        } else {
            pwp_element =
                <div>
                    <div className={this.state.loading ? 'ds-fadeout' : 'ds-fadein ds-margin-left'}>
                        <TabContainer id="server-tabs-pf" onSelect={this.handleNavSelect} activeKey={this.state.activeKey}>
                            <div className="ds-margin-top">
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
                                            <Row className="ds-margin-top-lg" title="Allow subtree/user defined local password policies (nsslapd-pwpolicy-local).">
                                                <Col sm={11}>
                                                    <Checkbox
                                                        id="nsslapd-pwpolicy-local"
                                                        isChecked={this.state['nsslapd-pwpolicy-local']}
                                                        onChange={(checked, e) => {
                                                            this.handleGeneralChange(e);
                                                        }}
                                                        label="Allow Local Password Policies"
                                                    />
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top" title="If a local password policy does not defined any syntax rules then inherit the local policy syntax (nsslapd-pwpolicy-inherit-global).">
                                                <Col sm={11}>
                                                    <Checkbox
                                                        id="nsslapd-pwpolicy-inherit-global"
                                                        isChecked={this.state["nsslapd-pwpolicy-inherit-global"]}
                                                        onChange={(checked, e) => {
                                                            this.handleGeneralChange(e);
                                                        }}
                                                        label="Local Policies Inherit Global Policy"
                                                    />
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top" title="Allow anyone to add a prehashed password (nsslapd-allow-hashed-passwords).">
                                                <Col sm={11}>
                                                    <Checkbox
                                                        id="nsslapd-allow-hashed-passwords"
                                                        isChecked={this.state["nsslapd-allow-hashed-passwords"]}
                                                        onChange={(checked, e) => {
                                                            this.handleGeneralChange(e);
                                                        }}
                                                        label="Allow Adding Pre-Hashed Passwords"
                                                    />
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top" title="Allow password policy state attributes to replicate (passwordIsGlobalPolicy).">
                                                <Col sm={11}>
                                                    <Checkbox
                                                        id="passwordisglobalpolicy"
                                                        isChecked={this.state.passwordisglobalpolicy}
                                                        onChange={(checked, e) => {
                                                            this.handleGeneralChange(e);
                                                        }}
                                                        label="Replicate Password Policy State Attributes"
                                                    />
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top" title="Record a separate timestamp specifically for the last time that the password for an entry was changed. If this is enabled, then it adds the pwdUpdateTime operational attribute to the user account entry (passwordTrackUpdateTime).">
                                                <Col sm={11}>
                                                    <Checkbox
                                                        id="passwordtrackupdatetime"
                                                        isChecked={this.state.passwordtrackupdatetime}
                                                        onChange={(checked, e) => {
                                                            this.handleGeneralChange(e);
                                                        }}
                                                        label="Track Password Update Time"
                                                    />
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top" title="Allow user's to change their passwords (passwordChange).">
                                                <Col sm={11}>
                                                    <Checkbox
                                                        id="passwordchange"
                                                        isChecked={this.state.passwordchange}
                                                        onChange={(checked, e) => {
                                                            this.handleGeneralChange(e);
                                                        }}
                                                        label="Allow Users To Change Their Passwords"
                                                    />
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top" title="User must change its password after its been reset by an administrator (passwordMustChange).">
                                                <Col sm={11}>
                                                    <Checkbox
                                                        id="passwordmustchange"
                                                        isChecked={this.state.passwordmustchange}
                                                        onChange={(checked, e) => {
                                                            this.handleGeneralChange(e);
                                                        }}
                                                        label="User Must Change Password After Reset"
                                                    />
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top" title="Maintain a password history for each user (passwordHistory).">
                                                <Col sm={11}>
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
                                            <Row className="ds-margin-top-lg" title="Indicates the number of seconds that must pass before a user can change their password again. (passwordMinAge)">
                                                <Col sm={3}>
                                                    <ControlLabel>
                                                        Password Minimum Age
                                                    </ControlLabel>
                                                </Col>
                                                <Col sm={2}>
                                                    <FormControl
                                                        id="passwordminage"
                                                        type="number"
                                                        min="0"
                                                        max="2147483647"
                                                        value={this.state.passwordminage}
                                                        onChange={this.handleGeneralChange}
                                                    />
                                                </Col>
                                            </Row>
                                            <Row className="ds-margin-top-lg" title="The DN for a password administrator or administrator group (passwordAdminDN).">
                                                <Col sm={3}>
                                                    <ControlLabel>
                                                        Password Administrator
                                                    </ControlLabel>
                                                </Col>
                                                <Col sm={7}>
                                                    <FormControl
                                                        id="passwordadmindn"
                                                        type="text"
                                                        value={this.state.passwordadmindn}
                                                        onChange={this.handleGeneralChange}
                                                    />
                                                </Col>
                                            </Row>
                                            <Button
                                                disabled={this.state.saveGeneralDisabled}
                                                variant="primary"
                                                className="ds-margin-top-lg"
                                                onClick={this.saveGeneral}
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
                                                        isChecked={this.state.passwordexp}
                                                        onChange={(checked, e) => {
                                                            this.handleExpChange(e);
                                                        }}
                                                        label="Enforce Password Expiration"
                                                    />
                                                </Col>
                                            </Row>
                                            {pwExpirationRows}
                                            <Button
                                                disabled={this.state.saveExpDisabled}
                                                variant="primary"
                                                className="ds-margin-top-lg ds-margin-left"
                                                onClick={this.saveExp}
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
                                                        isChecked={this.state.passwordlockout}
                                                        onChange={(checked, e) => {
                                                            this.handleLockoutChange(e);
                                                        }}
                                                        label="Enable Account Lockout"
                                                    />
                                                </Col>
                                            </Row>
                                            {pwLockoutRows}
                                            <Button
                                                disabled={this.state.saveLockoutDisabled}
                                                variant="primary"
                                                className="ds-margin-top-lg ds-margin-left"
                                                onClick={this.saveLockout}
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
                                                        isChecked={this.state.passwordchecksyntax}
                                                        onChange={(checked, e) => {
                                                            this.handleSyntaxChange(e);
                                                        }}
                                                        label="Enable Password Syntax Checking"
                                                    />
                                                </Col>
                                            </Row>
                                            {pwSyntaxRows}
                                            <Button
                                                disabled={this.state.saveSyntaxDisabled}
                                                variant="primary"
                                                className="ds-margin-top-lg ds-margin-left"
                                                onClick={this.saveSyntax}
                                            >
                                                Save
                                            </Button>
                                        </Form>
                                    </TabPane>
                                </TabContent>
                            </div>
                        </TabContainer>
                    </div>
                </div>;
        }

        return (
            <div>
                <Row>
                    <Col sm={5}>
                        <ControlLabel className="ds-suffix-header ds-margin-top-lg ds-margin-left-sm">
                            Global Password Policy
                            <Icon className="ds-left-margin ds-refresh"
                                type="fa" name="refresh" title="Refresh global password policy settings"
                                onClick={this.loadGlobal}
                            />
                        </ControlLabel>
                    </Col>
                </Row>
                {pwp_element}
            </div>
        );
    }
}

GlobalPwPolicy.propTypes = {
    attrs: PropTypes.array,
};

GlobalPwPolicy.defaultProps = {
    attrs: [],
};
