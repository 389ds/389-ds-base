import React from "react";
import {
    Row,
    Checkbox,
    Form,
    FormControl,
    FormGroup,
    ControlLabel,
    Col
} from "patternfly-react";
import {
    Button,
    // Form,
    // FormGroup,
    Modal,
    ModalVariant,
    Spinner,
    // TextInput,
    noop
} from "@patternfly/react-core";
import { Typeahead } from "react-bootstrap-typeahead";
import PropTypes from "prop-types";

class ObjectClassModal extends React.Component {
    render() {
        const {
            newOcEntry,
            ocModalViewOnly,
            addHandler,
            editHandler,
            handleTypeaheadChange,
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
            loading
        } = this.props;
        let spinner = "";
        if (loading) {
            spinner = (
                <Row>
                    <div className="ds-margin-top-lg ds-modal-spinner">
                        <Spinner loading inline size="lg" />
                        Saving objectClass...
                    </div>
                </Row>
            );
        }

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
            btnList.unshift(
                <Button
                    key="at"
                    variant="primary"
                    onClick={newOcEntry ? addHandler : editHandler}
                >
                    {newOcEntry ? "Add" : "Save"}
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
                                <Typeahead
                                    allowNew
                                    onChange={values => {
                                        handleTypeaheadChange("ocParent", values);
                                    }}
                                    selected={ocParent}
                                    newSelectionPrefix="Add a parent: "
                                    options={objectclasses}
                                    placeholder="Type a parent objectClass..."
                                    disabled={ocModalViewOnly}
                                />
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
                                <Typeahead
                                    allowNew
                                    multiple
                                    onChange={values => {
                                        handleTypeaheadChange("ocMust", values);
                                    }}
                                    selected={ocMust}
                                    newSelectionPrefix="Add a required attribute: "
                                    options={attributes}
                                    placeholder="Type an attribute name..."
                                    disabled={ocModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup key="ocMay" controlId="ocMay" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="A may attribute name">
                                    Allowed Attributes
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Typeahead
                                    allowNew
                                    multiple
                                    onChange={values => {
                                        handleTypeaheadChange("ocMay", values);
                                    }}
                                    selected={ocMay}
                                    newSelectionPrefix="Add an allowed attribute: "
                                    options={attributes}
                                    placeholder="Type an attribute name..."
                                    disabled={ocModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                    </Form>
                    {spinner}
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
            handleTypeaheadChange,
            handleFieldChange,
            attributes,
            syntaxes,
            matchingrules,
            closeModal,
            loading
        } = this.props;
        let spinner = "";
        if (loading) {
            spinner = (
                <Row>
                    <div className="ds-margin-top-lg ds-modal-spinner">
                        <Spinner size="lg" />
                        Saving attribute...
                    </div>
                </Row>
            );
        }
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
            btnList.unshift(
                <Button
                    key="at"
                    variant="primary"
                    onClick={newAtEntry ? addHandler : editHandler}
                >
                    {newAtEntry ? "Add" : "Save"}
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
                                <Typeahead
                                    allowNew
                                    onChange={values => {
                                        handleTypeaheadChange("atParent", values);
                                    }}
                                    selected={atParent}
                                    newSelectionPrefix="Add a parent: "
                                    options={attributes}
                                    placeholder="Type a parent attribute..."
                                    disabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup key="atSyntax" controlId="atSyntax" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An attribute's syntax">
                                    Syntax Name
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Typeahead
                                    onChange={values => {
                                        handleTypeaheadChange("atSyntax", values);
                                    }}
                                    selected={atSyntax}
                                    newSelectionPrefix="Add a syntax: "
                                    options={syntaxes}
                                    placeholder="Type a syntax name..."
                                    disabled={atModalViewOnly}
                                />
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
                                    checked={atMultivalued}
                                    title="If attribute can have a multiple values"
                                    onChange={handleFieldChange}
                                    disabled={atModalViewOnly}
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
                                    checked={atNoUserMod}
                                    title="If attribute is not modifiable by a client application"
                                    onChange={handleFieldChange}
                                    disabled={atModalViewOnly}
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
                                <Typeahead
                                    allowNew
                                    multiple
                                    onChange={values => {
                                        handleTypeaheadChange("atAlias", values);
                                    }}
                                    selected={atAlias}
                                    options={[]}
                                    newSelectionPrefix="Add an alias: "
                                    placeholder="Type an alias name..."
                                    disabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup key="atEqMr" controlId="atEqMr" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An equality matching rule">
                                    Equality Matching Rule
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Typeahead
                                    onChange={values => {
                                        handleTypeaheadChange("atEqMr", values);
                                    }}
                                    selected={atEqMr}
                                    newSelectionPrefix="Add an matching rule: "
                                    options={matchingrules}
                                    placeholder="Type an matching rule..."
                                    disabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup key="atOrder" controlId="atOrder" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="An order matching rule">
                                    Order Matching Rule
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Typeahead
                                    onChange={values => {
                                        handleTypeaheadChange("atOrder", values);
                                    }}
                                    selected={atOrder}
                                    newSelectionPrefix="Add an matching rule: "
                                    options={matchingrules}
                                    placeholder="Type an matching rule..."
                                    disabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                        <FormGroup key="atSubMr" controlId="atSubMr" disabled={false}>
                            <Col sm={4}>
                                <ControlLabel title="A substring matching rule">
                                    Substring Matching Rule
                                </ControlLabel>
                            </Col>
                            <Col sm={8}>
                                <Typeahead
                                    onChange={values => {
                                        handleTypeaheadChange("atSubMr", values);
                                    }}
                                    selected={atSubMr}
                                    newSelectionPrefix="Add an matching rule: "
                                    options={matchingrules}
                                    placeholder="Type an matching rule..."
                                    disabled={atModalViewOnly}
                                />
                            </Col>
                        </FormGroup>
                    </Form>
                    {spinner}
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
