import React from "react";
import cockpit from "cockpit";
import PropTypes from "prop-types";
import {
    Button,
    Checkbox,
    Form,
    FormSelect,
    FormSelectOption,
    Grid,
    GridItem,
    TextInput,
} from "@patternfly/react-core";

const _ = cockpit.gettext;

export class SuffixConfig extends React.Component {
    render() {
        let cacheInputs;
        if (this.props.autoTuning) {
            const cacheValue = this.props.cachesize + "  (auto-sized)";
            const cachememValue = this.props.cachememsize + "  (auto-sized)";
            cacheInputs = (
                <Form isHorizontal autoComplete="off">
                    <Grid title={_("The entry cache size in bytes setting is being auto-sized and is read-only - see Global Database Configuration")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Entry Cache Size")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={cachememValue}
                                type="text"
                                id="cachememsize"
                                aria-describedby="cachememsize"
                                name="cachememsize"
                                isDisabled
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("The entry cache max entries setting is being auto-sized and is read-only - see Global Database Configuration")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Entry Cache Max Entries")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={cacheValue}
                                type="text"
                                id="cachesize"
                                aria-describedby="cachesize"
                                name="cachesize"
                                isDisabled
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("The available memory space in bytes for the DN cache. The DN cache is similar to the entry cache for a database, only its table stores only the entry ID and the entry DN (nsslapd-dncachememsize).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("DN Cache Size")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.props.dncachememsize}
                                type="number"
                                id="dncachememsize"
                                aria-describedby="dncachememsize"
                                name="dncachememsize"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                </Form>
            );
        } else {
            cacheInputs = (
                <Form isHorizontal autoComplete="off">
                    <Grid title={_("The size for the available memory space in bytes for the entry cache (nsslapd-cachememsize).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Entry Cache Size")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.props.cachememsize}
                                type="number"
                                id="cachememsize"
                                aria-describedby="cachememsize"
                                name="cachememsize"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("The number of entries to keep in the entry cache, use'-1' for unlimited (nsslapd-cachesize).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("Entry Cache Max Entries")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.props.cachesize}
                                type="number"
                                id="cachesize"
                                aria-describedby="cachesize"
                                name="cachesize"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("the available memory space in bytes for the DN cache. The DN cache is similar to the entry cache for a database, only its table stores only the entry ID and the entry DN (nsslapd-dncachememsize).")}>
                        <GridItem className="ds-label" span={3}>
                            {_("DN Cache Size")}
                        </GridItem>
                        <GridItem span={9}>
                            <TextInput
                                value={this.props.dncachememsize}
                                type="number"
                                id="dncachememsize"
                                aria-describedby="dncachememsize"
                                name="dncachememsize"
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                            />
                        </GridItem>
                    </Grid>
                </Form>
            );
        }

        let saveBtnName = _("Save Configuration");
        const extraPrimaryProps = {};
        if (this.props.saving) {
            saveBtnName = _("Saving ...");
            extraPrimaryProps.spinnerAriaValueText = _("saving");
        }
        return (
            <div className="ds-margin-top-lg">
                {cacheInputs}
                <Form isHorizontal autoComplete="off">
                    <Grid
                        className="ds-margin-top-lg"
                        title={_("Set the backend type.  Warning, changing this setting could lead to unexpected database behavior.")}
                    >
                        <GridItem className="ds-label" span={3}>
                            {_("Backend State")}
                        </GridItem>
                        <GridItem span={9}>
                            <FormSelect
                                id="dbstate"
                                value={this.props.dbstate}
                                onChange={(str, e) => {
                                    this.props.handleChange(e);
                                }}
                                aria-label="FormSelect Input"
                            >
                                <FormSelectOption value="backend" label={_("Backend")} />
                                <FormSelectOption value="disabled" label={_("Disabled")} />
                                <FormSelectOption value="referral" label={_("Referral")} />
                                <FormSelectOption value="referral on update" label={_("Referral On Update")} />
                            </FormSelect>
                        </GridItem>
                    </Grid>
                    <Grid title={_("Put database in Read-Only mode (nsslapd-readonly).")}>
                        <GridItem span={12}>
                            <Checkbox
                                label={_("Database Read-Only Mode")}
                                id="readOnly"
                                isChecked={this.props.readOnly}
                                onChange={(checked, e) => {
                                    this.props.handleChange(e);
                                }}
                                aria-label="send ref"
                            />
                        </GridItem>
                    </Grid>
                    <Grid title={_("Block unindexed searches on this suffix (nsslapd-require-index).")}>
                        <GridItem span={12}>
                            <Checkbox
                                label={_("Block Unindexed Searches")}
                                id="requireIndex"
                                isChecked={this.props.requireIndex}
                                onChange={(checked, e) => {
                                    this.props.handleChange(e);
                                }}
                                aria-label="requireIndex"
                            />
                        </GridItem>
                    </Grid>
                </Form>
                <div className="ds-margin-top-lg">
                    <Button
                        className="ds-margin-top-lg"
                        onClick={this.props.handleSave}
                        variant="primary"
                        isLoading={this.props.saving}
                        spinnerAriaValueText={this.props.saving ? _("Saving") : undefined}
                        {...extraPrimaryProps}
                        isDisabled={this.props.saveBtnDisabled || this.props.saving}
                    >
                        {saveBtnName}
                    </Button>
                </div>
            </div>
        );
    }
}

// Property types and defaults

SuffixConfig.propTypes = {
    cachememsize: PropTypes.string,
    cachesize: PropTypes.string,
    dncachememsize: PropTypes.string,
    readOnly: PropTypes.bool,
    requireIndex: PropTypes.bool,
    autoTuning: PropTypes.bool,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
};

SuffixConfig.defaultProps = {
    cachememsize: "",
    cachesize: "",
    dncachememsize: "",
    readOnly: false,
    requireIndex: false,
    autoTuning: false,
};
