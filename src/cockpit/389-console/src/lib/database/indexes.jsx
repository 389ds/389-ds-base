import cockpit from "cockpit";
import React from "react";
import { DoubleConfirmModal } from "../notifications.jsx";
import { IndexTable } from "./databaseTables.jsx";
import { log_cmd } from "../tools.jsx";
import {
    Button,
    Checkbox,
    Form,
    Grid,
    GridItem,
    Modal,
    ModalVariant,
    Select,
    SelectVariant,
    SelectOption,
    Tab,
    Tabs,
    TabTitleText,
    Text,
    TextContent,
    TextVariants,
} from "@patternfly/react-core";
import PropTypes from "prop-types";

const edit_attrs = ['mrs', 'indexTypeEq', 'indexTypeSub',
    'indexTypePres', 'indexTypeApprox',
];

export class SuffixIndexes extends React.Component {
    constructor (props) {
        super(props);
        this.state = {
            // indexes
            showIndexModal: false,
            showEditIndexModal: false,
            activeTabKey: 0,
            modalChecked: false,
            modalSpinning: false,
            types: "",
            attributes: [],
            matchingRules: [],
            mrs: [],
            _mrs: [],
            showConfirmReindex: false,
            reindexAttrName: "",
            showConfirmDeleteIndex: false,
            deleteAttrName: "",
            saving: false,
            saveBtnDisabled: true,
            // Add indexes
            indexName: [],
            indexTypeEq: false,
            indexTypePres: false,
            indexTypeSub: false,
            indexTypeApprox: false,
            reindexOnAdd: false,

            // Edit indexes
            errObj: {},
            _isMounted: true,

            // Select Typeahead
            isAttributeOpen: false,
            isMatchingruleOpen: false,
        };

        // Toggle currently active tab
        this.handleNavSelect = (event, tabIndex) => {
            this.setState({
                activeTabKey: tabIndex
            });
        };

        // Select Attribute
        this.onAttributeSelect = (event, selection) => {
            if (this.state.indexName.includes(selection)) {
                this.setState(
                    (prevState) => ({
                        indexName: prevState.indexName.filter((item) => item !== selection),
                        isAttributeOpen: false
                    }), () => { this.validateSaveBtn() }
                );
            } else {
                this.setState(
                    (prevState) => ({
                        indexName: [...prevState.indexName, selection],
                        isAttributeOpen: false
                    }), () => { this.validateSaveBtn() }
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
                indexName: [],
                isAttributeOpen: false
            });
        };

        this.onMatchingruleAddToggle = isMatchingruleOpen => {
            this.setState({
                isMatchingruleOpen
            });
        };
        this.onMatchingruleAddClear = () => {
            this.setState({
                mrs: [],
                isMatchingruleOpen: false
            });
        };

        this.onMatchingruleEditToggle = isMatchingruleOpen => {
            this.setState({
                isMatchingruleOpen
            });
        };
        this.onMatchingruleEditClear = () => {
            this.setState({
                mrs: [],
                isMatchingruleOpen: false
            });
        };

        this.loadIndexes = this.loadIndexes.bind(this);
        this.showIndexModal = this.showIndexModal.bind(this);
        this.closeIndexModal = this.closeIndexModal.bind(this);
        this.handleChange = this.handleChange.bind(this);
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
        this.validateSaveBtn = this.validateSaveBtn.bind(this);
        this.onMatchingruleSelect = this.onMatchingruleSelect.bind(this);
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
                    const mrs = [];
                    for (let i = 0; i < mrContent.items.length; i++) {
                        if (mrContent.items[i].name[0] != "") {
                            mrs.push(mrContent.items[i].name[0]);
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
                                const idxContent = JSON.parse(content);
                                const indexList = idxContent.items;
                                const attr_cmd = [
                                    "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
                                    "schema", "attributetypes", "list"
                                ];
                                log_cmd("loadIndexes (suffix config)", "Get attrs", attr_cmd);
                                cockpit
                                        .spawn(attr_cmd, { superuser: true, err: "message" })
                                        .done(content => {
                                            const attrContent = JSON.parse(content);
                                            const attrs = [];
                                            for (const content of attrContent.items) {
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
                                                    saveBtnDisabled: true,
                                                });
                                            }
                                        })
                                        .fail(err => {
                                            const errMsg = JSON.parse(err);
                                            this.props.addNotification(
                                                "error",
                                                `Failed to get attributes - ${errMsg.desc}`
                                            );
                                        });
                            });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
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
            indexName: [],
            indexTypeEq: false,
            indexTypeSub: false,
            indexTypePres: false,
            indexTypeApprox: false,
            reindexOnAdd: false,
            modalChecked: false,
            modalSpinning: false,
            saveBtnDisabled: true,
        });
    }

    closeIndexModal() {
        this.setState({
            showIndexModal: false
        });
        if (this.state.isMatchingruleOpen) {
            this.setState({
                isMatchingruleOpen: false
            });
        }
    }

    validateSaveBtn() {
        let saveBtnDisabled = true;
        const check_attrs = [
            "indexTypeEq",
            "indexTypeSub",
            "indexTypePres",
            "indexTypeApprox",
        ];

        // Check if a setting was changed, if so enable the save button
        for (const config_attr of check_attrs) {
            if (this.state[config_attr] != this.state['_' + config_attr]) {
                saveBtnDisabled = false;
                break;
            }
        }

        if (JSON.stringify(this.state._mrs) != JSON.stringify(this.state.mrs)) {
            saveBtnDisabled = false;
        }

        // We must have at least one index type set
        if (!this.state.indexTypeEq && !this.state.indexTypeSub &&
            !this.state.indexTypePres && !this.state.indexTypeApprox ||
            (this.state.indexName.length === 0 || this.state.indexName[0] === "")) {
            // Must always have one index type
            saveBtnDisabled = true;
        }

        this.setState({
            saveBtnDisabled: saveBtnDisabled,
        });
    }

    handleChange(e) {
        // Handle the modal changes
        const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        this.setState({
            [e.target.id]: value,
        }, () => { this.validateSaveBtn() });
    }

    // Edit Matching Rules
    onMatchingruleSelect(event, selection) {
        const new_mrs = [...this.state.mrs];
        if (new_mrs.includes(selection)) {
            const index = new_mrs.indexOf(selection);
            new_mrs.splice(index, 1);
            this.setState({
                mrs: new_mrs,
                isMatchingruleOpen: false,
            }, () => { this.validateSaveBtn() });
        } else {
            new_mrs.push(selection);
            this.setState({
                mrs: new_mrs,
                isMatchingruleOpen: false,
            }, () => { this.validateSaveBtn() });
        }
    };

    saveIndex() {
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "add", "--attr=" + this.state.indexName[0],
            this.props.suffix,
        ];

        if (this.state.indexTypeEq) {
            cmd.push('--index-type=eq');
        }
        if (this.state.indexTypePres) {
            cmd.push('--index-type=pres');
        }
        if (this.state.indexTypeSub) {
            cmd.push('--index-type=sub');
        }
        if (this.state.indexTypeApprox) {
            cmd.push('--index-type=approx');
        }
        for (let i = 0; i < this.state.mrs.length; i++) {
            cmd.push('--matching-rule=' + this.state.mrs[i]);
        }
        if (this.state.reindexOnAdd) {
            cmd.push('--reindex');
        }

        this.setState({
            saving: true,
        });

        log_cmd("saveIndex", "Create new index", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    // this.loadIndexes();
                    this.props.reload(this.props.suffix);
                    this.closeIndexModal();
                    if (this.state.reindexOnAdd) {
                        this.reindexAttr(this.state.indexName[0]);
                    }
                    this.props.addNotification(
                        "success",
                        `Successfully created new index`
                    );
                    this.setState({
                        saving: false,
                        saveBtnDisabled: true,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.closeIndexModal();
                    this.props.addNotification(
                        "error",
                        `Error creating index - ${errMsg.desc}`
                    );
                    this.setState({
                        saving: false,
                        saveBtnDisabled: true,
                    });
                });
    }

    showEditIndexModal(item) {
        // Set the state types and matching Rules
        const currentMRS = [];
        if (item[2] !== undefined &&
            item[2].length > 0) {
            const parts = item[2].split(",").map(item => item.trim());
            for (const part of parts) {
                currentMRS.push(part);
            }
        }

        this.setState({
            indexName: [item[0]],
            types: item[1],
            mrs: currentMRS,
            _mrs: currentMRS,
            showEditIndexModal: true,
            errObj: {},
            reindexOnAdd: false,
            indexTypeEq: item[1].includes("eq"),
            indexTypeSub: item[1].includes("sub"),
            indexTypePres: item[1].includes("pres"),
            indexTypeApprox: item[1].includes("approx"),
            _indexTypeEq: item[1].includes("eq"),
            _indexTypeSub: item[1].includes("sub"),
            _indexTypePres: item[1].includes("pres"),
            _indexTypeApprox: item[1].includes("approx"),
            modalChecked: false,
            modalSpinning: false,
            saveBtnDisabled: true,
        });
    }

    closeEditIndexModal() {
        this.setState({
            showEditIndexModal: false
        });
        if (this.state.isMatchingruleOpen) {
            this.setState({
                isMatchingruleOpen: false
            });
        }
    }

    reindexAttr(attr) {
        const reindex_cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "reindex", "--wait", "--attr=" + attr, this.props.suffix,
        ];

        this.setState({
            modalSpinning: true,
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
                        saving: false,
                        modalSpinning: false,
                        showConfirmReindex: false,
                    });
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.addNotification(
                        "error",
                        `Error indexing attribute ${attr} - ${errMsg.desc}`
                    );
                    this.setState({
                        saving: false,
                        modalSpinning: false,
                        showConfirmReindex: false,
                    });
                });
    }

    saveEditIndex() {
        const origMRS = this.state._mrs;
        const newMRS = this.state.mrs;
        let cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "set", "--attr=" + this.state.indexName,
            this.props.suffix,
        ];

        // Check if we have to add mrs
        for (const newMR of newMRS) {
            let found = false;
            for (const origMR of origMRS) {
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
        for (const origMR of origMRS) {
            let found = false;
            for (const newMR of newMRS) {
                if (newMR == origMR) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                cmd.push('--del-mr=' + origMR);
            }
        }

        // Check if we have to add/delete index types
        if (this.state.indexTypeEq && !this.state._indexTypeEq) {
            cmd.push('--add-type=eq');
        } else if (!this.state.indexTypeEq && this.state._indexTypeEq) {
            cmd.push('--del-type=eq');
        }
        if (this.state.indexTypeSub && !this.state._indexTypeSub) {
            cmd.push('--add-type=sub');
        } else if (!this.state.indexTypeSub && this.state._indexTypeSub) {
            cmd.push('--del-type=sub');
        }
        if (this.state.indexTypePres && !this.state._indexTypePres) {
            cmd.push('--add-type=pres');
        } else if (!this.state.indexTypePres && this.state._indexTypePres) {
            cmd.push('--del-type=pres');
        }
        if (this.state.indexTypeApprox && !this.state._indexTypeApprox) {
            cmd.push('--add-type=approx');
        } else if (!this.state.indexTypeApprox && this.state._indexTypeApprox) {
            cmd.push('--del-type=approx');
        }

        if (cmd.length > 8) {
            // We have changes, do it
            this.setState({
                saving: true,
            });
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
                            this.reindexAttr(this.state.indexName);
                        } else {
                            this.setState({
                                saving: false,
                            });
                        }
                    })
                    .fail(err => {
                        const errMsg = JSON.parse(err);
                        this.props.reload(this.props.suffix);
                        this.closeEditIndexModal();
                        this.props.addNotification(
                            "error",
                            `Error editing index - ${errMsg.desc}`
                        );
                        this.setState({
                            saving: false,
                        });
                    });
        }
    }

    showConfirmReindex(item) {
        this.setState({
            reindexAttrName: item,
            showConfirmReindex: true,
            modalChecked: false,
            modalSpinning: false,
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
            showConfirmDeleteIndex: true,
            modalChecked: false,
            modalSpinning: false,
        });
    }

    closeConfirmDeleteIndex() {
        this.setState({
            deleteAttrName: "",
            showConfirmDeleteIndex: false,
            modalSpinning: false,
            modalChecked: false,
        });
    }

    reindexIndex() {
        this.reindexAttr(this.state.reindexAttrName);
    }

    deleteIndex() {
        const cmd = [
            "dsconf", "-j", "ldapi://%2fvar%2frun%2fslapd-" + this.props.serverId + ".socket",
            "backend", "index", "delete", "--attr=" + this.state.deleteAttrName,
            this.props.suffix,
        ];

        this.setState({
            modalSpinning: true,
        });
        log_cmd("deleteIndex", "deleteEdit index", cmd);
        cockpit
                .spawn(cmd, { superuser: true, err: "message" })
                .done(content => {
                    this.props.addNotification(
                        "success",
                        "Successfully deleted index: " + this.state.deleteAttrName
                    );
                    this.props.reload(this.props.suffix);
                    this.closeConfirmDeleteIndex();
                })
                .fail(err => {
                    const errMsg = JSON.parse(err);
                    this.props.reload(this.props.suffix);
                    this.props.addNotification(
                        "error",
                        `Error deleting index - ${errMsg.desc}`
                    );
                    this.closeConfirmDeleteIndex();
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
                indexName: [],
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
        return (
            <div className="ds-margin-top-xlg ds-left-indent">
                <Tabs isSecondary isBox activeKey={this.state.activeTabKey} onSelect={this.handleNavSelect}>
                    <Tab eventKey={0} title={<TabTitleText>Database Indexes <font size="2">({this.props.indexRows.length})</font></TabTitleText>}>
                        <div className="ds-left-indent ds-margin-bottom-md">
                            <IndexTable
                                editable
                                rows={this.props.indexRows}
                                key={this.props.indexRows}
                                editIndex={this.showEditIndexModal}
                                reindexIndex={this.showConfirmReindex}
                                deleteIndex={this.showConfirmDeleteIndex}
                            />
                            <Button
                                variant="primary"
                                type="button"
                                onClick={this.showIndexModal}
                            >
                                Add Index
                            </Button>
                        </div>
                    </Tab>
                    <Tab eventKey={1} title={<TabTitleText>System Indexes <font size="2">({this.props.systemIndexRows.length})</font></TabTitleText>}>
                        <div className="ds-left-indent">
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
                    attributeName={this.state.indexName}
                    indexTypeEq={this.state.indexTypeEq}
                    indexTypePres={this.state.indexTypePres}
                    indexTypeSub={this.state.indexTypeSub}
                    indexTypeApprox={this.state.indexTypeApprox}
                    reindexOnAdd={this.state.reindexOnAdd}
                    onAttributeSelect={this.onAttributeSelect}
                    onAttributeToggle={this.onAttributeToggle}
                    onAttributeClear={this.onAttributeClear}
                    isAttributeOpen={this.state.isAttributeOpen}
                    onMatchingruleSelect={this.onMatchingruleSelect}
                    onMatchingruleAddToggle={this.onMatchingruleAddToggle}
                    onMatchingruleAddClear={this.onMatchingruleAddClear}
                    isMatchingruleOpen={this.state.isMatchingruleOpen}
                    saving={this.state.saving}
                    saveBtnDisabled={this.state.saveBtnDisabled}
                />
                <EditIndexModal
                    showModal={this.state.showEditIndexModal}
                    closeHandler={this.closeEditIndexModal}
                    handleChange={this.handleChange}
                    saveHandler={this.saveEditIndex}
                    types={this.state.types}
                    mrs={this.state.mrs}
                    matchingRules={this.state.matchingRules}
                    indexName={this.state.indexName}
                    indexTypeEq={this.state.indexTypeEq}
                    indexTypePres={this.state.indexTypePres}
                    indexTypeSub={this.state.indexTypeSub}
                    indexTypeApprox={this.state.indexTypeApprox}
                    reindexOnAdd={this.state.reindexOnAdd}
                    onMatchingruleSelect={this.onMatchingruleSelect}
                    onMatchingruleEditToggle={this.onMatchingruleEditToggle}
                    onMatchingruleEditClear={this.onMatchingruleEditClear}
                    isMatchingruleOpen={this.state.isMatchingruleOpen}
                    saving={this.state.saving}
                    saveBtnDisabled={this.state.saveBtnDisabled}
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmReindex}
                    closeHandler={this.closeConfirmReindex}
                    handleChange={this.handleChange}
                    actionHandler={this.reindexIndex}
                    spinning={this.state.modalSpinning}
                    item={this.state.reindexAttrName}
                    checked={this.state.modalChecked}
                    mTitle="Reindex Attribute"
                    mMsg="Are you sure you want to reindex this attribute?"
                    mSpinningMsg="Reindexing ..."
                    mBtnName="Reindex"
                />
                <DoubleConfirmModal
                    showModal={this.state.showConfirmDeleteIndex}
                    closeHandler={this.closeConfirmDeleteIndex}
                    handleChange={this.handleChange}
                    actionHandler={this.deleteIndex}
                    spinning={this.state.modalSpinning}
                    item={this.state.deleteAttrName}
                    checked={this.state.modalChecked}
                    mTitle="Delete Index"
                    mMsg="Are you sure you want to delete this index?"
                    mSpinningMsg="Deleting ..."
                    mBtnName="Delete"
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
            onMatchingruleSelect,
            isMatchingruleOpen,
            saving,
            saveBtnDisabled
        } = this.props;

        const availMR = [];
        for (const mr of matchingRules) {
            availMR.push(mr);
        }
        const availAttrs = [];
        for (const attr of attributes) {
            availAttrs.push(attr);
        }
        let saveBtnName = "Create Index";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Creating ...";
            extraPrimaryProps.spinnerAriaValueText = "Creating";
        }

        return (
            <Modal
                variant={ModalVariant.medium}
                title="Add Database Index"
                isOpen={showModal}
                onClose={closeHandler}
                aria-labelledby="ds-modal"
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? "Creating" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={saveBtnDisabled || saving}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent title="Select an attribute to index">
                        <Text component={TextVariants.h4}>
                            Select An Attribute
                        </Text>
                    </TextContent>
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
                        validated={attributeName.length === 0 || attributeName[0] === "" ? "error" : "default"}
                    >
                        {availAttrs.map((attr, index) => (
                            <SelectOption
                                key={index}
                                value={attr}
                            />
                        ))}
                    </Select>
                    <TextContent className="ds-margin-top">
                        <Text component={TextVariants.h4}>
                            Index Types
                        </Text>
                    </TextContent>
                    <div className="ds-indent">
                        <Grid>
                            <GridItem>
                                <Checkbox
                                    id="indexTypeEq"
                                    isChecked={this.props.indexTypeEq}
                                    onChange={(checked, e) => {
                                        handleChange(e);
                                    }}
                                    label="Equailty Indexing"
                                />
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top">
                            <GridItem>
                                <Checkbox
                                    id="indexTypePres"
                                    isChecked={this.props.indexTypePres}
                                    onChange={(checked, e) => {
                                        handleChange(e);
                                    }}
                                    label="Presence Indexing"
                                />
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top">
                            <GridItem>
                                <Checkbox
                                    id="indexTypeSub"
                                    isChecked={this.props.indexTypeSub}
                                    onChange={(checked, e) => {
                                        handleChange(e);
                                    }}
                                    label="Substring Indexing"
                                />
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top">
                            <GridItem>
                                <Checkbox
                                    id="indexTypeApprox"
                                    isChecked={this.props.indexTypeApprox}
                                    onChange={(checked, e) => {
                                        handleChange(e);
                                    }}
                                    label="Approximate Indexing"
                                />
                            </GridItem>
                        </Grid>
                    </div>
                    <Grid className="ds-margin-top-lg">
                        <GridItem span={12} title="List of matching rules separated by a 'space'">
                            <TextContent>
                                <Text component={TextVariants.h4}>
                                    Matching Rules
                                </Text>
                            </TextContent>
                            <div className="ds-indent ds-margin-top">
                                <Select
                                    id="addMatchingRules"
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a matching rule name"
                                    onToggle={onMatchingruleAddToggle}
                                    onSelect={onMatchingruleSelect}
                                    onClear={onMatchingruleAddClear}
                                    selections={mrs}
                                    isOpen={isMatchingruleOpen}
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
                        </GridItem>
                    </Grid>
                    <Grid>
                        <GridItem className="ds-margin-top" span={12}>
                            <Checkbox
                                id="reindexOnAdd"
                                isChecked={this.props.reindexOnAdd}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                label="Index attribute after creation"
                            />
                        </GridItem>
                    </Grid>
                    <hr />
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
            onMatchingruleSelect,
            isMatchingruleOpen,
            saving,
            saveBtnDisabled
        } = this.props;

        let saveBtnName = "Save Index";
        const extraPrimaryProps = {};
        if (saving) {
            saveBtnName = "Saving index ...";
            extraPrimaryProps.spinnerAriaValueText = "Saving";
        }

        let attrTypes = "";
        if (types != "" && types.length > 0) {
            attrTypes = types.split(",").map(item => item.trim());
        }

        const currentMrs = mrs;
        const availMR = matchingRules;

        // Default settings
        let eq = <div>
            <Checkbox
                id="indexTypeEq"
                isChecked={this.props.indexTypeEq}
                onChange={(checked, e) => {
                    handleChange(e);
                }}
                label="Equailty Indexing"
            />
        </div>;
        let pres = <div>
            <Checkbox
                id="indexTypePres"
                isChecked={this.props.indexTypePres}
                onChange={(checked, e) => {
                    handleChange(e);
                }}
                label="Presence Indexing"
            />
        </div>;
        let sub = <div>
            <Checkbox
                id="indexTypeSub"
                isChecked={this.props.indexTypeSub}
                onChange={(checked, e) => {
                    handleChange(e);
                }}
                label="Substring Indexing"
            />
        </div>;
        let approx = <div>
            <Checkbox
                id="indexTypeApprox"
                isChecked={this.props.indexTypeApprox}
                onChange={(checked, e) => {
                    handleChange(e);
                }}
                label="Approximate Indexing"
            />
        </div>;

        if (attrTypes.includes('eq')) {
            eq = <div>
                <Checkbox
                    id="indexTypeEq"
                    isChecked={this.props.indexTypeEq}
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
                    id="indexTypePres"
                    isChecked={this.props.indexTypePres}
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
                    id="indexTypeSub"
                    isChecked={this.props.indexTypeSub}
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
                    id="indexTypeApprox"
                    isChecked={this.props.indexTypeApprox}
                    onChange={(checked, e) => {
                        handleChange(e);
                    }}
                    label="Approximate Indexing"
                />
            </div>;
        }

        const title = <div>Edit Database Index (<b>{indexName[0]}</b>)</div>;

        return (
            <Modal
                variant={ModalVariant.medium}
                title={title}
                isOpen={showModal}
                aria-labelledby="ds-modal"
                onClose={closeHandler}
                actions={[
                    <Button
                        key="confirm"
                        variant="primary"
                        onClick={saveHandler}
                        isLoading={saving}
                        spinnerAriaValueText={saving ? "Saving" : undefined}
                        {...extraPrimaryProps}
                        isDisabled={saveBtnDisabled || saving}
                    >
                        {saveBtnName}
                    </Button>,
                    <Button key="cancel" variant="link" onClick={closeHandler}>
                        Cancel
                    </Button>
                ]}
            >
                <Form isHorizontal autoComplete="off">
                    <TextContent>
                        <Text className="ds-margin-top" component={TextVariants.h4}>
                            Index Types
                        </Text>
                    </TextContent>
                    <div className="ds-indent">
                        <Grid>
                            <GridItem span={9}>
                                {eq}
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top">
                            <GridItem span={9}>
                                {pres}
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top">
                            <GridItem span={9}>
                                {sub}
                            </GridItem>
                        </Grid>
                        <Grid className="ds-margin-top">
                            <GridItem span={9}>
                                {approx}
                            </GridItem>
                        </Grid>
                    </div>
                    <Grid className="ds-margin-top-lg">
                        <GridItem span={12}>
                            <TextContent>
                                <Text component={TextVariants.h4}>
                                    Matching Rules
                                </Text>
                            </TextContent>
                            <div className="ds-indent ds-margin-top">
                                <Select
                                    variant={SelectVariant.typeaheadMulti}
                                    typeAheadAriaLabel="Type a matching rule name"
                                    onToggle={onMatchingruleEditToggle}
                                    onSelect={onMatchingruleSelect}
                                    onClear={onMatchingruleEditClear}
                                    selections={currentMrs}
                                    isOpen={isMatchingruleOpen}
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
                        </GridItem>
                    </Grid>
                    <Grid className="ds-margin-top">
                        <GridItem span={12}>
                            <Checkbox
                                id="reindexOnAdd"
                                isChecked={this.props.reindexOnAdd}
                                onChange={(checked, e) => {
                                    handleChange(e);
                                }}
                                label="Reindex Attribute After Saving"
                            />
                        </GridItem>
                    </Grid>
                    <hr />
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
    indexTypeEq:  PropTypes.bool,
    indexTypePres:  PropTypes.bool,
    indexTypeSub:  PropTypes.bool,
    indexTypeApprox:  PropTypes.bool,
    reindexOnAdd:  PropTypes.bool,
};

AddIndexModal.defaultProps = {
    showModal: false,
    matchingRules: [],
    attributes: [],
    mrs: [],
    attributeName: [],
    indexTypeEq:  false,
    indexTypePres:  false,
    indexTypeSub:  false,
    indexTypeApprox:  false,
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
    indexName: PropTypes.array,
    indexTypeEq:  PropTypes.bool,
    indexTypePres:  PropTypes.bool,
    indexTypeSub:  PropTypes.bool,
    indexTypeApprox:  PropTypes.bool,
    reindexOnAdd:  PropTypes.bool,
};

EditIndexModal.defaultProps = {
    showModal: false,
    matchingRules: [],
    types: "",
    mrs: [],
    indexName: [],
    indexTypeEq:  false,
    indexTypePres:  false,
    indexTypeSub:  false,
    indexTypeApprox:  false,
    reindexOnAdd:  false,
};
