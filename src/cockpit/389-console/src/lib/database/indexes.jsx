import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup } from "../notifications.jsx";
import { IndexTable } from "./databaseTables.jsx";
import { ReindexModal } from "./databaseModal.jsx";
import { log_cmd } from "../tools.jsx";
import {
    Nav,
    NavItem,
    TabContent,
    TabPane,
    Modal,
    Row,
    Checkbox,
    Col,
    Icon,
    Button,
    Form,
    noop
} from "patternfly-react";
import PropTypes from "prop-types";
import { Typeahead } from "react-bootstrap-typeahead";
import "../../css/ds.css";

export class SuffixIndexes extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            // indexes
            showIndexModal: false,
            showEditIndexModal: false,
            showReindexModal: false,
            reindexMsg: "",
            editIndexName: "",
            types: [],
            attributes: [],
            matchingRules: [],
            mrs: [],
            _mrs: [],
            showConfirmReindex: false,
            reindexAttrName: "",
            showConfirmDeleteIndex: false,
            deleteAttrName: "",
            // Add indexes
            addIndexName: [],
            addIndexTypeEq: false,
            addIndexTypePres: false,
            addIndexTypeSub: false,
            addIndexTypeApprox: false,
            reindexOnAdd: false,
            // Edit indexes
            errObj: {},
            _isMounted: true
        };

        this.loadIndexes = this.loadIndexes.bind(this);
        this.showIndexModal = this.showIndexModal.bind(this);
        this.closeIndexModal = this.closeIndexModal.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleTypeaheadChange = this.handleTypeaheadChange.bind(this);
        this.saveIndex = this.saveIndex.bind(this);
        this.saveEditIndex = this.saveEditIndex.bind(this);
        this.reindexIndex = this.reindexIndex.bind(this);
        this.deleteIndex = this.deleteIndex.bind(this);
        this.showEditIndexModal = this.showEditIndexModal.bind(this);
        this.closeEditIndexModal = this.closeEditIndexModal.bind(this);
        this.showConfirmReindex = this.showConfirmReindex.bind(this);
        this.closeConfirmReindex = this.closeConfirmReindex.bind(this);
        this.showConfirmDeleteIndex = this.showConfirmDeleteIndex.bind(this);
        this.closeConfirmDeleteIndex = this.closeConfirmDeleteIndex.bind(this);
        this.closeReindexModal = this.closeReindexModal.bind(this);
    }

    componentWillMount () {
        this.loadIndexes();
    }

    componentWillUnmount() {
        this.state._isMounted = false;
    }

    loadIndexes () {
        // Get all the attributes and matching rules now
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "schema", "matchingrules", "list"
        ];
        log_cmd("loadIndexes (suffix config)", "Get matching rules", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    const mrContent = JSON.parse(content);
                    let mrs = [];
                    for (let i = 0; i < mrContent['items'].length; i++) {
                        if (mrContent['items'][i].name[0] != "") {
                            mrs.push(mrContent['items'][i].name[0]);
                        }
                    }

                    const idx_cmd = [
                        "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                        "backend", "index", "list", "--just-names", this.props.suffix
                    ];
                    log_cmd("loadIndexes (suffix config)", "Get current index list", idx_cmd);
                    cockpit
                            .spawn(idx_cmd, { superuser: true, err: "message" })
                            .done(content => {
                                let idxContent = JSON.parse(content);
                                let indexList = idxContent['items'];
                                const attr_cmd = [
                                    "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                                    "schema", "attributetypes", "list"
                                ];
                                log_cmd("loadIndexes (suffix config)", "Get attrs", attr_cmd);
                                cockpit
                                        .spawn(attr_cmd, { superuser: true, err: "message" })
                                        .done(content => {
                                            const attrContent = JSON.parse(content);
                                            let attrs = [];
                                            for (let content of attrContent['items']) {
                                                if (indexList.indexOf(content.name[0]) == -1) {
                                                    // Attribute is not a current index, add it to the list
                                                    // of available attributes to index
                                                    attrs.push(content.name[0]);
                                                }
                                            }
                                            if (this.state._isMounted) {
                                                this.setState({
                                                    matchingRules: mrs,
                                                    attributes: attrs,
                                                });
                                            }
                                        })
                                        .fail(err => {
                                            let errMsg = JSON.parse(err);
                                            this.props.addNotification(
                                                "error",
                                                `Failed to get attributes - ${errMsg.desc}`
                                            );
                                        });
                            });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Failed to get matching rules - ${errMsg.desc}`
                    );
                });
    }

    //
    // Index Modal functions
    //
    showIndexModal() {
        this.setState({
            showIndexModal: true,
            errObj: {},
            mrs: [],
            addIndexName: [],
            addIndexTypeEq: "",
            addIndexTypeSub: "",
            addIndexTypePres:"",
            addIndexTypeApprox: "",
        });
    }

    closeIndexModal() {
        this.setState({
            showIndexModal: false
        });
    }

    handleChange(e) {
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        let valueErr = false;
        let errObj = this.state.errObj;
        if (value == "") {
            valueErr = true;
        }
        errObj[e.target.id] = valueErr;
        this.setState({
            [e.target.id]: value,
            errObj: errObj
        });
    }

    saveIndex() {
        // Validate the form
        if (!this.state.addIndexTypeEq && !this.state.addIndexTypePres &&
            !this.state.addIndexTypeSub && !this.state.addIndexTypeApprox) {
            this.props.addNotification(
                "warning",
                "You must select at least one 'Index Type'"
            );
            return;
        }
        if (this.state.addIndexName == "" || this.state.addIndexName.length == 0) {
            this.props.addNotification(
                "warning",
                "You must select an attribute to index"
            );
            return;
        }

        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "add", "--attr=" + this.state.addIndexName[0],
            this.props.suffix,
        ];

        if (this.state.addIndexTypeEq) {
            cmd.push('--index-type=eq');
        }
        if (this.state.addIndexTypePres) {
            cmd.push('--index-type=pres');
        }
        if (this.state.addIndexTypeSub) {
            cmd.push('--index-type=sub');
        }
        if (this.state.addIndexTypeApprox) {
            cmd.push('--index-type=approx');
        }
        for (let i = 0; i < this.state.mrs.length; i++) {
            cmd.push('--matching-rule=' + this.state.mrs[i]);
        }
        if (this.state.reindexOnAdd) {
            cmd.push('--reindex');
        }

        log_cmd("saveIndex", "Create new index", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    // this.loadIndexes();
                    this.props.reload(this.props.suffix);
                    this.closeIndexModal();
                    if (this.state.reindexOnAdd) {
                        this.reindexAttr(this.state.addIndexName[0]);
                    }
                    this.props.addNotification(
                        "success",
                        `Successfully created new index`
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.closeIndexModal();
                    this.props.addNotification(
                        "error",
                        `Error creating index - ${errMsg.desc}`
                    );
                });
    }

    showEditIndexModal(item) {
        // Set the state types and matching Rules
        let currentMRS = [];
        if (item.matchingrules !== undefined &&
            item.matchingrules.length > 0 &&
            item.matchingrules[0].length > 0) {
            let parts = item.matchingrules[0].split(",").map(item => item.trim());
            for (let part of parts) {
                currentMRS.push(part);
            }
        }

        this.setState({
            editIndexName: item.name[0],
            types: item.types,
            mrs: currentMRS,
            _mrs: currentMRS,
            showEditIndexModal: true,
            errObj: {},
            reindexOnAdd: false,
            editIndexTypeEq: item.types[0].includes("eq"),
            editIndexTypeSub: item.types[0].includes("sub"),
            editIndexTypePres: item.types[0].includes("pres"),
            editIndexTypeApprox: item.types[0].includes("approx"),
            _eq: item.types[0].includes("eq"),
            _sub: item.types[0].includes("sub"),
            _pres: item.types[0].includes("pres"),
            _approx: item.types[0].includes("approx"),
        });
    }

    closeEditIndexModal() {
        this.setState({
            showEditIndexModal: false
        });
    }

    handleTypeaheadChange(values, item) {
        if (item == "matchingRules") {
            this.setState({
                mrs: values
            });
        } else if (item == "indexName") {
            this.setState({
                addIndexName: values
            });
        }
    }

    reindexAttr(attr) {
        const reindex_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "reindex", "--wait", "--attr=" + attr, this.props.suffix,
        ];

        // Open spinner modal
        this.setState({
            showReindexModal: true,
            reindexMsg: attr
        });
        log_cmd("reindexAttr", "index attribute", reindex_cmd);
        cockpit
                .spawn(reindex_cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        "Attribute (" + attr + ") has successfully been reindexed"
                    );
                    this.setState({
                        showReindexModal: false,
                    });
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error indexing attribute ${attr} - ${errMsg.desc}`
                    );
                });
    }

    saveEditIndex() {
        const origMRS = this.state._mrs;
        const newMRS = this.state.mrs;
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "set", "--attr=" + this.state.editIndexName,
            this.props.suffix,
        ];

        // Make sure we have at least one index type
        if (!this.state.editIndexTypeEq && !this.state.editIndexTypeSub &&
            !this.state.editIndexTypePres && !this.state.editIndexTypeApprox) {
            this.props.addNotification(
                "warning",
                "You must have at least one index type"
            );
            return;
        }

        // Check if we have to add mrs
        for (let newMR of newMRS) {
            let found = false;
            for (let origMR of origMRS) {
                if (origMR == newMR) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                cmd.push('--add-mr=' + newMR);
            }
        }
        // Check if we have to remove mrs
        for (let origMR of origMRS) {
            let found = false;
            for (let newMR of newMRS) {
                if (newMR == origMR) {
                    console.log("Found mr no need to delete");
                    found = true;
                    break;
                }
            }
            if (!found) {
                cmd.push('--del-mr=' + origMR);
            }
        }

        // Check if we have to add/delete index types
        if (this.state.editIndexTypeEq && !this.state._eq) {
            cmd.push('--add-type=eq');
        } else if (!this.state.editIndexTypeEq && this.state._eq) {
            cmd.push('--del-type=eq');
        }
        if (this.state.editIndexTypeSub && !this.state._sub) {
            cmd.push('--add-type=sub');
        } else if (!this.state.editIndexTypeSub && this.state._sub) {
            cmd.push('--del-type=sub');
        }
        if (this.state.editIndexTypePres && !this.state._pres) {
            cmd.push('--add-type=pres');
        } else if (!this.state.editIndexTypePres && this.state._pres) {
            cmd.push('--del-type=pres');
        }
        if (this.state.editIndexTypeApprox && !this.state._approx) {
            cmd.push('--add-type=approx');
        } else if (!this.state.editIndexTypeApprox && this.state._approx) {
            cmd.push('--del-type=approx');
        }

        if (cmd.length > 8) {
            // We have changes, do it
            log_cmd("saveEditIndex", "Edit index", cmd);
            cockpit
                    .spawn(cmd, { superuser: true, err: "message" })
                    .done(content => {
                        this.props.reload(this.props.suffix);
                        this.closeEditIndexModal();
                        this.props.addNotification(
                            "success",
                            `Successfully edited index`
                        );
                        if (this.state.reindexOnAdd) {
                            this.reindexAttr(this.state.editIndexName);
                        }
                    })
                    .fail(err => {
                        let errMsg = JSON.parse(err);
                        this.props.reload(this.props.suffix);
                        this.closeEditIndexModal();
                        this.props.addNotification(
                            "error",
                            `Error editing index - ${errMsg.desc}`
                        );
                    });
        }
    }

    showConfirmReindex(item) {
        this.setState({
            reindexAttrName: item.name[0],
            showConfirmReindex: true
        });
    }

    closeConfirmReindex(item) {
        this.setState({
            reindexAttrName: "",
            showConfirmReindex: false
        });
    }

    showConfirmDeleteIndex(item) {
        this.setState({
            deleteAttrName: item.name[0],
            showConfirmDeleteIndex: true
        });
    }

    closeConfirmDeleteIndex(item) {
        this.setState({
            deleteAttrName: "",
            showConfirmDeleteIndex: false
        });
    }

    reindexIndex(attr) {
        this.reindexAttr(attr);
    }

    closeReindexModal() {
        this.setState({
            showReindexModal: false
        });
    }

    deleteIndex(idxName) {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "delete", "--attr=" + idxName,
            this.props.suffix,
        ];

        log_cmd("deleteIndex", "deleteEdit index", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "success",
                        "Successfully deleted index: " + idxName
                    );
                })
                .fail(err => {
                    let errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Error deleting index - ${errMsg.desc}`
                    );
                });
    }

    render() {
        const reindex_attr = <b>{this.state.reindexAttrName}</b>;
        const delete_attr = <b>{this.state.deleteAttrName}</b>;

        return (
            <div>
                <Nav bsClass="nav nav-tabs nav-tabs-pf">
                    <NavItem className="ds-nav-med" eventKey={1}>
                        <div dangerouslySetInnerHTML={{__html: 'Database Indexes'}} />
                    </NavItem>
                    <NavItem className="ds-nav-med" eventKey={2}>
                        <div dangerouslySetInnerHTML={{__html: 'System Indexes'}} />
                    </NavItem>
                </Nav>
                <TabContent>
                    <TabPane eventKey={1}>
                        <div className="ds-indent">
                            <IndexTable
                                editable
                                rows={this.props.indexRows}
                                editIndex={this.showEditIndexModal}
                                reindexIndex={this.showConfirmReindex}
                                deleteIndex={this.showConfirmDeleteIndex}
                            />
                            <button className="btn btn-primary ds-margin-top" type="button" onClick={this.showIndexModal} >Add Index</button>
                        </div>
                    </TabPane>
                    <TabPane eventKey={2}>
                        <div className="ds-indent">
                            <IndexTable
                                rows={this.props.systemIndexRows}
                            />
                        </div>
                    </TabPane>
                </TabContent>

                <AddIndexModal
                    showModal={this.state.showIndexModal}
                    closeHandler={this.closeIndexModal}
                    handleChange={this.handleChange}
                    saveHandler={this.saveIndex}
                    matchingRules={this.state.matchingRules}
                    attributes={this.state.attributes}
                    mrs={this.state.mrs}
                    attributeName={this.state.addIndexName}
                    handleTypeaheadChange={this.handleTypeaheadChange}
                />
                <EditIndexModal
                    showModal={this.state.showEditIndexModal}
                    closeHandler={this.closeEditIndexModal}
                    handleChange={this.handleChange}
                    saveHandler={this.saveEditIndex}
                    types={this.state.types}
                    mrs={this.state.mrs}
                    matchingRules={this.state.matchingRules}
                    indexName={this.state.editIndexName}
                    handleTypeaheadChange={this.handleTypeaheadChange}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmReindex}
                    closeHandler={this.closeConfirmReindex}
                    actionFunc={this.reindexIndex}
                    actionParam={this.state.reindexAttrName}
                    msg="Are you sure you want to reindex this attribute?"
                    msgContent={reindex_attr}
                />
                <ConfirmPopup
                    showModal={this.state.showConfirmDeleteIndex}
                    closeHandler={this.closeConfirmDeleteIndex}
                    actionFunc={this.deleteIndex}
                    actionParam={this.state.deleteAttrName}
                    msg="Are you sure you want to delete this attribute index?"
                    msgContent={delete_attr}
                />
                <ReindexModal
                    showModal={this.state.showReindexModal}
                    closeHandler={this.closeReindexModal}
                    msg={this.state.reindexMsg}
                />
            </div>
        );
    }
}

class AddIndexModal extends React.Component {
    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            matchingRules,
            attributes,
            mrs,
            handleTypeaheadChange,
            attributeName
        } = this.props;

        let availMR = [];
        for (let mr of matchingRules) {
            availMR.push(mr);
        }
        let availAttrs = [];
        for (let attr of attributes) {
            availAttrs.push(attr);
        }

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div className="ds-no-horizontal-scrollbar">
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Add Database Index
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <label className="ds-config-label" htmlFor="indexAttributeName" title="Select an attribute to index">Select An Attribute</label>
                            <Typeahead
                                id="indexAttributeName"
                                onChange={values => {
                                    handleTypeaheadChange(values, "indexName");
                                }}
                                selected={attributeName}
                                maxResults={1000}
                                options={availAttrs}
                                placeholder="Type a attribute name to index..."
                            />
                            <p className="ds-margin-top"><b>Index Types</b></p>
                            <div className="ds-indent ds-margin-top">
                                <Row>
                                    <Col sm={5}>
                                        <Checkbox id="addIndexTypeEq" onChange={handleChange}> Equailty Indexing</Checkbox>
                                    </Col>
                                </Row>
                                <Row>
                                    <Col sm={5}>
                                        <Checkbox id="addIndexTypePres" onChange={handleChange}> Presence Indexing</Checkbox>
                                    </Col>
                                </Row>
                                <Row>
                                    <Col sm={5}>
                                        <Checkbox id="addIndexTypeSub" onChange={handleChange}> Substring Indexing</Checkbox>
                                    </Col>
                                </Row>
                                <Row>
                                    <Col sm={5}>
                                        <Checkbox id="addIndexTypeApprox" onChange={handleChange}> Approximate Indexing</Checkbox>
                                    </Col>
                                </Row>
                            </div>
                            <Row className="ds-margin-top-lg">
                                <Col sm={12} title="List of matching rules separated by a 'space'">
                                    <p><b>Matching Rules</b></p>
                                    <div className="ds-indent ds-margin-top">
                                        <Typeahead
                                            multiple
                                            id="matchingRules"
                                            onChange={values => {
                                                handleTypeaheadChange(values, "matchingRules");
                                            }}
                                            maxResults={1000}
                                            selected={mrs}
                                            options={availMR}
                                            placeholder="Type a matching rule name..."
                                        />
                                    </div>
                                </Col>
                            </Row>
                            <hr />
                            <Row>
                                <Col sm={12}>
                                    <Checkbox className="ds-float-right" id="reindexOnAdd" onChange={handleChange}>
                                        Index attribute after creation
                                    </Checkbox>
                                </Col>
                            </Row>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Create Index
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

class EditIndexModal extends React.Component {
    constructor(props) {
        super(props);
        this.state = {
            mrs: [],
        };
    }

    render() {
        const {
            showModal,
            closeHandler,
            handleChange,
            saveHandler,
            indexName,
            types,
            mrs,
            matchingRules,
            handleTypeaheadChange,
        } = this.props;

        let attrTypes = "";
        if (types != "" && types.length > 0) {
            attrTypes = types[0].split(",").map(item => item.trim());
        }

        const currentMrs = mrs;
        let availMR = matchingRules;

        // Default settings
        let eq = <div>
            <Checkbox id="editIndexTypeEq" onChange={handleChange}> Equailty Indexing</Checkbox>
        </div>;
        let pres = <div>
            <Checkbox id="editIndexTypePres" onChange={handleChange}> Presence Indexing</Checkbox>
        </div>;
        let sub = <div>
            <Checkbox id="editIndexTypeSub" onChange={handleChange}> Substring Indexing</Checkbox>
        </div>;
        let approx = <div>
            <Checkbox id="editIndexTypeApprox" onChange={handleChange}> Approximate Indexing</Checkbox>
        </div>;

        if (attrTypes.includes('eq')) {
            eq = <div>
                <Checkbox id="editIndexTypeEq" onChange={handleChange} defaultChecked>
                    Equality Indexing
                </Checkbox>
            </div>;
        }
        if (attrTypes.includes('pres')) {
            pres = <div>
                <Checkbox id="editIndexTypePres" onChange={handleChange} defaultChecked>
                    Presence Indexing
                </Checkbox>
            </div>;
        }
        if (attrTypes.includes('sub')) {
            sub = <div>
                <Checkbox id="editIndexTypeSub" onChange={handleChange} defaultChecked>
                    Substring Indexing
                </Checkbox>
            </div>;
        }
        if (attrTypes.includes('approx')) {
            approx = <div>
                <Checkbox id="editIndexTypeApprox" onChange={handleChange} defaultChecked>
                    Approximate Indexing
                </Checkbox>
            </div>;
        }

        return (
            <Modal show={showModal} onHide={closeHandler}>
                <div>
                    <Modal.Header>
                        <button
                            className="close"
                            onClick={closeHandler}
                            aria-hidden="true"
                            aria-label="Close"
                        >
                            <Icon type="pf" name="close" />
                        </button>
                        <Modal.Title>
                            Edit Database Index ({indexName})
                        </Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <Form horizontal autoComplete="off">
                            <p><Icon type="pf" style={{'marginRight': '15px'}} name="edit" /><font size="4"><b>{indexName}</b></font></p>
                            <hr />
                            <p><b>Index Types</b></p>
                            <div className="ds-indent ds-margin-top">
                                <Row>
                                    <Col sm={9}>
                                        {eq}
                                    </Col>
                                </Row>
                                <Row>
                                    <Col sm={9}>
                                        {pres}
                                    </Col>
                                </Row>
                                <Row>
                                    <Col sm={9}>
                                        {sub}
                                    </Col>
                                </Row>
                                <Row>
                                    <Col sm={9}>
                                        {approx}
                                    </Col>
                                </Row>
                            </div>
                            <Row className="ds-margin-top-lg">
                                <Col sm={12}>
                                    <p><b>Matching Rules</b></p>
                                    <div className="ds-indent ds-margin-top">
                                        <Typeahead
                                            multiple
                                            id="matchingRulesEdit"
                                            onChange={values => {
                                                handleTypeaheadChange(values, "matchingRules");
                                            }}
                                            selected={currentMrs}
                                            options={availMR}
                                            placeholder="Type a matching rule name..."
                                        />
                                    </div>
                                </Col>
                            </Row>
                            <hr />
                            <Row>
                                <Col sm={12}>
                                    <Checkbox className="ds-float-right" id="reindexOnAdd" onChange={handleChange}>
                                        Reindex Attribute After Saving
                                    </Checkbox>
                                </Col>
                            </Row>
                        </Form>
                    </Modal.Body>
                    <Modal.Footer>
                        <Button
                            bsStyle="default"
                            className="btn-cancel"
                            onClick={closeHandler}
                        >
                            Cancel
                        </Button>
                        <Button
                            bsStyle="primary"
                            onClick={saveHandler}
                        >
                            Save Index
                        </Button>
                    </Modal.Footer>
                </div>
            </Modal>
        );
    }
}

// Property types and defaults

SuffixIndexes.propTypes = {
    systemIndexRows: PropTypes.array,
    indexRows: PropTypes.array,
    serverId: PropTypes.string,
    suffix: PropTypes.string,
    addNotification: PropTypes.func,
    reload: PropTypes.func,
};

SuffixIndexes.defaultProps = {
    systemIndexRows: [],
    indexRows: [],
    serverId: "",
    suffix: "",
    addNotification: noop,
    reload: noop,
};

AddIndexModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    matchingRules: PropTypes.array,
    attributes: PropTypes.array,
    mrs: PropTypes.array,
    attributeName: PropTypes.array,
    handleTypeaheadChange: PropTypes.func,
};

AddIndexModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    matchingRules: [],
    attributes: [],
    mrs: [],
    attributeName: [],
    handleTypeaheadChange: noop,
};

EditIndexModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    matchingRules: PropTypes.array,
    types: PropTypes.array,
    mrs: PropTypes.array,
    indexName: PropTypes.string,
    handleTypeaheadChange: PropTypes.func,
};

EditIndexModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    matchingRules: [],
    types: [],
    mrs: [],
    indexName: "",
    handleTypeaheadChange: noop,
};
