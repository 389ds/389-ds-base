import cockpit from "cockpit";
import React from "react";
import { ConfirmPopup } from "../notifications.jsx";
import { IndexTable } from "./databaseTables.jsx";
import { ReindexModal } from "./databaseModal.jsx";
import { log_cmd } from "../tools.jsx";
import {
    Icon,
    Row,
    Col,
    Form,
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
    Tab,
    Tabs,
    TabTitleText,
    // TextInput,
    noop
} from "@patternfly/react-core";
import PropTypes from "prop-types";

export class SuffixIndexes extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            // indexes
            showIndexModal: false,
            showEditIndexModal: false,
            showReindexModal: false,
            activeTabKey: 0,
            reindexMsg: "",
            editIndexName: "",
            types: "",
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
            _isMounted: true,

            // Select Typeahead
            isAttributeOpen: false,
            isMatchingruleAddOpen: false,
            isMatchingruleEditOpen: false
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        // Select Attribute
        this.onAttributeSelect = (event, selection) => {
            if (this.state.addIndexName.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        addIndexName: prevState.addIndexName.filter((item) => item !== selection),
                        isAttributeOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        addIndexName: [...prevState.addIndexName, selection],
                        isAttributeOpen: false
                    }),
                );
            }
        };
        this.onAttributeToggle = isAttributeOpen => {
            this.setState({
                isAttributeOpen
            });
        };
        this.onAttributeClear = () => {
            this.setState({
                addIndexName: [],
                isAttributeOpen: false
            });
        };

        // Add Matching Rule
        this.onMatchingruleAddSelect = (event, selection) => {
            if (this.state.mrs.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        mrs: prevState.mrs.filter((item) => item !== selection),
                        isMatchingruleAddOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        mrs: [...prevState.mrs, selection],
                        isMatchingruleAddOpen: false
                    }),
                );
            }
        };
        this.onMatchingruleAddToggle = isMatchingruleAddOpen => {
            this.setState({
                isMatchingruleAddOpen
            });
        };
        this.onMatchingruleAddClear = () => {
            this.setState({
                mrs: [],
                isMatchingruleAddOpen: false
            });
        };

        // Edit Matching Rules
        this.onMatchingruleEditSelect = (event, selection) => {
            if (this.state.mrs.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        mrs: prevState.mrs.filter((item) => item !== selection),
                        isMatchingruleEditOpen: false
                    }),
                );
            } else {
                this.setState(
                    (prevState) => ({
                        mrs: [...prevState.mrs, selection],
                        isMatchingruleEditOpen: false
                    })
                );
            }
        };
        this.onMatchingruleEditToggle = isMatchingruleEditOpen => {
            this.setState({
                isMatchingruleEditOpen
            });
        };
        this.onMatchingruleEditClear = () => {
            this.setState({
                mrs: [],
                isMatchingruleEditOpen: false
            });
        };

        this.loadIndexes = this.loadIndexes.bind(this);
        this.showIndexModal = this.showIndexModal.bind(this);
        this.closeIndexModal = this.closeIndexModal.bind(this);
        this.handleChange = this.handleChange.bind(this);
        this.handleTypeaheadChange = this.handleTypeaheadChange.bind(this);
        this.onSelectToggle = this.onSelectToggle.bind(this);
        this.onSelectClear = this.onSelectClear.bind(this);
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

    componentDidMount () {
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
            addIndexTypeEq: false,
            addIndexTypeSub: false,
            addIndexTypePres: false,
            addIndexTypeApprox: false,
            reindexOnAdd: false,
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
        if (item[2] !== undefined &&
            item[2].length > 0 &&
            item[2].length > 0) {
            let parts = item[2].split(",").map(item => item.trim());
            for (let part of parts) {
                currentMRS.push(part);
            }
        }

        this.setState({
            editIndexName: item[0],
            types: item[1],
            mrs: currentMRS,
            _mrs: currentMRS,
            showEditIndexModal: true,
            errObj: {},
            reindexOnAdd: false,
            editIndexTypeEq: item[1].includes("eq"),
            editIndexTypeSub: item[1].includes("sub"),
            editIndexTypePres: item[1].includes("pres"),
            editIndexTypeApprox: item[1].includes("approx"),
            _eq: item[1].includes("eq"),
            _sub: item[1].includes("sub"),
            _pres: item[1].includes("pres"),
            _approx: item[1].includes("approx"),
        });
    }

    closeEditIndexModal() {
        this.setState({
            showEditIndexModal: false
        });
    }

    handleTypeaheadChange = item => (event, values) => {
        switch (item) {
        case "addIndexAttributes":
            this.setState({
                addIndexName: [...this.state.addIndexName, values]
            });
            break;
        case "addMatchingRules":
        case "matchingRulesEdit":
            this.setState({
                mrs: [...this.state.mrs, values]
            });
            break;
        default:
            break;
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
            reindexAttrName: item,
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
            deleteAttrName: item,
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

    onSelectToggle = (isExpanded, toggleId) => {
        this.setState({
            [toggleId]: isExpanded
        });
    }

    onSelectClear = item => event => {
        switch (item) {
        case "addIndexAttributes":
            this.setState({
                addIndexName: [],
                isAddIndexSelectOpen: false
            });
            break;
        case "addMatchingRules":
            this.setState({
                mrs: [],
                isAddMrsSelectOpen: false
            });
            break;
        case "matchingRulesEdit":
            this.setState({
                mrs: [],
                isEditIndexSelectOpen: false
            });
            break;
        default:
            break;
        }
    }

    render() {
        const reindex_attr = <b>{this.state.reindexAttrName}</b>;
        const delete_attr = <b>{this.state.deleteAttrName}</b>;

        return (
            <div className="ds-margin-top-lg">
                <Tabs isSecondary activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Database Indexes</TabTitleText>}>
                        <div className="ds-indent">
                            <IndexTable
                                editable
                                rows={this.props.indexRows}
                                key={this.props.indexRows}
                                editIndex={this.showEditIndexModal}
                                reindexIndex={this.showConfirmReindex}
                                deleteIndex={this.showConfirmDeleteIndex}
                            />
                            <button className="btn btn-primary ds-margin-top" type="button" onClick={this.showIndexModal} >Add Index</button>
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>System Indexes</TabTitleText>}>
                        <div className="ds-indent">
                            <IndexTable
                                rows={this.props.systemIndexRows}
                            />
                        </div>
                    </Tab>
                </Tabs>

                <AddIndexModal
                    showModal={this.state.showIndexModal}
                    closeHandler={this.closeIndexModal}
                    handleChange={this.handleChange}
                    saveHandler={this.saveIndex}
                    matchingRules={this.state.matchingRules}
                    attributes={this.state.attributes}
                    mrs={this.state.mrs}
                    attributeName={this.state.addIndexName}
                    addIndexTypeEq={this.state.addIndexTypeEq}
                    addIndexTypePres={this.state.addIndexTypePres}
                    addIndexTypeSub={this.state.addIndexTypeSub}
                    addIndexTypeApprox={this.state.addIndexTypeApprox}
                    reindexOnAdd={this.state.reindexOnAdd}
                    onAttributeSelect={this.onAttributeSelect}
                    onAttributeToggle={this.onAttributeToggle}
                    onAttributeClear={this.onAttributeClear}
                    isAttributeOpen={this.state.isAttributeOpen}
                    onMatchingruleAddSelect={this.onMatchingruleAddSelect}
                    onMatchingruleAddToggle={this.onMatchingruleAddToggle}
                    onMatchingruleAddClear={this.onMatchingruleAddClear}
                    isMatchingruleAddOpen={this.state.isMatchingruleAddOpen}
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
                    editIndexTypeEq={this.state.editIndexTypeEq}
                    editIndexTypePres={this.state.editIndexTypePres}
                    editIndexTypeSub={this.state.editIndexTypeSub}
                    editIndexTypeApprox={this.state.editIndexTypeApprox}
                    reindexOnAdd={this.state.reindexOnAdd}
                    onMatchingruleEditSelect={this.onMatchingruleEditSelect}
                    onMatchingruleEditToggle={this.onMatchingruleEditToggle}
                    onMatchingruleEditClear={this.onMatchingruleEditClear}
                    isMatchingruleEditOpen={this.state.isMatchingruleEditOpen}
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
            attributeName,
            onAttributeToggle,
            onAttributeClear,
            onAttributeSelect,
            isAttributeOpen,
            onMatchingruleAddToggle,
            onMatchingruleAddClear,
            onMatchingruleAddSelect,
            isMatchingruleAddOpen,
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
            <Modal
                variant={ModalVariant.small}
                title="Add Database Index"
                isOpen={showModal}
                onClose={closeHandler}
                aria-labelledby="ds-modal"
                actions={[
                    <Button key="confirm" variant="primary" onClick={saveHandler}>
                        Create Index
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form horizontal autoComplete="off">
                    <label className="ds-config-label" htmlFor="indexAttributeName" title="Select an attribute to index">Select An Attribute</label>
                    <Select
                        variant={SelectVariant.typeahead}
                        typeAheadAriaLabel="Type a attribute name to index"
                        onToggle={onAttributeToggle}
                        onClear={onAttributeClear}
                        onSelect={onAttributeSelect}
                        selections={attributeName}
                        isOpen={isAttributeOpen}
                        aria-labelledby="typeAhead-attr-add"
                        placeholderText="Type a attribute name to index.."
                        noResultsFoundText="There are no matching entries"
                        >
                        {availAttrs.map((attr, index) => (
                            <SelectOption
                                key={index}
                                value={attr}
                            />
                        ))}
                    </Select>
                    <p className="ds-margin-top"><b>Index Types</b></p>
                    <div className="ds-indent ds-margin-top">
                        <Row>
                            <Col sm={5}>
                                <Checkbox
                                    id="addIndexTypeEq"
                                    isChecked={this.props.addIndexTypeEq}
                                    onChange={(checked, e) => {
                                        handleChange(e);
                                    }}
                                    label="Equailty Indexing"
                                />
                            </Col>
                        </Row>
                        <Row>
                            <Col sm={5}>
                                <Checkbox
                                    id="addIndexTypePres"
                                    isChecked={this.props.addIndexTypePres}
                                    onChange={(checked, e) => {
                                        handleChange(e);
                                    }}
                                    label="Presence Indexing"
                                />
                            </Col>
                        </Row>
                        <Row>
                            <Col sm={5}>
                                <Checkbox
                                    id="addIndexTypeSub"
                                    isChecked={this.props.addIndexTypeSub}
                                    onChange={(checked, e) => {
                                        handleChange(e);
                                    }}
                                    label="Substring Indexing"
                                />
                            </Col>
                        </Row>
                        <Row>
                            <Col sm={5}>
                                <Checkbox
                                    id="addIndexTypeApprox"
                                    isChecked={this.props.addIndexTypeApprox}
                                    onChange={(checked, e) => {
                                        handleChange(e);
                                    }}
                                    label="Approximate Indexing" />
                            </Col>
                        </Row>
                    </div>
                    <Row className="ds-margin-top-lg">
                        <Col sm={12} title="List of matching rules separated by a 'space'">
                            <p><b>Matching Rules</b></p>
                            <div className="ds-indent ds-margin-top">
                                <Select
                                    id="addMatchingRules"
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a matching rule name"
                                    onToggle={onMatchingruleAddToggle}
                                    onSelect={onMatchingruleAddSelect}
                                    onClear={onMatchingruleAddClear}
                                    selections={mrs}
                                    isOpen={isMatchingruleAddOpen}
                                    aria-labelledby="typeAhead-mr-add"
                                    placeholderText="Type a matching rule name..."
                                    noResultsFoundText="There are no matching entries"
                                    >
                                    {availMR.map((mrs, index) => (
                                        <SelectOption
                                           key={index}
                                           value={mrs}
                                       />
                                   ))}
                                </Select>
                            </div>
                        </Col>
                    </Row>
                    <hr />
                    <Row>
                        <Col sm={12}>
                            <Checkbox
                                className="ds-float-right"
                                id="reindexOnAdd"
                                isChecked={this.props.reindexOnAdd}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                label="Index attribute after creation"
                            />
                        </Col>
                    </Row>
                </Form>
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
            onMatchingruleEditToggle,
            onMatchingruleEditClear,
            onMatchingruleEditSelect,
            isMatchingruleEditOpen,
        } = this.props;

        let attrTypes = "";
        if (types != "" && types.length > 0) {
            attrTypes = types.split(",").map(item => item.trim());
        }

        const currentMrs = mrs;
        let availMR = matchingRules;

        // Default settings
        let eq = <div>
            <Checkbox
                id="editIndexTypeEq"
                isChecked={this.props.editIndexTypeEq}
                onChange={(checked, e) => {
                    handleChange(e);
                }}
                label="Equailty Indexing"
            />
        </div>;
        let pres = <div>
            <Checkbox
                id="editIndexTypePres"
                isChecked={this.props.editIndexTypePres}
                onChange={(checked, e) => {
                    handleChange(e);
                }}
                label="Presence Indexing"
            />
        </div>;
        let sub = <div>
            <Checkbox
                id="editIndexTypeSub"
                isChecked={this.props.editIndexTypeSub}
                onChange={(checked, e) => {
                    handleChange(e);
                }}
                label="Substring Indexing"
            />
        </div>;
        let approx = <div>
            <Checkbox
                id="editIndexTypeApprox"
                isChecked={this.props.editIndexTypeApprox}
                onChange={(checked, e) => {
                    handleChange(e);
                }}
                label="Approximate Indexing"
            />
        </div>;

        if (attrTypes.includes('eq')) {
            eq = <div>
                <Checkbox
                    id="editIndexTypeEq"
                    isChecked={this.props.editIndexTypeEq}
                    onChange={(checked, e) => {
                        handleChange(e);
                    }}
                    label="Equality Indexing"
                />
            </div>;
        }
        if (attrTypes.includes('pres')) {
            pres = <div>
                <Checkbox
                    id="editIndexTypePres"
                    isChecked={this.props.editIndexTypePres}
                    onChange={(checked, e) => {
                        handleChange(e);
                    }}
                    label="Presence Indexing"
                />
            </div>;
        }
        if (attrTypes.includes('sub')) {
            sub = <div>
                <Checkbox
                    id="editIndexTypeSub"
                    isChecked={this.props.editIndexTypeSub}
                    onChange={(checked, e) => {
                        handleChange(e);
                    }}
                    label="Substring Indexing"
                />
            </div>;
        }
        if (attrTypes.includes('approx')) {
            approx = <div>
                <Checkbox
                    id="editIndexTypeApprox"
                    isChecked={this.props.editIndexTypeApprox}
                    onChange={(checked, e) => {
                        handleChange(e);
                    }}
                    label="Approximate Indexing"
                />
            </div>;
        }

        let title = "Edit Database Index (" + indexName + ")";

        return (
            <Modal
                variant={ModalVariant.small}
                title={title}
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button key="confirm" variant="primary" onClick={saveHandler}>
                        Save Index
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
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
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a matching rule name"
                                    onToggle={onMatchingruleEditToggle}
                                    onSelect={onMatchingruleEditSelect}
                                    onClear={onMatchingruleEditClear}
                                    selections={currentMrs}
                                    isOpen={isMatchingruleEditOpen}
                                    aria-labelledby="typeAhead-mr-edit"
                                    placeholderText="Type a matching rule name..."
                                    noResultsFoundText="There are no matching entries"
                                    >
                                    {availMR.map((mr, index) => (
                                        <SelectOption
                                            key={index}
                                            value={mr}
                                        />
                                    ))}
                                </Select>
                            </div>
                        </Col>
                    </Row>
                    <hr />
                    <Row>
                        <Col sm={12}>
                            <Checkbox
                                className="ds-float-right"
                                id="reindexOnAdd"
                                isChecked={this.props.reindexOnAdd}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                label="Reindex Attribute After Saving"
                            />
                        </Col>
                    </Row>
                </Form>
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
    addIndexTypeEq:  PropTypes.bool,
    addIndexTypePres:  PropTypes.bool,
    addIndexTypeSub:  PropTypes.bool,
    addIndexTypeApprox:  PropTypes.bool,
    reindexOnAdd:  PropTypes.bool,
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
    addIndexTypeEq:  false,
    addIndexTypePres:  false,
    addIndexTypeSub:  false,
    addIndexTypeApprox:  false,
    reindexOnAdd:  false,
};

EditIndexModal.propTypes = {
    showModal: PropTypes.bool,
    closeHandler: PropTypes.func,
    handleChange: PropTypes.func,
    saveHandler: PropTypes.func,
    matchingRules: PropTypes.array,
    types: PropTypes.string,
    mrs: PropTypes.array,
    indexName: PropTypes.string,
    editIndexTypeEq:  PropTypes.bool,
    editIndexTypePres:  PropTypes.bool,
    editIndexTypeSub:  PropTypes.bool,
    editIndexTypeApprox:  PropTypes.bool,
    reindexOnAdd:  PropTypes.bool,
};

EditIndexModal.defaultProps = {
    showModal: false,
    closeHandler: noop,
    handleChange: noop,
    saveHandler: noop,
    matchingRules: [],
    types: "",
    mrs: [],
    indexName: "",
    handleTypeaheadChange: noop,
    editIndexTypeEq:  false,
    editIndexTypePres:  false,
    editIndexTypeSub:  false,
    editIndexTypeApprox:  false,
    reindexOnAdd:  false,
};
