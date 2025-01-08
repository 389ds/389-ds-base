import cockpit from "cockpit";
import React from "react";
import {
	Button,
	Checkbox,
	Form,
	FormSelect,
	FormSelectOption,
	Grid,
	GridItem,
	Modal,
	ModalVariant,
	TextInput,
	ValidatedOptions
} from '@patternfly/react-core';
import {
	Select,
	SelectVariant,
	SelectOption
} from '@patternfly/react-core/deprecated';
import PropTypes from "prop-types";

const _ = cockpit.gettext;

class ObjectClassModal extends React.Component {
    render() {
        const {
            newOcEntry,
            ocModalViewOnly,
            addHandler,
            editHandler,
            handleFieldChange,
            objectclasses,
            attributes,
            ocName,
            ocDesc,
            ocOID,
            ocParent,
            ocKind,
            ocMust,
            ocMay,
            objectclassModalShow,
            closeModal,
            loading,
            isRequiredAttrsOpen,
            isAllowedAttrsOpen,
            onRequiredAttrsToggle,
            onRequiredAttrsClear,
            onRequiredAttrsSelect,
            onRequiredAttrsCreateOption,
            onAllowedAttrsToggle,
            onAllowedAttrsClear,
            onAllowedAttrsSelect,
            onAllowedAttrsCreateOption,
            error,
            saveBtnDisabled,
        } = this.props;

        const modalTitle =
            ocModalViewOnly
                ? (
                    cockpit.format(_("View ObjectClass - $0"), ocName)
                )
                : (
                    <div>
                        {cockpit.format(_("$0 ObjectClass"), newOcEntry ? _("Add") : _("Edit"))}
                        {ocName !== "" ? ` - ${ocName}` : ""}{" "}
                    </div>
                );

        const btnList = [
            <Button key="cancel" variant="link" onClick={closeModal}>
                {_("Close")}
            </Button>
        ];
        if (!ocModalViewOnly) {
            let btnText = _("Save");
            const extraPrimaryProps = {};

            if (loading) {
                btnText = _("Saving...");
                extraPrimaryProps.spinnerAriaValueText = _("Loading");
            }
            btnList.unshift(
                <Button
                    key="oc"
                    isLoading={loading}
                    spinnerAriaValueText={loading ? _("Loading") : undefined}
                    variant="primary"
                    onClick={newOcEntry ? addHandler : editHandler}
                    {...extraPrimaryProps}
                    isDisabled={saveBtnDisabled || loading}
                >
                    {btnText}
                </Button>
            );
        }

        return (
            <div>
                <Modal
                    variant={ModalVariant.medium}
                    aria-labelledby="ds-modal"
                    title={modalTitle}
                    isOpen={objectclassModalShow}
                    onClose={closeModal}
                    actions={btnList}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title={_("Name of the objectClass")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Objectclass Name")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={ocName}
                                    type="text"
                                    id="ocName"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="ocName"
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                    validated={error.ocName ? ValidatedOptions.error : ValidatedOptions.default}
                                    isDisabled={!newOcEntry}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Describes what is the objectClass's purpose")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Description")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={ocDesc}
                                    type="text"
                                    id="ocDesc"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="ocDesc"
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                    validated={error.ocDesc ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Object identifier")}>
                            <GridItem className="ds-label" span={3}>
                                {_("OID (optional)")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={ocOID}
                                    type="text"
                                    id="ocOID"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="ocOID"
                                    isDisabled={ocModalViewOnly}
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("An objectClass's parent")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Parent Objectclass")}
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="ocParent"
                                    value={ocParent}
                                    isDisabled={ocModalViewOnly}
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                    aria-label="FormSelect Input"
                                >
                                    {objectclasses.map((obj, index) => (
                                        <FormSelectOption key={index} value={obj} label={obj} />
                                    ))}
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title={_("An objectClass's kind")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Objectclass Kind")}
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="ocKind"
                                    value={ocKind}
                                    isDisabled={ocModalViewOnly}
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key={0} value="STRUCTURAL" label="STRUCTURAL" />
                                    <FormSelectOption key={1} value="ABSTRACT" label="ABSTRACT" />
                                    <FormSelectOption key={2} value="AUXILIARY" label="AUXILIARY" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title={_("A must attribute name")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Required Attributes")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={onRequiredAttrsToggle}
                                    onSelect={onRequiredAttrsSelect}
                                    onClear={onRequiredAttrsClear}
                                    selections={ocMust}
                                    id="ocMust"
                                    isOpen={isRequiredAttrsOpen}
                                    aria-labelledby="typeAhead-required-attrs"
                                    placeholderText={_("Type an attribute name...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                    onCreateOption={onRequiredAttrsCreateOption}
                                    direction="up"
                                    maxHeight="225px"
                                >
                                    {attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("A may attribute name")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Allowed Attributes")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={onAllowedAttrsToggle}
                                    onSelect={onAllowedAttrsSelect}
                                    onClear={onAllowedAttrsClear}
                                    selections={ocMay}
                                    id="ocMay"
                                    isOpen={isAllowedAttrsOpen}
                                    aria-labelledby="typeAhead-allowed-attrs"
                                    placeholderText={_("Type an attribute name...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                    onCreateOption={onAllowedAttrsCreateOption}
                                    direction="up"
                                    maxHeight="225px"
                                >
                                    {attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>
            </div>
        );
    }
}

ObjectClassModal.propTypes = {
    addHandler: PropTypes.func,
    editHandler: PropTypes.func,
    newOcEntry: PropTypes.bool,
    ocModalViewOnly: PropTypes.bool,
    handleTypeaheadChange: PropTypes.func,
    handleFieldChange: PropTypes.func,
    objectclasses: PropTypes.array,
    attributes: PropTypes.array,
    ocName: PropTypes.string,
    ocDesc: PropTypes.string,
    ocOID: PropTypes.string,
    ocParent: PropTypes.string,
    ocKind: PropTypes.string,
    ocMust: PropTypes.array,
    ocMay: PropTypes.array,
    objectclassModalShow: PropTypes.bool,
    closeModal: PropTypes.func,
    loading: PropTypes.bool
};

ObjectClassModal.defaultProps = {
    newOcEntry: true,
    ocModalViewOnly: false,
    objectclasses: [],
    attributes: [],
    ocName: "",
    ocDesc: "",
    ocOID: "",
    ocParent: "",
    ocKind: "",
    ocMust: [],
    ocMay: [],
    objectclassModalShow: false,
    loading: false
};

class AttributeTypeModal extends React.Component {
    render() {
        const {
            atName,
            atDesc,
            atOID,
            atParent,
            atSyntax,
            atUsage,
            atMultivalued,
            atNoUserMod,
            atAlias,
            atEqMr,
            atOrder,
            atSubMr,
            atModalViewOnly,
            attributeModalShow,
            newAtEntry,
            addHandler,
            editHandler,
            handleFieldChange,
            attributes,
            syntaxes,
            matchingrules,
            closeModal,
            loading,
            isParentAttrOpen,
            isAliasNameOpen,
            isEqualityMROpen,
            isOrderMROpen,
            isSubstringMROpen,
            onParentAttrToggle,
            onParentAttrClear,
            onParentAttrSelect,
            onAliasNameToggle,
            onAliasNameClear,
            onAliasNameSelect,
            onAliasNameCreateOption,
            onEqualityMRToggle,
            onEqualityMRClear,
            onEqualityMRSelect,
            onOrderMRToggle,
            onOrderMRClear,
            onOrderMRSelect,
            onSubstringMRToggle,
            onSubstringMRClear,
            onSubstringMRSelect,
            error,
            saveBtnDisabled,
        } = this.props;
        const modalTitle =
            atModalViewOnly
                ? (
                    cockpit.format(_("View Attribute - $0"), atName)
                )
                : (
                    <div>
                        {cockpit.format(_("$0 Attribute"), newAtEntry ? _("Add") : _("Edit"))}
                        {atName !== "" ? ` - ${atName}` : ""}{" "}
                    </div>
                );

        const btnList = [
            <Button key="cancel" variant="link" onClick={closeModal}>
                {_("Close")}
            </Button>
        ];
        if (!atModalViewOnly) {
            let btnText = _("Save");
            const extraPrimaryProps = {};

            if (loading) {
                btnText = _("Saving...");
                extraPrimaryProps.spinnerAriaValueText = _("Loading");
            }
            btnList.unshift(
                <Button
                    key="at"
                    isLoading={loading}
                    spinnerAriaValueText={loading ? _("Loading") : undefined}
                    variant="primary"
                    onClick={newAtEntry ? addHandler : editHandler}
                    {...extraPrimaryProps}
                    isDisabled={saveBtnDisabled || loading}
                >
                    {btnText}
                </Button>
            );
        }

        return (
            <div>
                <Modal
                    variant={ModalVariant.medium}
                    aria-labelledby="ds-modal"
                    title={modalTitle}
                    isOpen={attributeModalShow}
                    onClose={closeModal}
                    actions={btnList}
                >
                    <Form isHorizontal autoComplete="off">
                        <Grid title={_("Name of the attribute")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Attribute Name")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={atName}
                                    type="text"
                                    id="atName"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="atName"
                                    isDisabled={!newAtEntry}
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                    validated={error.atName ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Describes what is the attribute's purpose")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Description")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={atDesc}
                                    type="text"
                                    id="atDesc"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="atDesc"
                                    isDisabled={atModalViewOnly}
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                    validated={error.atDesc ? ValidatedOptions.error : ValidatedOptions.default}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("Object identifier")}>
                            <GridItem className="ds-label" span={3}>
                                {_("OID (optional)")}
                            </GridItem>
                            <GridItem span={9}>
                                <TextInput
                                    value={atOID}
                                    type="text"
                                    id="atOID"
                                    aria-describedby="horizontal-form-name-helper"
                                    name="atOID"
                                    isDisabled={atModalViewOnly}
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("An attribute's parent/superior")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Parent Attribute")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={onParentAttrToggle}
                                    onSelect={onParentAttrSelect}
                                    onClear={onParentAttrClear}
                                    selections={atParent}
                                    isOpen={isParentAttrOpen}
                                    aria-labelledby="typeAhead-parent-attr"
                                    placeholderText={_("Type an attribute name...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                >
                                    {attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("An attribute's syntax")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Syntax Name")}
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="atSyntax"
                                    value={atSyntax}
                                    isDisabled={atModalViewOnly}
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                    aria-label="FormSelect Input"
                                >
                                    {syntaxes.map((syntax, index) => (
                                        <FormSelectOption key={index} value={syntax.id} label={syntax.label} />
                                    ))}
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title={_("An attribute's usage purpose")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Attribute Usage")}
                            </GridItem>
                            <GridItem span={9}>
                                <FormSelect
                                    id="atUsage"
                                    value={atUsage}
                                    isDisabled={atModalViewOnly}
                                    onChange={(e, str) => { handleFieldChange(e) }}
                                    aria-label="FormSelect Input"
                                >
                                    <FormSelectOption key={0} value="userApplications" label="userApplications" />
                                    <FormSelectOption key={1} value="directoryOperation" label="directoryOperation" />
                                    <FormSelectOption key={2} value="distributedOperation" label="distributedOperation" />
                                    <FormSelectOption key={3} value="dSAOperation" label="dSAOperation" />
                                </FormSelect>
                            </GridItem>
                        </Grid>
                        <Grid title={_("If attribute can have a multiple values")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Multivalued Attribute")}
                            </GridItem>
                            <GridItem span={9}>
                                <Checkbox
                                    id="atMultivalued"
                                    isChecked={atMultivalued}
                                    title={_("If attribute can have a multiple values")}
                                    onChange={(e, checked) => {
                                        handleFieldChange(e);
                                    }}
                                    isDisabled={atModalViewOnly}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("If attribute is not modifiable by a client application")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Not Modifiable By A User")}
                            </GridItem>
                            <GridItem span={9}>
                                <Checkbox
                                    id="atNoUserMod"
                                    isChecked={atNoUserMod}
                                    title={_("If attribute is not modifiable by a client application")}
                                    onChange={(e, checked) => {
                                        handleFieldChange(e);
                                    }}
                                    isDisabled={atModalViewOnly}
                                />
                            </GridItem>
                        </Grid>
                        <Grid title={_("An alias name for the attribute")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Alias Names")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an alias name"
                                    onToggle={onAliasNameToggle}
                                    onSelect={onAliasNameSelect}
                                    onClear={onAliasNameClear}
                                    selections={atAlias}
                                    isOpen={isAliasNameOpen}
                                    aria-labelledby="typeAhead-alias-name"
                                    placeholderText={_("Type an alias name...")}
                                    noResultsFoundText={_("There are no matching entries")}
                                    isCreatable
                                    onCreateOption={onAliasNameCreateOption}
                                >
                                    {atAlias.map((alias, index) => (
                                        <SelectOption
                                            key={index}
                                            value={alias}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>

                        <Grid title={_("An equality matching rule")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Equality Matching Rule")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a matching rule"
                                    onToggle={onEqualityMRToggle}
                                    onSelect={onEqualityMRSelect}
                                    onClear={onEqualityMRClear}
                                    selections={atEqMr}
                                    isOpen={isEqualityMROpen}
                                    aria-labelledby="typeAhead-equality-mr"
                                    placeholderText={_("Type an Equality matching rule...")}
                                    noResultsFoundText={_("There are no matching rules")}
                                >
                                    {matchingrules.map((mr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={mr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("An order matching rule")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Order Matching Rule")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a matching rule"
                                    onToggle={onOrderMRToggle}
                                    onSelect={onOrderMRSelect}
                                    onClear={onOrderMRClear}
                                    selections={atOrder}
                                    isOpen={isOrderMROpen}
                                    aria-labelledby="typeAhead-order-mr"
                                    placeholderText={_("Type an Ordering matching rule..")}
                                    noResultsFoundText={_("There are no matching rules")}
                                >
                                    {matchingrules.map((mr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={mr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                        <Grid title={_("A substring matching rule")}>
                            <GridItem className="ds-label" span={3}>
                                {_("Substring Matching Rule")}
                            </GridItem>
                            <GridItem span={9}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a matching rule"
                                    onToggle={onSubstringMRToggle}
                                    onSelect={onSubstringMRSelect}
                                    onClear={onSubstringMRClear}
                                    selections={atSubMr}
                                    isOpen={isSubstringMROpen}
                                    placeholderText={_("Type a Substring matching rule...")}
                                    noResultsFoundText={_("There are no matching rules")}
                                >
                                    {matchingrules.map((mr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={mr}
                                        />
                                    ))}
                                </Select>
                            </GridItem>
                        </Grid>
                    </Form>
                </Modal>
            </div>
        );
    }
}

AttributeTypeModal.propTypes = {
    addHandler: PropTypes.func,
    editHandler: PropTypes.func,
    handleTypeaheadChange: PropTypes.func,
    handleFieldChange: PropTypes.func,
    attributes: PropTypes.array,
    matchingrules: PropTypes.array,
    syntaxes: PropTypes.array,
    atName: PropTypes.string,
    atDesc: PropTypes.string,
    atOID: PropTypes.string,
    atParent: PropTypes.string,
    atSyntax: PropTypes.string,
    atUsage: PropTypes.string,
    atMultivalued: PropTypes.bool,
    atNoUserMod: PropTypes.bool,
    atAlias: PropTypes.array,
    atEqMr: PropTypes.string,
    atOrder: PropTypes.string,
    atSubMr: PropTypes.string,
    atModalViewOnly: PropTypes.bool,
    attributeModalShow: PropTypes.bool,
    newAtEntry: PropTypes.bool,
    closeModal: PropTypes.func,
    loading: PropTypes.bool
};

AttributeTypeModal.defaultProps = {
    attributes: [],
    matchingrules: [],
    syntaxes: [],
    atName: "",
    atDesc: "",
    atOID: "",
    atParent: "",
    atSyntax: "",
    atUsage: "userApplications",
    atMultivalued: false,
    atNoUserMod: false,
    atAlias: [],
    atEqMr: "",
    atOrder: "",
    atSubMr: "",
    atModalViewOnly: false,
    attributeModalShow: false,
    newAtEntry: true,
    loading: false
};

export { ObjectClassModal, AttributeTypeModal };
