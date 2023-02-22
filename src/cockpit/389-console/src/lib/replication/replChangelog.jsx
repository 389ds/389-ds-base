import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    NumberInput,
    Spinner,
    Text,
    TextContent,
    TextVariants,
    Tooltip,
} from '@patternfly/react-core';
import OutlinedQuestionCircleIcon from '@patternfly/react-icons/dist/js/icons/outlined-question-circle-icon';

export class Changelog extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            loading: false,
            errObj: {},
            showConfirmDelete: false,
            saveBtnDisabled: true,
            // Changelog settings
            clMaxEntries: Number(this.props.clMaxEntries) == 0 ? -1 : Number(this.props.clMaxEntries),
            clMaxAge: Number(this.props.clMaxAge.slice(0, -1)) == 0 ? -1 : Number(this.props.clMaxAge.slice(0, -1)),
            clMaxAgeUnit: this.props.clMaxAge != "" ? this.props.clMaxAge.slice(-1).toLowerCase() : "s",
            clTrimInt: Number(this.props.clTrimInt) == 0 ? -1 : Number(this.props.clTrimInt),
            clEncrypt: this.props.clEncrypt,
            // Preserve original settings
            _clMaxEntries: Number(this.props.clMaxEntries) == 0 ? -1 : Number(this.props.clMaxEntries),
            _clMaxAge: Number(this.props.clMaxAge.slice(0, -1)) == 0 ? -1 : Number(this.props.clMaxAge.slice(0, -1)),
            _clMaxAgeUnit: this.props.clMaxAge != "" ? this.props.clMaxAge.slice(-1).toLowerCase() : "s",
            _clTrimInt: Number(this.props.clTrimInt) == 0 ? -1 : Number(this.props.clTrimInt),
            _clEncrypt: this.props.clEncrypt,
        };

        this.minValue = -1;
        this.maxValue = 20000000;

        this.onMinus = (id) => {
            this.setState({
                [id]: Number(this.state[id]) - 1
            }, () => { this.validateSaveBtn() });
        };

        this.onChange = (event, id) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                [id]: newValue > this.maxValue ? this.maxValue : newValue < this.minValue ? this.minValue : newValue
            }, () => { this.validateSaveBtn() });
        };

        this.onPlus = (id) => {
            this.setState({
                [id]: Number(this.state[id]) + 1
            }, () => { this.validateSaveBtn() });
        };

        this.handleChange = this.handleChange.bind(this);
        this.saveSettings = this.saveSettings.bind(this);
    }

    saveSettings () {
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'set-changelog', '--suffix', this.props.suffix
        ];
        let requires_restart = false;
        let msg = "Successfully updated changelog configuration.";

        if (this.state.clMaxEntries != this.state._clMaxEntries) {
            cmd.push("--max-entries=" + this.state.clMaxEntries);
        }
        if (this.state.clMaxAge != this.state._clMaxAge || this.state.clMaxAgeUnit != this.state._clMaxAgeUnit) {
            cmd.push("--max-age=" + this.state.clMaxAge + this.state.clMaxAgeUnit);
        }
        if (this.state.clTrimInt != this.state._clTrimInt) {
            cmd.push("--trim-interval=" + this.state.clTrimInt);
        }
        if (this.state.clEncrypt != this.state._clEncrypt) {
            if (this.state.clEncrypt) {
                cmd.push("--encrypt");
                msg += "  This requires a server restart to take effect";
                requires_restart = true;
            } else {
                cmd.push("--disable-encrypt");
            }
        }
        if (cmd.length > 6) {
            this.setState({
                // Start the spinner
                saving: true
            });
            log_cmd("saveSettings", "Applying replication changelog changes", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.reloadChangelog();
                        this.props.addNotification(
                            requires_restart ? "warning" : "success",
                            msg
                        );
                        this.setState({
                            saving: false,
                            saveBtnDisabled: true,
                        });
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.reloadChangelog();
                        this.setState({
                            saving: false
                        });
                        let msg = errMsg.desc;
                        if ('info' in errMsg) {
                            msg = errMsg.desc + " - " + errMsg.info;
                        }
                        this.props.addNotification(
                            "error",
                            `Error updating changelog configuration - ${msg}`
                        );
                    });
        }
    }

    validateSaveBtn() {
        let saveBtnDisabled = true;

        if (this.state.clMaxEntries != this.state._clMaxEntries ||
            this.state.clMaxAge != this.state._clMaxAge ||
            this.state.clMaxAgeUnit != this.state._clMaxAgeUnit ||
            this.state.clTrimInt != this.state._clTrimInt ||
            this.state.clEncrypt != this.state._clEncrypt) {
            saveBtnDisabled = false;
        }

        this.setState({
            saveBtnDisabled: saveBtnDisabled,
        });
    }

    handleChange(str, e) {
        // Update the state, then validate the values/save btn
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;

        this.setState({
            [attr]: value,
        }, () => { this.validateSaveBtn() });
    }

    reloadChangelog () {
        this.setState({
            loading: true,
        });
        const cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'get-changelog', '--suffix', this.props.suffix];
        log_cmd("reloadChangelog", "Load the replication changelog info", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const config = JSON.parse(content);
                    let clMaxEntries = "";
                    let clMaxAge = "";
                    let clMaxAgeUnit = "s";
                    let clTrimInt = "";
                    let clEncrypt = false;
                    for (const attr in config.attrs) {
                        const val = config.attrs[attr][0];
                        if (attr == "nsslapd-changelogmaxentries") {
                            clMaxEntries = val;
                        } else if (attr == "nsslapd-changelogmaxage") {
                            clMaxAge = val.slice(0, -1);
                            clMaxAgeUnit = val.slice(-1).toLowerCase();
                        } else if (attr == "nsslapd-changelogtrim-interval") {
                            clTrimInt = val;
                        } else if (attr == "nsslapd-encryptionalgorithm") {
                            clEncrypt = true;
                        }
                    }
                    this.setState({
                        clMaxEntries: Number(clMaxEntries) == 0 ? -1 : Number(clMaxEntries),
                        clMaxAge: Number(clMaxAge) == 0 ? -1 : Number(clMaxAge),
                        clMaxAgeUnit: clMaxAgeUnit,
                        clTrimInt: Number(clTrimInt) == 0 ? -1 : Number(clTrimInt),
                        clEncrypt: clEncrypt,
                        // Preserve original settings
                        _clMaxEntries: Number(clMaxEntries) == 0 ? -1 : Number(clMaxEntries),
                        _clMaxAge: Number(clMaxAge) == 0 ? -1 : Number(clMaxAge),
                        _clMaxAgeUnit: clMaxAgeUnit,
                        _clTrimInt: Number(clTrimInt) == 0 ? -1 : Number(clTrimInt),
                        _clEncrypt: clEncrypt,
                        saveBtnDisabled: true,
                        loading: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to reload changelog for "${this.props.suffix}" - ${errMsg.desc}`
                    );
                    this.setState({
                        loading: false,
                        saveBtnDisabled: true,
                    });
                });
    }

    render() {
        let clPage;
        let saveBtnName = "Save Settings";
        const extraPrimaryProps = {};

        if (this.state.saving) {
            saveBtnName = "Saving settings ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        } else if (this.loading) {
            clPage =
                <div className="ds-margin-top-xlg ds-center">
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Loading Changelog Configuration ...
                        </Text>
                    </TextContent>
                    <Spinner className="ds-margin-top-lg" size="md" />
                </div>;
        } else {
            clPage =
                <div className="ds-margin-top-lg">
                    <Form isHorizontal>
                        <Grid
                            title="Changelog trimming parameter.  Set the maximum number of changelog entries allowed in the database (nsslapd-changelogmaxentries)."
                        >
                            <GridItem className="ds-label" span={3}>
                                Changelog Maximum Entries
                            </GridItem>
                            <GridItem span={2}>
                                <NumberInput
                                    value={this.state.clMaxEntries}
                                    min={this.minValue}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinus("clMaxEntries") }}
                                    onChange={(e) => { this.onChange(e, "clMaxEntries") }}
                                    onPlus={() => { this.onPlus("clMaxEntries") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="Changelog trimming parameter.  This set the maximum age of a changelog entry (nsslapd-changelogmaxage)."
                        >
                            <GridItem className="ds-label" span={3}>
                                Changelog Maximum Age
                            </GridItem>
                            <GridItem span={2}>
                                <NumberInput
                                    value={this.state.clMaxAge}
                                    min={this.minValue}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinus("clMaxAge") }}
                                    onChange={(e) => { this.onChange(e, "clMaxAge") }}
                                    onPlus={() => { this.onPlus("clMaxAge") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem className="ds-margin-left" span={3}>
                                <FormSelect
                                    className="ds-margin-left"
                                    id="clMaxAgeUnit"
                                    value={this.state.clMaxAgeUnit}
                                    onChange={this.handleChange}
                                    aria-label="FormSelect Input"
                                    isDisabled={this.state.clMaxAge < 1}
                                >
                                    <FormSelectOption key="s" value="s" label="Seconds" />
                                    <FormSelectOption key="m" value="m" label="Minutes" />
                                    <FormSelectOption key="h" value="h" label="Hours" />
                                    <FormSelectOption key="d" value="d" label="Days" />
                                    <FormSelectOption key="w" value="w" label="Weeks" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The changelog trimming interval.  Set how often the changelog checks if there are entries that can be purged from the changelog based on the trimming parameters (nsslapd-changelogtrim-interval)."
                        >
                            <GridItem className="ds-label" span={3}>
                                Changelog Trimming Interval
                            </GridItem>
                            <GridItem span={2}>
                                <NumberInput
                                    value={this.state.clTrimInt}
                                    min={this.minValue}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinus("clTrimInt") }}
                                    onChange={(e) => { this.onChange(e, "clTrimInt") }}
                                    onPlus={() => { this.onPlus("clTrimInt") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid>
                            <GridItem className="ds-label" span={3}>
                                Changelog Encryption
                                <Tooltip
                                    id='CLtooltip'
                                    position="bottom"
                                    content={
                                        <div>
                                            Changelog encryption requires that the server must already be
                                            configured for security/TLS.  This setting also requires
                                            that you export and import the changelog which must be done
                                            while the database is in read-only mode.  So first put the
                                            database into read-only mode, then export the changelog, enable
                                            changelog encryption, restart the server, import the changelog,
                                            and finally unset the database read-only mode.
                                        </div>
                                    }
                                >
                                    <OutlinedQuestionCircleIcon
                                        className="ds-left-margin"
                                    />
                                </Tooltip>
                            </GridItem>
                            <GridItem span={9}>
                                <Checkbox
                                    id="clEncrypt"
                                    isChecked={this.state.clEncrypt}
                                    onChange={this.handleChange}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                    <Button
                        className="ds-margin-top-xlg"
                        variant="primary"
                        onClick={this.saveSettings}
                        isDisabled={this.state.saveBtnDisabled || this.state.saving}
                        isLoading={this.state.saving}
                        spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>
                </div>;
        }

        return (
            <div className={this.state.saving ? "ds-disabled" : ""}>
                {clPage}
            </div>
        );
    }
}

Changelog.propTypes = {
    serverId: PropTypes.string,
    clMaxEntries: PropTypes.string,
    clMaxAge: PropTypes.string,
    clTrimInt: PropTypes.string,
    clEncrypt: PropTypes.bool,
    addNotification: PropTypes.func,
    suffix: PropTypes.string,
};

Changelog.defaultProps = {
    serverId: "",
    clMaxEntries: "",
    clMaxAge: "",
    clTrimInt: "",
    clEncrypt: false,
    suffix: "",
};
