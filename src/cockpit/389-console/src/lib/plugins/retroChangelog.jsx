import cockpit from "cockpit";
import React from "react";
import {
	Button,
	Checkbox,
	Form,
	FormHelperText,
	FormSelect,
	FormSelectOption,
	Grid,
	GridItem,
	NumberInput,
	ValidatedOptions
} from '@patternfly/react-core';
import {
	Select,
	SelectVariant,
	SelectOption
} from '@patternfly/react-core/deprecated';
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";

const _ = cockpit.gettext;

class RetroChangelog extends React.Component {
    componentDidMount(prevProps) {
        if (this.state.firstLoad) {
            this.updateFields();
        }
    }

    componentDidUpdate(prevProps) {
        if (this.props.rows !== prevProps.rows) {
            this.updateFields();
        }
    }

    constructor(props) {
        super(props);

        this.state = {
            firstLoad: true,
            saveBtnDisabled: true,
            saving: false,
            error: {},
            // Config settings
            isReplicated: false,
            maxAge: 0,
            maxAgeUnit: "w",
            trimInterval: 300,
            excludeSuffix: [],
            excludeSuffixOptions: [],
            excludeAttrs: [],
            // original values
            _isReplicated: false,
            _maxAge: 0,
            _maxAgeUnit: "w",
            _trimInterval: 300,
            _excludeSuffix: [],
            _excludeAttrs: [],

            isExcludeAttrOpen: false,
            isExcludeSuffixOpen: false,
        };

        this.maxValue = 20000000;
        this.minValue = 0;
        this.handleMinusConfig = () => {
            this.setState({
                maxAge: Number(this.state.maxAge) - 1
            }, () => { this.validate() });
        };
        this.handleMaxAgeChange = (event) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                maxAge: newValue > this.maxValue ? this.maxValue : newValue < this.minValue ? this.minValue : newValue
            }, () => { this.validate() });
        };
        this.handlePlusConfig = () => {
            this.setState({
                maxAge: Number(this.state.maxAge) + 1
            }, () => { this.validate() });
        };

        this.handleMinusTrim = () => {
            this.setState({
                trimInterval: Number(this.state.trimInterval) - 1
            }, () => { this.validate() });
        };
        this.handleTrimChange = (event) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                trimInterval: newValue > this.maxValue ? this.maxValue : newValue < this.minValue ? this.minValue : newValue
            }, () => { this.validate() });
        };
        this.handlePlusTrim = () => {
            this.setState({
                trimInterval: Number(this.state.trimInterval) + 1
            }, () => { this.validate() });
        };

        this.handleExcludeAttrSelect = (event, selection) => {
            if (this.state.excludeAttrs.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        excludeAttrs: prevState.excludeAttrs.filter((item) => item !== selection),
                        isExcludeAttrOpen: false
                    }), () => { this.validate() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        excludeAttrs: [...prevState.excludeAttrs, selection],
                        isExcludeAttrOpen: false
                    }), () => { this.validate() }
                );
            }
        };
        this.handleExcludeAttrToggle = (_event, isExcludeAttrOpen) => {
            this.setState({
                isExcludeAttrOpen
            });
        };
        this.handleExcludeAttrClear = () => {
            this.setState({
                excludeAttrs: [],
                isExcludeAttrOpen: false
            }, () => { this.validate() });
        };

        this.handleExcludeSuffixSelect = (event, selection) => {
            if (this.state.excludeSuffix.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        excludeSuffix: prevState.excludeSuffix.filter((item) => item !== selection),
                        isExcludeSuffixOpen: false
                    }), () => { this.validate() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        excludeSuffix: [...prevState.excludeSuffix, selection],
                        isExcludeSuffixOpen: false
                    }), () => { this.validate() }
                );
            }
        };
        this.handleExcludeSuffixToggle = (_event, isExcludeSuffixOpen) => {
            this.setState({
                isExcludeSuffixOpen
            }, () => { this.validate() });
        };
        this.handleExcludeSuffixClear = () => {
            this.setState({
                excludeSuffix: [],
                isExcludeSuffixOpen: false
            }, () => { this.validate() });
        };
        this.handleOnExcludeSuffixCreateOption = newValue => {
            if (!this.state.excludeSuffixOptions.includes(newValue)) {
                this.setState({
                    excludeSuffixOptions: [...this.state.excludeSuffixOptions, newValue],
                    isExcludeSuffixOpen: false
                }, () => { this.validate() });
            }
        };

        this.updateFields = this.updateFields.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.validate = this.validate.bind(this);
        this.handleSavePlugin = this.handleSavePlugin.bind(this);
    }

    validate() {
        const errObj = {};
        let all_good = true;

        const dnAttrs = [
            'excludeSuffix'
        ];

        // Check DN attrs
        for (const attr of dnAttrs) {
            for (const dnAttr of this.state[attr]) {
                if (!valid_dn(dnAttr)) {
                    errObj[attr] = true;
                    all_good = false;
                }
            }
        }

        if (all_good) {
            // Check for value differences to see if the save btn should be enabled
            all_good = false;

            const attrs = [
                'isReplicated', 'maxAge', 'maxAgeUnit',
                'trimInterval',
            ];

            const attrLists = [
                'excludeSuffix', "excludeAttrs",
            ];

            for (const check_attr of attrs) {
                if (this.state[check_attr] !== this.state['_' + check_attr]) {
                    all_good = true;
                }
            }

            for (const check_list of attrLists) {
                if (!listsEqual(this.state[check_list], this.state['_' + check_list])) {
                    all_good = true;
                    break;
                }
            }
        }
        this.setState({
            saveBtnDisabled: !all_good,
            error: errObj
        });
    }

    handleFieldChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value
        }, () => { this.validate() });
    }

    updateFields() {
        if (this.props.rows.length > 0) {
            const pluginRow = this.props.rows.find(row => row.cn[0] === "Retro Changelog Plugin");
            let maxAge = 0;
            let maxAgeUnit = "w";
            if (pluginRow["nsslapd-changelogmaxage"] !== undefined) {
                maxAge = Number(pluginRow["nsslapd-changelogmaxage"][0].slice(0, -1)) === 0 ? 0 : Number(pluginRow["nsslapd-changelogmaxage"][0].slice(0, -1));
                maxAgeUnit = pluginRow["nsslapd-changelogmaxage"][0] !== "" ? pluginRow["nsslapd-changelogmaxage"][0].slice(-1).toLowerCase() : "w";
            }
            this.setState({
                isReplicated: !(
                    pluginRow.isReplicated === undefined ||
                    pluginRow.isReplicated[0] === "FALSE"
                ),
                excludeSuffix:
                    pluginRow["nsslapd-exclude-suffix"] === undefined
                        ? []
                        : pluginRow["nsslapd-exclude-suffix"],
                excludeAttrs:
                        pluginRow["nsslapd-exclude-attrs"] === undefined
                            ? []
                            : pluginRow["nsslapd-exclude-attrs"],
                maxAge,
                maxAgeUnit,
                trimInterval: pluginRow["nsslapd-changelog-trim-interval"] === undefined
                    ? 300
                    : pluginRow["nsslapd-changelog-trim-interval"][0],
                _isReplicated: !(
                    pluginRow.isReplicated === undefined ||
                    pluginRow.isReplicated[0] === "FALSE"
                ),
                _excludeSuffix:
                    pluginRow["nsslapd-exclude-suffix"] === undefined
                        ? []
                        : pluginRow["nsslapd-exclude-suffix"],
                _excludeAttrs:
                        pluginRow["nsslapd-exclude-attrs"] === undefined
                            ? []
                            : pluginRow["nsslapd-exclude-attrs"],
                _maxAge: maxAge,
                _maxAgeUnit: maxAgeUnit,
                _trimInterval: pluginRow["nsslapd-changelog-trim-interval"] === undefined
                    ? 300
                    : pluginRow["nsslapd-changelog-trim-interval"][0],
            });
        }
    }

    handleSavePlugin () {
        const maxAge = this.state.maxAge.toString() + this.state.maxAgeUnit;
        let cmd = [
            "dsconf",
            "-j",
            "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "plugin",
            "retro-changelog",
            "set",
            "--is-replicated",
            this.state.isReplicated ? "TRUE" : "FALSE",
            "--max-age",
            maxAge || "delete",
            "--trim-interval",
            this.state.trimInterval.toString() || "300"
        ];
        if (this.state._excludeSuffix !== this.state.excludeSuffix) {
            cmd = [...cmd, "--exclude-suffix"];
            if (this.state.excludeSuffix.length !== 0) {
                for (const value of this.state.excludeSuffix) {
                    cmd = [...cmd, value];
                }
            } else {
                cmd = [...cmd, "delete"];
            }
        }
        if (this.state._excludeAttrs !== this.state.excludeAttrs) {
            cmd = [...cmd, "--exclude-attrs"];
            if (this.state.excludeAttrs.length !== 0) {
                for (const value of this.state.excludeAttrs) {
                    cmd = [...cmd, value];
                }
            } else {
                cmd = [...cmd, "delete"];
            }
        }
        this.setState({
            saving: true
        });

        log_cmd('handleSavePlugin', 'update retrocl', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        _("Successfully updated the Retro Changelog")
                    );
                    this.props.pluginListHandler();
                    this.setState({
                        saving: false
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        cockpit.format(_("Failed to update Retro Changelog Plugin - $0"), errMsg.desc)
                    );
                    this.props.pluginListHandler();
                    this.setState({
                        saving: false
                    });
                });
    }

    render() {
        const {
            isReplicated,
            maxAge,
            maxAgeUnit,
            excludeSuffix,
            excludeAttrs,
            trimInterval,
            saving,
            error
        } = this.state;

        let saveBtnName = _("Save");
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = _("Saving ...");
            extraPrimaryProps.spinnerAriaValueText = _("Saving");
        }

        return (
            <div className={saving ? "ds-disabled" : ""}>
                <PluginBasicConfig
                    rows={this.props.rows}
                    serverId={this.props.serverId}
                    cn="Retro Changelog Plugin"
                    pluginName="Retro Changelog"
                    cmdName="retro-changelog"
                    savePluginHandler={this.props.savePluginHandler}
                    pluginListHandler={this.props.pluginListHandler}
                    addNotification={this.props.addNotification}
                    toggleLoadingHandler={this.props.toggleLoadingHandler}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title={_("This attribute specifies the suffix which will be excluded from the scope of the plugin (nsslapd-exclude-suffix)")}>
                            <GridItem className="ds-label" span={2}>
                                {_("Exclude Suffix")}
                            </GridItem>
                            <GridItem span={5}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a suffix"
                                    onToggle={(event, isOpen) => this.handleExcludeSuffixToggle(event, isOpen)}
                                    onSelect={this.handleExcludeSuffixSelect}
                                    onClear={this.handleExcludeSuffixClear}
                                    selections={excludeSuffix}
                                    isOpen={this.state.isExcludeSuffixOpen}
                                    aria-labelledby="typeAhead-config-exclude-suffix"
                                    placeholderText={_("Type a suffix...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                    onCreateOption={this.handleOnExcludeSuffixCreateOption}
                                    validated={error.excludeSuffix ? ValidatedOptions.error : ValidatedOptions.default}
                                >
                                    {[""].map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText  >
                                    {_("Values must be valid DN !")}
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={2}>
                                <Checkbox
                                    id="isReplicated"
                                    isChecked={isReplicated}
                                    onChange={(e, checked) => { this.handleFieldChange(e) }}
                                    title={_("Sets a flag to indicate on a change in the changelog whether the change is newly made on that server or whether it was replicated over from another server (isReplicated)")}
                                    label={_("Is Replicated")}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("This specifies attribute which will be excluded from the scope of the plugin (nsslapd-exclude-attrs)")}>
                            <GridItem className="ds-label" span={2}>
                                {_("Exclude attribute")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an attribute"
                                    onToggle={(event, isOpen) => this.handleExcludeAttrToggle(event, isOpen)}
                                    onSelect={this.handleExcludeAttrSelect}
                                    onClear={this.handleExcludeAttrClear}
                                    selections={excludeAttrs}
                                    isOpen={this.state.isExcludeAttrOpen}
                                    aria-labelledby="typeAhead-config-exclude-attr"
                                    placeholderText={_("Type an attribute...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    validated={error.excludeAttrs ? ValidatedOptions.error : ValidatedOptions.default}
                                >
                                    {this.props.attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("Specifies the maximum age of any entry in the changelog before it is trimmed from the database (nsslapd-changelogmaxage)")}>
                            <GridItem className="ds-label" span={2}>
                                {_("Max Age")}
                            </GridItem>
                            <GridItem span={2}>
                                <NumberInput
                                    value={maxAge}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={this.handleMinusConfig}
                                    onChange={this.handleMaxAgeChange}
                                    onPlus={this.handlePlusConfig}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                            <GridItem span={2}>
                                <FormSelect
                                    className="ds-margin-left"
                                    id="maxAgeUnit"
                                    value={maxAgeUnit}
                                    onChange={(event, value) => {
                                        this.handleFieldChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                    isDisabled={maxAge === 0}
                                >
                                    <FormSelectOption key="1" value="w" label={_("Weeks")} />
                                    <FormSelectOption key="2" value="d" label={_("Days")} />
                                    <FormSelectOption key="3" value="h" label={_("Hours")} />
                                    <FormSelectOption key="4" value="m" label={_("Minutes")} />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top" title={_("Sets in seconds how often the plugin checks if changes can be trimmed from the database based on the entry's max age (nsslapd-changelog-trim-interval)")}>
                            <GridItem className="ds-label" span={2}>
                                {_("Trimming Interval")}
                            </GridItem>
                            <GridItem span={9}>
                                <NumberInput
                                    value={trimInterval}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={this.handleMinusTrim}
                                    onChange={this.handleTrimChange}
                                    onPlus={this.handlePlusTrim}
                                    inputName="input"
                                    inputAriaLabel="number input"
                                    minusBtnAriaLabel="minus"
                                    plusBtnAriaLabel="plus"
                                    widthChars={8}
                                />
                            </GridItem>
                        </Grid>
                    </Form>
                    <Button
                        className="ds-margin-top-xlg"
                        variant="primary"
                        onClick={this.handleSavePlugin}
                        isDisabled={this.state.saveBtnDisabled || this.state.saving}
                        isLoading={this.state.saving}
                        spinnerAriaValueText={this.state.saving ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                    >
                        {saveBtnName}
                    </Button>
                </PluginBasicConfig>
            </div>
        );
    }
}

RetroChangelog.propTypes = {
    rows: PropTypes.array,
    serverId: PropTypes.string,
    savePluginHandler: PropTypes.func,
    pluginListHandler: PropTypes.func,
    addNotification: PropTypes.func,
    toggleLoadingHandler: PropTypes.func
};

RetroChangelog.defaultProps = {
    rows: [],
    serverId: "",
};

export default RetroChangelog;
