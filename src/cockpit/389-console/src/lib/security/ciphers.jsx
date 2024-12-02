import React from "react";
import cockpit from "cockpit";
import {
	Button,
	Form,
	FormSelect,
	FormSelectOption,
	FormHelperText,
	Grid,
	GridItem,
	SimpleList,
	SimpleListItem,
	Spinner,
	Text,
	TextContent,
	TextVariants
} from '@patternfly/react-core';
import {
	Select,
	SelectVariant,
	SelectOption
} from '@patternfly/react-core/deprecated';
import { log_cmd } from "../../lib/tools.jsx";
import PropTypes from "prop-types";

const _ = cockpit.gettext;

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
            // Select Typeahead
            isAllowCipherOpen: false,
            isDenyCipherOpen: false,
            disableSaveBtn: true,
        };

        // Allow Ciphers
        this.handleAllowCipherToggle = (_event, isAllowCipherOpen) => {
            this.setState({
                isAllowCipherOpen
            });
        };
        this.handleAllowCipherClear = () => {
            this.setState({
                allowCiphers: [],
                isAllowCipherOpen: false
            });
        };

        this.handleDenyCipherToggle = (_event, isDenyCipherOpen) => {
            this.setState({
                isDenyCipherOpen
            });
        };
        this.handleDenyCipherClear = () => {
            this.setState({
                denyCiphers: [],
                isDenyCipherOpen: false
            });
        };

        this.handlePrefChange = this.handlePrefChange.bind(this);
        this.saveCipherPref = this.saveCipherPref.bind(this);
        this.handleAllowCipherChange = this.handleAllowCipherChange.bind(this);
        this.handleDenyCipherChange = this.handleDenyCipherChange.bind(this);
    }

    componentDidMount () {
        let cipherPref = "default";
        const allowedCiphers = [];
        const deniedCiphers = [];
        const availableCiphers = this.props.supportedCiphers.slice(0); // Copy array

        // Parse SSL cipher attributes (nsSSL3Ciphers)
        if (this.props.cipherPref !== "") {
            let rawCiphers = this.props.cipherPref.split(",");

            // First check the first element as it has special meaning
            if (rawCiphers[0].toLowerCase() === "default") {
                rawCiphers.shift();
            } else if (rawCiphers[0].toLowerCase() === "+all") {
                cipherPref = "+all";
                rawCiphers.shift();
            } else if (rawCiphers[0].toLowerCase() === "-all") {
                cipherPref = "-all";
                rawCiphers.shift();
            }

            // Process the remaining ciphers
            rawCiphers = rawCiphers.map(function(x) { return x.toUpperCase() });
            for (const cipher of rawCiphers) {
                if (cipher.startsWith("+")) {
                    allowedCiphers.push(cipher.substring(1));
                } else if (cipher.startsWith("-")) {
                    deniedCiphers.push(cipher.substring(1));
                }
            }

            // Remove all enabled ciphers from the list of available ciphers
            for (const enabled_cipher of this.props.enabledCiphers) {
                if (availableCiphers.includes(enabled_cipher)) {
                    // Remove val from availableCiphers
                    const index = availableCiphers.indexOf(enabled_cipher);
                    if (index > -1) {
                        availableCiphers.splice(index, 1);
                    }
                }
            }
            // Remove allowed ciphers from the list of available ciphers
            for (const allow_cipher of allowedCiphers) {
                if (availableCiphers.includes(allow_cipher)) {
                    // Remove val from availableCiphers
                    const index = availableCiphers.indexOf(allow_cipher);
                    if (index > -1) {
                        availableCiphers.splice(index, 1);
                    }
                }
            }
            // Remove denied ciphers from the list of available ciphers
            for (const deny_cipher of deniedCiphers) {
                if (availableCiphers.includes(deny_cipher)) {
                    // Remove val from availableCiphers
                    const index = availableCiphers.indexOf(deny_cipher);
                    if (index > -1) {
                        availableCiphers.splice(index, 1);
                    }
                }
            }
        }

        this.setState({
            cipherPref,
            _cipherPref: cipherPref,
            allowCiphers: allowedCiphers,
            _allowCiphers: [...allowedCiphers],
            denyCiphers: deniedCiphers,
            _denyCiphers: [...deniedCiphers],
            availableCiphers,
        });
    }

    saveCipherPref () {
        /* start the spinner */
        this.setState({
            saving: true
        });
        let prefs = this.state.cipherPref;
        if (this.state.cipherPref !== "default") {
            for (const cipher of this.state.allowCiphers) {
                prefs += ",+" + cipher;
            }
            for (const cipher of this.state.denyCiphers) {
                prefs += ",-" + cipher;
            }
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
                        _("Successfully set cipher preferences.")
                    );
                    this.props.addNotification(
                        "warning",
                        _("You must restart the Directory Server for these changes to take effect.")
                    );
                    this.setState({
                        saving: false,
                        disableSaveBtn: true,
                    });
                    this.props.reload();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Error setting cipher preferences - $0"), msg)
                    );
                    this.setState({
                        saving: false,
                        disableSaveBtn: true,
                    });
                });
    }

    handlePrefChange (val) {
        let disableSaveBtn = true;

        if (JSON.stringify(this.state.allowCiphers) !== JSON.stringify(this.state._allowCiphers) ||
            JSON.stringify(this.state.denyCiphers) !== JSON.stringify(this.state._denyCiphers)) {
            disableSaveBtn = false;
        }
        if (this.state._cipherPref !== val) {
            disableSaveBtn = false;
        }

        this.setState({
            cipherPref: val,
            disableSaveBtn,
        });
    }

    handleAllowCipherChange(e, selection) {
        let disableSaveBtn = true;
        const availableCiphers = [...this.state.availableCiphers];

        if (this.state.cipherPref !== this.state._cipherPref) {
            disableSaveBtn = false;
        }

        if (this.state.allowCiphers.includes(selection)) {
            // Removing cipher from list
            availableCiphers.push(selection);
            availableCiphers.sort();

            // Buld a list of of waht the new cipher list will be, so we can
            // check if the value changed and the save button can be enabled/disabled
            const copy_new_ciphers = [...this.state.allowCiphers];
            const index = copy_new_ciphers.indexOf(selection);
            if (index > -1) {
                copy_new_ciphers.splice(index, 1);
            }
            if (JSON.stringify(this.state._allowCiphers) !== JSON.stringify(copy_new_ciphers)) {
                disableSaveBtn = false;
            }

            this.setState(
                (prevState) => ({
                    allowCiphers: prevState.allowCiphers.filter((item) => item !== selection),
                    isAllowCipherOpen: false,
                    availableCiphers,
                    disableSaveBtn,
                }),
            );
        } else {
            // Adding cipher, but first remove cipher from availableCiphers
            const index = availableCiphers.indexOf(selection);
            if (index > -1) {
                availableCiphers.splice(index, 1);
                availableCiphers.sort();
            }
            if (JSON.stringify(this.state._allowCiphers) !== JSON.stringify([...this.state.allowCiphers, selection])) {
                disableSaveBtn = false;
            }
            this.setState(
                (prevState) => ({
                    allowCiphers: [...prevState.allowCiphers, selection],
                    isAllowCipherOpen: false,
                    availableCiphers,
                    disableSaveBtn,
                }),
            );
        }
    }

    handleDenyCipherChange(e, selection) {
        let disableSaveBtn = true;
        const availableCiphers = [...this.state.availableCiphers];

        if (this.state.cipherPref !== this.state._cipherPref) {
            disableSaveBtn = false;
        }

        if (this.state.denyCiphers.includes(selection)) {
            // Removing cipher from list
            availableCiphers.push(selection);
            availableCiphers.sort();

            // Buld a list of of waht the new cipher list will be, so we can
            // check if the value changed and the save button can be enabled/disabled
            const copy_new_ciphers = [...this.state.denyCiphers];
            const index = copy_new_ciphers.indexOf(selection);
            if (index > -1) {
                copy_new_ciphers.splice(index, 1);
            }
            if (JSON.stringify(this.state._denyCiphers) !== JSON.stringify(copy_new_ciphers)) {
                disableSaveBtn = false;
            }

            this.setState(
                (prevState) => ({
                    denyCiphers: prevState.denyCiphers.filter((item) => item !== selection),
                    isDenyCipherOpen: false,
                    availableCiphers,
                    disableSaveBtn,
                }),
            );
        } else {
            // Adding cipher, but first remove cipher from availableCiphers
            const index = availableCiphers.indexOf(selection);
            if (index > -1) {
                availableCiphers.splice(index, 1);
                availableCiphers.sort();
            }
            if (JSON.stringify(this.state._denyCiphers) !== JSON.stringify([...this.state.denyCiphers, selection])) {
                disableSaveBtn = false;
            }

            this.setState(
                (prevState) => ({
                    denyCiphers: [...prevState.denyCiphers, selection],
                    isDenyCipherOpen: false,
                    availableCiphers,
                    disableSaveBtn,
                }),
            );
        }
    }

    render () {
        const supportedCiphers = [];
        const enabledCiphers = [];
        let cipherPage;
        let saveBtnName = _("Save Settings");
        const extraPrimaryProps = {};
        if (this.state.saving) {
            saveBtnName = _("Saving settings ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        for (const cipher of this.props.supportedCiphers) {
            if (!this.props.enabledCiphers.includes(cipher)) {
                // This cipher is not currently enabled, so list it as available
                supportedCiphers.push(cipher);
            }
        }
        for (const cipher of this.props.enabledCiphers) {
            enabledCiphers.push(cipher);
        }
        let supportedList = supportedCiphers.map((name) =>
            <SimpleListItem key={name}>{name}</SimpleListItem>
        );
        if (supportedList.length === 0) {
            supportedList = "";
        }
        let enabledList = enabledCiphers.map((name) =>
            <SimpleListItem key={name}>{name}</SimpleListItem>
        );
        if (enabledList.length === 0) {
            enabledList = "";
        }

        if (this.state.saving) {
            cipherPage = (
                <div className="ds-center ds-margin-top-lg">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            {_("Saving Cipher Preferences ...")}
                        </Text>
                    </TextContent>
                    <Spinner size="lg" />
                </div>
            );
        } else {
            cipherPage = (
                <div className="ds-indent">
                    <Form className="ds-margin-top-lg" isHorizontal>
                        <Grid>
                            <GridItem span={5} title={_("The current ciphers the server is accepting.  This list is only updated after a server restart.")}>
                                <TextContent>
                                    <Text component={TextVariants.h3}>
                                        {_("Enabled Ciphers")} <font size="2">({enabledList.length})</font>
                                    </Text>
                                </TextContent>
                                <hr />
                                <div className="ds-box">
                                    <SimpleList aria-label={_("enabled cipher list")}>
                                        {enabledList}
                                    </SimpleList>
                                </div>
                            </GridItem>
                            <GridItem span={2} />
                            <GridItem span={5} title={_("The current ciphers the server supports")}>
                                <TextContent>
                                    <Text component={TextVariants.h3}>
                                        {_("Supported Ciphers")} <font size="2">({supportedList.length})</font>
                                    </Text>
                                </TextContent>
                                <hr />
                                <div className="ds-box">
                                    <SimpleList aria-label="supported cipher list">
                                        {supportedList}
                                    </SimpleList>
                                </div>
                            </GridItem>
                        </Grid>
                        <hr />
                        <Grid>
                            <GridItem className="ds-label" span={2}>
                                {_("Cipher Suite")}
                            </GridItem>
                            <GridItem span={10}>
                                <FormSelect
                                    id="cipherPref"
                                    value={this.state.cipherPref}
                                    onChange={(_event, val) => this.handlePrefChange(val)}
                                    aria-label="pref select"
                                >
                                    <FormSelectOption key="1" value="default" label={_("Default Ciphers")} />
                                    <FormSelectOption key="2" value="+all" label={_("All Ciphers")} />
                                    <FormSelectOption key="3" value="-all" label={_("No Ciphers")} />
                                </FormSelect>
                                <FormHelperText >
                                    {_("Default cipher suite is chosen. It enables the default ciphers advertised by NSS except weak ciphers.")}{(this.state.allowCiphers.length !== 0 || this.state.denyCiphers.length !== 0) ? _(" Any data in the 'Allow Specific Ciphers' and 'Deny Specific Ciphers' fields will be cleaned after the restart.") : ""}
                                </FormHelperText>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={2}>
                                {_("Allow Specific Ciphers")}
                            </GridItem>
                            <GridItem span={10}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel={_("Type a cipher")}
                                    isDisabled={this.state.cipherPref === "default"}
                                    onToggle={(event, isOpen) => this.handleAllowCipherToggle(event, isOpen)}
                                    onSelect={this.handleAllowCipherChange}
                                    onClear={this.handleAllowCipherClear}
                                    selections={this.state.allowCiphers}
                                    isOpen={this.state.isAllowCipherOpen}
                                    aria-labelledby="typeAhead-allow-cipher"
                                    placeholderText={_("Type a cipher...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    maxHeight="200px"
                                >
                                    {this.state.availableCiphers.map((cipher, index) => (
                                        <SelectOption
                                            key={index}
                                            value={cipher}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={2}>
                                {_("Deny Specific Ciphers")}
                            </GridItem>
                            <GridItem span={10}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel={_("Type a cipher")}
                                    isDisabled={this.state.cipherPref === "default"}
                                    onToggle={(event, isOpen) => this.handleDenyCipherToggle(event, isOpen)}
                                    onSelect={this.handleDenyCipherChange}
                                    onClear={this.handleDenyCipherClear}
                                    selections={this.state.denyCiphers}
                                    isOpen={this.state.isDenyCipherOpen}
                                    aria-labelledby="typeAhead-allow-deny"
                                    placeholderText={_("Type a cipher...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    maxHeight="200px"
                                >
                                    {this.state.availableCiphers.map((cipher, index) => (
                                        <SelectOption
                                            key={index}
                                            value={cipher}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                    </Form>
                    <Button
                        variant="primary"
                        className="ds-margin-top-xlg"
                        onClick={() => {
                            this.saveCipherPref();
                        }}
                        isDisabled={this.state.disableSaveBtn || this.state.saving}
                        isLoading={this.state.saving}
                        spinnerAriaValueText={this.state.saving ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>
                </div>
            );
        }

        return (
            <div className={this.state.saving ? "ds-disabled" : ""}>
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
};

export default Ciphers;
