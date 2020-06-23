import React from "react";
import cockpit from "cockpit";
import {
    Button,
    Row,
    Col,
    ControlLabel,
    Spinner,
    noop,
} from "patternfly-react";
import { log_cmd } from "../../lib/tools.jsx";
import PropTypes from "prop-types";
import "../../css/ds.css";
import { Typeahead } from "react-bootstrap-typeahead";

export class Ciphers extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            allowCiphers: [],
            denyCiphers: [],
            cipherPref: "default",
            prefs: this.props.cipherPref,
            saving: false,
            availableCiphers: this.props.supportedCiphers,
        };

        this.handlePrefChange = this.handlePrefChange.bind(this);
        this.saveCipherPref = this.saveCipherPref.bind(this);
        this.handleCipherChange = this.handleCipherChange.bind(this);
    }

    componentDidMount () {
        let cipherPref = "default";
        let allowedCiphers = [];
        let deniedCiphers = [];
        let availableCiphers = this.props.supportedCiphers.slice(0); // Copy array

        // Parse SSL cipher attributes (nsSSL3Ciphers)
        if (this.props.cipherPref != "") {
            let rawCiphers = this.props.cipherPref.split(",");

            // First check the first element as it has special meaning
            if (rawCiphers[0].toLowerCase() == "default") {
                rawCiphers.shift();
            } else if (rawCiphers[0].toLowerCase() == "+all") {
                cipherPref = "+all";
                rawCiphers.shift();
            } else if (rawCiphers[0].toLowerCase() == "-all") {
                cipherPref = "-all";
                rawCiphers.shift();
            }

            // Process the remaining ciphers
            rawCiphers = rawCiphers.map(function(x) { return x.toUpperCase() });
            for (let cipher of rawCiphers) {
                if (cipher.startsWith("+")) {
                    allowedCiphers.push(cipher.substring(1));
                } else if (cipher.startsWith("-")) {
                    deniedCiphers.push(cipher.substring(1));
                }
            }

            // Now modify the available list
            for (let i = 0; i < availableCiphers.length; i++) {
                for (let ii = 0; ii < allowedCiphers.length; ii++) {
                    if (availableCiphers[i] === allowedCiphers[ii]) {
                        availableCiphers.splice(i, 1);
                        i--;
                        break;
                    }
                }
                for (let ii = 0; ii < deniedCiphers.length; ii++) {
                    if (availableCiphers[i] === deniedCiphers[ii]) {
                        availableCiphers.splice(i, 1);
                        i--;
                        break;
                    }
                }
            }
        }

        this.setState({
            cipherPref: cipherPref,
            allowCiphers: allowedCiphers,
            denyCiphers: deniedCiphers,
            availableCiphers: availableCiphers,
        });
    }

    saveCipherPref () {
        /* start the spinner */
        this.setState({
            saving: true
        });
        let prefs = this.state.cipherPref;
        for (let cipher of this.state.allowCiphers) {
            prefs += ",+" + cipher;
        }
        for (let cipher of this.state.denyCiphers) {
            prefs += ",-" + cipher;
        }

        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "security", "ciphers", "set", "--", prefs
        ];
        log_cmd("saveCipherPref", "Saving cipher preferences", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(() => {
                    this.props.addNotification(
                        "success",
                        `Successfully set cipher preferences.`
                    );
                    this.props.addNotification(
                        "warning",
                        `You must restart the Directory Server for these changes to take effect.`
                    );
                    this.setState({
                        saving: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        `Error setting cipher preferences - ${msg}`
                    );
                    this.setState({
                        saving: false,
                    });
                });
    }

    handlePrefChange (e) {
        this.setState({
            cipherPref: e.target.value,
        });
    }

    handleCipherChange (type, values) {
        let availableCiphers = this.state.availableCiphers.slice(0); // Copy array
        for (let i = 0; i < availableCiphers.length; i++) {
            for (let ci = 0; ci < values.length; ci++) {
                if (availableCiphers[i] === values[ci]) {
                    availableCiphers.splice(i, 1);
                    i--;
                    break;
                }
            }
        }
        if (type == "allow") {
            this.setState({
                allowCiphers: values,
                availableCiphers: availableCiphers
            });
        } else {
            this.setState({
                denyCiphers: values,
                availableCiphers: availableCiphers
            });
        }
    }

    render () {
        let supportedCiphers = [];
        let enabledCiphers = [];
        let cipherPage;

        for (let cipher of this.props.supportedCiphers) {
            if (!this.props.enabledCiphers.includes(cipher)) {
                // This cipher is not currently enabled, so list it as available
                supportedCiphers.push(cipher);
            }
        }
        for (let cipher of this.props.enabledCiphers) {
            enabledCiphers.push(cipher);
        }
        let supportedList = supportedCiphers.map((name) =>
            <option key={name}>{name}</option>
        );
        let enabledList = enabledCiphers.map((name) =>
            <option key={name}>{name}</option>
        );

        let eCiphers = '<h4>Enabled Ciphers <font size="2">(' + enabledList.length + ')</font></h4>';
        let sCiphers = '<h4>Other Available Ciphers <font size="2">(' + supportedList.length + ')</font><h4>';

        if (this.state.saving) {
            cipherPage =
                <div className="ds-center ds-margin-top-lg">
                    <h4>Saving cipher preferences ...</h4>
                    <Spinner loading size="md" />
                </div>;
        } else {
            cipherPage =
                <div>
                    <div className="ds-container">
                        <div className='ds-inline'>
                            <div dangerouslySetInnerHTML={{__html: eCiphers}} />
                            <div>
                                <select
                                    className="ds-cipher-width"
                                    size="16"
                                    title="The current ciphers the server is accepting.  This is only updated after a server restart"
                                >
                                    {enabledList}
                                </select>
                            </div>
                        </div>
                        <div className="ds-divider-lrg" />
                        <div className='ds-inline'>
                            <div>
                                <div dangerouslySetInnerHTML={{__html: sCiphers}} />
                            </div>
                            <div>
                                <select className="ds-cipher-width" size="16">
                                    {supportedList}
                                </select>
                            </div>
                        </div>
                    </div>
                    <hr />
                    <Row>
                        <Col componentClass={ControlLabel} sm={3}>
                            Cipher Suite
                        </Col>
                        <Col sm={9}>
                            <select
                                id="cipherPref"
                                onChange={this.handlePrefChange}
                                defaultValue={this.state.cipherPref}
                            >
                                <option title="default" value="default" key="default">Default Ciphers</option>
                                <option title="+all" value="+all" key="all">All Ciphers</option>
                                <option title="-all" value="-all" key="none">No Ciphers</option>
                            </select>
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Allow Specific Ciphers
                        </Col>
                        <Col sm={9}>
                            <Typeahead
                                multiple
                                onChange={value => {
                                    this.handleCipherChange("allow", value);
                                }}
                                selected={this.state.allowCiphers}
                                options={this.state.availableCiphers}
                                newSelectionPrefix="Add a cipher: "
                                placeholder="Type a cipher..."
                                id="allowCipher"
                            />
                        </Col>
                    </Row>
                    <Row className="ds-margin-top">
                        <Col componentClass={ControlLabel} sm={3}>
                            Deny Specific Ciphers
                        </Col>
                        <Col sm={9}>
                            <Typeahead
                                multiple
                                onChange={value => {
                                    this.handleCipherChange("deny", value);
                                }}
                                selected={this.state.denyCiphers}
                                options={this.state.availableCiphers}
                                newSelectionPrefix="Add a cipher: "
                                placeholder="Type a cipher..."
                                id="denyCipher"
                            />
                        </Col>
                    </Row>
                    <Button
                        bsStyle="primary"
                        className="ds-margin-top-lg"
                        onClick={() => {
                            this.saveCipherPref();
                        }}
                    >
                        Save Cipher Preferences
                    </Button>
                </div>;
        }

        return (
            <div className="container-fluid">
                {cipherPage}
            </div>
        );
    }
}

// Props and defaults

Ciphers.propTypes = {
    serverId: PropTypes.string,
    supportedCiphers: PropTypes.array,
    enabledCiphers: PropTypes.array,
    cipherPref: PropTypes.string,
    addNotification: PropTypes.func,
};

Ciphers.defaultProps = {
    serverId: "",
    supportedCiphers: [],
    enabledCiphers: [],
    cipherPref: "",
    addNotification: noop,
};

export default Ciphers;
