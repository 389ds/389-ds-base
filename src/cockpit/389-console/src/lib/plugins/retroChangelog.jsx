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
    Select,
    SelectVariant,
    SelectOption,
    TextInput,
    NumberInput,
    ValidatedOptions,
} from "@patternfly/react-core";
import PropTypes from "prop-types";
import PluginBasicConfig from "./pluginBasicConfig.jsx";
import { log_cmd, valid_dn, listsEqual } from "../tools.jsx";

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
        this.onMinusConfig = () => {
            this.setState({
                maxAge: Number(this.state.maxAge) - 1
            }, () => { this.validate() });
        };
        this.onMaxAgeChange = (event) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                maxAge: newValue > this.maxValue ? this.maxValue : newValue < this.minValue ? this.minValue : newValue
            }, () => { this.validate() });
        };
        this.onPlusConfig = () => {
            this.setState({
                maxAge: Number(this.state.maxAge) + 1
            }, () => { this.validate() });
        };

        this.onMinusTrim = () => {
            this.setState({
                trimInterval: Number(this.state.trimInterval) - 1
            }, () => { this.validate() });
        };
        this.onTrimChange = (event) => {
            const newValue = isNaN(event.target.value) ? 0 : Number(event.target.value);
            this.setState({
                trimInterval: newValue > this.maxValue ? this.maxValue : newValue < this.minValue ? this.minValue : newValue
            }, () => { this.validate() });
        };
        this.onPlusTrim = () => {
            this.setState({
                trimInterval: Number(this.state.trimInterval) + 1
            }, () => { this.validate() });
        };

        this.onExcludeAttrSelect = (event, selection) => {
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
        this.onExcludeAttrToggle = isExcludeAttrOpen => {
            this.setState({
                isExcludeAttrOpen
            });
        };
        this.onExcludeAttrClear = () => {
            this.setState({
                excludeAttrs: [],
                isExcludeAttrOpen: false
            }, () => { this.validate() });
        };

        this.onExcludeSuffixSelect = (event, selection) => {
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
        this.onExcludeSuffixToggle = isExcludeSuffixOpen => {
            this.setState({
                isExcludeSuffixOpen
            }, () => { this.validate() });
        };
        this.onExcludeSuffixClear = () => {
            this.setState({
                excludeSuffix: [],
                isExcludeSuffixOpen: false
            }, () => { this.validate() });
        };
        this.onExcludeSuffixCreateOption = newValue => {
            if (!this.state.excludeSuffix.includes(newValue)) {
                this.setState({
                    excludeSuffix: [...this.state.excludeSuffix, newValue],
                    isExcludeSuffixOpen: false
                }, () => { this.validate() });
            }
        };

        this.updateFields = this.updateFields.bind(this);
        this.handleFieldChange = this.handleFieldChange.bind(this);
        this.validate = this.validate.bind(this);
        this.savePlugin = this.savePlugin.bind(this);
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
                if (this.state[check_attr] != this.state['_' + check_attr]) {
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
                maxAge = Number(pluginRow["nsslapd-changelogmaxage"][0].slice(0, -1)) == 0 ? 0 : Number(pluginRow["nsslapd-changelogmaxage"][0].slice(0, -1));
                maxAgeUnit = pluginRow["nsslapd-changelogmaxage"][0] != "" ? pluginRow["nsslapd-changelogmaxage"][0].slice(-1).toLowerCase() : "w";
            }
            this.setState({
                isReplicated: !(
                    pluginRow.isReplicated === undefined ||
                    pluginRow.isReplicated[0] == "FALSE"
                ),
                excludeSuffix:
                    pluginRow["nsslapd-exclude-suffix"] === undefined
                        ? []
                        : pluginRow["nsslapd-exclude-suffix"],
                excludeAttrs:
                        pluginRow["nsslapd-exclude-attrs"] === undefined
                            ? []
                            : pluginRow["nsslapd-exclude-attrs"],
                maxAge: maxAge,
                maxAgeUnit: maxAgeUnit,
                trimInterval: pluginRow["nsslapd-changelog-trim-interval"] === undefined
                    ? 300
                    : pluginRow["nsslapd-changelog-trim-interval"][0],
                _isReplicated: !(
                    pluginRow.isReplicated === undefined ||
                    pluginRow.isReplicated[0] == "FALSE"
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

    savePlugin () {
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
        if (this.state._excludeSuffix != this.state.excludeSuffix) {
            cmd = [...cmd, "--exclude-suffix"];
            if (this.state.excludeSuffix.length != 0) {
                for (const value of this.state.excludeSuffix) {
                    cmd = [...cmd, value];
                }
            } else {
                cmd = [...cmd, "delete"];
            }
        }
        if (this.state._excludeAttrs != this.state.excludeAttrs) {
            cmd = [...cmd, "--exclude-attrs"];
            if (this.state.excludeAttrs.length != 0) {
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

        log_cmd('savePlugin', 'update retrocl', cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        `Successfully updated the Retro Changelog`
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
                        `Failed to update Retro Changelog Plugin - ${errMsg.desc}`
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

        let saveBtnName = "Save";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Saving ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
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
                        <Grid title="This attribute specifies the suffix which will be excluded from the scope of the plugin (nsslapd-exclude-suffix)">
                            <GridItem className="ds-label" span={2}>
                                Exclude Suffix
                            </GridItem>
                            <GridItem span={5}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a suffix"
                                    onToggle={this.onExcludeSuffixToggle}
                                    onSelect={this.onExcludeSuffixSelect}
                                    onClear={this.onExcludeSuffixClear}
                                    selections={excludeSuffix}
                                    isOpen={this.state.isExcludeSuffixOpen}
                                    aria-labelledby="typeAhead-config-exclude-suffix"
                                    placeholderText="Type a suffix..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={this.onSubtreeScopeCreateOption}
                                    validated={error.excludeSuffix ? ValidatedOptions.error : ValidatedOptions.default}
                                >
                                    {[""].map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                                <FormHelperText isError isHidden={!error.excludeSuffix}>
                                    Values must be valid DN !
                                </FormHelperText>
                            </GridItem>
                            <GridItem className="ds-left-margin" span={2}>
                                <Checkbox
                                    id="isReplicated"
                                    isChecked={isReplicated}
                                    onChange={(checked, e) => { this.handleFieldChange(e) }}
                                    title="Sets a flag to indicate on a change in the changelog whether the change is newly made on that server or whether it was replicated over from another server (isReplicated)"
                                    label="Is Replicated"
                                />
                            </GridItem>
                        </Grid>
                        <Grid title="This specifies attribute which will be excluded from the scope of the plugin (nsslapd-exclude-attrs)">
                            <GridItem className="ds-label" span={2}>
                                Exclude attribute
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an attribute"
                                    onToggle={this.onExcludeAttrToggle}
                                    onSelect={this.onExcludeAttrSelect}
                                    onClear={this.onExcludeAttrClear}
                                    selections={excludeAttrs}
                                    isOpen={this.state.isExcludeAttrOpen}
                                    aria-labelledby="typeAhead-config-exclude-attr"
                                    placeholderText="Type an attribute..."
                                    noResultsFoundText="There are no matching entries"
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
                        <Grid title="Specifies the maximum age of any entry in the changelog before it is trimmed from the database (nsslapd-changelogmaxage)">
                            <GridItem className="ds-label" span={2}>
                                Max Age
                            </GridItem>
                            <GridItem span={2}>
                                <NumberInput
                                    value={maxAge}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={this.onMinusConfig}
                                    onChange={this.onMaxAgeChange}
                                    onPlus={this.onPlusConfig}
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
                                    onChange={(value, event) => {
                                        this.handleFieldChange(event);
                                    }}
                                    aria-label="FormSelect Input"
                                    isDisabled={maxAge == 0}
                                >
                                    <FormSelectOption key="1" value="w" label="Weeks" />
                                    <FormSelectOption key="2" value="d" label="Days" />
                                    <FormSelectOption key="3" value="h" label="Hours" />
                                    <FormSelectOption key="4" value="m" label="Minutes" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top" title="Sets in seconds how often the plugin checks if changes can be trimmed from the database based on the entry's max age (nsslapd-changelog-trim-interval)">
                            <GridItem className="ds-label" span={2}>
                                Trimming Interval
                            </GridItem>
                            <GridItem span={9}>
                                <NumberInput
                                    value={trimInterval}
                                    min={0}
                                    max={this.maxValue}
                                    onMinus={this.onMinusTrim}
                                    onChange={this.onTrimChange}
                                    onPlus={this.onPlusTrim}
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
                        onClick={this.savePlugin}
                        isDisabled={this.state.saveBtnDisabled || this.state.saving}
                        isLoading={this.state.saving}
                        spinnerAriaValueText={this.state.saving ? "Saving" : undefined}
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
