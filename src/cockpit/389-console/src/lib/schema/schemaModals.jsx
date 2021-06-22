import React from "react";
import {
    Form,
    FormControl,
    FormGroup,
    ControlLabel,
    Col
} from "patternfly-react";
import {
    Button,
    Checkbox,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    Select,
    SelectVariant,
    SelectOption,
    // TextInput,
    noop
} from "@patternfly/react-core";
import PropTypes from "prop-types";

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
            isParentObjOpen,
            isRequiredAttrsOpen,
            isAllowedAttrsOpen,
            onParentObjToggle,
            onParentObjClear,
            onParentObjSelect,
            onParentObjCreateOption,
            onRequiredAttrsToggle,
            onRequiredAttrsClear,
            onRequiredAttrsSelect,
            onRequiredAttrsCreateOption,
            onAllowedAttrsToggle,
            onAllowedAttrsClear,
            onAllowedAttrsSelect,
            onAllowedAttrsCreateOption,
        } = this.props;

        let modalTitle =
            ocModalViewOnly ? (
                `View ObjectClass - ${ocName}`
            ) : (
                <div>
                    {newOcEntry ? "Add" : "Edit"} ObjectClass
                    {ocName != "" ? ` - ${ocName}` : ""}{" "}
                </div>
            );

        let btnList = [
            <Button key="cancel" variant="link" onClick={closeModal}>
                Close
            </Button>
        ];
        if (!ocModalViewOnly) {
            let btnText = "Save";
            let extraPrimaryProps = {};

            if (loading) {
                btnText = "Saving...";
                extraPrimaryProps.spinnerAriaValueText = "Loading";
            }
            btnList.unshift(
                <Button
                    key="oc"
                    isLoading={loading}
                    spinnerAriaValueText={loading ? "Loading" : undefined}
                    variant="primary"
                    onClick={newOcEntry ? addHandler : editHandler}
                    {...extraPrimaryProps}
                >
                    {btnText}
                </Button>
            );
        }

        return (
            <div>
                <Modal
                    variant={ModalVariant.small}
                    aria-labelledby="ds-modal"
                    title={modalTitle}
                    isOpen={objectclassModalShow}
                    onClose={closeModal}
                    actions={btnList}
                >
                    <Form horizontal>
                        <FormGroup controlId="ocName">
                            <Col sm={4}>
                                <ControlLabel title="Name of the objectClass">
                                    Objectclass Name
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    id="ocName"
                                    type="text"
                                    value={ocName}
                                    onChange={handleFieldChange}
                                    disabled={ocModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup controlId="ocDesc">
                            <Col sm={4}>
                                <ControlLabel title="Describes what is the objectClass's purpose">
                                    Description
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    value={ocDesc}
                                    onChange={handleFieldChange}
                                    disabled={ocModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup controlId="ocOID">
                            <Col sm={4}>
                                <ControlLabel title="Object identifier">
                                    OID (optional)
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    value={ocOID}
                                    onChange={handleFieldChange}
                                    disabled={ocModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup key="ocParent" controlId="ocParent" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An objectClass's parent">
                                    Parent Objectclass
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a parent objectClass"
                                    onToggle={onParentObjToggle}
                                    onSelect={onParentObjSelect}
                                    onClear={onParentObjClear}
                                    selections={ocParent}
                                    isOpen={isParentObjOpen}
                                    aria-labelledby="typeAhead-parent-obj"
                                    placeholderText="Type a parent objectClass..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={onParentObjCreateOption}
                                    >
                                    {objectclasses.map((obj, index) => (
                                        <SelectOption
                                            key={index}
                                            value={obj}
                                        />
                                        ))}
                                </Select>
                            </Col>
                        </FormGroup>
                        <FormGroup key="ocKind" controlId="ocKind" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An objectClass kind">
                                    Objectclass Kind
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <select
                                    id="ocKind"
                                    className="btn btn-default dropdown"
                                    onChange={handleFieldChange}
                                    value={ocKind}
                                    disabled={ocModalViewOnly}
                                >
                                    <option>STRUCTURAL</option>
                                    <option>ABSTRACT</option>
                                    <option>AUXILIARY</option>
                                </select>
                            </Col>
                        </FormGroup>
                        <FormGroup key="ocMust" controlId="ocMust" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="A must attribute name">
                                    Required Attributes
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={onRequiredAttrsToggle}
                                    onSelect={onRequiredAttrsSelect}
                                    onClear={onRequiredAttrsClear}
                                    selections={ocMust}
                                    isOpen={isRequiredAttrsOpen}
                                    aria-labelledby="typeAhead-required-attrs"
                                    placeholderText="Type an attribute name..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={onRequiredAttrsCreateOption}
                                    >
                                    {attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                        ))}
                                </Select>
                            </Col>
                        </FormGroup>
                        <FormGroup key="ocMay" controlId="ocMay" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="A may attribute name">
                                    Allowed Attributes
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={onAllowedAttrsToggle}
                                    onSelect={onAllowedAttrsSelect}
                                    onClear={onAllowedAttrsClear}
                                    selections={ocMay}
                                    isOpen={isAllowedAttrsOpen}
                                    aria-labelledby="typeAhead-allowed-attrs"
                                    placeholderText="Type an attribute name..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={onAllowedAttrsCreateOption}
                                    >
                                    {attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                        ))}
                                </Select>
                            </Col>
                        </FormGroup>
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
    ocParent: PropTypes.array,
    ocKind: PropTypes.string,
    ocMust: PropTypes.array,
    ocMay: PropTypes.array,
    objectclassModalShow: PropTypes.bool,
    closeModal: PropTypes.func,
    loading: PropTypes.bool
};

ObjectClassModal.defaultProps = {
    addHandler: noop,
    editHandler: noop,
    newOcEntry: true,
    ocModalViewOnly: false,
    handleTypeaheadChange: noop,
    handleFieldChange: noop,
    objectclasses: [],
    attributes: [],
    ocName: "",
    ocDesc: "",
    ocOID: "",
    ocParent: [],
    ocKind: "",
    ocMust: [],
    ocMay: [],
    objectclassModalShow: false,
    closeModal: noop,
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
            isSyntaxNameOpen,
            isAliasNameOpen,
            isEqualityMROpen,
            isOrderMROpen,
            isSubstringMROpen,
            onParentAttrToggle,
            onParentAttrClear,
            onParentAttrSelect,
            onParentAttrCreateOption,
            onSyntaxNameToggle,
            onSyntaxNameClear,
            onSyntaxNameSelect,
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
        } = this.props;
        let modalTitle =
            atModalViewOnly ? (
                `View Attribute - ${atName}`
            ) : (
                <div>
                    {newAtEntry ? "Add" : "Edit"} Attribute
                    {atName != "" ? ` - ${atName}` : ""}{" "}
                </div>
            );

        let btnList = [
            <Button key="cancel" variant="link" onClick={closeModal}>
                Close
            </Button>
        ];
        if (!atModalViewOnly) {
            let btnText = "Save";
            let extraPrimaryProps = {};

            if (loading) {
                btnText = "Saving...";
                extraPrimaryProps.spinnerAriaValueText = "Loading";
            }
            btnList.unshift(
                <Button
                    key="at"
                    isLoading={loading}
                    spinnerAriaValueText={loading ? "Loading" : undefined}
                    variant="primary"
                    onClick={newAtEntry ? addHandler : editHandler}
                    {...extraPrimaryProps}
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
                    <Form horizontal>
                        <FormGroup controlId="atName">
                            <Col sm={4}>
                                <ControlLabel title="Name of the attribute">
                                    Attribute Name
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    id="atName"
                                    type="text"
                                    value={atName}
                                    onChange={handleFieldChange}
                                    disabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup controlId="atDesc">
                            <Col sm={4}>
                                <ControlLabel title="Describes what is the attribute's purpose">
                                    Description
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    value={atDesc}
                                    onChange={handleFieldChange}
                                    disabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup controlId="atOID">
                            <Col sm={4}>
                                <ControlLabel title="Object identifier">
                                    OID (optional)
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <FormControl
                                    type="text"
                                    value={atOID}
                                    onChange={handleFieldChange}
                                    disabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup key="atParent" controlId="atParent" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An attribute's parent">
                                    Parent Attribute
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type an attribute name"
                                    onToggle={onParentAttrToggle}
                                    onSelect={onParentAttrSelect}
                                    onClear={onParentAttrClear}
                                    selections={atParent}
                                    isOpen={isParentAttrOpen}
                                    aria-labelledby="typeAhead-parent-attr"
                                    placeholderText="Type an attribute name..."
                                    noResultsFoundText="There are no matching entries"
                                    isCreatable
                                    onCreateOption={onParentAttrCreateOption}
                                    >
                                    {attributes.map((attr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={attr}
                                        />
                                        ))}
                                </Select>
                            </Col>
                        </FormGroup>
                        <FormGroup key="atSyntax" controlId="atSyntax" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An attribute's syntax">
                                    Syntax Name
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an syntax name"
                                    onToggle={onSyntaxNameToggle}
                                    onSelect={onSyntaxNameSelect}
                                    onClear={onSyntaxNameClear}
                                    selections={atSyntax}
                                    isOpen={isSyntaxNameOpen}
                                    aria-labelledby="typeAhead-syntax-name"
                                    placeholderText="Type a syntax name..."
                                    noResultsFoundText="There are no matching entries"
                                    >
                                    {syntaxes.map((syntax, index) => (
                                        <SelectOption
                                            key={index}
                                            value={syntax.label}
                                        />
                                        ))}
                                </Select>
                            </Col>
                        </FormGroup>
                        <FormGroup key="atUsage" controlId="atUsage" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An attribute usage purpose">
                                    Attribute Usage
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <select
                                    id="atUsage"
                                    className="btn btn-default dropdown"
                                    onChange={handleFieldChange}
                                    value={atUsage}
                                    disabled={atModalViewOnly}
                                >
                                    <option>userApplications</option>
                                    <option>directoryOperation</option>
                                    <option>distributedOperation</option>
                                    <option>dSAOperation</option>
                                </select>
                            </Col>
                        </FormGroup>
                        <FormGroup key="atMultivalued" controlId="atMultivalued">
                            <Col sm={4}>
                                <ControlLabel title="If attribute can have a multiple values">
                                    Multivalued Attribute
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Checkbox
                                    id="atMultivalued"
                                    isChecked={atMultivalued}
                                    title="If attribute can have a multiple values"
                                    onChange={(checked, e) => {
                                        handleFieldChange(e);
                                    }}
                                    isDisabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup key="atNoUserMod" controlId="atNoUserMod">
                            <Col sm={4}>
                                <ControlLabel title="If attribute is not modifiable by a client application">
                                    Not Modifiable By A User
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Checkbox
                                    id="atNoUserMod"
                                    isChecked={atNoUserMod}
                                    title="If attribute is not modifiable by a client application"
                                    onChange={(checked, e) => {
                                        handleFieldChange(e);
                                    }}
                                    isDisabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup key="atAlias" controlId="atAlias" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An alias name for the attribute">
                                    Alias Names
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type an alias name"
                                    onToggle={onAliasNameToggle}
                                    onSelect={onAliasNameSelect}
                                    onClear={onAliasNameClear}
                                    selections={atAlias}
                                    isOpen={isAliasNameOpen}
                                    aria-labelledby="typeAhead-alias-name"
                                    placeholderText="jc0 Type an alias name..."
                                    noResultsFoundText="There are no matching entries"
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
                            </Col>
                        </FormGroup>
                        <FormGroup key="atEqMr" controlId="atEqMr" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An equality matching rule">
                                    Equality Matching Rule
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a matching rule"
                                    onToggle={onEqualityMRToggle}
                                    onSelect={onEqualityMRSelect}
                                    onClear={onEqualityMRClear}
                                    selections={atEqMr}
                                    isOpen={isEqualityMROpen}
                                    aria-labelledby="typeAhead-equality-mr"
                                    placeholderText="Type a matching rule..."
                                    noResultsFoundText="There are no matching entries"
                                    >
                                    {matchingrules.map((mr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={mr}
                                        />
                                        ))}
                                </Select>
                            </Col>
                        </FormGroup>
                        <FormGroup key="atOrder" controlId="atOrder" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An order matching rule">
                                    Order Matching Rule
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a matching rule"
                                    onToggle={onOrderMRToggle}
                                    onSelect={onOrderMRSelect}
                                    onClear={onOrderMRClear}
                                    selections={atOrder}
                                    isOpen={isOrderMROpen}
                                    aria-labelledby="typeAhead-order-mr"
                                    placeholderText="Type an matching rule.."
                                    noResultsFoundText="There are no matching entries"
                                    >
                                    {matchingrules.map((mr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={mr}
                                        />
                                        ))}
                                </Select>
                            </Col>
                        </FormGroup>
                        <FormGroup key="atSubMr" controlId="atSubMr" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="A substring matching rule">
                                    Substring Matching Rule
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Select
                                    variant={SelectVariant.typeahead}
                                    typeAheadAriaLabel="Type a matching rule"
                                    onToggle={onSubstringMRToggle}
                                    onSelect={onSubstringMRSelect}
                                    onClear={onSubstringMRClear}
                                    selections={atSubMr}
                                    isOpen={isSubstringMROpen}
                                    placeholderText="Type an matching rule..."
                                    noResultsFoundText="There are no matching entries"
                                    >
                                    {matchingrules.map((mr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={mr}
                                        />
                                        ))}
                                </Select>
                            </Col>
                        </FormGroup>
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
    atParent: PropTypes.array,
    atSyntax: PropTypes.array,
    atUsage: PropTypes.string,
    atMultivalued: PropTypes.bool,
    atNoUserMod: PropTypes.bool,
    atAlias: PropTypes.array,
    atEqMr: PropTypes.array,
    atOrder: PropTypes.array,
    atSubMr: PropTypes.array,
    atModalViewOnly: PropTypes.bool,
    attributeModalShow: PropTypes.bool,
    newAtEntry: PropTypes.bool,
    closeModal: PropTypes.func,
    loading: PropTypes.bool
};

AttributeTypeModal.defaultProps = {
    addHandler: noop,
    editHandler: noop,
    handleTypeaheadChange: noop,
    handleFieldChange: noop,
    attributes: [],
    matchingrules: [],
    syntaxes: [],
    atName: "",
    atDesc: "",
    atOID: "",
    atParent: [],
    atSyntax: [],
    atUsage: "userApplications",
    atMultivalued: false,
    atNoUserMod: false,
    atAlias: [],
    atEqMr: [],
    atOrder: [],
    atSubMr: [],
    atModalViewOnly: false,
    attributeModalShow: false,
    newAtEntry: true,
    closeModal: noop,
    loading: false
};

export { ObjectClassModal, AttributeTypeModal };
