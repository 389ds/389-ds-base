import cockpit from "cockpit";
import React from "react";
import PropTypes from "prop-types";
import { log_cmd } from "../tools.jsx";
import { DoubleConfirmModal } from "../notifications.jsx";
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
    TextInput,
    TextVariants,
    Tooltip,
    ValidatedOptions
} from '@patternfly/react-core';
import OutlinedQuestionCircleIcon from '@patternfly/react-icons/dist/js/icons/outlined-question-circle-icon';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
    faSyncAlt
} from '@fortawesome/free-solid-svg-icons';
import '@fortawesome/fontawesome-svg-core/styles.css';

export class Changelog extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            showDeleteChangelogModal: false,
            saveBtnDisabled: true,
            modalSpinning: false,
            modalChecked: false,
            // Changelog settings
            clDir: this.props.clDir,
            clMaxEntries: Number(this.props.clMaxEntries) == 0 ? 0 : Number(this.props.clMaxEntries),
            clMaxAge: Number(this.props.clMaxAge.slice(0, -1)) == 0 ? 0 : Number(this.props.clMaxAge.slice(0, -1)),
            clMaxAgeUnit: this.props.clMaxAge != "" ? this.props.clMaxAge.slice(-1).toLowerCase() : "s",
            clTrimInt: Number(this.props.clTrimInt) == 0 ? 300 : Number(this.props.clTrimInt),
            clCompactInt: Number(this.props.clCompactInt) == 0 ? 2592000 : Number(this.props.clCompactInt),
            clEncrypt: this.props.clEncrypt,
            // Preserve original settings
            _clMaxEntries: Number(this.props.clMaxEntries) == 0 ? 0 : Number(this.props.clMaxEntries),
            _clMaxAge: Number(this.props.clMaxAge.slice(0, -1)) == 0 ? 0 : Number(this.props.clMaxAge.slice(0, -1)),
            _clMaxAgeUnit: this.props.clMaxAge != "" ? this.props.clMaxAge.slice(-1).toLowerCase() : "s",
            _clTrimInt: Number(this.props.clTrimInt) == 0 ? 300 : Number(this.props.clTrimInt),
            _clCompactInt: Number(this.props.clCompactInt) == 0 ? 2592000 : Number(this.props.clCompactInt),
            _clEncrypt: this.props.clEncrypt,
            _clDir: this.props.clDir,
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
        this.showDeleteChangelogModal = this.showDeleteChangelogModal.bind(this);
        this.closeDeleteChangelogModal = this.closeDeleteChangelogModal.bind(this);
        this.createChangelog = this.createChangelog.bind(this);
        this.deleteChangelog = this.deleteChangelog.bind(this);
    }

    componentDidMount() {
        this.props.enableTree();
    }

    saveSettings () {
        const cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'set-changelog',
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
        if (this.state.clCompactInt != this.state._clCompactInt) {
            cmd.push("--compact-interval=" + this.state.clCompactInt);
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
        if (cmd.length > 5) {
            this.setState({
                // Start the spinner
                saving: true
            });
            log_cmd("saveSettings", "Applying replication changelog changes", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.props.reload();
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
                        this.props.reload();
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
            this.state.clCompactInt != this.state._clCompactInt ||
            this.state.clDir != this.state._clDir ||
            this.state.clEncrypt != this.state._clEncrypt) {
            saveBtnDisabled = false;
        }

        if (this.state.clDir === "") {
            saveBtnDisabled = true;
        }

        this.setState({
            saveBtnDisabled: saveBtnDisabled,
        });
    }

    handleChange(e) {
        // Update the state, then validate the values/save btn
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        const attr = e.target.id;

        this.setState({
            [attr]: value,
        }, () => { this.validateSaveBtn() });
    }

    showDeleteChangelogModal () {
        this.setState({
            showDeleteChangelogModal: true,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    closeDeleteChangelogModal () {
        this.setState({
            showDeleteChangelogModal: false
        });
    }

    deleteChangelog () {
        this.setState({
            saving: true,
            modalSpinning: true,
        });
        let cmd = ['dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'delete-changelog'
        ];
        log_cmd("deleteChangelog", "Delete changelog", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.closeDeleteChangelogModal();
                    this.props.reload();
                    this.props.addNotification(
                        "success",
                        "Successfully deleted replication changelog"
                    );
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.closeDeleteChangelogModal();
                    this.props.reload();
                    this.setState({
                        saving: false
                    });
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        `Error deleting changelog - ${msg}`
                    );
                });
    }

    createChangelog () {
        this.setState({
            saving: true
        });
        let cmd = [
            'dsconf', '-j', 'ldapi://%2fvar%2frun%2fslapd-' + this.props.serverId + '.socket',
            'replication', 'create-changelog'
        ];
        log_cmd("createChangelog", "Create changelog", cmd);
        cockpit
                .spawn(cmd, {superuser: true, "err": "message"})
                .done(content => {
                    this.props.reload();
                    this.props.addNotification(
                        "success",
                        "Successfully created replication changelog"
                    );
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload();
                    this.setState({
                        saving: false
                    });
                    let msg = errMsg.desc;
                    if ('info' in errMsg) {
                        msg = errMsg.desc + " - " + errMsg.info;
                    }
                    this.props.addNotification(
                        "error",
                        `Error creating changelog - ${msg}`
                    );
                });
    }


    render() {
        let clPage;
        let saveBtnName = "Save Settings";
        const extraPrimaryProps = {};

        if (this.state._clDir == "") {
            // No changelog, only show clDir and Create button
            clPage =
                <div>
                    <TextContent>
                        <Text component={TextVariants.h3}>
                            Replication Changelog <FontAwesomeIcon
                                size="lg"
                                className="ds-left-margin ds-refresh"
                                icon={faSyncAlt}
                                title="Refresh changelog settings"
                                onClick={this.props.reload}
                            />
                        </Text>
                    </TextContent>
                    <div className="ds-margin-top-xlg ds-center">
                        <p>There is no Replication Changelog</p>
                        <Button
                            className="ds-margin-top-lg"
                            title="Create the replication changelog"
                            variant="primary"
                            onClick={this.createChangelog}
                        >
                            Create Changelog
                        </Button>
                    </div>
                </div>;
        } else if (this.state.saving) {
            saveBtnName = "Saving settings ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        } else if (this.props.loading) {
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
                    <Grid className="ds-margin-top">
                        <GridItem span={5} className="ds-word-wrap">
                            <TextContent>
                                <Text component={TextVariants.h3}>
                                    Replication Changelog <FontAwesomeIcon
                                        size="lg"
                                        className="ds-left-margin ds-refresh"
                                        icon={faSyncAlt}
                                        title="Refresh changelog settings"
                                        onClick={this.props.reload}
                                    />
                                </Text>
                            </TextContent>
                        </GridItem>
                        <GridItem span={7}>
                            <Button
                                className="ds-float-right"
                                variant="danger"
                                onClick={this.showDeleteChangelogModal}
                            >
                                Delete Changelog
                            </Button>
                        </GridItem>
                    </Grid>
                    <Form isHorizontal>
                        <Grid className="ds-margin-top-xlg" title="Changelog location (nsslapd-changelogdir)">
                            <GridItem className="ds-label" span={3}>
                                Changelog Directory
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={this.state.clDir}
                                    type="text"
                                    id="clDir"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="clDir"
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
                                    validated={this.state.clDir === "" ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
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
                            <GridItem span={3}>
                                <FormSelect
                                    className="ds-margin-left"
                                    id="clMaxAgeUnit"
                                    value={this.state.clMaxAgeUnit}
                                    onChange={(str, e) => {
                                        this.handleChange(e);
                                    }}
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
                        <Grid
                            title="The changelog compaction interval.  Set how often the changelog will compact itself, meaning remove empty/trimmed database slots.  The default is 30 days. (nsslapd-changelogcompactdb-interval)."
                        >
                            <GridItem className="ds-label" span={3}>
                                Changelog Compaction Interval
                            </GridItem>
                            <GridItem span={2}>
                                <NumberInput
                                    value={this.state.clCompactInt}
                                    min={this.minValue}
                                    max={this.maxValue}
                                    onMinus={() => { this.onMinus("clCompactInt") }}
                                    onChange={(e) => { this.onChange(e, "clCompactInt") }}
                                    onPlus={() => { this.onPlus("clCompactInt") }}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                        <Grid
                            title="The changelog trimming interval.  Set how often the changelog checks if there are entries that can be purged from the changelog based on the trimming parameters (nsslapd-changelogtrim-interval)."
                        >
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
                                    onChange={(checked, e) => {
                                        this.handleChange(e);
                                    }}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                    <Button
                        className="ds-margin-top-xlg"
                        variant="primary"
                        onClick={this.saveSettings}
                        isDisabled={this.state.saveBtnDisabled}
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
                <DoubleConfirmModal
                    showModal={this.state.showDeleteChangelogModal}
                    closeHandler={this.closeDeleteChangelogModal}
                    handleChange={this.handleChange}
                    actionHandler={this.deleteChangelog}
                    spinning={this.state.modalSpinning}
                    checked={this.state.modalChecked}
                    mTitle="Delete Replication Changelog"
                    mMsg="Are you sure you want to delete the replication changelog?"
                    mSpinningMsg="Deleting Changelog ..."
                    mBtnName="Delete Changelog"
                />
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
